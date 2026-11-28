# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Tests for gitlab_release.py: python3 -I -m unittest discover -s desktop/scripts"""

import io
import os
import subprocess
import sys
import unittest
import urllib.error
from contextlib import redirect_stdout

import gitlab_release

SCRIPT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "gitlab_release.py")

PATH = "/projects/12/releases"
ONE = PATH + "/v1.0.0.1-1"


class FakeGitLab:
    """Answers the release API from a dict of tag -> description, recording each call."""

    def __init__(self, releases=None, created_meanwhile=None):
        self.releases = dict(releases or {})
        self.created_meanwhile = created_meanwhile   # another job creates it right after our GET
        self.calls = []

    def __call__(self, method, path, data):
        self.calls.append((method, path, data))
        tag = path.rsplit("/", 1)[-1]
        if method == "GET":
            if tag not in self.releases:
                if self.created_meanwhile is not None:
                    self.releases[tag], self.created_meanwhile = self.created_meanwhile, None
                return 404, {"message": "404 Not Found"}
            return 200, {"tag_name": tag, "description": self.releases[tag]}
        if method == "POST":
            if data["tag_name"] in self.releases:
                return 409, {"message": "Release already exists"}
            self.releases[data["tag_name"]] = data["description"]
            return 201, {}
        if method == "PUT":
            self.releases[tag] = data["description"]
            return 200, {}
        return 405, None


class SyncTest(unittest.TestCase):
    def test_first_build_creates_the_release(self):
        api = FakeGitLab()
        self.assertEqual(gitlab_release.sync("12", "v1.0.0.1-1", "arm64", api), "created")
        self.assertEqual(api.releases, {"v1.0.0.1-1": "arm64"})
        self.assertEqual(api.calls[-1], ("POST", PATH, {"tag_name": "v1.0.0.1-1",
                                                        "name": "v1.0.0.1-1", "description": "arm64"}))

    def test_a_later_build_updates_it(self):
        api = FakeGitLab({"v1.0.0.1-1": "arm64"})
        self.assertEqual(gitlab_release.sync("12", "v1.0.0.1-1", "arm64, x64", api), "updated")
        self.assertEqual(api.releases, {"v1.0.0.1-1": "arm64, x64"})
        self.assertEqual(api.calls[-1], ("PUT", ONE, {"description": "arm64, x64"}))

    def test_the_same_text_is_left_alone(self):
        api = FakeGitLab({"v1.0.0.1-1": "arm64, x64"})
        self.assertEqual(gitlab_release.sync("12", "v1.0.0.1-1", "arm64, x64", api), "unchanged")
        self.assertEqual([c[0] for c in api.calls], ["GET"])

    def test_created_by_another_job_meanwhile_is_updated(self):
        api = FakeGitLab(created_meanwhile="arm64")
        self.assertEqual(gitlab_release.sync("12", "v1.0.0.1-1", "arm64, x64", api), "updated")
        self.assertEqual(api.releases, {"v1.0.0.1-1": "arm64, x64"})

    def test_names_in_the_path_are_escaped(self):
        api = FakeGitLab()
        gitlab_release.sync("group/project", "v1/x", "d", api)
        self.assertEqual(api.calls[0][1], "/projects/group%2Fproject/releases/v1%2Fx")

    def test_api_errors_stop_the_job(self):
        for answers in ([(500, None)], [(404, None), (403, {"message": "403 Forbidden"})],
                        [(200, {"description": "old"}), (403, None)]):
            replies = iter(answers)
            with self.subTest(answers=answers), self.assertRaises(gitlab_release.ReleaseError):
                gitlab_release.sync("12", "v1.0.0.1-1", "new", lambda *a: next(replies))


class MainTest(unittest.TestCase):
    RELEASES = {"releases": [{"version": "1.0.0.1-1", "notes": ["A."], "files": [
        {"platform": "macos", "arch": "arm64"}, {"platform": "macos", "arch": "x64"}]}]}
    ENV = {"CI_API_V4_URL": "https://gitlab.invalid/api/v4", "CI_PROJECT_ID": "12",
           "CI_JOB_TOKEN": "t"}

    URL = "https://downloads.invalid/releases.json"

    def run_main(self, argv, api, fetch=None, env=None):
        out = io.StringIO()
        with redirect_stdout(out):
            gitlab_release.main(argv, env=self.ENV if env is None else env, request=api,
                                fetch=fetch or (lambda url: self.RELEASES), sleep=lambda s: None)
        return out.getvalue()

    def test_the_text_comes_from_the_live_releases_json(self):
        api = FakeGitLab({"v1.0.0.1-1": "older text, arm64 only"})
        out = self.run_main(["v1.0.0.1-1", "macos-x64", self.URL], api)
        self.assertEqual(api.releases["v1.0.0.1-1"],
                         "Little Browser 1.0.0.1-1\n\n- A.\n\nBuilds: macOS arm64, macOS x64\n\n"
                         "Download: https://littlebrowser.app/downloads\n")
        self.assertIn("Updated the GitLab release v1.0.0.1-1", out)

    def test_waits_until_releases_json_lists_the_build(self):
        # CloudFront may serve the releases.json from before the publish for a while.
        stale = {"releases": [{"version": "1.0.0.1-1", "files": [{"platform": "macos", "arch": "arm64"}]}]}
        answers = iter([stale, stale, self.RELEASES])
        api = FakeGitLab()
        self.run_main(["v1.0.0.1-1", "macos-x64", self.URL], api, fetch=lambda url: next(answers))
        self.assertIn("Builds: macOS arm64, macOS x64", api.releases["v1.0.0.1-1"])

    def test_gives_up_when_the_build_never_shows(self):
        slept = []
        with self.assertRaises(gitlab_release.ReleaseError):
            gitlab_release.published(self.URL, "1.0.0.1-1", "linux-x64", lambda url: self.RELEASES,
                                     wait=90, pause=30, sleep=slept.append)
        self.assertEqual(slept, [30, 30, 30])

    def test_a_version_not_published_yet_fails(self):
        with self.assertRaises(SystemExit):
            self.run_main(["v9.9.9.9-1", "macos-arm64", self.URL], FakeGitLab())

    def test_network_and_json_errors_are_one_line(self):
        def refused(url):
            raise urllib.error.URLError("connection refused")
        def garbled(url):
            raise ValueError("Expecting value: line 1 column 1")
        for fetch in (refused, garbled):
            with self.subTest(fetch=fetch.__name__), self.assertRaises(SystemExit) as e:
                self.run_main(["v1.0.0.1-1", "macos-arm64", self.URL], FakeGitLab(), fetch=fetch)
            self.assertIn("gitlab_release.py:", str(e.exception.code))

    def test_without_a_downloads_address(self):
        # $LB_DOWNLOADS_URL/releases.json with the variable unset.
        with self.assertRaises(SystemExit) as e:
            self.run_main(["v1.0.0.1-1", "macos-arm64", "/releases.json"], FakeGitLab())
        self.assertIn("LB_DOWNLOADS_URL", str(e.exception.code))

    def test_outside_ci(self):
        with self.assertRaises(SystemExit) as e:
            gitlab_release.main(["v1.0.0.1-1", "macos-arm64", self.URL], env={},
                                fetch=lambda url: self.RELEASES)
        self.assertIn("CI_API_V4_URL", str(e.exception.code))

    def test_wrong_arguments(self):
        for argv in ([], ["v1.0.0.1-1"], ["v1.0.0.1-1", self.URL], ["1.0.0.1-1", "macos-arm64", self.URL]):
            with self.subTest(argv=argv), self.assertRaises(SystemExit):
                gitlab_release.main(argv, env=self.ENV, request=FakeGitLab())


class CommandLineTest(unittest.TestCase):
    def test_runs_as_ci_runs_it(self):
        # python3 -I, from the repository's root: the module path lacks the script's folder.
        root = os.path.dirname(os.path.dirname(os.path.dirname(SCRIPT)))
        result = subprocess.run([sys.executable, "-I", os.path.relpath(SCRIPT, root)],
                                cwd=root, capture_output=True, text=True)
        self.assertNotIn("ModuleNotFoundError", result.stderr)
        self.assertIn("usage: gitlab_release.py", result.stderr)


if __name__ == "__main__":
    unittest.main()

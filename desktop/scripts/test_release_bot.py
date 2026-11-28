# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Tests for release-bot.sh tag: a push to main tags a release only when VERSIONS' pins change."""

import os
import shutil
import subprocess
import tempfile
import unittest

SCRIPTS = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(SCRIPTS))


def git(cwd, *args):
    return subprocess.run(["git", *args], cwd=cwd, check=True, capture_output=True,
                          text=True).stdout.strip()


PINS = "CHROMIUM_VERSION=1.2.3.4\nUNGOOGLED_MACOS_TAG=1-1\n"


class TagTest(unittest.TestCase):
    """A repository with release-bot.sh and lib.sh where they live, and a local `origin`."""

    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="lb-release-bot-")
        self.addCleanup(shutil.rmtree, self.tmp)
        self.repo = os.path.join(self.tmp, "repo")
        for rel in ("desktop/scripts/release-bot.sh", "chromium/scripts/lib.sh"):
            os.makedirs(os.path.dirname(os.path.join(self.repo, rel)), exist_ok=True)
            shutil.copy2(os.path.join(REPO, rel), os.path.join(self.repo, rel))
        git(self.tmp, "init", "-q", "--bare", "origin.git")
        git(self.repo, "init", "-q")
        git(self.repo, "config", "user.name", "t")
        git(self.repo, "config", "user.email", "t@example.invalid")
        git(self.repo, "remote", "add", "origin", os.path.join(self.tmp, "origin.git"))

    def commit(self, files, remove=()):
        for rel in remove:
            git(self.repo, "rm", "-q", rel)
        for rel, text in files.items():
            path = os.path.join(self.repo, rel)
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path, "w") as f:
                f.write(text)
            git(self.repo, "add", rel)
        git(self.repo, "add", "-A")
        git(self.repo, "commit", "-q", "-m", "c")
        git(self.repo, "push", "-q", "origin", "HEAD:refs/heads/main")
        return git(self.repo, "rev-parse", "HEAD")

    def tag(self, before):
        env = dict(os.environ, LB_DRY_RUN="1", CI_SERVER_HOST="host.invalid",
                   CI_PROJECT_PATH="lb/lb", CI_DEFAULT_BRANCH="main", CI_COMMIT_BEFORE_SHA=before)
        return subprocess.run(["bash", "desktop/scripts/release-bot.sh", "tag"], cwd=self.repo,
                              env=env, capture_output=True, text=True)

    def test_moving_versions_and_rewording_it_tags_nothing(self):
        before = self.commit({"desktop/VERSIONS": "# old\n" + PINS})
        self.commit({"chromium/VERSIONS": "# new header\n# more\n" + PINS},
                    remove=["desktop/VERSIONS"])
        result = self.tag(before)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("no release", result.stdout)
        self.assertNotIn("refs/tags", result.stdout)

    def test_a_changed_pin_tags(self):
        before = self.commit({"chromium/VERSIONS": PINS})
        self.commit({"chromium/VERSIONS": "CHROMIUM_VERSION=1.2.3.4\nUNGOOGLED_MACOS_TAG=1-2\n"})
        result = self.tag(before)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("dry run: git push <remote> refs/tags/v1.2.3.4-1", result.stdout)

    def test_the_first_versions_file_tags(self):
        before = self.commit({"README": "x\n"})
        self.commit({"chromium/VERSIONS": PINS})
        result = self.tag(before)
        self.assertIn("refs/tags/v1.2.3.4-1", result.stdout, result.stderr)

    def test_an_unknown_previous_commit_fails(self):
        self.commit({"chromium/VERSIONS": PINS})
        result = self.tag("0123456789abcdef0123456789abcdef01234567")
        self.assertNotEqual(result.returncode, 0)
        self.assertNotIn("refs/tags", result.stdout)


if __name__ == "__main__":
    unittest.main()

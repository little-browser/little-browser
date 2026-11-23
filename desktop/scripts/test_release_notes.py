# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Tests for release_notes.py: python3 -I -m unittest discover -s desktop/scripts"""

import io
import json
import os
import subprocess
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path
from unittest import mock

import release_notes

# git in these tests sees none of the developer's config, hooks or repository variables.
_ENV = mock.patch.dict(os.environ, {"GIT_CONFIG_GLOBAL": os.devnull, "GIT_CONFIG_NOSYSTEM": "1"})


def setUpModule():
    _ENV.start()
    for name in [n for n in os.environ if n.startswith("GIT_") and n not in
                 ("GIT_CONFIG_GLOBAL", "GIT_CONFIG_NOSYSTEM")]:
        del os.environ[name]
    os.environ.pop("LB_RELEASE_NOTES", None)


def tearDownModule():
    _ENV.stop()


class ChromiumNoteTest(unittest.TestCase):
    def test_new_chromium_security_release(self):
        self.assertEqual(release_notes.chromium_note("157.0.8103.90", "157.0.8103.78", True),
                         "Based on Chromium 157.0.8103.90, with its security fixes.")

    def test_new_chromium_without_security(self):
        self.assertEqual(release_notes.chromium_note("157.0.8103.90", "157.0.8103.78", False),
                         "Based on Chromium 157.0.8103.90.")

    def test_same_chromium_security_release(self):
        self.assertEqual(release_notes.chromium_note("157.0.8103.90", "157.0.8103.90", True),
                         "Security fixes.")

    def test_same_chromium_no_line(self):
        self.assertIsNone(release_notes.chromium_note("157.0.8103.90", "157.0.8103.90", False))

    def test_first_release_claims_no_security_fixes(self):
        self.assertEqual(release_notes.chromium_note("157.0.8103.90", "", True),
                         "Based on Chromium 157.0.8103.90.")

    def test_chromium_line_comes_first(self):
        self.assertEqual(release_notes.release_notes("1.2.3.4", "1.2.3.3", False, ["A."]),
                         ["Based on Chromium 1.2.3.4.", "A."])
        self.assertEqual(release_notes.release_notes("1.2.3.4", "1.2.3.4", False, ["A."]), ["A."])


class OverrideTest(unittest.TestCase):
    def test_one_note_per_line_without_bullets(self):
        self.assertEqual(release_notes.override_notes("- First.\n\n  * Second.\nThird.\n"),
                         ["First.", "Second.", "Third."])

    def test_notes_separated_by_bars_on_one_line(self):
        # GitLab's form for running a job takes one line.
        self.assertEqual(release_notes.override_notes("- First. | - Second.|Third."),
                         ["First.", "Second.", "Third."])

    def test_a_hyphen_inside_a_note_stays(self):
        self.assertEqual(release_notes.override_notes("Sign-in works again."),
                         ["Sign-in works again."])


class MessageNotesTest(unittest.TestCase):
    def notes(self, message):
        return release_notes.message_notes(message)

    def test_a_note_next_to_the_sign_off(self):
        self.assertEqual(self.notes("Subject\n\nBody.\n\nRelease-note: Peek opens faster.\n"
                                    "Signed-off-by: A <a@example.invalid>\n"),
                         ["Peek opens faster."])

    def test_a_wrapped_note_is_joined(self):
        self.assertEqual(self.notes("Subject\n\nRelease-note: Links lose their utm_*, fbclid and"
                                    " other tracking\nparameters.\nSigned-off-by: A\n"),
                         ["Links lose their utm_*, fbclid and other tracking parameters."])

    def test_an_indented_continuation_is_joined(self):
        self.assertEqual(self.notes("S\n\nRelease-note: One\n  two.\n"), ["One two."])

    def test_a_note_in_its_own_paragraph_before_the_trailers(self):
        self.assertEqual(self.notes("S\n\nBody.\n\nRelease-note: Peek opens faster.\n\n"
                                    "Co-Authored-By: B\nSigned-off-by: A\n"),
                         ["Peek opens faster."])

    def test_a_note_ends_at_a_blank_line(self):
        self.assertEqual(self.notes("S\n\nRelease-note: First.\n\nMore body text.\n"), ["First."])

    def test_a_note_ends_at_a_cherry_pick_line(self):
        self.assertEqual(self.notes("S\n\nRelease-note: First.\n(cherry picked from commit abc)\n"),
                         ["First."])

    def test_two_notes_in_one_message(self):
        self.assertEqual(self.notes("S\n\nRelease-note: First.\nrelease-note: Second.\n"),
                         ["First.", "Second."])

    def test_only_a_line_that_starts_with_the_key_counts(self):
        self.assertEqual(self.notes("S\n\nA commit's Release-note: line says what changed.\n"), [])

    def test_an_empty_note_counts_for_nothing(self):
        self.assertEqual(self.notes("S\n\nRelease-note:\nSigned-off-by: A\n"), [])


class MarkdownTest(unittest.TestCase):
    RELEASES = {"releases": [
        {"version": "1.0.0.1-2", "chromium": "1.0.0.1",
         "notes": ["Based on Chromium 1.0.0.1.", "Typing 2*3*4 works in <select> menus."]},
        {"version": "1.0.0.1-1", "chromium": "1.0.0.1"},
    ]}

    def test_notes_are_a_list_read_as_plain_text(self):
        self.assertEqual(release_notes.markdown(self.RELEASES, "1.0.0.1-2"),
                         "Little Browser 1.0.0.1-2\n\n"
                         "- Based on Chromium 1.0.0.1.\n"
                         "- Typing 2\\*3\\*4 works in \\<select\\> menus.\n\n"
                         "Download: https://littlebrowser.app/downloads\n")

    def test_a_release_without_notes(self):
        self.assertEqual(release_notes.markdown(self.RELEASES, "1.0.0.1-1"),
                         "Little Browser 1.0.0.1-1\n\n"
                         "Download: https://littlebrowser.app/downloads\n")

    def test_an_unknown_version_is_an_error(self):
        with self.assertRaises(release_notes.NotesError):
            release_notes.markdown(self.RELEASES, "9.9.9.9-1")


class GitRepo:
    """A throwaway repository with commits and tags."""

    def __init__(self, path: Path):
        self.path = path
        path.mkdir()
        self.git("init", "-q", "-b", "main")
        self.git("config", "user.name", "Test")
        self.git("config", "user.email", "test@example.invalid")

    def git(self, *args):
        return subprocess.run(["git", "-C", str(self.path), *args], check=True,
                              stdout=subprocess.PIPE, universal_newlines=True).stdout

    def commit(self, message):
        self.git("commit", "-q", "--no-verify", "--allow-empty", "-m", message)
        return self.git("rev-parse", "HEAD").strip()

    def revert(self, sha):
        """A commit with git revert's message (the commits are empty, so there's nothing to undo)."""
        sha = self.git("rev-parse", sha).strip()
        subject = self.git("log", "-1", "--format=%s", sha).strip()
        return self.commit('Revert "%s"\n\nThis reverts commit %s.' % (subject, sha))

    def tag(self, name):
        self.git("tag", name)


def body(subject, *notes):
    return "%s\n\nWhy it changed.\n\n%s" % (subject, "\n".join(
        ["Release-note: " + n for n in notes] + ["Signed-off-by: Test <test@example.invalid>"]))


class GitTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        self.repo = GitRepo(Path(self._tmp.name) / "repo")
        r = self.repo
        r.commit(body("first", "Old note."))
        r.tag("v1.0.0.1-1")
        r.commit(body("ci only"))
        r.tag("v9")   # not a release tag
        r.commit(body("feature", "Pinned tabs keep their order."))
        r.tag("v1.0.0.1-2")   # tagged, never published
        r.commit(body("two notes", "Peek opens faster.", "Pinned tabs keep their order."))
        r.commit("wrapped\n\nRelease-note: A note that goes on\nacross two lines.\n"
                 "Signed-off-by: Test <test@example.invalid>")
        r.tag("v1.0.0.1-3")

    def notes(self, base, head):
        return release_notes.commit_notes(self.repo.path, base, head)

    def base(self, head, published):
        with redirect_stderr(io.StringIO()) as err:
            base = release_notes.base_ref(self.repo.path, head, published)
        return base, err.getvalue()

    def test_base_is_the_published_release(self):
        self.assertEqual(self.base("v1.0.0.1-3", "1.0.0.1-1"), ("v1.0.0.1-1", ""))

    def test_first_release_starts_at_the_first_commit(self):
        # Tags exist, but none was published.
        self.assertEqual(self.base("v1.0.0.1-3", ""), (None, ""))
        self.assertEqual(self.notes(None, "v1.0.0.1-2"),
                         ["Old note.", "Pinned tabs keep their order."])

    def test_a_missing_published_tag_is_an_error(self):
        with self.assertRaisesRegex(release_notes.NotesError, "v9.9.9.9-1"):
            self.base("v1.0.0.1-3", "9.9.9.9-1")

    def test_a_shallow_checkout_is_an_error(self):
        shallow = Path(self._tmp.name) / "shallow"
        subprocess.run(["git", "clone", "-q", "--depth", "1", "--no-local",
                        "file://" + str(self.repo.path), str(shallow)], check=True)
        with self.assertRaisesRegex(release_notes.NotesError, "history"):
            release_notes.base_ref(shallow, "HEAD", "1.0.0.1-1")

    def test_a_published_tag_off_the_branch_falls_back_to_the_release_tag_before_head(self):
        self.repo.git("checkout", "-q", "-b", "side", "v1.0.0.1-1")
        self.repo.commit("side")
        self.repo.tag("v1.0.0.1-9")
        self.repo.git("checkout", "-q", "main")
        self.repo.commit("after")
        self.repo.tag("v1.0.0.1-4")
        base, warning = self.base("v1.0.0.1-4", "1.0.0.1-9")
        self.assertEqual(base, "v1.0.0.1-3")   # skips the non-release tag v9 too
        self.assertIn("isn't an ancestor", warning)

    def test_the_release_tag_before_head_skips_other_tags(self):
        self.repo.git("checkout", "-q", "-b", "side", "v1.0.0.1-1")
        self.repo.commit("side")
        self.repo.tag("v1.0.0.1-9")
        self.repo.git("checkout", "-q", "main")
        self.assertEqual(self.base("v1.0.0.1-2", "1.0.0.1-9")[0], "v1.0.0.1-1")

    def test_notes_since_the_published_release_oldest_first_each_once(self):
        self.assertEqual(self.notes("v1.0.0.1-1", "v1.0.0.1-3"), [
            "Pinned tabs keep their order.",
            "Peek opens faster.",
            "A note that goes on across two lines.",
        ])

    def test_a_reverted_commit_has_no_notes(self):
        sha = self.repo.commit(body("flaky", "Tabs sleep sooner."))
        self.repo.commit(body("kept", "Kept."))
        self.repo.revert(sha)
        self.repo.tag("v1.0.0.1-4")
        self.assertEqual(self.notes("v1.0.0.1-3", "v1.0.0.1-4"), ["Kept."])

    def test_a_reverted_revert_brings_the_notes_back(self):
        sha = self.repo.commit(body("flaky", "Tabs sleep sooner."))
        self.repo.revert(self.repo.revert(sha))
        self.repo.tag("v1.0.0.1-4")
        self.assertEqual(self.notes("v1.0.0.1-3", "v1.0.0.1-4"), ["Tabs sleep sooner."])

    def test_signature_checks_stay_out_of_the_notes(self):
        # log.showSignature=true would print each signed commit's check into git log's output.
        with mock.patch.object(release_notes.subprocess, "run",
                               wraps=release_notes.subprocess.run) as run:
            self.notes("v1.0.0.1-2", "v1.0.0.1-3")
        log = [c.args[0] for c in run.call_args_list if "log" in c.args[0]]
        self.assertTrue(log)
        for argv in log:
            self.assertLess(argv.index("log.showSignature=false"), argv.index("log"))

    def run_main(self, *args, env=None):
        out, err = io.StringIO(), io.StringIO()
        with mock.patch.dict(os.environ, env or {}), redirect_stdout(out), redirect_stderr(err):
            code = release_notes.main(["notes", "--repo", str(self.repo.path), *args])
        return code, json.loads(out.getvalue()) if code == 0 else None, err.getvalue()

    def test_main_prints_json(self):
        self.assertEqual(
            self.run_main("--head", "v1.0.0.1-3", "--published", "1.0.0.1-2",
                          "--chromium", "1.0.0.1", "--previous-chromium", "1.0.0.1",
                          "--security", "false"),
            (0, ["Peek opens faster.", "Pinned tabs keep their order.",
                 "A note that goes on across two lines."], ""))

    def test_main_override_keeps_the_chromium_line(self):
        self.assertEqual(
            self.run_main("--head", "v1.0.0.1-3", "--published", "1.0.0.1-1",
                          "--chromium", "1.0.0.2", "--previous-chromium", "1.0.0.1",
                          "--security", "true", env={"LB_RELEASE_NOTES": "- Fixed wording."}),
            (0, ["Based on Chromium 1.0.0.2, with its security fixes.", "Fixed wording."], ""))

    def test_main_blank_override_uses_the_commits(self):
        code, notes, _ = self.run_main("--head", "v1.0.0.1-2", "--published", "1.0.0.1-1",
                                       "--chromium", "1.0.0.1", "--previous-chromium", "1.0.0.1",
                                       "--security", "false", env={"LB_RELEASE_NOTES": " \n | "})
        self.assertEqual((code, notes), (0, ["Pinned tabs keep their order."]))

    def test_main_warns_when_the_commits_have_no_notes(self):
        # The Chromium line doesn't count: the warning is about the commits.
        self.repo.commit(body("ci only"))
        self.repo.tag("v1.0.0.2-1")
        code, notes, err = self.run_main("--head", "v1.0.0.2-1", "--published", "1.0.0.1-3",
                                         "--chromium", "1.0.0.2", "--previous-chromium",
                                         "1.0.0.1", "--security", "true")
        self.assertEqual((code, notes), (0, ["Based on Chromium 1.0.0.2, with its security fixes."]))
        self.assertIn("no Release-note lines", err)

    def test_main_fails_without_the_head_tag(self):
        code, _, err = self.run_main("--head", "v1.0.0.1-8", "--published", "1.0.0.1-1",
                                     "--chromium", "1.0.0.1", "--security", "false")
        self.assertEqual(code, 1)
        self.assertIn("error:", err)


if __name__ == "__main__":
    unittest.main()

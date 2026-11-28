# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Tests for package-macos.sh's release version: the build it packages has to have been made for
that release (LB_RELEASE), so that its About page shows the version the package is named after.
The check runs before any signing, so a folder standing in for the app, and a stand-in lipo that
says it is built for this machine, are enough."""

import os
import shutil
import stat
import subprocess
import tempfile
import unittest

SCRIPTS = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(SCRIPTS))
PINS = "CHROMIUM_VERSION=1.2.3.4\n"
# Where package-macos.sh stops once the version is accepted: this tree has no entitlements.
PAST_THE_CHECK = "no entitlements"


class ReleaseVersionTest(unittest.TestCase):
    """A repository with package-macos.sh, lib.sh and VERSIONS where they live, and a build."""

    def setUp(self):
        self.repo = tempfile.mkdtemp(prefix="lb-package-macos-")
        self.addCleanup(shutil.rmtree, self.repo)
        for rel in ("desktop/scripts/package-macos.sh", "chromium/scripts/lib.sh"):
            os.makedirs(os.path.dirname(os.path.join(self.repo, rel)), exist_ok=True)
            shutil.copy2(os.path.join(REPO, rel), os.path.join(self.repo, rel))
        with open(os.path.join(self.repo, "chromium/VERSIONS"), "w") as f:
            f.write(PINS)
        self.out = os.path.join(self.repo, "desktop/build/src/out/Default")
        os.makedirs(os.path.join(self.out, "Little Browser.app"))
        # The app's architecture, as lipo names it: this machine's, which out/Default is for.
        self.bin = os.path.join(self.repo, "bin")
        os.makedirs(self.bin)
        lipo = os.path.join(self.bin, "lipo")
        with open(lipo, "w") as f:
            f.write("#!/bin/sh\nuname -m\n")
        os.chmod(lipo, os.stat(lipo).st_mode | stat.S_IXUSR)

    def build(self, revision=None, line=None):
        """args.gn as write_gn_args writes it, for a release when <revision> is given, or with
        <line> as its last line."""
        with open(os.path.join(self.out, "args.gn"), "w") as f:
            f.write('is_official_build = true\ntarget_cpu = "arm64"\n')
            if revision is not None:
                f.write(f"lb_release_revision = {revision}\n")
            if line is not None:
                f.write(line + "\n")

    def package(self, *args):
        env = {k: v for k, v in os.environ.items() if k != "CI"}
        env["LB_USER_SESSION"] = "1"  # never re-run itself in another launchd session
        env["PATH"] = f"{self.bin}:{env['PATH']}"
        return subprocess.run(
            ["bash", os.path.join(self.repo, "desktop/scripts/package-macos.sh"), *args],
            capture_output=True, text=True, env=env)

    def test_a_release_of_its_build(self):
        self.build(revision=3)
        for args in (["--release", "1.2.3.4-3"], ["1.2.3.4-3"]):
            result = self.package(*args)
            self.assertIn(PAST_THE_CHECK, result.stderr, args)
            self.assertNotIn("the build is for revision", result.stderr, args)

    def test_a_release_stops_on_a_build_for_another_revision(self):
        self.build(revision=2)
        result = self.package("--release", "1.2.3.4-3")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("the build is for revision 2", result.stderr)
        self.assertIn("LB_RELEASE=1.2.3.4-3", result.stderr)
        self.assertNotIn(PAST_THE_CHECK, result.stderr)

    def test_a_release_stops_on_a_build_that_isnt_one(self):
        self.build()
        result = self.package("--release", "1.2.3.4-1")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("the build is for revision 0 (0: not a release)", result.stderr)
        self.assertNotIn(PAST_THE_CHECK, result.stderr)

    # A local package with a version, for trying an update, only warns.
    def test_a_local_package_warns(self):
        self.build()
        result = self.package("1.2.3.4-1")
        self.assertIn("warning: the build is for revision 0", result.stderr)
        self.assertIn(PAST_THE_CHECK, result.stderr)

    # No args.gn (a build made some other way) counts as a build that isn't a release.
    def test_a_build_without_args(self):
        result = self.package("--release", "1.2.3.4-1")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("the build is for revision 0 (0: not a release)", result.stderr)

    # Any spelling GN reads counts, and the last line wins, as in GN.
    def test_args_as_written_by_hand(self):
        for line in ("lb_release_revision=3", "  lb_release_revision = 3  # by hand",
                     "lb_release_revision = 03"):
            self.build(line=line)
            result = self.package("--release", "1.2.3.4-3")
            self.assertNotIn("the build is for revision", result.stderr, line)
            self.assertIn(PAST_THE_CHECK, result.stderr, line)
        self.build(revision=3, line="lb_release_revision = 2")
        result = self.package("--release", "1.2.3.4-3")
        self.assertIn("the build is for revision 2", result.stderr)

    # Packaging without a version (build.sh --package) gives no updater and the Chromium build
    # number as the bundle version: a release build's About would say otherwise.
    def test_without_a_version_a_release_build_warns(self):
        self.build(revision=2)
        result = self.package()
        self.assertIn("warning: the build is for release 1.2.3.4-2, which its About page shows",
                      result.stderr)
        self.assertIn(PAST_THE_CHECK, result.stderr)

    def test_without_a_version_a_plain_build_is_quiet(self):
        self.build()
        result = self.package()
        self.assertNotIn("the build is for", result.stderr)
        self.assertIn(PAST_THE_CHECK, result.stderr)

    def test_a_version_on_another_chromium_version(self):
        self.build(revision=1)
        result = self.package("--release", "1.2.3.5-1")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("not on this tree's Chromium version, 1.2.3.4", result.stderr)


if __name__ == "__main__":
    unittest.main()

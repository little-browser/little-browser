# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Tests for publish-release.sh, run as a dry run against a stand-in `aws` (no uploads):
python3 -I -m unittest discover -s desktop/scripts. Needs bash, jq and git."""

import json
import os
import re
import shutil
import stat
import subprocess
import tempfile
import unittest

SCRIPTS = os.path.dirname(os.path.abspath(__file__))
PUBLISH = os.path.join(SCRIPTS, "publish-release.sh")
with open(os.path.join(os.path.dirname(os.path.dirname(SCRIPTS)), "chromium", "VERSIONS")) as _f:
    CHROMIUM = re.search(r"^CHROMIUM_VERSION=(\S+)", _f.read(), re.M).group(1)
HOST = "https://downloads.invalid"

# Answers the two reads a dry run makes: is a file already published, and the current
# releases.json. Everything else fails, so a dry run that tried to upload would show it.
AWS = """#!/bin/sh
case "$1 $2" in
  "s3api head-object") key="$(echo "$@" | sed 's/.*--key //')"; [ -f "$STUB/published/$key" ] ;;
  "s3 cp") [ -f "$STUB/releases.json" ] && cp "$STUB/releases.json" "$5" ;;
  *) echo "unexpected aws $*" >&2; exit 9 ;;
esac
"""


def version(revision):
    return "%s-%d" % (CHROMIUM, revision)


def entry(revision, arches=("arm64",), chromium=CHROMIUM, security=False):
    v = version(revision)
    return {"version": v, "date": "2026-11-20", "channel": "beta", "chromium": chromium,
            "security": security, "notes": ["Notes of %s." % v],
            "files": [{"platform": "macos", "arch": a,
                       "url": "%s/%s/LittleBrowser-%s-macos-%s.dmg" % (HOST, v, v, a),
                       "size": 1, "sha256": "ab"} for a in arches]}


@unittest.skipUnless(shutil.which("jq") and shutil.which("git"), "needs jq and git")
class PublishReleaseTest(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.mkdtemp(prefix="lb-publish-")
        self.addCleanup(shutil.rmtree, self.dir)
        os.makedirs(os.path.join(self.dir, "bin"))
        os.makedirs(os.path.join(self.dir, "published"))
        aws = os.path.join(self.dir, "bin", "aws")
        with open(aws, "w") as f:
            f.write(AWS)
        os.chmod(aws, os.stat(aws).st_mode | stat.S_IXUSR)

    def listed(self, *releases):
        with open(os.path.join(self.dir, "releases.json"), "w") as f:
            json.dump({"releases": list(releases)}, f)

    def publish(self, revision, arch, name=None, check=True, **env):
        dmg = os.path.join(self.dir, name or "LittleBrowser-macos-%s.dmg" % arch)
        with open(dmg, "w") as f:
            f.write("disk image")
        run_env = dict(os.environ, PATH="%s/bin:%s" % (self.dir, os.environ["PATH"]),
                       STUB=self.dir, LB_DRY_RUN="1", LB_DOWNLOADS_BUCKET="bucket",
                       LB_DOWNLOADS_DISTRIBUTION="dist", LB_DOWNLOADS_URL=HOST,
                       LB_RELEASE_NOTES="A note.")
        for k in ("LB_SECURITY_RELEASE",):
            run_env.pop(k, None)
        run_env.update(env)
        result = subprocess.run(["bash", PUBLISH, version(revision), dmg], env=run_env,
                                capture_output=True, text=True)
        if check:
            self.assertEqual(result.returncode, 0, result.stderr)
        return result

    def releases(self, result):
        text = result.stdout.split("dry run: this releases.json:\n", 1)[1]
        return json.loads(text.split("dry run: GitLab's release would then read:", 1)[0])

    def test_the_architecture_comes_from_the_file_name(self):
        result = self.publish(1, "x64")
        files = self.releases(result)["releases"][0]["files"]
        self.assertEqual([(f["platform"], f["arch"]) for f in files], [("macos", "x64")])
        self.assertTrue(files[0]["url"].endswith("/LittleBrowser-%s-macos-x64.dmg" % version(1)))
        self.assertIn("dry run: this updates/macos-x64.xml:", result.stdout)
        self.assertIn("s3://bucket/%s/LittleBrowser-%s-macos-x64.dmg" % (version(1), version(1)),
                      result.stdout)

    def test_other_file_names_are_refused(self):
        for name in ("LittleBrowser-macos-x86_64.dmg", "LittleBrowser.dmg"):
            with self.subTest(name=name):
                result = self.publish(1, "x64", name=name, check=False)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("is not LittleBrowser-macos-<arm64 or x64>.dmg", result.stderr)

    def test_a_published_build_is_never_replaced(self):
        v = version(1)
        os.makedirs(os.path.join(self.dir, "published", v))
        open(os.path.join(self.dir, "published", v, "LittleBrowser-%s-macos-x64.dmg" % v), "w").close()
        result = self.publish(1, "x64", check=False)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("is already published", result.stderr)

    def test_a_later_build_joins_its_release(self):
        self.listed(entry(1, security=True))
        result = self.publish(1, "x64", LB_RELEASE_NOTES="")
        release = self.releases(result)["releases"][0]
        self.assertEqual([f["arch"] for f in release["files"]], ["arm64", "x64"])
        self.assertEqual(release["notes"], ["Notes of %s." % version(1)])
        self.assertTrue(release["security"])
        self.assertIn("security release: true", result.stdout)

    def test_a_later_build_refuses_overrides_it_cannot_apply(self):
        self.listed(entry(1))
        for env in ({"LB_SECURITY_RELEASE": "1", "LB_RELEASE_NOTES": ""}, {"LB_RELEASE_NOTES": "Reworded."}):
            with self.subTest(env=env):
                result = self.publish(1, "x64", check=False, **env)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("is already out for another build", result.stderr)

    def test_a_release_goes_in_version_order(self):
        # Apple silicon's -6 and -10 are out; Intel's -5 and then -9 arrive later.
        self.listed(entry(10), entry(6), entry(4))
        result = self.publish(9, "x64")
        self.assertEqual([r["version"] for r in self.releases(result)["releases"]],
                         [version(10), version(9), version(6), version(4)])

    def test_notes_and_security_count_from_the_release_before_by_version(self):
        # -4 is on an older Chromium, -6 on this one: -5's previous release is -4, so it is a
        # Chromium update with its security fixes, even though -6 is listed first.
        self.listed(entry(6), entry(4, chromium="1.0.0.1"))
        result = self.publish(5, "x64")
        release = next(r for r in self.releases(result)["releases"] if r["version"] == version(5))
        self.assertTrue(release["security"])
        self.assertEqual(release["notes"][0], "Based on Chromium %s, with its security fixes." % CHROMIUM)


if __name__ == "__main__":
    unittest.main()

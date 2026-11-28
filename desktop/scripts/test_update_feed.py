# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Tests for update_feed.py: python3 -I -m unittest discover -s desktop/scripts"""

import io
import json
import tempfile
import unittest
import xml.etree.ElementTree as ET
from contextlib import redirect_stdout
from pathlib import Path

import update_feed

SP = "{%s}" % update_feed.SPARKLE_NS
HOST = "https://downloads.littlebrowser.app/"


def release(version, build, security=False, update=True, platform="macos", arch="arm64",
            date="2026-11-21"):
    name = "%s/LittleBrowser-%s-%s-%s.dmg" % (version, version, platform, arch)
    f = {"platform": platform, "arch": arch, "url": HOST + name, "size": 123456, "sha256": "ab"}
    if update:
        f["update"] = {"url": HOST + "updates/" + name, "bundle_version": build,
                       "minimum_system_version": "12.0", "ed_signature": "c2ln+/=="}
    return {"version": version, "date": date, "channel": "alpha", "chromium": version[:-2],
            "security": security, "files": [f]}


def both(version, build, security=False, archs=("arm64", "x64")):
    """A release with a file for each architecture in archs."""
    r = release(version, build, security=security, arch=archs[0])
    for arch in archs[1:]:
        r["files"] += release(version, build, arch=arch)["files"]
    return r


def items(releases, platform="macos", arch="arm64"):
    root = ET.fromstring(update_feed.appcast({"releases": releases}, platform, arch))
    return root.findall("./channel/item")


class AppcastTest(unittest.TestCase):
    def test_lists_only_the_newest_release(self):
        found = items([release("157.0.8103.90-1", "8103.90.1"),
                       release("157.0.8103.78-1", "8103.78.1")])
        self.assertEqual(len(found), 1)
        item = found[0]
        self.assertEqual(item.findtext(SP + "version"), "8103.90.1")
        self.assertEqual(item.findtext(SP + "shortVersionString"), "157.0.8103.90-1")
        self.assertEqual(item.findtext(SP + "minimumSystemVersion"), "12.0")
        self.assertEqual(item.findtext(SP + "hardwareRequirements"), "arm64")
        self.assertEqual(item.findtext("pubDate"), "Sat, 21 Nov 2026 00:00:00 +0000")

    def test_enclosure_is_the_update_copy_with_its_signature(self):
        enclosure = items([release("157.0.8103.78-1", "8103.78.1")])[0].find("enclosure")
        self.assertEqual(enclosure.get("url"), HOST + "updates/157.0.8103.78-1/"
                         "LittleBrowser-157.0.8103.78-1-macos-arm64.dmg")
        self.assertEqual(enclosure.get("length"), "123456")
        self.assertEqual(enclosure.get(SP + "edSignature"), "c2ln+/==")

    def test_release_without_an_update_file_is_skipped(self):
        found = items([release("157.0.8103.90-1", "8103.90.1", update=False),
                       release("157.0.8103.78-1", "8103.78.1")])
        self.assertEqual(found[0].findtext(SP + "version"), "8103.78.1")

    def test_other_platforms_are_skipped(self):
        self.assertEqual(items([release("157.0.8103.78-1", "8103.78.1", arch="x64")]), [])

    def test_no_releases_is_an_empty_feed(self):
        self.assertEqual(items([]), [])

    def test_not_critical_without_a_security_release(self):
        item = items([release("157.0.8103.78-2", "8103.78.2")])[0]
        self.assertIsNone(item.find(SP + "criticalUpdate"))

    def test_security_release_is_critical_for_every_older_version(self):
        item = items([release("157.0.8103.90-1", "8103.90.1", security=True)])[0]
        self.assertEqual(item.find(SP + "criticalUpdate").get(SP + "version"), "8103.90.1")

    def test_older_security_release_stays_critical_for_versions_before_it(self):
        item = items([release("157.0.8103.90-2", "8103.90.2"),
                      release("157.0.8103.90-1", "8103.90.1", security=True),
                      release("157.0.8103.78-1", "8103.78.1", security=True)])[0]
        self.assertEqual(item.findtext(SP + "version"), "8103.90.2")
        self.assertEqual(item.find(SP + "criticalUpdate").get(SP + "version"), "8103.90.1")

    def test_text_is_escaped(self):
        r = release("157.0.8103.78-1", "8103.78.1")
        r["files"][0]["update"]["ed_signature"] = 'a"<&'
        enclosure = items([r])[0].find("enclosure")
        self.assertEqual(enclosure.get(SP + "edSignature"), 'a"<&')


class ArchitecturesTest(unittest.TestCase):
    # -5 is out for Apple silicon only; Intel is still on -4.
    STAGGERED = [both("157.0.8103.78-5", "8103.78.5", archs=("arm64",)),
                 both("157.0.8103.78-4", "8103.78.4")]

    def test_each_architecture_gets_its_own_newest_release(self):
        arm = items(self.STAGGERED, arch="arm64")[0]
        intel = items(self.STAGGERED, arch="x64")[0]
        self.assertEqual(arm.findtext(SP + "version"), "8103.78.5")
        self.assertEqual(intel.findtext(SP + "version"), "8103.78.4")
        self.assertEqual(intel.find("enclosure").get("url"), HOST + "updates/157.0.8103.78-4/"
                         "LittleBrowser-157.0.8103.78-4-macos-x64.dmg")

    def test_only_the_apple_silicon_feed_requires_arm64(self):
        self.assertEqual(items(self.STAGGERED, arch="arm64")[0].findtext(SP + "hardwareRequirements"),
                         "arm64")
        self.assertIsNone(items(self.STAGGERED, arch="x64")[0].find(SP + "hardwareRequirements"))

    def test_security_release_published_for_another_architecture_only_still_counts(self):
        # Intel skipped -3, a security release; -4 has its fixes, so Intel's update is critical
        # for every version before -3.
        item = items([both("157.0.8103.90-4", "8103.90.4"),
                      both("157.0.8103.90-3", "8103.90.3", security=True, archs=("arm64",)),
                      both("157.0.8103.78-1", "8103.78.1")], arch="x64")[0]
        self.assertEqual(item.findtext(SP + "version"), "8103.90.4")
        self.assertEqual(item.find(SP + "criticalUpdate").get(SP + "version"), "8103.90.3")

    def test_security_release_newer_than_the_offered_one_does_not_count(self):
        # Intel's -6 isn't out yet: its feed still offers -5, which isn't a security release.
        item = items([both("158.0.8200.1-6", "8200.1.6", security=True, archs=("arm64",)),
                      both("157.0.8103.78-5", "8103.78.5")], arch="x64")[0]
        self.assertEqual(item.findtext(SP + "version"), "8103.78.5")
        self.assertIsNone(item.find(SP + "criticalUpdate"))


class OrderTest(unittest.TestCase):
    def test_releases_are_taken_in_version_order_whatever_the_list_order(self):
        # Intel's -5 published after Apple silicon's -6 (a security release) was listed above it.
        listed = [both("157.0.8103.78-5", "8103.78.5", archs=("x64",)),
                  both("157.0.8103.78-6", "8103.78.6", security=True),
                  both("157.0.8103.78-4", "8103.78.4")]
        intel = items(listed, arch="x64")[0]
        self.assertEqual(intel.findtext(SP + "version"), "8103.78.6")
        arm = items(listed, arch="arm64")[0]
        self.assertEqual(arm.findtext(SP + "version"), "8103.78.6")

    def test_an_older_release_isnt_critical_for_a_newer_security_release(self):
        listed = [both("157.0.8103.78-5", "8103.78.5", archs=("x64",)),
                  both("157.0.8103.78-6", "8103.78.6", security=True, archs=("arm64",))]
        intel = items(listed, arch="x64")[0]
        self.assertEqual(intel.findtext(SP + "version"), "8103.78.5")
        self.assertIsNone(intel.find(SP + "criticalUpdate"))

    def test_ten_comes_after_nine(self):
        listed = [both("157.0.8103.78-9", "8103.78.9"), both("157.0.8103.78-10", "8103.78.10")]
        self.assertEqual(items(listed)[0].findtext(SP + "version"), "8103.78.10")


class MainTest(unittest.TestCase):
    def test_reads_releases_json(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp, "releases.json")
            path.write_text(json.dumps({"releases": [release("157.0.8103.78-1", "8103.78.1")]}))
            out = io.StringIO()
            with redirect_stdout(out):
                update_feed.main([str(path), "macos", "arm64"])
            self.assertEqual(len(ET.fromstring(out.getvalue()).findall("./channel/item")), 1)

    def test_wrong_arguments(self):
        with self.assertRaises(SystemExit):
            update_feed.main(["only-one"])

    def test_architecture_uses_the_published_names(self):
        # x64, not uname's x86_64: the feed's name and releases.json use the same word.
        for arch in ("x86_64", "aarch64", ""):
            with self.assertRaises(SystemExit):
                update_feed.main(["releases.json", "macos", arch])


if __name__ == "__main__":
    unittest.main()

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


if __name__ == "__main__":
    unittest.main()

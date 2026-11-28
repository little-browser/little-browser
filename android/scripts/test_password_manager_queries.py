# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""The manifest names every password manager first run offers, so the browser can see whether
one is installed (android/patches/password-manager-queries.patch)."""

import os
import re
import unittest

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def read(*path):
    with open(os.path.join(REPO, *path), encoding="utf-8") as f:
        return f.read()


class PasswordManagerQueriesTest(unittest.TestCase):
    def test_the_manifest_names_every_offered_app(self):
        offered = set(re.findall(r'android_package: "([^"]+)"',
                                 read("core", "lb-core", "src", "password_manager.rs")))
        self.assertTrue(offered)
        added = set(re.findall(r'^\+\s*<package android:name="([^"]+)"',
                               read("android", "patches", "password-manager-queries.patch"),
                               re.M))
        self.assertEqual(offered, added)


if __name__ == "__main__":
    unittest.main()

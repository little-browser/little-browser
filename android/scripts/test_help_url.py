# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Help opens our site's guide, the same page on Android as on desktop
(android/patches/help-url.patch, desktop/patches/lb/help-url.patch)."""

import os
import re
import unittest

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
GUIDE = "https://littlebrowser.app/features"


def added_urls(*path):
    with open(os.path.join(REPO, *path), encoding="utf-8") as f:
        return re.findall(r'^\+.*"(https://[^"]+)";', f.read(), re.M)


class HelpUrlTest(unittest.TestCase):
    def test_android_help_opens_the_guide(self):
        self.assertEqual(added_urls("android", "patches", "help-url.patch"), [GUIDE])

    def test_desktop_help_opens_the_same_guide(self):
        self.assertEqual(set(added_urls("desktop", "patches", "lb", "help-url.patch")), {GUIDE})

    def test_the_guide_is_a_page_of_our_site(self):
        self.assertTrue(os.path.isfile(os.path.join(REPO, "site", "features", "index.html")))


if __name__ == "__main__":
    unittest.main()

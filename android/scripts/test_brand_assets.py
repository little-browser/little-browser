# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Tests for brand_assets.sh: our launcher icon replaces Chromium's, under Chromium's names."""

import os
import subprocess
import tempfile
import unittest

SCRIPTS = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(SCRIPTS))
SCRIPT = os.path.join(SCRIPTS, "brand_assets.sh")
EXPORTS = os.path.join(REPO, "branding", "exports", "android", "res")
DENSITIES = ("mdpi", "hdpi", "xhdpi", "xxhdpi", "xxxhdpi")


def read(path):
    with open(path, "rb") as f:
        return f.read()


class BrandAssetsTest(unittest.TestCase):
    def setUp(self):
        self.src = tempfile.mkdtemp()
        self.res = os.path.join(self.src, "chrome", "android", "java", "res_chromium_base")
        self.logos = os.path.join(self.src, "components", "browser_ui", "styles", "android",
                                  "java", "res_chromium")

    def chromium_icons(self, densities=DENSITIES):
        for d in densities:
            folder = os.path.join(self.res, f"mipmap-{d}")
            os.makedirs(folder)
            for name in ("app_icon", "layered_app_icon", "layered_app_icon_background"):
                with open(os.path.join(folder, f"{name}.png"), "wb") as f:
                    f.write(b"chromium")
            folder = os.path.join(self.logos, f"drawable-{d}")
            os.makedirs(folder)
            for name in ("fre_product_logo", "product_logo_name"):
                with open(os.path.join(folder, f"{name}.png"), "wb") as f:
                    f.write(b"chromium")

    def run_script(self):
        return subprocess.run(["bash", SCRIPT, self.src], capture_output=True, text=True)

    def test_every_density_gets_our_layers(self):
        self.chromium_icons()
        self.assertEqual(self.run_script().returncode, 0)
        for d in DENSITIES:
            ours = os.path.join(EXPORTS, f"mipmap-{d}")
            theirs = os.path.join(self.res, f"mipmap-{d}")
            self.assertEqual(read(os.path.join(theirs, "app_icon.png")),
                             read(os.path.join(ours, "ic_launcher.png")))
            self.assertEqual(read(os.path.join(theirs, "layered_app_icon.png")),
                             read(os.path.join(ours, "ic_launcher_foreground.png")))
            self.assertEqual(read(os.path.join(theirs, "layered_app_icon_background.png")),
                             read(os.path.join(ours, "ic_launcher_background.png")))

    def test_every_density_gets_our_logos_and_a_dark_lockup(self):
        self.chromium_icons()
        self.assertEqual(self.run_script().returncode, 0)
        for d in DENSITIES:
            ours = os.path.join(EXPORTS, f"drawable-{d}")
            for name in ("fre_product_logo.png", "product_logo_name.png"):
                self.assertEqual(read(os.path.join(self.logos, f"drawable-{d}", name)),
                                 read(os.path.join(ours, name)), f"{d}/{name}")
            self.assertEqual(
                read(os.path.join(self.logos, f"drawable-night-{d}", "product_logo_name.png")),
                read(os.path.join(EXPORTS, f"drawable-night-{d}", "product_logo_name.png")))

    def test_a_tree_without_chromiums_icons_is_an_error(self):
        self.chromium_icons(DENSITIES[:-1])
        result = self.run_script()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("mipmap-xxxhdpi is missing", result.stderr)


class NotificationIconTest(unittest.TestCase):
    # Notifications show ic_chrome in the status bar and on the lock screen; it is a vector, so
    # app-icon.patch, not brand_assets.sh, puts our mark there.
    def test_app_icon_patch_replaces_the_notification_icon(self):
        patch = read(os.path.join(REPO, "android", "patches", "app-icon.patch"))
        self.assertIn(
            b"+++ b/components/browser_ui/styles/android/java/res/drawable/ic_chrome.xml", patch)


if __name__ == "__main__":
    unittest.main()

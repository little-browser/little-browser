# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Tests for check_series.py, and for check_patches.py's series reading."""

import os
import sys
import tempfile
import textwrap
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import check_patches  # noqa: E402
import check_series  # noqa: E402


def patch_for(*paths):
    return "".join(
        f"--- a/{p}\n+++ b/{p}\n@@ -1 +1 @@\n-old\n+new\n" for p in paths)


class Tree:
    """A patches folder: a series and its patch files."""

    def __init__(self, series, patches):
        self.dir = tempfile.mkdtemp(prefix="lb-series-")
        with open(os.path.join(self.dir, "series"), "w") as f:
            f.write(textwrap.dedent(series))
        for name, text in patches.items():
            path = os.path.join(self.dir, name)
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path, "w") as f:
                f.write("# why\n\n" + text)

    def desktop(self, platforms=("mac",)):
        return check_series.check_tree(self.dir, list(platforms), check_series.desktop_layer)

    def android(self):
        return check_series.check_tree(self.dir, [], check_series.android_layer)

    def shared(self):
        return check_series.check_tree(self.dir, ["mac", "android", "desktop"],
                                       check_series.shared_layer, only="engine")


class ReadSeriesTest(unittest.TestCase):
    def test_tags_and_comments(self):
        tree = Tree("""\
            # header comment
            lb/a.patch   # for every platform
            lb/b.patch   # [mac] macOS only

            lb/c.patch
            lb/d.patch   # [linux] Linux only, with [brackets] later
            """, {})
        path = os.path.join(tree.dir, "series")
        self.assertEqual(check_patches.read_series(path), [
            ("lb/a.patch", None), ("lb/b.patch", "mac"), ("lb/c.patch", None),
            ("lb/d.patch", "linux")])
        self.assertEqual([n for n, _ in check_patches.read_series(path, "mac")],
                         ["lb/a.patch", "lb/b.patch", "lb/c.patch"])
        self.assertEqual([n for n, _ in check_patches.read_series(path, "linux")],
                         ["lb/a.patch", "lb/c.patch", "lb/d.patch"])

    def test_desktop_tag_is_every_desktop_platform(self):
        tree = Tree("""\
            a.patch   # [desktop] every desktop platform
            b.patch   # [android] its Android twin
            c.patch
            """, {})
        path = os.path.join(tree.dir, "series")
        for platform in ("mac", "linux", "win"):
            self.assertEqual([n for n, _ in check_patches.read_series(path, platform)],
                             ["a.patch", "c.patch"])
        self.assertEqual([n for n, _ in check_patches.read_series(path, "android")],
                         ["b.patch", "c.patch"])

    def test_tag_only_at_the_start_of_the_reason(self):
        tree = Tree("lb/a.patch  # a reason that mentions [mac] later\n", {})
        self.assertEqual(check_patches.read_series(os.path.join(tree.dir, "series")),
                         [("lb/a.patch", None)])

    def test_platform_repo(self):
        self.assertEqual(check_patches.platform_repo("mac"), "ungoogled-chromium-macos")


class DesktopTreeTest(unittest.TestCase):
    def test_clean(self):
        tree = Tree("""\
            lb/engine.patch
            lb/engine-ui.patch
            lb/mac.patch   # [mac] anything goes on its own platform
            """, {
                "lb/engine.patch": patch_for("chrome/browser/ui/tabs/tab_strip_model.cc",
                                             "chrome/app/generated_resources.grd"),
                "lb/engine-ui.patch": patch_for("chrome/browser/ui/views/tabs/tab_view.cc",
                                                "chrome/browser/resources/settings/a.ts",
                                                "chrome/app/settings_strings.grdp"),
                "lb/mac.patch": patch_for("chrome/browser/ui/cocoa/x.mm",
                                          "chrome/common/chrome_paths.cc"),
            })
        self.assertEqual(tree.desktop(), [])

    def test_mixed_layers(self):
        tree = Tree("lb/both.patch\n", {
            "lb/both.patch": patch_for("components/page_info/page_info.h",
                                       "chrome/browser/ui/views/page_info/view.cc")})
        [error] = tree.desktop()
        self.assertIn("lb/both.patch: mixes engine and view code", error)
        self.assertIn("view: chrome/browser/ui/views/page_info/view.cc", error)

    def test_webui_handlers_are_view_code(self):
        tree = Tree("lb/both.patch\n", {
            "lb/both.patch": patch_for("chrome/browser/ui/webui/settings/handler.cc",
                                       "chrome/browser/chrome_browser_main.cc")})
        self.assertIn("mixes engine and view code", tree.desktop()[0])

    def test_strings_go_with_either(self):
        tree = Tree("lb/a.patch\nlb/b.patch\n", {
            "lb/a.patch": patch_for("components/x.cc", "components/x_strings.grdp"),
            "lb/b.patch": patch_for("ui/views/x.cc", "chrome/app/x.grd")})
        self.assertEqual(tree.desktop(), [])

    def test_platform_files_need_their_tag(self):
        tree = Tree("lb/a.patch\nlb/b.patch  # [mac] fine\nlb/c.patch\n", {
            "lb/a.patch": patch_for("chrome/browser/app_controller_mac.mm"),
            "lb/b.patch": patch_for("chrome/browser/mac/initial_prefs.mm"),
            "lb/c.patch": patch_for("chrome/browser/foo_win.cc")})
        errors = tree.desktop()
        self.assertEqual(len(errors), 2, errors)
        self.assertIn("lb/a.patch: touches mac-only files", errors[0])
        self.assertIn("tag it [mac]", errors[0])
        self.assertIn("lb/c.patch: touches win-only files", errors[1])

    def test_unknown_tag(self):
        tree = Tree("lb/a.patch  # [beos] no such platform\n", {
            "lb/a.patch": patch_for("components/x.cc")})
        self.assertEqual(tree.desktop(), [
            "lb/a.patch: tagged [beos], not a platform here (known: mac)"])

    def test_missing_and_unlisted(self):
        tree = Tree("lb/listed.patch\nlb/gone.patch\n", {
            "lb/listed.patch": patch_for("components/x.cc"),
            "lb/stray.patch": patch_for("components/y.cc")})
        errors = tree.desktop()
        self.assertIn("lb/stray.patch: not in", errors[0])
        self.assertEqual(errors[1], "lb/gone.patch: in the series but missing")


class AndroidTreeTest(unittest.TestCase):
    def test_java_and_native_apart(self):
        tree = Tree("a.patch\nb.patch\n", {
            "a.patch": patch_for("components/x.cc", "chrome/browser/y.h"),
            "b.patch": patch_for("chrome/android/java/src/A.java")})
        self.assertEqual(tree.android(), [])

    def test_java_and_native_mixed(self):
        tree = Tree("a.patch\n", {
            "a.patch": patch_for("components/x.cc", "chrome/android/java/src/A.java")})
        self.assertIn("a.patch: mixes Java and native code", tree.android()[0])

    def test_android_files_need_no_tag(self):
        tree = Tree("a.patch\n", {"a.patch": patch_for("chrome/browser/android/tab_android.cc")})
        self.assertEqual(tree.android(), [])

    def test_no_platform_tags(self):
        tree = Tree("a.patch  # [mac] no platforms on Android\n", {
            "a.patch": patch_for("components/x.cc")})
        self.assertIn("not a platform here (known: none)", tree.android()[0])


class SharedSeriesTest(unittest.TestCase):
    def test_engine_and_strings(self):
        tree = Tree("a.patch\n", {
            "a.patch": patch_for("components/x.cc", "chrome/browser/x.cc", "chrome/app/x.grdp")})
        self.assertEqual(tree.shared(), [])

    def test_view_code_goes_to_desktop(self):
        tree = Tree("a.patch\n", {"a.patch": patch_for("chrome/browser/ui/views/x.cc")})
        self.assertEqual(tree.shared(), [
            "a.patch: view code (chrome/browser/ui/views/x.cc) in a shared patch; it goes in "
            "desktop/patches"])

    def test_java_goes_to_android(self):
        tree = Tree("a.patch\n", {"a.patch": patch_for("chrome/android/java/src/A.java")})
        self.assertIn(
            "a.patch: Java code (chrome/android/java/src/A.java) in a shared patch; it goes in "
            "android/patches", tree.shared())

    def test_android_tag(self):
        # A shared patch whose bases differ: the desktop's copy and its [android] twin.
        tree = Tree("a.patch  # [desktop] the tarball's base\n"
                    "a-android.patch  # [android] Android's base differs\n", {
                        "a.patch": patch_for("components/x.cc"),
                        "a-android.patch": patch_for("components/x.cc")})
        self.assertEqual(tree.shared(), [])

    def test_tagged_patches_hold_engine_code_too(self):
        tree = Tree("a.patch  # [mac]\nb.patch  # [android]\n", {
            "a.patch": patch_for("chrome/browser/ui/cocoa/x.mm"),
            "b.patch": patch_for("chrome/android/java/src/B.java")})
        self.assertEqual(tree.shared(), [
            "a.patch: view code (chrome/browser/ui/cocoa/x.mm) in a shared patch; it goes in "
            "desktop/patches",
            "b.patch: Java code (chrome/android/java/src/B.java) in a shared patch; it goes in "
            "android/patches"])

    def test_android_files_need_their_tag(self):
        tree = Tree("a.patch\nb.patch  # [android]\n", {
            "a.patch": patch_for("chrome/browser/android/tab_android.cc"),
            "b.patch": patch_for("chrome/browser/android/tab_android.cc")})
        errors = tree.shared()
        self.assertEqual(len(errors), 1)
        self.assertIn("a.patch: touches android-only files", errors[0])

    def test_platform_files_still_tagged(self):
        tree = Tree("a.patch\n", {"a.patch": patch_for("chrome/browser/x_mac.mm")})
        self.assertIn("touches mac-only files", tree.shared()[0])


class RepositoryTest(unittest.TestCase):
    def test_our_series_are_layered(self):
        self.assertEqual(check_series.check_repository(), [])

    def test_shared_series_is_not_empty(self):
        series = os.path.join(check_series.REPO, "chromium", "patches", "series")
        self.assertTrue(check_patches.read_series(series))


if __name__ == "__main__":
    unittest.main()

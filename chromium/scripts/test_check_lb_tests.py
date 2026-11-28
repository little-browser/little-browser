# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Tests for check_lb_tests.py's checks."""

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import check_lb_tests  # noqa: E402

BUILD_GN = """
rust_static_library("lb_core") {
  crate_root = "//lb_core/src/lib.rs"
  sources = [
    "//lb_core/src/import/mod.rs",
    "//lb_core/src/lib.rs",
  ]
}
"""


class CoreSourcesTest(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.src = Path(self.dir.name)
        (self.src / "import").mkdir()
        (self.src / "lib.rs").write_text("")
        (self.src / "import" / "mod.rs").write_text("")

    def tearDown(self):
        self.dir.cleanup()

    def test_matching_lists_pass(self):
        self.assertEqual(check_lb_tests.core_source_problems(BUILD_GN, self.src), [])

    def test_a_new_file_must_be_listed(self):
        (self.src / "import" / "arc.rs").write_text("")
        self.assertEqual(
            check_lb_tests.core_source_problems(BUILD_GN, self.src),
            ["//lb_core/src/import/arc.rs: in core/lb-core but not in lb_core's sources"],
        )

    def test_a_removed_file_must_be_unlisted(self):
        (self.src / "lib.rs").unlink()
        self.assertEqual(
            check_lb_tests.core_source_problems(BUILD_GN, self.src),
            ["//lb_core/src/lib.rs: in lb_core's sources but not in core/lb-core"],
        )

    def test_the_real_tree_passes(self):
        build_gn = (check_lb_tests.LB / "BUILD.gn").read_text()
        self.assertEqual(check_lb_tests.core_source_problems(build_gn, check_lb_tests.CORE_SRC), [])


class TestedSourcesTest(unittest.TestCase):
    def test_every_tested_target_counts(self):
        build_gn = """
source_set("model") {
  sources = [ "a.cc", "a.h" ]
}
source_set("engine") {
  sources = [ "e.cc", "e.h" ]
}
if (!is_android) {
  source_set("prefs") {
    sources = [ "p.cc", "p.h" ]
  }
}
source_set("page_names") {
  sources = [ "common/page_names.cc", "common/page_names.h" ]
}
"""
        self.assertEqual(
            check_lb_tests.tested_sources(build_gn), ["a.cc", "e.cc", "p.cc", "common/page_names.cc"]
        )

    def test_a_missing_tested_target_fails(self):
        with self.assertRaises(SystemExit):
            check_lb_tests.tested_sources('source_set("model") {\n  sources = [ "a.cc" ]\n}\n')

    def test_a_target_without_sources_does_not_take_the_next_ones(self):
        build_gn = """
source_set("prefs") {
  public = [ "p.h" ]
}
source_set("engine") {
  sources = [ "e.cc" ]
}
"""
        with self.assertRaises(SystemExit):
            check_lb_tests.target_sources(build_gn, "source_set", "prefs")

    def test_conditional_sources_count(self):
        build_gn = """
source_set("engine") {
  sources = [
    "e.cc",  # a comment with "quotes" and a } brace
  ]
  if (is_android) {
    sources += [ "e_android.cc" ]
  }
}
source_set("other") {
  sources = [ "o.cc" ]
}
"""
        self.assertEqual(check_lb_tests.target_sources(build_gn, "source_set", "engine"),
                         ["e.cc", "e_android.cc"])

    def test_the_real_tree_tests_shared_code(self):
        # The engine code every Chromium platform builds is tested, not only the desktop model.
        sources = check_lb_tests.tested_sources((check_lb_tests.LB / "BUILD.gn").read_text())
        for source in ("common/page_names.cc", "browser/prefs/lb_prefs.cc",
                       "browser/privacy/tracking_params.cc", "browser/adblock/filter_lists.cc"):
            self.assertIn(source, sources)


ANDROID_BUILD_GN = """
robolectric_binary("lb_junit_tests") {
  sources = [ "junit/src/app/lb/tabs/TabRulesTest.java" ]
}
"""


class JavaTestsTest(unittest.TestCase):
    """Every class of the Android shell has a Robolectric test, listed in lb_junit_tests."""

    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.android = Path(self.dir.name)
        self.write("BUILD.gn", ANDROID_BUILD_GN)
        self.write("java/src/app/lb/tabs/TabRules.java", "class TabRules {}")
        self.write("junit/src/app/lb/tabs/TabRulesTest.java", "    @Test\n    public void a() {}")

    def tearDown(self):
        self.dir.cleanup()

    def write(self, rel, text):
        path = self.android / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)

    def test_a_tested_class_passes(self):
        self.assertEqual(check_lb_tests.java_test_problems(self.android), [])

    # The error names the Android BUILD.gn, the file to fix.
    def test_a_missing_test_target_names_its_build_file(self):
        self.write("BUILD.gn", 'robolectric_binary("other_tests") {\n  sources = [ "a" ]\n}\n')
        with self.assertRaises(SystemExit) as raised:
            check_lb_tests.java_test_problems(self.android)
        self.assertIn(f'no robolectric_binary("lb_junit_tests") with sources in '
                      f'{self.android / "BUILD.gn"}', str(raised.exception))

    def test_a_class_without_a_test_fails(self):
        self.write("java/src/app/lb/adblock/SiteAds.java", "class SiteAds {}")
        problems = check_lb_tests.java_test_problems(self.android)
        self.assertEqual(len(problems), 1)
        self.assertIn("junit/src/app/lb/adblock/SiteAdsTest.java", problems[0])

    def test_an_unlisted_test_fails(self):
        self.write("java/src/app/lb/adblock/SiteAds.java", "class SiteAds {}")
        self.write("junit/src/app/lb/adblock/SiteAdsTest.java", "@Test")
        self.assertIn("no junit/src/app/lb/adblock/SiteAdsTest.java in lb_junit_tests",
                      check_lb_tests.java_test_problems(self.android)[0])

    def test_a_test_without_test_methods_fails(self):
        self.write("junit/src/app/lb/tabs/TabRulesTest.java", "class TabRulesTest {}")
        self.assertEqual(check_lb_tests.java_test_problems(self.android),
                         ["android/junit/src/app/lb/tabs/TabRulesTest.java: has no @Test"])

    def test_a_listed_test_must_exist(self):
        (self.android / "junit/src/app/lb/tabs/TabRulesTest.java").unlink()
        self.assertIn("listed in lb_junit_tests but missing",
                      check_lb_tests.java_test_problems(self.android)[0])

    def test_no_java_no_problems(self):
        with tempfile.TemporaryDirectory() as empty:
            self.assertEqual(check_lb_tests.java_test_problems(Path(empty)), [])

    def test_the_real_tree_passes(self):
        self.assertEqual(check_lb_tests.java_test_problems(check_lb_tests.ANDROID), [])


if __name__ == "__main__":
    unittest.main()

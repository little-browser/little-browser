# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Tests for check_lb_tests.py's lb_core check."""

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


if __name__ == "__main__":
    unittest.main()

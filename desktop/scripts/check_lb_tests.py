#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Checks that every source of desktop/lb's view-free model has unit tests.

    desktop/scripts/check_lb_tests.py

Every .cc file in the //lb:model target (desktop/lb/BUILD.gn) needs a <name>_unittest.cc next to
it, listed in lb_unittests, with at least one TEST. CI runs this on every push; it needs no
Chromium tree. The tests themselves run with the Chromium build (desktop/lb/AGENTS.md).
"""

import re
import sys
from pathlib import Path

LB = Path(__file__).resolve().parent.parent / "lb"


def target_sources(build_gn: str, kind: str, name: str) -> list[str]:
    """The sources list of `kind("name") { ... }` in a BUILD.gn file."""
    match = re.search(
        rf'{kind}\("{re.escape(name)}"\)\s*\{{.*?\bsources\s*=\s*\[(.*?)\]', build_gn, re.S
    )
    if not match:
        sys.exit(f"check_lb_tests: no {kind}(\"{name}\") with sources in {LB / 'BUILD.gn'}")
    return re.findall(r'"([^"]+)"', match.group(1))


def main() -> int:
    build_gn = (LB / "BUILD.gn").read_text()
    model = [s for s in target_sources(build_gn, "source_set", "model") if s.endswith(".cc")]
    tests = set(target_sources(build_gn, "test", "lb_unittests"))
    problems = []
    for source in model:
        test = source[: -len(".cc")] + "_unittest.cc"
        if test not in tests:
            problems.append(f"{source}: no {test} in lb_unittests")
        elif not (LB / test).is_file():
            problems.append(f"{test}: listed in lb_unittests but missing")
        elif not re.search(r"^\s*TEST(_F|_P)?\(", (LB / test).read_text(), re.M):
            problems.append(f"{test}: has no TEST")
    for test in sorted(tests):
        if not (LB / test).is_file():
            problems.append(f"{test}: listed in lb_unittests but missing")
    for problem in dict.fromkeys(problems):
        print(f"check_lb_tests: {problem}", file=sys.stderr)
    if problems:
        print(
            "Each //lb:model source needs unit tests (desktop/lb/AGENTS.md, Tests).",
            file=sys.stderr,
        )
        return 1
    print(f"check_lb_tests: {len(model)} model sources, each with unit tests")
    return 0


if __name__ == "__main__":
    sys.exit(main())

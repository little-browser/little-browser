#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Checks that every source of desktop/lb's view-free logic has unit tests.

    desktop/scripts/check_lb_tests.py

Every .cc file in the //lb:model and //lb:page_names targets (desktop/lb/BUILD.gn,
TESTED_TARGETS) needs a <name>_unittest.cc next to it, listed in lb_unittests, with at least one
TEST. The lb_core target must list exactly the .rs files of core/lb-core/src, so the browser
builds (and lb_core_unittests runs) all of lb-core. CI runs this on every push; it needs no
Chromium tree. The tests themselves run with the Chromium build (desktop/lb/AGENTS.md).
"""

import re
import sys
from pathlib import Path

LB = Path(__file__).resolve().parent.parent / "lb"
CORE_SRC = Path(__file__).resolve().parent.parent.parent / "core" / "lb-core" / "src"
# Where fetch.sh links core/lb-core in the Chromium tree.
CORE_GN_PREFIX = "//lb_core/src/"
# The source_sets of logic that needs no browser window, each .cc of which needs unit tests.
TESTED_TARGETS = ("model", "page_names")


def target_sources(build_gn: str, kind: str, name: str) -> list[str]:
    """The sources list of `kind("name") { ... }` in a BUILD.gn file."""
    match = re.search(
        rf'{kind}\("{re.escape(name)}"\)\s*\{{.*?\bsources\s*=\s*\[(.*?)\]', build_gn, re.S
    )
    if not match:
        sys.exit(f"check_lb_tests: no {kind}(\"{name}\") with sources in {LB / 'BUILD.gn'}")
    return re.findall(r'"([^"]+)"', match.group(1))


def core_source_problems(build_gn: str, core_src: Path) -> list[str]:
    """How the lb_core target's sources differ from the .rs files in `core_src`."""
    listed = set(target_sources(build_gn, "rust_static_library", "lb_core"))
    on_disk = {CORE_GN_PREFIX + p.relative_to(core_src).as_posix() for p in core_src.rglob("*.rs")}
    problems = [f"{s}: in core/lb-core but not in lb_core's sources" for s in sorted(on_disk - listed)]
    problems += [f"{s}: in lb_core's sources but not in core/lb-core" for s in sorted(listed - on_disk)]
    return problems


def tested_sources(build_gn: str) -> list[str]:
    """The .cc files of TESTED_TARGETS, which need unit tests."""
    return [
        source
        for target in TESTED_TARGETS
        for source in target_sources(build_gn, "source_set", target)
        if source.endswith(".cc")
    ]


def main() -> int:
    build_gn = (LB / "BUILD.gn").read_text()
    model = tested_sources(build_gn)
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
    core_problems = core_source_problems(build_gn, CORE_SRC)
    problems += core_problems
    for problem in dict.fromkeys(problems):
        print(f"check_lb_tests: {problem}", file=sys.stderr)
    if problems:
        print(
            "Each //lb:model and //lb:page_names source needs unit tests"
            " (desktop/lb/AGENTS.md, Tests)"
            + ("; lb_core lists lb-core's files (desktop/lb/BUILD.gn)." if core_problems else "."),
            file=sys.stderr,
        )
        return 1
    print(f"check_lb_tests: {len(model)} tested sources, each with unit tests")
    return 0


if __name__ == "__main__":
    sys.exit(main())

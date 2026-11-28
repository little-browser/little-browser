#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Checks that every source of chromium/lb's view-free logic has unit tests.

    chromium/scripts/check_lb_tests.py

Every .cc file in the //lb:model, //lb:engine, //lb:prefs and //lb:page_names targets
(chromium/lb/BUILD.gn, TESTED_TARGETS) needs a <name>_unittest.cc next to it, listed in lb_unittests, with at least one
TEST. The lb_core target must list exactly the .rs files of core/lb-core/src, so the browser
builds (and lb_core_unittests runs) all of lb-core. Every Java class of the Android shell
(chromium/lb/android/java/src) needs a <Name>Test.java at the same path under
chromium/lb/android/junit/src, listed in lb_junit_tests (chromium/lb/android/BUILD.gn), with at
least one @Test. CI runs this on every push; it needs no Chromium tree. The tests themselves run
with the Chromium builds (chromium/lb/AGENTS.md).
"""

import re
import sys
from pathlib import Path

LB = Path(__file__).resolve().parent.parent / "lb"
CORE_SRC = Path(__file__).resolve().parent.parent.parent / "core" / "lb-core" / "src"
# Where fetch.sh links core/lb-core in the Chromium tree.
CORE_GN_PREFIX = "//lb_core/src/"
ANDROID = LB / "android"
# The source_sets of logic that needs no browser window, each .cc of which needs unit tests.
TESTED_TARGETS = ("model", "engine", "prefs", "page_names")


def target_body(build_gn: str, kind: str, name: str) -> str | None:
    """The text between the braces of `kind("name") { ... }` in a BUILD.gn file, or None. Braces
    inside strings and comments don't count."""
    match = re.search(rf'\b{kind}\("{re.escape(name)}"\)\s*\{{', build_gn)
    if not match:
        return None
    depth, i, start = 1, match.end(), match.end()
    while i < len(build_gn):
        c = build_gn[i]
        if c == '"':
            i = build_gn.find('"', i + 1)
            if i < 0:
                break
        elif c == "#":
            i = build_gn.find("\n", i)
            if i < 0:
                break
        elif c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                return build_gn[start:i]
        i += 1
    return None


def target_sources(build_gn: str, kind: str, name: str, where: Path = LB / "BUILD.gn") -> list[str]:
    """Every source of `kind("name") { ... }` in a BUILD.gn file (`where`, for the error): its
    `sources = [...]` and each `sources += [...]` inside it, conditional ones included."""
    body = target_body(build_gn, kind, name)
    lists = re.findall(r'\bsources\s*\+?=\s*\[(.*?)\]', body or "", re.S)
    if not lists:
        sys.exit(f"check_lb_tests: no {kind}(\"{name}\") with sources in {where}")
    return [s for text in lists for s in re.findall(r'"([^"]+)"', re.sub(r"#[^\n]*", "", text))]


def core_source_problems(build_gn: str, core_src: Path) -> list[str]:
    """How the lb_core target's sources differ from the .rs files in `core_src`."""
    listed = set(target_sources(build_gn, "rust_static_library", "lb_core"))
    on_disk = {CORE_GN_PREFIX + p.relative_to(core_src).as_posix() for p in core_src.rglob("*.rs")}
    problems = [f"{s}: in core/lb-core but not in lb_core's sources" for s in sorted(on_disk - listed)]
    problems += [f"{s}: in lb_core's sources but not in core/lb-core" for s in sorted(listed - on_disk)]
    return problems


def java_test_problems(android: Path) -> list[str]:
    """What is missing for the Android shell's Java tests: each class under java/src needs its
    <Name>Test.java under junit/src, listed in lb_junit_tests and with an @Test."""
    build = android / "BUILD.gn"
    java = android / "java" / "src"
    if not java.is_dir():
        return []
    listed = set(target_sources(build.read_text(), "robolectric_binary", "lb_junit_tests",
                                build)) if build.is_file() else set()
    problems = []
    for source in sorted(java.rglob("*.java")):
        rel = source.relative_to(java)
        test = Path("junit") / "src" / rel.with_name(rel.stem + "Test.java")
        if test.as_posix() not in listed:
            problems.append(f"android/java/src/{rel.as_posix()}: no {test.as_posix()} in "
                            "lb_junit_tests")
        elif not (android / test).is_file():
            problems.append(f"android/{test.as_posix()}: listed in lb_junit_tests but missing")
        elif not re.search(r"^\s*@Test\b", (android / test).read_text(), re.M):
            problems.append(f"android/{test.as_posix()}: has no @Test")
    for test in sorted(listed):
        if not (android / test).is_file():
            problems.append(f"android/{test}: listed in lb_junit_tests but missing")
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
    java_problems = java_test_problems(ANDROID)
    problems += java_problems
    for problem in dict.fromkeys(problems):
        print(f"check_lb_tests: {problem}", file=sys.stderr)
    if problems:
        print(
            "Each source of " + ", ".join(f"//lb:{t}" for t in TESTED_TARGETS)
            + " needs unit tests"
            " (chromium/lb/AGENTS.md, Tests)"
            + ("; lb_core lists lb-core's files (chromium/lb/BUILD.gn)" if core_problems else "")
            + ("; each Android shell class needs a Robolectric test" if java_problems else "")
            + ".",
            file=sys.stderr,
        )
        return 1
    java_classes = len(list((ANDROID / "java" / "src").rglob("*.java")))
    print(f"check_lb_tests: {len(model)} tested sources and {java_classes} Android shell classes, "
          "each with unit tests")
    return 0


if __name__ == "__main__":
    sys.exit(main())

# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Tests for lib.sh, the helpers of the desktop and Android fetch and build scripts, and for the
desktop platform entries (desktop/platforms/*.sh)."""

import os
import shlex
import stat
import subprocess
import sys
import tempfile
import textwrap
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import check_patches  # noqa: E402

SCRIPTS = os.path.dirname(os.path.abspath(__file__))
DESKTOP = os.path.dirname(SCRIPTS)
REPO = os.path.dirname(DESKTOP)
LIB = os.path.join(SCRIPTS, "lib.sh")


def bash(script, env=None, check=True):
    """Runs <script> in bash after sourcing lib.sh, with the scripts' own shell options."""
    full = f"set -euo pipefail\nsource {shlex.quote(LIB)}\n{script}"
    run_env = {k: v for k, v in os.environ.items() if k != "CI"}
    run_env.update(env or {})
    result = subprocess.run(["bash", "-c", full], capture_output=True, text=True, env=run_env)
    if check and result.returncode != 0:
        raise AssertionError(f"bash failed ({result.returncode}):\n{result.stderr}")
    return result


def write(path, text, executable=False):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        f.write(textwrap.dedent(text))
    if executable:
        os.chmod(path, os.stat(path).st_mode | stat.S_IXUSR)
    return path


class SeriesForTest(unittest.TestCase):
    def series_for(self, path, platform):
        return bash(f"series_for {shlex.quote(path)} {platform}").stdout.split()

    def test_tags(self):
        tmp = tempfile.mkdtemp()
        path = write(os.path.join(tmp, "series"), """\
            # a comment
            lb/a.patch   # every platform
              lb/b.patch # [mac] indented, macOS only

            lb/c.patch
            lb/d.patch   #[linux] no space before the tag
            lb/e.patch   # mentions [mac] later, so for every platform
            """)
        self.assertEqual(self.series_for(path, "mac"),
                         ["lb/a.patch", "lb/b.patch", "lb/c.patch", "lb/e.patch"])
        self.assertEqual(self.series_for(path, "linux"),
                         ["lb/a.patch", "lb/c.patch", "lb/d.patch", "lb/e.patch"])

    def test_agrees_with_check_patches_on_our_series(self):
        # fetch.sh (lib.sh) and check_patches.py must pick the same patches.
        for series, platforms in ((os.path.join(DESKTOP, "patches", "series"), ("mac", "linux")),
                                  (os.path.join(REPO, "android", "patches", "series"), ("android",))):
            for platform in platforms:
                with self.subTest(series=series, platform=platform):
                    expected = [n for n, _ in check_patches.read_series(series, platform)]
                    self.assertEqual(self.series_for(series, platform), expected)
                    self.assertTrue(expected)


class GnArgsTest(unittest.TestCase):
    def test_gn_cpu(self):
        out = bash("for a in arm64 aarch64 x86_64 x64; do gn_cpu $a; done").stdout.split()
        self.assertEqual(out, ["arm64", "arm64", "x64", "x64"])
        result = bash("gn_cpu riscv64", check=False)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("unknown architecture riscv64", result.stderr)

    def test_merge_later_files_win(self):
        tmp = tempfile.mkdtemp()
        a = write(os.path.join(tmp, "a.gn"), """\
            # comment = ignored
            is_debug = true
            target_os = "android"   # trailing comment
            """)
        b = write(os.path.join(tmp, "b.gn"), "is_debug = false\nsymbol_level = 0\n")
        out = bash(f"merge_gn_args {a} {b}").stdout.splitlines()
        self.assertEqual(out, ["is_debug = false", 'target_os = "android"', "symbol_level = 0"])

    def test_write_gn_args(self):
        tmp = tempfile.mkdtemp()
        a = write(os.path.join(tmp, "a.gn"), "is_debug = false\n")
        out_dir = os.path.join(tmp, "out", "x")
        bash(f"write_gn_args {out_dir} arm64 {a}")
        with open(os.path.join(out_dir, "args.gn")) as f:
            self.assertEqual(f.read(), 'is_debug = false\ntarget_cpu = "arm64"\n')

    def test_write_gn_args_with_a_compiler_cache(self):
        tmp = tempfile.mkdtemp()
        a = write(os.path.join(tmp, "a.gn"), "is_debug = false\n")
        write(os.path.join(tmp, "bin", "sccache"), "#!/bin/sh\n", executable=True)
        out_dir = os.path.join(tmp, "out")
        path = f"{tmp}/bin:{os.environ['PATH']}"
        bash(f"write_gn_args {out_dir} x64 {a}", env={"LB_CC_WRAPPER": "sccache", "PATH": path})
        with open(os.path.join(out_dir, "args.gn")) as f:
            self.assertEqual(f.read().splitlines()[-1], 'cc_wrapper = "sccache"')

    def test_write_gn_args_missing_compiler_cache(self):
        tmp = tempfile.mkdtemp()
        a = write(os.path.join(tmp, "a.gn"), "is_debug = false\n")
        result = bash(f"write_gn_args {tmp}/out x64 {a}",
                      env={"LB_CC_WRAPPER": "no-such-cache"}, check=False)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("LB_CC_WRAPPER=no-such-cache not found", result.stderr)


class RunNinjaTest(unittest.TestCase):
    STEPS = "printf '[1/1000] a\\n[2/1000] b\\n[500/1000] c\\nwarning: w\\n[1000/1000] d\\n'"

    def test_ci_keeps_every_500th_step_and_the_rest(self):
        out = bash(f"run_ninja sh -c {shlex.quote(self.STEPS)}", env={"CI": "true"}).stdout
        self.assertEqual(out.splitlines(), ["[500/1000] c", "warning: w", "[1000/1000] d"])

    def test_outside_ci_prints_everything(self):
        out = bash(f"run_ninja sh -c {shlex.quote(self.STEPS)}").stdout
        self.assertEqual(len(out.splitlines()), 5)

    def test_failure_fails_in_ci_too(self):
        for env in ({}, {"CI": "true"}):
            with self.subTest(env=env):
                result = bash("run_ninja false; echo reached", env=env, check=False)
                self.assertNotEqual(result.returncode, 0)
                self.assertNotIn("reached", result.stdout)


class GnBuildTest(unittest.TestCase):
    def setUp(self):
        self.src = tempfile.mkdtemp(prefix="lb-src-")
        self.log = os.path.join(self.src, "calls")
        for tool in ("gn", "ninja"):
            write(os.path.join(self.src, "tools", tool),
                  f'#!/bin/sh\necho "{tool} $*" >> {shlex.quote(self.log)}\n', executable=True)
        write(os.path.join(self.src, "out", "A", "args.gn"), 'target_cpu = "arm64"\n')

    def calls(self):
        with open(self.log) as f:
            return f.read().splitlines()

    def test_gen_then_ninja(self):
        out = bash(f"gn_build {self.src} out/A tools/gn tools/ninja chrome lb_unittests").stdout
        self.assertIn('  target_cpu = "arm64"', out)
        self.assertEqual(self.calls(), ["gn gen out/A --fail-on-unused-args",
                                        "ninja -C out/A chrome lb_unittests"])

    def test_ninja_flags(self):
        bash(f"gn_build {self.src} out/A tools/gn tools/ninja chrome",
             env={"LB_NINJA_FLAGS": "-k 0"})
        self.assertEqual(self.calls()[-1], "ninja -k 0 -C out/A chrome")

    def test_gen_failure_stops_the_build(self):
        write(os.path.join(self.src, "tools", "gn"), "#!/bin/sh\nexit 3\n", executable=True)
        result = bash(f"gn_build {self.src} out/A tools/gn tools/ninja chrome", check=False)
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(os.path.exists(self.log))


ENTRY = """\
    PLATFORM_LABEL="Test OS"
    PLATFORM_HOST="TestOS"
    PLATFORM_ARCHS="arm64 x86_64"
    PLATFORM_REPO="ungoogled-chromium-test"
    PLATFORM_REPO_URL="https://example.invalid/repo"
    PLATFORM_TAG="1-1"
    PLATFORM_FLAGS="flags.test.gn"
    PLATFORM_GN="gn"
    PLATFORM_NINJA="ninja"
    PLATFORM_TARGETS="chrome"
    platform_fetch_source() { :; }
    platform_toolchains() { :; }
    platform_branding() { :; }
    platform_prebuild() { :; }
    platform_report() { :; }
    platform_package() { :; }
    """


class PlatformEntryTest(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.mkdtemp(prefix="lb-platforms-")

    def load(self, name, then="echo $PLATFORM"):
        return bash(f"load_platform {self.dir} {name}\n{then}", check=False)

    def test_complete_entry(self):
        write(os.path.join(self.dir, "test.sh"), ENTRY)
        result = self.load("test", 'echo "$PLATFORM $PLATFORM_LABEL"; check_arch arm64')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.strip(), "test Test OS")

    def test_unknown_platform_lists_the_known_ones(self):
        write(os.path.join(self.dir, "test.sh"), ENTRY)
        write(os.path.join(self.dir, "other.sh"), ENTRY)
        result = self.load("beos")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('no desktop platform "beos" (have: other test)', result.stderr)

    def test_missing_variable(self):
        write(os.path.join(self.dir, "test.sh"), ENTRY.replace('PLATFORM_TAG="1-1"\n', ""))
        result = self.load("test")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("test.sh doesn't set PLATFORM_TAG", result.stderr)

    def test_missing_hook(self):
        write(os.path.join(self.dir, "test.sh"),
              ENTRY.replace("platform_toolchains() { :; }\n", ""))
        result = self.load("test")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("test.sh doesn't define platform_toolchains", result.stderr)

    def test_unknown_arch(self):
        write(os.path.join(self.dir, "test.sh"), ENTRY)
        result = self.load("test", "check_arch riscv64")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Test OS builds arm64 x86_64, not riscv64", result.stderr)

    def test_our_entries_are_complete(self):
        # Every entry loads as fetch.sh and build.sh load it: after desktop/VERSIONS.
        platforms = os.path.join(DESKTOP, "platforms")
        names = sorted(n[:-3] for n in os.listdir(platforms) if n.endswith(".sh"))
        self.assertIn("mac", names)
        for name in names:
            with self.subTest(platform=name):
                result = bash(f"source {shlex.quote(os.path.join(DESKTOP, 'VERSIONS'))}\n"
                              f"load_platform {shlex.quote(platforms)} {name}\n"
                              f'echo "$PLATFORM_REPO $PLATFORM_TAG"', check=False)
                self.assertEqual(result.returncode, 0, result.stderr)
                repo, tag = result.stdout.split()
                self.assertEqual(repo, check_patches.platform_repo(name))
                self.assertTrue(tag)
                self.assertTrue(os.path.exists(os.path.join(DESKTOP, "args", f"{name}.gn")),
                                f"desktop/args/{name}.gn")


if __name__ == "__main__":
    unittest.main()

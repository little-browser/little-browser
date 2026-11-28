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
    def test_arch_from_machine(self):
        # What uname -m and lipo say, in our names; our own names aren't machine names.
        out = bash("for a in arm64 aarch64 x86_64; do arch_from_machine $a; done").stdout.split()
        self.assertEqual(out, ["arm64", "arm64", "x64"])
        for name in ("x64", "riscv64"):
            result = bash(f"arch_from_machine {name}", check=False)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn(f"unknown machine architecture {name}", result.stderr)

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


    def test_write_gn_args_keeps_another_architectures_build(self):
        tmp = tempfile.mkdtemp()
        a = write(os.path.join(tmp, "a.gn"), "is_debug = false\n")
        out_dir = os.path.join(tmp, "out")
        bash(f"write_gn_args {out_dir} arm64 {a}")
        bash(f"write_gn_args {out_dir} arm64 {a}")   # the same architecture again is fine
        result = bash(f"write_gn_args {out_dir} x64 {a}", check=False)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('holds an arm64 build, not x64: move or delete it', result.stderr)
        with open(os.path.join(out_dir, "args.gn")) as f:
            self.assertEqual(f.read().splitlines()[-1], 'target_cpu = "arm64"')

    def test_write_gn_args_sets_the_architecture_once(self):
        # The architecture is build.sh's alone: an arg file's own target_cpu doesn't reach args.gn.
        tmp = tempfile.mkdtemp()
        a = write(os.path.join(tmp, "a.gn"), 'target_cpu = "arm64"\nis_debug = false\n')
        out_dir = os.path.join(tmp, "out")
        bash(f"write_gn_args {out_dir} x64 {a}")
        with open(os.path.join(out_dir, "args.gn")) as f:
            self.assertEqual(f.read(), 'is_debug = false\ntarget_cpu = "x64"\n')

    def test_our_arg_files_leave_the_architecture_to_build_sh(self):
        args = os.path.join(DESKTOP, "args")
        for name in sorted(os.listdir(args)):
            with open(os.path.join(args, name)) as f:
                lines = [l.split("#", 1)[0].strip() for l in f]
            with self.subTest(file=name):
                self.assertFalse([l for l in lines if l.startswith("target_cpu")])

    def test_host_arch(self):
        tmp = tempfile.mkdtemp()
        def host(machine, translated):
            write(os.path.join(tmp, "bin", "uname"), f"#!/bin/sh\necho {machine}\n", executable=True)
            write(os.path.join(tmp, "bin", "sysctl"),
                  "#!/bin/sh\n" + (f"echo {translated}\n" if translated is not None else "exit 1\n"),
                  executable=True)
            return bash("host_arch", env={"PATH": f"{tmp}/bin:{os.environ['PATH']}"}, check=False)
        self.assertEqual(host("arm64", "0").stdout.strip(), "arm64")
        self.assertEqual(host("x86_64", "0").stdout.strip(), "x64")
        self.assertEqual(host("x86_64", None).stdout.strip(), "x64")   # an Intel Mac, or Linux
        rosetta = host("x86_64", "1")
        self.assertNotEqual(rosetta.returncode, 0)
        self.assertIn("Rosetta", rosetta.stderr)

    def test_out_dir_per_architecture(self):
        out = bash("out_dir Default arm64 arm64; out_dir Default x64 arm64; "
                   "out_dir Dev x64 x64; out_dir Dev arm64 x64").stdout.split()
        self.assertEqual(out, ["out/Default", "out/Default-x64", "out/Dev", "out/Dev-arm64"])
        for args in ("riscv64 arm64", "x86_64 arm64", "aarch64 arm64", "arm64 x86_64"):
            result = bash(f"out_dir Default {args}", check=False)
            self.assertNotEqual(result.returncode, 0, args)
            self.assertIn("is arm64 or x64", result.stderr)


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
    PLATFORM_ARCHS="arm64 x64"
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

    def test_arch_by_our_names_only(self):
        write(os.path.join(self.dir, "test.sh"), ENTRY)
        for arch in ("x64", "arm64"):
            self.assertEqual(self.load("test", f"check_arch {arch}").returncode, 0)
        result = self.load("test", "check_arch x86_64")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Test OS builds arm64 x64, not x86_64", result.stderr)

    def test_our_entries_name_architectures_arm64_or_x64(self):
        platforms = os.path.join(DESKTOP, "platforms")
        for name in sorted(n[:-3] for n in os.listdir(platforms) if n.endswith(".sh")):
            result = bash(f"source {shlex.quote(os.path.join(DESKTOP, 'VERSIONS'))}\n"
                          f"load_platform {shlex.quote(platforms)} {name}\necho $PLATFORM_ARCHS")
            with self.subTest(platform=name):
                self.assertLessEqual(set(result.stdout.split()), {"arm64", "x64"})

    def test_unknown_arch(self):
        write(os.path.join(self.dir, "test.sh"), ENTRY)
        result = self.load("test", "check_arch riscv64")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Test OS builds arm64 x64, not riscv64", result.stderr)

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


class MacToolchainsTest(unittest.TestCase):
    """mac.sh's toolchains: this Mac's own compilers, and the Rust standard library of every
    architecture it can build, so one tree builds both."""

    def toolchains(self, host, call="platform_toolchains", python=None, check=True):
        tmp = tempfile.mkdtemp(prefix="lb-mac-")
        self.pu = os.path.join(tmp, "pu")
        log = os.path.join(tmp, "calls")
        write(os.path.join(tmp, "pu", "retrieve_and_unpack_resource.py"),
              f"import sys\nopen({log!r}, 'a').write(' '.join(sys.argv[1:]) + '\\n')\n")
        write(os.path.join(tmp, "bin", "uname"), f"#!/bin/sh\necho {host}\n", executable=True)
        mac = shlex.quote(os.path.join(DESKTOP, "platforms", "mac.sh"))
        self.rustlib = os.path.join(tmp, "src", "third_party", "rust-toolchain", "rustc", "lib", "rustlib")
        for d in ("aarch64-apple-darwin", "x86_64-apple-darwin", "rust-nightly-x86_64-apple-darwin"):
            os.makedirs(os.path.join(self.rustlib, d))
        # python3's own machine: the host's, unless the test says otherwise.
        machine = f"mac_python_machine() {{ echo {python or host}; }}\n"
        self.result = bash(f"source {shlex.quote(os.path.join(DESKTOP, 'VERSIONS'))}\nsource {mac}\n"
                           f"PU={shlex.quote(self.pu)}\nSRC={shlex.quote(os.path.join(tmp, 'src'))}\n"
                           f"{machine}{call}",
                           env={"PATH": f"{tmp}/bin:{os.environ['PATH']}"}, check=check)
        if not os.path.exists(log):
            return []
        with open(log) as f:
            return f.read().splitlines()

    def test_apple_silicon_gets_the_intel_standard_library_too(self):
        # Upstream's script installs the host's toolchain, plus the Rust standard library of the
        # target it is given when that isn't the host.
        self.assertEqual(self.toolchains("arm64"), ["--platform-specific x86_64"])

    def test_upstream_works_in_our_tree_whichever_step_runs(self):
        # Its script works in its build/src and imports its ungoogled-chromium: both must be links
        # to ours, also for fetch.sh --toolchains-only, which skips the source step.
        for call in ("platform_toolchains", "platform_fetch_source arm64"):
            with self.subTest(call=call):
                self.toolchains("arm64", call)
                self.assertEqual(os.readlink(os.path.join(self.pu, "build")), "../../build")
                self.assertEqual(os.readlink(os.path.join(self.pu, "ungoogled-chromium")),
                                 "../ungoogled-chromium")

    def test_a_python3_of_another_architecture_stops_it(self):
        # An Intel-only python3 on Apple silicon: upstream would pick Intel toolchains.
        calls = self.toolchains("arm64", python="x86_64", check=False)
        self.assertNotEqual(self.result.returncode, 0)
        self.assertIn("runs as x64 on this arm64 Mac", self.result.stderr)
        self.assertEqual(calls, [])

    def test_unpacked_package_left_beside_the_standard_library_goes(self):
        # Upstream's unpacker leaves the rest of the other architecture's Rust package (594 MB).
        self.toolchains("arm64")
        self.assertEqual(sorted(os.listdir(self.rustlib)), ["aarch64-apple-darwin", "x86_64-apple-darwin"])

    def test_gn_is_bootstrapped_into_a_folder_that_may_not_exist_yet(self):
        # A fresh tree building only the other architecture has out/Default-x64, no out/Default.
        # The bootstrap stand-in, like gn's own, copies the binary without making the folder.
        src = tempfile.mkdtemp(prefix="lb-src-")
        write(os.path.join(src, "tools", "gn", "bootstrap", "bootstrap.py"),
              "#!/usr/bin/env python3\nimport sys\nopen(sys.argv[sys.argv.index('-o') + 1], 'w').write('gn')\n",
              executable=True)
        mac = shlex.quote(os.path.join(DESKTOP, "platforms", "mac.sh"))
        bash(f"source {shlex.quote(os.path.join(DESKTOP, 'VERSIONS'))}\nsource {mac}\n"
             f"cd {shlex.quote(src)}\nmac_bootstrap_gn")
        self.assertTrue(os.path.isfile(os.path.join(src, "out", "Default", "gn")))

    def test_intel_gets_the_apple_silicon_standard_library_too(self):
        self.assertEqual(self.toolchains("x86_64"), ["--platform-specific arm64"])

    def test_source_gets_the_macos_repo_name_for_the_architecture(self):
        # The macOS repo's script calls x64 x86_64.
        self.assertEqual(self.toolchains("arm64", "platform_fetch_source x64"),
                         ["--download --generic x86_64"])
        self.assertEqual(self.toolchains("arm64", "platform_fetch_source arm64"),
                         ["--download --generic arm64"])


class ScriptArgumentsTest(unittest.TestCase):
    """The build scripts' --arch handling, through their refusals: nothing is built, packaged or
    launched. The folder a valid arch picks is out_dir's (GnArgsTest)."""

    def run_script(self, *args):
        env = {k: v for k, v in os.environ.items() if k != "CI"}
        return subprocess.run(["bash", os.path.join(SCRIPTS, args[0]), *args[1:]],
                              capture_output=True, text=True, env=env)

    def refused(self, needle, *args):
        result = self.run_script(*args)
        self.assertNotEqual(result.returncode, 0, args)
        self.assertIn(needle, result.stderr, args)

    def host(self):
        return bash("host_arch").stdout.strip()

    def other(self):
        return "x64" if self.host() == "arm64" else "arm64"

    def test_only_our_architecture_names(self):
        for script in (["build.sh", "--platform", "mac"], ["run.sh", "--arch"],
                       ["package-macos.sh", "--arch"]):
            with self.subTest(script=script[0]):
                self.refused("x86_64", *script, "x86_64")

    def test_arch_needs_a_value(self):
        self.refused("--arch needs an architecture", "run.sh", "--arch")
        self.refused("usage: package-macos.sh", "package-macos.sh", "--arch")

    def test_build_package_passes_the_architecture_on(self):
        # build.sh --package <arch> runs package-macos.sh --arch <arch>, which checks the name.
        self.refused("an architecture is arm64 or x64, not x86_64",
                     "build.sh", "--platform", "mac", "--package", "x86_64")

    def test_another_architecture_has_its_own_folder(self):
        other = self.other()
        app = os.path.join(DESKTOP, "build", "src", "out", f"Default-{other}", "Little Browser.app")
        if os.path.exists(app):
            self.skipTest(f"{app} is built here; the scripts would use it")
        self.refused(f"out/Default-{other}/Little Browser.app", "package-macos.sh", "--arch", other)
        self.refused(f"out/Default-{other}/Little Browser.app", "run.sh", "--arch", other)

    @unittest.skipUnless(sys.platform == "darwin", "fetch.sh runs on macOS only")
    def test_fetch_takes_our_names_only(self):
        self.refused("macOS builds arm64 x64, not x86_64", "fetch.sh", "--toolchains-only", "x86_64")


if __name__ == "__main__":
    unittest.main()

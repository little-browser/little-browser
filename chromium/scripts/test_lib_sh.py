# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Tests for lib.sh, the helpers of the desktop and Android fetch and build scripts, and for the
desktop platform entries (desktop/platforms/*.sh)."""

import os
import re
import shlex
import shutil
import stat
import subprocess
import sys
import tempfile
import textwrap
import unittest
from unittest import mock

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import check_patches  # noqa: E402

SCRIPTS = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(SCRIPTS))
DESKTOP = os.path.join(REPO, "desktop")
LIB = os.path.join(SCRIPTS, "lib.sh")


# Variables lib.sh reads from the environment: a test sets the ones it needs, so a developer's own
# (a release build's LB_RELEASE, say) doesn't change the results, and a developer's credentials
# never reach a test's output.
LIB_ENV = ("CI", "LB_RELEASE", "CHROMIUM_VERSION", "LB_CC_WRAPPER", "LB_NINJA_FLAGS", "LB_IMDS_URL",
           "SCCACHE_BUCKET", "SCCACHE_IDLE_TIMEOUT", "AWS_ACCESS_KEY_ID", "AWS_SECRET_ACCESS_KEY",
           "AWS_SESSION_TOKEN")


def bash(script, env=None, check=True):
    """Runs <script> in bash after sourcing lib.sh, with the scripts' own shell options."""
    full = f"set -euo pipefail\nsource {shlex.quote(LIB)}\n{script}"
    run_env = {k: v for k, v in os.environ.items() if k not in LIB_ENV}
    run_env.update(env or {})
    result = subprocess.run(["bash", "-c", full], capture_output=True, text=True, env=run_env)
    if check and result.returncode != 0:
        raise AssertionError(f"bash failed ({result.returncode}):\n{result.stderr}")
    return result


# Models sccache 0.19's server commands as a build uses them: --show-stats always succeeds (it
# starts no server), --start-server fails when the cache is unreachable (UNREACHABLE set), and
# --stop-server fails when no server runs. Each call is logged with the idle timeout it sees.
FAKE_SCCACHE = """#!/bin/sh
echo "sccache $* idle=${SCCACHE_IDLE_TIMEOUT:-unset}" >> "$SCCACHE_LOG"
case "$1" in
  --start-server)
    if [ -n "$UNREACHABLE" ]; then echo 'cache storage failed to read' >&2; exit 2; fi ;;
  --stop-server) exit 2 ;;
esac
exit 0
"""


class LibEnvTest(unittest.TestCase):
    # A developer's own cache, flags and credentials don't reach the tests.
    def test_the_developers_environment_is_left_out(self):
        mine = {k: "mine" for k in ("SCCACHE_BUCKET", "LB_NINJA_FLAGS", "LB_IMDS_URL",
                                    "AWS_ACCESS_KEY_ID", "AWS_SECRET_ACCESS_KEY",
                                    "AWS_SESSION_TOKEN", "SCCACHE_IDLE_TIMEOUT")}
        with mock.patch.dict(os.environ, mine):
            out = bash('for k in ' + " ".join(mine) + '; do printf "%s|" "${!k:-}"; done').stdout
        self.assertEqual(out, "|" * len(mine))


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

    def test_desktop_tag(self):
        tmp = tempfile.mkdtemp()
        path = write(os.path.join(tmp, "series"), """\
            a.patch   # [desktop] every desktop platform
            b.patch   # [android]
            c.patch
            """)
        for platform in ("mac", "linux", "win"):
            self.assertEqual(self.series_for(path, platform), ["a.patch", "c.patch"])
        self.assertEqual(self.series_for(path, "android"), ["b.patch", "c.patch"])

    def test_unreadable_series_is_an_error(self):
        result = bash("series_for /nonexistent/series mac", check=False)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("can't read the patch series /nonexistent/series", result.stderr)

    def test_series_paths_keep_the_directory_as_it_is(self):
        for name in ("plain", "with space", "R&D", "a|b", "it's"):
            with self.subTest(name=name):
                folder = os.path.join(tempfile.mkdtemp(), name)
                write(os.path.join(folder, "series"), "lb/a.patch\nb.patch  # [linux]\n")
                out = bash(f"series_paths {shlex.quote(folder)} mac").stdout
                self.assertEqual(out, f"{folder}/lb/a.patch\n")

    def test_series_paths_of_an_empty_series(self):
        folder = tempfile.mkdtemp()
        write(os.path.join(folder, "series"), "# nothing yet\n")
        self.assertEqual(bash(f"series_paths {shlex.quote(folder)} mac").stdout, "")

    def test_series_paths_unreadable_series_fails(self):
        result = bash("series_paths /nonexistent mac; echo after", check=False)
        self.assertNotEqual(result.returncode, 0)
        self.assertNotIn("after", result.stdout)

    def test_fetch_scripts_apply_the_shared_series_first(self):
        with open(os.path.join(REPO, "android", "scripts", "fetch.sh")) as f:
            android = f.read()
        self.assertIn('series() { series_paths "$SHARED_PATCHES" android && '
                      'series_paths "$PATCHES" android; }', android)
        with open(os.path.join(DESKTOP, "scripts", "fetch.sh")) as f:
            desktop = f.read()
        self.assertIn('apply_series "$CHROMIUM/patches"\napply_series "$DESKTOP/patches"\n',
                      desktop)

    def test_agrees_with_check_patches_on_our_series(self):
        # fetch.sh (lib.sh) and check_patches.py must pick the same patches.
        for series, platforms in ((os.path.join(REPO, "chromium", "patches", "series"),
                                   ("mac", "linux", "android")),
                                  (os.path.join(DESKTOP, "patches", "series"), ("mac", "linux")),
                                  (os.path.join(REPO, "android", "patches", "series"), ("android",))):
            for platform in platforms:
                with self.subTest(series=series, platform=platform):
                    expected = [n for n, _ in check_patches.read_series(series, platform)]
                    self.assertEqual(self.series_for(series, platform), expected)
                    self.assertTrue(expected)


class BrandingValueTest(unittest.TestCase):
    def test_reads_the_products_branding(self):
        self.assertEqual(bash("branding_value PRODUCT_FULLNAME").stdout, "Little Browser\n")
        self.assertEqual(bash("branding_value COMPANY_FULLNAME").stdout,
                         "The Little Browser Team\n")

    def test_an_unknown_key_is_empty(self):
        self.assertEqual(bash("branding_value NO_SUCH_KEY").stdout, "")


class VersionPinsTest(unittest.TestCase):
    def pins(self, text):
        path = write(os.path.join(tempfile.mkdtemp(), "VERSIONS"), text)
        return bash(f"version_pins {shlex.quote(path)}").stdout

    def test_comments_and_order_dont_count(self):
        self.assertEqual(self.pins("# old header\nA=1\nB_TAG=2-1\n"),
                         self.pins("# a new header\n# over two lines\nB_TAG=2-1\nA=1\n"))

    def test_a_changed_pin_counts(self):
        self.assertNotEqual(self.pins("A=1\nB_TAG=2-1\n"), self.pins("A=1\nB_TAG=2-2\n"))

    # VERSIONS is sourced: what counts is the value bash reads.
    def test_inline_comments_quoting_and_spacing_dont_count(self):
        self.assertEqual(self.pins("A=1\nB_TAG=2-1\n"),
                         self.pins("A='1'  # one\nB_TAG=\"2-1\"   \n"))
        self.assertEqual(self.pins("A=1  # one\n"), "A=1\n")

    def test_a_pipe_is_read_once(self):
        out = bash("version_pins <(printf 'A=1\\nB=2\\n')").stdout
        self.assertEqual(out, "A=1\nB=2\n")

    def test_our_versions_have_pins(self):
        pins = self.pins(open(os.path.join(REPO, "chromium", "VERSIONS")).read())
        self.assertIn("CHROMIUM_VERSION=", pins)


class TreeLinksTest(unittest.TestCase):
    def setUp(self):
        self.repo = tempfile.mkdtemp()
        for d in ("chromium/lb", "core/lb-core", "old/lb", "src"):
            os.makedirs(os.path.join(self.repo, d))

    def check(self, lb_target):
        src = os.path.join(self.repo, "src")
        os.symlink(os.path.join(self.repo, lb_target), os.path.join(src, "lb"))
        os.symlink(os.path.join(self.repo, "core/lb-core"), os.path.join(src, "lb_core"))
        return bash(f"check_tree_links {shlex.quote(src)} {shlex.quote(self.repo)}", check=False)

    def test_current_links(self):
        self.assertEqual(self.check("chromium/lb").returncode, 0)

    def test_link_to_the_old_place(self):
        result = self.check("old/lb")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Delete", result.stderr)

    def test_dangling_link(self):
        result = self.check("desktop/lb")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("doesn't link to", result.stderr)

    def test_android_build_stops_on_a_tree_for_another_checkout(self):
        # build.sh checks the links before building, as the desktop's does.
        for rel in ("android/scripts/build.sh", "chromium/scripts/lib.sh", "chromium/VERSIONS"):
            os.makedirs(os.path.dirname(os.path.join(self.repo, rel)), exist_ok=True)
            shutil.copy2(os.path.join(REPO, rel), os.path.join(self.repo, rel))
        os.makedirs(os.path.join(self.repo, "android/build/src"))
        result = subprocess.run(["bash", os.path.join(self.repo, "android/scripts/build.sh")],
                                capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("doesn't link to", result.stderr)
        self.assertIn("fetch it again", result.stderr)

    def test_our_tree(self):
        src = os.path.join(DESKTOP, "build", "src")
        if not os.path.isdir(src):
            self.skipTest("no desktop tree")
        bash(f"check_tree_links {shlex.quote(src)} {shlex.quote(REPO)}")


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

    def test_write_gn_args_for_a_release(self):
        tmp = tempfile.mkdtemp()
        a = write(os.path.join(tmp, "a.gn"), "is_debug = false\n")
        out_dir = os.path.join(tmp, "out")
        bash(f"write_gn_args {out_dir} arm64 {a}",
             env={"LB_RELEASE": "157.0.8103.78-3", "CHROMIUM_VERSION": "157.0.8103.78"})
        with open(os.path.join(out_dir, "args.gn")) as f:
            self.assertEqual(f.read(), 'is_debug = false\ntarget_cpu = "arm64"\n'
                                       "lb_release_revision = 3\n")

    # An empty LB_RELEASE (CI without a tag) is no release: the build keeps GN's default, 0.
    def test_write_gn_args_with_an_empty_release(self):
        tmp = tempfile.mkdtemp()
        a = write(os.path.join(tmp, "a.gn"), "is_debug = false\n")
        out_dir = os.path.join(tmp, "out")
        bash(f"write_gn_args {out_dir} arm64 {a}", env={"LB_RELEASE": ""})
        with open(os.path.join(out_dir, "args.gn")) as f:
            self.assertNotIn("lb_release_revision", f.read())

    def test_write_gn_args_for_a_release_of_another_version(self):
        tmp = tempfile.mkdtemp()
        a = write(os.path.join(tmp, "a.gn"), "is_debug = false\n")
        result = bash(f"write_gn_args {tmp}/out arm64 {a}", check=False,
                      env={"LB_RELEASE": "156.0.1.2-1", "CHROMIUM_VERSION": "157.0.8103.78"})
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("not on this tree's Chromium version, 157.0.8103.78", result.stderr)
        self.assertFalse(os.path.exists(os.path.join(tmp, "out", "args.gn")))

    # A caller that tests write_gn_args (errexit off there) still sees it fail, with no args.gn.
    def test_write_gn_args_for_a_wrong_release_fails_in_a_condition_too(self):
        tmp = tempfile.mkdtemp()
        a = write(os.path.join(tmp, "a.gn"), "is_debug = false\n")
        result = bash(f"if write_gn_args {tmp}/out arm64 {a}; then echo wrote; else echo failed; fi",
                      env={"LB_RELEASE": "157.0.8103.79-3", "CHROMIUM_VERSION": "157.0.8103.78"})
        self.assertEqual(result.stdout, "failed\n")
        self.assertIn("not on this tree's Chromium version", result.stderr)
        self.assertFalse(os.path.exists(os.path.join(tmp, "out", "args.gn")))

    def test_write_gn_args_for_a_release_needs_the_chromium_version(self):
        tmp = tempfile.mkdtemp()
        a = write(os.path.join(tmp, "a.gn"), "is_debug = false\n")
        result = bash(f"unset CHROMIUM_VERSION; write_gn_args {tmp}/out arm64 {a}", check=False,
                      env={"LB_RELEASE": "157.0.8103.78-3"})
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("source chromium/VERSIONS first", result.stderr)

    def test_write_gn_args_with_a_compiler_cache(self):
        tmp = tempfile.mkdtemp()
        a = write(os.path.join(tmp, "a.gn"), "is_debug = false\n")
        write(os.path.join(tmp, "bin", "sccache"), FAKE_SCCACHE, executable=True)
        log = os.path.join(tmp, "calls")
        out_dir = os.path.join(tmp, "out")
        path = f"{tmp}/bin:{os.environ['PATH']}"
        bash(f"write_gn_args {out_dir} x64 {a}",
             env={"LB_CC_WRAPPER": "sccache", "PATH": path, "SCCACHE_LOG": log})
        with open(os.path.join(out_dir, "args.gn")) as f:
            self.assertEqual(f.read().splitlines()[-2:],
                             ['cc_wrapper = "sccache"', "use_clang_modules = false"])
        # A server already running is replaced by one that lives for the whole build.
        with open(log) as f:
            self.assertEqual(f.read().splitlines(), ["sccache --stop-server idle=0",
                                                     "sccache --start-server idle=0"])

    # Only sccache needs Clang modules off; another wrapper leaves the tree's default.
    def test_write_gn_args_with_another_compiler_wrapper(self):
        tmp = tempfile.mkdtemp()
        a = write(os.path.join(tmp, "a.gn"), "is_debug = false\n")
        write(os.path.join(tmp, "bin", "ccache"), "#!/bin/sh\n", executable=True)
        out_dir = os.path.join(tmp, "out")
        path = f"{tmp}/bin:{os.environ['PATH']}"
        bash(f"write_gn_args {out_dir} x64 {a}", env={"LB_CC_WRAPPER": "ccache", "PATH": path})
        with open(os.path.join(out_dir, "args.gn")) as f:
            lines = f.read().splitlines()
        self.assertEqual(lines[-1], 'cc_wrapper = "ccache"')
        self.assertNotIn("use_clang_modules = false", lines)

    # An sccache whose cache can't be reached fails every compile: the build goes without it.
    def test_write_gn_args_without_a_cache_that_cannot_start(self):
        tmp = tempfile.mkdtemp()
        a = write(os.path.join(tmp, "a.gn"), "is_debug = false\n")
        write(os.path.join(tmp, "bin", "sccache"), FAKE_SCCACHE, executable=True)
        out_dir = os.path.join(tmp, "out")
        path = f"{tmp}/bin:{os.environ['PATH']}"
        result = bash(f"write_gn_args {out_dir} x64 {a}",
                      env={"LB_CC_WRAPPER": "sccache", "PATH": path, "UNREACHABLE": "1",
                           "SCCACHE_LOG": os.path.join(tmp, "calls")})
        with open(os.path.join(out_dir, "args.gn")) as f:
            self.assertEqual(f.read(), 'is_debug = false\ntarget_cpu = "x64"\n')
        self.assertIn("cache storage failed to read", result.stderr)
        self.assertIn("building without the compiler cache", result.stderr)

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


class ReleaseRevisionTest(unittest.TestCase):
    def revision(self, release, chromium="157.0.8103.78"):
        return bash(f"release_revision {shlex.quote(release)} {chromium}", check=False)

    def test_revisions(self):
        for release, revision in (("157.0.8103.78-1", "1"), ("157.0.8103.78-3", "3"),
                                  ("157.0.8103.78-12", "12"), ("157.0.8103.78-9999", "9999")):
            result = self.revision(release)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stdout, revision + "\n")

    def test_not_a_release_version(self):
        for release in ("157.0.8103.78", "157.0.8103.78-", "157.0.8103.78-0",
                        "157.0.8103.78-01", "v157.0.8103.78-1", "157.0.8103-1",
                        "157.0.8103.78-1a", "157.0.8103.78-1 ", "157.0.8103.78-10000",
                        "157.0.8103.78-4294967299"):
            result = self.revision(release)
            self.assertNotEqual(result.returncode, 0, release)
            self.assertIn("is not <Chromium version>-<revision>, such as 157.0.8103.78-1",
                          result.stderr)

    def test_another_chromium_version(self):
        result = self.revision("157.0.8103.79-1")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("release 157.0.8103.79-1 is not on this tree's Chromium version, "
                      "157.0.8103.78", result.stderr)



class CiReleaseTagTest(unittest.TestCase):
    """The release jobs in .gitlab-ci.yml run on the tags release_revision accepts, and their
    builds get the tag's version, so About shows it."""

    TAGS = ("v157.0.8103.78-1", "v157.0.8103.78-12", "v157.0.8103.78-9999", "v157.0.8103.78-0",
            "v157.0.8103.78-01", "v157.0.8103.78-10000", "v157.0.8103.78-1-rc1",
            "v157.0.8103-1", "157.0.8103.78-1", "android-test", "")

    @classmethod
    def setUpClass(cls):
        with open(os.path.join(REPO, ".gitlab-ci.yml")) as f:
            cls.ci = f.read()

    @staticmethod
    def accepted(tag):
        """Whether release_revision accepts <tag> without its v, as a release on its own
        Chromium version."""
        release = tag[1:] if tag.startswith("v") else "-"
        chromium = release.split("-", 1)[0]
        return bash(f"release_revision {shlex.quote(release)} {shlex.quote(chromium)}",
                    check=False).returncode == 0 and tag.startswith("v")

    def test_release_rules_take_the_releases_form(self):
        rules = set(re.findall(r"\$CI_COMMIT_TAG =~ /(.*)/$", self.ci, re.M))
        self.assertEqual(len(rules), 1, rules)  # one form for every release job
        rule = re.compile(rules.pop())  # GitLab's RE2 syntax here is also Python's
        for tag in self.TAGS:
            self.assertEqual(bool(rule.search(tag)), self.accepted(tag), tag)

    def test_the_macos_build_gets_the_tag(self):
        # One template builds every architecture (LB_ARCH), so each gets the tag.
        self.assertRegex(self.ci, r'(?m)^    - LB_RELEASE="\$\{CI_COMMIT_TAG#v\}" '
                                  r'desktop/scripts/build\.sh "\$LB_ARCH"')

    # The Android job runs on any pipeline: only a release tag gives its build a version.
    def test_the_android_build_gets_a_release_tag_only(self):
        line = re.search(r'(?m)^    - (if \[\[ "\$\{CI_COMMIT_TAG:-\}" .*export LB_RELEASE=.*)$',
                         self.ci)
        self.assertIsNotNone(line)
        self.assertRegex(self.ci, r'(?m)^    - android/scripts/build\.sh "\$LB_ANDROID_CPU"'
                                  r' \|\| build_status=\$\?$')
        for tag in self.TAGS:
            out = bash(f'{line.group(1)}\necho "${{LB_RELEASE:-}}"',
                       env={"CI_COMMIT_TAG": tag}).stdout
            self.assertEqual(out, (tag[1:] if self.accepted(tag) else "") + "\n", tag)


class ReleaseTreeTest(unittest.TestCase):
    def tree(self, version):
        src = tempfile.mkdtemp()
        major, minor, build, patch = version.split(".")
        write(os.path.join(src, "chrome", "VERSION"),
              f"MAJOR={major}\nMINOR={minor}\nBUILD={build}\nPATCH={patch}\n")
        return src

    def test_a_release_on_the_trees_version(self):
        src = self.tree("157.0.8103.78")
        bash(f"check_release_tree {src}",
             env={"LB_RELEASE": "157.0.8103.78-3", "CHROMIUM_VERSION": "157.0.8103.78"})

    # A tree fetched before a VERSIONS bump would show the old version with the new revision.
    def test_a_release_on_an_older_tree(self):
        src = self.tree("156.0.7000.1")
        result = bash(f"check_release_tree {src}", check=False,
                      env={"LB_RELEASE": "157.0.8103.78-1", "CHROMIUM_VERSION": "157.0.8103.78"})
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("is Chromium 156.0.7000.1, not 157.0.8103.78 (chromium/VERSIONS)",
                      result.stderr)

    def test_a_release_without_a_tree(self):
        result = bash("check_release_tree /nonexistent", check=False,
                      env={"LB_RELEASE": "157.0.8103.78-1", "CHROMIUM_VERSION": "157.0.8103.78"})
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("can't read /nonexistent/chrome/VERSION", result.stderr)

    def test_no_release_checks_nothing(self):
        bash("check_release_tree /nonexistent", env={"CHROMIUM_VERSION": "157.0.8103.78"})

    def test_the_build_scripts_check_it(self):
        for script in ("desktop/scripts/build.sh", "android/scripts/build.sh"):
            with open(os.path.join(REPO, script)) as f:
                self.assertIn('\ncheck_release_tree "$SRC"\n', f.read(), script)


FAKE_IMDS_CURL = """#!/bin/sh
# Answers as the instance metadata service would; fails when NO_IMDS is set.
[ -n "$NO_IMDS" ] && exit 7
for arg in "$@"; do
  case "$arg" in
    */latest/api/token) echo TOKEN; exit 0 ;;
    */security-credentials/) echo build-role; exit 0 ;;
    */security-credentials/build-role)
      echo '{"AccessKeyId": "AKIA1", "SecretAccessKey": "s3cret", "Token": "tok"}'; exit 0 ;;
  esac
done
exit 22
"""


class InstanceCredentialsTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp()
        write(os.path.join(self.tmp, "bin", "curl"), FAKE_IMDS_CURL, executable=True)
        self.env = {"PATH": f"{self.tmp}/bin:{os.environ['PATH']}", "AWS_ACCESS_KEY_ID": ""}

    def run_it(self, env=None):
        return bash('export_instance_credentials; '
                    'echo "${AWS_ACCESS_KEY_ID:-}|${AWS_SECRET_ACCESS_KEY:-}|${AWS_SESSION_TOKEN:-}"',
                    env={**self.env, **(env or {})})

    # The role's credentials are exported, and nothing about them is printed.
    def test_exports_the_roles_credentials_silently(self):
        result = self.run_it()
        self.assertEqual(result.stdout.strip(), "AKIA1|s3cret|tok")
        self.assertEqual(result.stderr, "")

    def test_credentials_already_set_stay(self):
        result = self.run_it({"AWS_ACCESS_KEY_ID": "MINE", "AWS_SECRET_ACCESS_KEY": "x"})
        self.assertEqual(result.stdout.strip(), "MINE|x|")

    # No metadata service (a developer's machine): nothing is exported, and nothing fails.
    def test_without_a_metadata_service_nothing_changes(self):
        result = self.run_it({"NO_IMDS": "1"})
        self.assertEqual(result.stdout.strip(), "||")

    # write_gn_args fetches them before starting an S3-backed sccache, which then has them.
    def test_an_s3_sccache_starts_with_the_credentials(self):
        seen = os.path.join(self.tmp, "seen")
        write(os.path.join(self.tmp, "bin", "sccache"),
              f'#!/bin/sh\necho "$AWS_ACCESS_KEY_ID" > {shlex.quote(seen)}\n', executable=True)
        a = write(os.path.join(self.tmp, "a.gn"), "is_debug = false\n")
        bash(f"write_gn_args {self.tmp}/out x64 {a}",
             env={**self.env, "LB_CC_WRAPPER": "sccache", "SCCACHE_BUCKET": "cache"})
        with open(seen) as f:
            self.assertEqual(f.read().strip(), "AKIA1")
        # A local cache (no bucket) needs none.
        os.remove(seen)
        bash(f"write_gn_args {self.tmp}/out2 x64 {a}",
             env={**self.env, "LB_CC_WRAPPER": "sccache"})
        with open(seen) as f:
            self.assertEqual(f.read().strip(), "")


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

    # The cache's stats follow the build only when the args use it: a build that left an
    # unreachable sccache out (write_gn_args) doesn't fail at the end asking it for them.
    def test_sccache_stats_only_when_the_args_use_it(self):
        write(os.path.join(self.src, "bin", "sccache"), FAKE_SCCACHE, executable=True)
        env = {"LB_CC_WRAPPER": "sccache", "PATH": f"{self.src}/bin:{os.environ['PATH']}",
               "SCCACHE_LOG": self.log}
        bash(f"gn_build {self.src} out/A tools/gn tools/ninja chrome", env=env)
        self.assertFalse([c for c in self.calls() if c.startswith("sccache")])
        write(os.path.join(self.src, "out", "A", "args.gn"),
              'target_cpu = "arm64"\ncc_wrapper = "sccache"\n')
        bash(f"gn_build {self.src} out/A tools/gn tools/ninja chrome", env=env)
        # The stats, then the server write_gn_args started stops.
        self.assertEqual([c.split(" idle=")[0] for c in self.calls()[-2:]],
                         ["sccache --show-stats", "sccache --stop-server"])

    # A failed build still prints the stats and stops the server, and still fails.
    def test_sccache_stops_after_a_failed_build(self):
        write(os.path.join(self.src, "bin", "sccache"), FAKE_SCCACHE, executable=True)
        write(os.path.join(self.src, "tools", "ninja"),
              f'#!/bin/sh\necho "ninja $*" >> {shlex.quote(self.log)}\nexit 1\n', executable=True)
        write(os.path.join(self.src, "out", "A", "args.gn"),
              'target_cpu = "arm64"\ncc_wrapper = "sccache"\n')
        result = bash(f"gn_build {self.src} out/A tools/gn tools/ninja chrome; echo reached",
                      env={"PATH": f"{self.src}/bin:{os.environ['PATH']}", "SCCACHE_LOG": self.log},
                      check=False)
        self.assertNotEqual(result.returncode, 0)
        self.assertNotIn("reached", result.stdout)
        self.assertEqual([c.split(" idle=")[0] for c in self.calls()[-2:]],
                         ["sccache --show-stats", "sccache --stop-server"])

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
            result = bash(f"source {shlex.quote(os.path.join(REPO, 'chromium', 'VERSIONS'))}\n"
                          f"load_platform {shlex.quote(platforms)} {name}\necho $PLATFORM_ARCHS")
            with self.subTest(platform=name):
                self.assertLessEqual(set(result.stdout.split()), {"arm64", "x64"})

    def test_unknown_arch(self):
        write(os.path.join(self.dir, "test.sh"), ENTRY)
        result = self.load("test", "check_arch riscv64")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Test OS builds arm64 x64, not riscv64", result.stderr)

    def test_our_entries_are_complete(self):
        # Every entry loads as fetch.sh and build.sh load it: after chromium/VERSIONS.
        platforms = os.path.join(DESKTOP, "platforms")
        names = sorted(n[:-3] for n in os.listdir(platforms) if n.endswith(".sh"))
        self.assertIn("mac", names)
        for name in names:
            with self.subTest(platform=name):
                result = bash(f"source {shlex.quote(os.path.join(REPO, 'chromium', 'VERSIONS'))}\n"
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
        self.result = bash(f"source {shlex.quote(os.path.join(REPO, 'chromium', 'VERSIONS'))}\nsource {mac}\n"
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
        bash(f"source {shlex.quote(os.path.join(REPO, 'chromium', 'VERSIONS'))}\nsource {mac}\n"
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
        return subprocess.run(["bash", os.path.join(DESKTOP, "scripts", args[0]), *args[1:]],
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

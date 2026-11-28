# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""The Android CI jobs in .gitlab-ci.yml: one per CPU Google Play serves (arm64 for phones,
tablets and Arm Chromebooks, x64 for Intel and AMD Chromebooks), each a CPU build.sh builds."""

import os
import re
import stat
import subprocess
import tempfile
import unittest

ANDROID = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REPO = os.path.dirname(ANDROID)
BUILD_SH = os.path.join(ANDROID, "scripts", "build.sh")


def jobs(ci):
    """Top-level keys of a GitLab CI file, each with the text of its block."""
    parts = re.split(r"(?m)^([.\w-]+):\n", ci)
    return dict(zip(parts[1::2], parts[2::2]))


class AndroidCiTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        with open(os.path.join(REPO, ".gitlab-ci.yml")) as f:
            cls.jobs = jobs(f.read())

    def cpu(self, job):
        block = self.jobs[job]
        self.assertRegex(block, r"(?m)^  extends: \.android$", job)
        return re.search(r"(?m)^    LB_ANDROID_CPU: (\S+)$", block).group(1)

    def test_a_job_per_cpu(self):
        android = sorted(j for j in self.jobs if re.fullmatch(r"android-(?!patch-check).*", j))
        self.assertEqual(android, ["android-arm64", "android-x64"])
        self.assertEqual(self.cpu("android-arm64"), "arm64")
        self.assertEqual(self.cpu("android-x64"), "x64")

    def test_every_job_cpu_is_one_build_sh_builds(self):
        with open(BUILD_SH) as f:
            accepted = re.search(r'case "\$CPU" in ([\w|]+)\)', f.read()).group(1).split("|")
        for job in ("android-arm64", "android-x64"):
            self.assertIn(self.cpu(job), accepted, job)

    def test_the_template_builds_and_keeps_the_jobs_cpu(self):
        block = self.jobs[".android"]
        self.assertRegex(block, r"(?m)^  extends: \.linux-chromium$")
        self.assertRegex(block, r'(?m)^    - android/scripts/build\.sh "\$LB_ANDROID_CPU" \|\| build_status=\$\?$')
        # build.sh writes the APK to out/<cpu>/apks/.
        self.assertIn("android/build/src/out/$LB_ANDROID_CPU/apks/ChromePublic.apk", block)

    # The classpath is the same for every CPU, so one job makes it.
    def test_only_the_arm64_job_makes_the_java_classpath(self):
        line = re.search(r"(?m)^    - (if .*java_classpath\.py.*)$", self.jobs[".android"]).group(1)
        with tempfile.TemporaryDirectory() as tmp:
            log = os.path.join(tmp, "log")
            python3 = os.path.join(tmp, "python3")
            with open(python3, "w") as f:
                f.write(f'#!/bin/sh\necho "$@" >> {log}\n')
            os.chmod(python3, os.stat(python3).st_mode | stat.S_IEXEC)
            for cpu in ("arm64", "x64"):
                env = dict(os.environ, PATH=f"{tmp}:{os.environ['PATH']}", LB_ANDROID_CPU=cpu)
                subprocess.run(["bash", "-c", line], env=env, check=True)
            with open(log) as f:
                self.assertEqual(f.read(), "android/scripts/java_classpath.py "
                                           "android/build/src/out/arm64 java-classpath.tar.gz\n")

    # A build whose tests fail still makes the classpath and the symbols `when: always` keeps,
    # and still fails the job.
    def test_a_failed_build_keeps_its_artifacts_and_fails(self):
        block = self.jobs[".android"]
        script = block[block.index("  script:\n"):block.index("  artifacts:\n")]
        lines = re.findall(r"(?m)^    - (.*)$", script)
        with tempfile.TemporaryDirectory() as tmp:
            log = os.path.join(tmp, "log")

            def fake(rel, body):
                path = os.path.join(tmp, rel)
                os.makedirs(os.path.dirname(path), exist_ok=True)
                with open(path, "w") as f:
                    f.write(f'#!/bin/sh\necho "$(basename "$0")" >> {log}\n{body}')
                os.chmod(path, os.stat(path).st_mode | stat.S_IEXEC)

            fake("android/scripts/fetch.sh", "")
            fake("android/build/src/build/install-build-deps.sh", "")
            fake("android/scripts/build.sh", "exit 1\n")
            fake("bin/python3", "")
            fake("android/build/src/third_party/llvm-build/Release+Asserts/bin/llvm-nm",
                 "echo '0000 T f'\n")
            env = dict(os.environ, PATH=f"{tmp}/bin:{os.environ['PATH']}", LB_ANDROID_CPU="arm64")
            env.pop("CI_COMMIT_TAG", None)
            # GitLab runs a job's script lines in one bash, with errexit and pipefail on.
            result = subprocess.run(["bash", "-eo", "pipefail", "-c", "\n".join(lines)],
                                    cwd=tmp, env=env)
            self.assertEqual(result.returncode, 1)
            with open(log) as f:
                self.assertEqual(f.read().split(), ["fetch.sh", "install-build-deps.sh",
                                                    "build.sh", "python3", "llvm-nm"])
            self.assertTrue(os.path.getsize(os.path.join(tmp, "libchrome-symbols.txt.gz")))


class BuildCpuTest(unittest.TestCase):
    # A CPU build.sh doesn't build fails before it looks at the tree.
    def test_another_cpu_is_refused(self):
        for cpu in ("x86", "arm", "aarch64", "x86_64"):
            result = subprocess.run(["bash", BUILD_SH, cpu], capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0, cpu)
            self.assertIn("usage: build.sh [arm64|x64]", result.stderr, cpu)


if __name__ == "__main__":
    unittest.main()

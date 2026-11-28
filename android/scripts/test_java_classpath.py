# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

import json
import os
import tarfile
import tempfile
import unittest

import java_classpath


def write(path, text=""):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        f.write(text)


class JavaClasspathTest(unittest.TestCase):
    def setUp(self):
        # A source root with the out dir two levels down, as src/out/arm64.
        self.src = tempfile.mkdtemp()
        self.out = os.path.join(self.src, "out", "arm64")
        write(os.path.join(self.out, java_classpath.CONFIGS[0]), json.dumps({"deps_info": {
            # The key names change between Chromium versions: every .jar counts.
            "some_own_jar": "obj/chrome/android/chrome_java.turbine.jar",
            "javac_full_interface_classpath": ["obj/base/base_java.turbine.jar",
                                               "obj/gone.turbine.jar"],
            "name": "chrome_java.javac.jar.info",
        }, "android": {"sdk_jars": ["../../third_party/android_sdk/android.jar"]},
            "outside": ["../../../elsewhere.jar"]}))
        write(os.path.join(self.out, java_classpath.CONFIGS[1]), json.dumps({"deps_info": {
            "javac_full_interface_classpath": ["obj/base/base_java.turbine.jar"],
        }}))
        for jar in ("out/arm64/obj/chrome/android/chrome_java.turbine.jar",
                    "out/arm64/obj/base/base_java.turbine.jar",
                    "third_party/android_sdk/android.jar", "../elsewhere.jar"):
            write(os.path.join(self.src, jar), "jar")
        write(os.path.join(self.out, "gen/chrome/android/chrome_java__R.srcjar"), "R")
        write(os.path.join(self.out, "gen/chrome/android/notes.txt"), "not a srcjar")

    def test_the_classpath_is_every_jar_the_configs_name_once_in_order(self):
        self.assertEqual(java_classpath.classpath(self.out), [
            "obj/chrome/android/chrome_java.turbine.jar",
            "obj/base/base_java.turbine.jar",
            "obj/gone.turbine.jar",
            "../../third_party/android_sdk/android.jar",
            "../../../elsewhere.jar",
        ])

    def test_a_missing_config_is_skipped(self):
        os.remove(os.path.join(self.out, java_classpath.CONFIGS[0]))
        self.assertEqual(java_classpath.classpath(self.out), ["obj/base/base_java.turbine.jar"])

    # Paths are relative to the source root, so nothing in the archive climbs out of it; jars
    # outside the root, and missing ones, are left out.
    def test_the_archive_holds_paths_under_the_source_root(self):
        archive = os.path.join(tempfile.mkdtemp(), "cp.tar.gz")
        java_classpath.main(self.out, archive)
        with tarfile.open(archive) as tar:
            names = set(tar.getnames())
            listing = tar.extractfile("classpath.txt").read().decode().split()
        self.assertEqual(names, {
            "out/arm64/obj/chrome/android/chrome_java.turbine.jar",
            "out/arm64/obj/base/base_java.turbine.jar",
            "third_party/android_sdk/android.jar",
            "out/arm64/gen/chrome/android/chrome_java__R.srcjar",
            "out/arm64/" + java_classpath.CONFIGS[0],
            "out/arm64/" + java_classpath.CONFIGS[1],
            "classpath.txt",
        })
        self.assertFalse(any(name.startswith("..") for name in names))
        self.assertEqual(listing, [
            "out/arm64/obj/chrome/android/chrome_java.turbine.jar",
            "out/arm64/obj/base/base_java.turbine.jar",
            "third_party/android_sdk/android.jar",
        ])


if __name__ == "__main__":
    unittest.main()

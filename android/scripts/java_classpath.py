#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Packs what compiling Chrome's Java needs into one archive, for compiling our Java and the Java
our patches change on a machine that can't build Chromium for Android (javac against it takes
seconds; the full build takes an hour).

  java_classpath.py <out dir> <archive.tar.gz>

The archive holds, relative to the source root (the out dir's grandparent, src/): every jar the
build configs of chrome_java and our Java name (their own jars and their compile classpaths, the
Android SDK's android.jar included), the generated sources under the out dir (.srcjar: R classes,
JNI glue, enums), those build configs, and classpath.txt, the jars in order. Paths outside the
source root are left out.
"""

import json
import os
import sys
import tarfile

CONFIGS = [
    "gen/chrome/android/chrome_java.build_config.json",
    "gen/lb/android/java.build_config.json",
    "gen/lb/android/shell_java.build_config.json",
]


def jars_in(value):
    """Every .jar path in a build config value (a string, a list, or a dict of them)."""
    if isinstance(value, str):
        return [value] if value.endswith(".jar") else []
    if isinstance(value, list):
        return [jar for item in value for jar in jars_in(item)]
    if isinstance(value, dict):
        return [jar for item in value.values() for jar in jars_in(item)]
    return []


def classpath(out_dir):
    """The jars, relative to out_dir, in the configs' order, without duplicates."""
    jars = []
    for config in CONFIGS:
        path = os.path.join(out_dir, config)
        if not os.path.exists(path):
            print(f"java_classpath: no {config}", file=sys.stderr)
            continue
        with open(path) as f:
            data = json.load(f)
        for jar in jars_in(data):
            if jar not in jars:
                jars.append(jar)
    return jars


def srcjars(out_dir):
    found = []
    for root, _, files in os.walk(os.path.join(out_dir, "gen")):
        found += [os.path.relpath(os.path.join(root, f), out_dir) for f in files
                  if f.endswith(".srcjar")]
    return sorted(found)


def main(out_dir, archive):
    src_root = os.path.dirname(os.path.dirname(os.path.abspath(out_dir)))

    def in_root(path):
        """`path` (relative to out_dir) relative to the source root; None outside it."""
        rel = os.path.relpath(os.path.join(os.path.abspath(out_dir), path), src_root)
        return None if rel.startswith("..") else rel

    jars = [in_root(j) for j in classpath(out_dir)
            if os.path.exists(os.path.join(out_dir, j)) and in_root(j)]
    configs = [in_root(c) for c in CONFIGS if os.path.exists(os.path.join(out_dir, c))]
    sources = [in_root(s) for s in srcjars(out_dir)]
    with tarfile.open(archive, "w:gz") as tar:
        for path in jars + sources + configs:
            tar.add(os.path.join(src_root, path), arcname=path)
        listing = os.path.join(out_dir, "classpath.txt")
        with open(listing, "w") as f:
            f.write("\n".join(jars) + "\n")
        tar.add(listing, arcname="classpath.txt")
    size = sum(os.path.getsize(os.path.join(src_root, p)) for p in jars + sources)
    print(f"java_classpath: {len(jars)} jars, {len(sources)} srcjars, {size >> 20} MiB "
          f"before compression, {os.path.getsize(archive) >> 20} MiB packed")


if __name__ == "__main__":
    main(*sys.argv[1:])

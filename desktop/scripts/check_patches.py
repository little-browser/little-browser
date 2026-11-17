#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Checks that our patch series applies to a clean tree, without re-fetching it.

    desktop/scripts/check_patches.py [--keep DIR]

For every file our series (desktop/patches/series) touches, rebuilds the file as fetch.sh sees it
just before our patches: taken from the pinned Chromium tarball in build/download_cache, then
patched by ungoogled-chromium's core and macOS series (minus patches/ungoogled-exclude.txt). Then
applies our series with the same `patch` flags as ungoogled's patches.py and fails on any rejected
hunk, fuzz or offset.

This is the check for a new or regenerated patch: `git apply --check -R` against build/src can't
catch a patch made from the wrong base, because build/src already has every patch in it.
With --keep DIR, the clean bases are left in DIR/base (good bases for writing a patch: diff them
against your edited copy) and the result in DIR/patched.
"""

import argparse
import os
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile

DESKTOP = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PATCH_FLAGS = ["-p1", "--ignore-whitespace", "--no-backup-if-mismatch", "--forward"]
# Without --verbose, macOS's patch doesn't report fuzz or offsets; GNU patch reports them anyway.
STRICT_FLAGS = ["--verbose"]
FUZZ_OR_OFFSET = re.compile(r"^Hunk #\d+ succeeded at \d+ (with fuzz|\(offset)", re.MULTILINE)


def read_list(path):
    """Lines of a series or exclude file, without comments and blank lines."""
    entries = []
    with open(path) as f:
        for line in f:
            line = line.split("#", 1)[0].strip()
            if line:
                entries.append(line)
    return entries


def versions():
    values = {}
    for line in open(os.path.join(DESKTOP, "VERSIONS")):
        if "=" in line and not line.lstrip().startswith("#"):
            key, value = line.strip().split("=", 1)
            values[key] = value
    return values


def file_chunks(patch_text):
    """Splits a patch into (target path, chunk text) per file."""
    lines = patch_text.splitlines(keepends=True)
    chunks, current = [], None
    i = 0
    while i < len(lines):
        line = lines[i]
        if line.startswith("--- ") and i + 1 < len(lines) and lines[i + 1].startswith("+++ "):
            target = lines[i + 1][4:].split("\t")[0].strip()
            if target == "/dev/null":
                target = line[4:].split("\t")[0].strip()
            target = re.sub(r"^[ab]/", "", target)
            current = [target, [line, lines[i + 1]]]
            chunks.append(current)
            i += 2
            continue
        if line.startswith("diff ") or line.startswith("Index:"):
            current = None
        elif current is not None:
            current[1].append(line)
        i += 1
    return [(target, "".join(text)) for target, text in chunks]


def apply_chunk(tree, label, text, strict):
    """Applies one file's chunk. Returns an error message, or None."""
    with tempfile.NamedTemporaryFile("w", suffix=".patch", delete=False) as f:
        f.write(text)
        path = f.name
    try:
        # Like ungoogled's patches.py: PATCH_BIN picks the patch program.
        command = [os.environ.get("PATCH_BIN", "patch"), *PATCH_FLAGS]
        if strict:
            command += STRICT_FLAGS
        result = subprocess.run([*command, "-i", path, "-d", tree], capture_output=True, text=True)
    finally:
        os.unlink(path)
    output = result.stdout + result.stderr
    if result.returncode != 0:
        return f"{label}: failed\n{output}"
    if strict and FUZZ_OR_OFFSET.search(output):
        return f"{label}: applies only with fuzz or an offset\n{output}"
    return None


def apply_series(tree, patches_dir, series, wanted, excluded, strict):
    errors = []
    for name in series:
        if name in excluded:
            continue
        text = open(os.path.join(patches_dir, name), errors="replace").read()
        for target, chunk in file_chunks(text):
            if target in wanted:
                if error := apply_chunk(tree, f"{name} -> {target}", chunk, strict):
                    errors.append(error)
    return errors


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--keep", help="keep the clean bases and the result in this folder")
    args = parser.parse_args()

    ours_dir = os.path.join(DESKTOP, "patches")
    ours = read_list(os.path.join(ours_dir, "series"))
    wanted = set()
    for name in ours:
        wanted.update(t for t, _ in file_chunks(open(os.path.join(ours_dir, name)).read()))

    version = versions()["CHROMIUM_VERSION"]
    tarball = os.path.join(DESKTOP, "build", "download_cache", f"chromium-{version}-lite.tar.xz")
    if not os.path.exists(tarball):
        sys.exit(f"missing {tarball}: run desktop/scripts/fetch.sh once to download it")

    work = args.keep or tempfile.mkdtemp(prefix="lb-check-patches-")
    try:
        check(work, ours_dir, ours, wanted, tarball, version)
    finally:
        if not args.keep:
            shutil.rmtree(work, ignore_errors=True)


def tar_members(tarball):
    """Opens the tarball (with a parallel xz when there is one) and yields its file members."""
    xz = shutil.which("xz")
    if xz:
        proc = subprocess.Popen([xz, "-T0", "-dc", tarball], stdout=subprocess.PIPE)
        tar = tarfile.open(fileobj=proc.stdout, mode="r|")
    else:
        proc, tar = None, tarfile.open(tarball, mode="r|xz")
    try:
        for member in tar:
            tar.members = []  # TarFile keeps every member it has read: a few hundred MB here
            yield tar, member
    finally:
        tar.close()
        if proc:
            proc.stdout.close()
            proc.wait()


def check(work, ours_dir, ours, wanted, tarball, version):
    base = os.path.join(work, "base")
    shutil.rmtree(base, ignore_errors=True)
    os.makedirs(base)

    # 1. The files from the tarball (some are created by ungoogled's patches instead).
    prefix = f"chromium-{version}/"
    for tar, member in tar_members(tarball):
        if member.isfile() and member.name.startswith(prefix):
            rel = member.name[len(prefix):]
            if rel in wanted:
                dest = os.path.join(base, rel)
                os.makedirs(os.path.dirname(dest), exist_ok=True)
                with tar.extractfile(member) as src, open(dest, "wb") as out:
                    shutil.copyfileobj(src, out)

    # 2. ungoogled-chromium's core and macOS series, as fetch.sh applies them.
    excluded = set(read_list(os.path.join(ours_dir, "ungoogled-exclude.txt")))
    errors = []
    for repo in ("ungoogled-chromium", "ungoogled-chromium-macos"):
        patches_dir = os.path.join(DESKTOP, "third_party", repo, "patches")
        series = read_list(os.path.join(patches_dir, "series"))
        errors += apply_series(base, patches_dir, series, wanted, excluded, strict=False)
    if errors:
        sys.exit("ungoogled-chromium's own patches failed (wrong checkout?):\n" + "\n".join(errors))

    # 3. Our series, strictly.
    patched = os.path.join(work, "patched")
    shutil.rmtree(patched, ignore_errors=True)
    shutil.copytree(base, patched)
    errors = apply_series(patched, ours_dir, ours, wanted, set(), strict=True)
    if errors:
        print("\n".join(errors), file=sys.stderr)
        sys.exit(f"{len(errors)} problem(s) in desktop/patches/series")
    print(f"OK: {len(ours)} patches apply cleanly to {len(wanted)} files")


if __name__ == "__main__":
    main()

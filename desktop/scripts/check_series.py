#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Checks how our Chromium patch series are layered, without a Chromium tree.

    desktop/scripts/check_series.py

For desktop/patches and android/patches:
  - every series entry exists, and every .patch file is in the series;
  - a platform tag ("# [mac] …") names a platform the tree has (desktop/platforms/; Android none);
  - a patch that touches a platform's own files (macOS: .mm, *_mac.*, mac/, cocoa/) is tagged for
    that platform, so another platform's build skips it;
  - a patch for every platform never mixes the layers. Desktop: engine code, and view code (Views,
    Cocoa, WebUI pages and their handlers). Android: native code, and Java (the app layer).
    Strings (.grd, .grdp) go with either. A change that needs both is two patches, so another
    platform can take the engine half without the views (desktop/patches/README.md).
"""

import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from check_patches import file_chunks, read_series  # noqa: E402

DESKTOP = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REPO = os.path.dirname(DESKTOP)

STRINGS = re.compile(r"\.(grd|grdp)$")
DESKTOP_VIEW = re.compile(
    r"^(chrome/browser/ui/views/|ui/views/|chrome/browser/ui/cocoa/|chrome/browser/resources/"
    r"|ui/webui/resources/|chrome/browser/ui/webui/)")
PLATFORM_FILES = {
    "mac": re.compile(r"(\.mm$|_mac\.(cc|h|mm)$|/mac/|/cocoa/|_cocoa\.)"),
    "win": re.compile(r"(_win\.(cc|h)$|/win/)"),
    "linux": re.compile(r"(_linux\.(cc|h)$|/linux/)"),
}


def desktop_layer(path):
    if STRINGS.search(path):
        return None
    return "view" if DESKTOP_VIEW.search(path) else "engine"


def android_layer(path):
    if STRINGS.search(path):
        return None
    return "Java" if path.endswith(".java") else "native"


def desktop_platforms():
    folder = os.path.join(DESKTOP, "platforms")
    return sorted(n[:-3] for n in os.listdir(folder) if n.endswith(".sh"))


def check_tree(patches_dir, platforms, layer_of):
    """The problems in one series, as messages."""
    errors = []
    series_path = os.path.join(patches_dir, "series")
    entries = read_series(series_path)
    listed = {name for name, _ in entries}
    on_disk = set()
    for root, _, files in os.walk(patches_dir):
        for f in files:
            if f.endswith(".patch"):
                on_disk.add(os.path.relpath(os.path.join(root, f), patches_dir))
    for name in sorted(on_disk - listed):
        errors.append(f"{name}: not in {series_path}")

    for name, tag in entries:
        path = os.path.join(patches_dir, name)
        if not os.path.exists(path):
            errors.append(f"{name}: in the series but missing")
            continue
        if tag is not None and tag not in platforms:
            known = ", ".join(platforms) or "none"
            errors.append(f"{name}: tagged [{tag}], not a platform here (known: {known})")
        with open(path, errors="replace") as f:
            targets = [t for t, _ in file_chunks(f.read())]
        for platform, pattern in PLATFORM_FILES.items():
            own = [t for t in targets if pattern.search(t)]
            if own and tag != platform:
                errors.append(f"{name}: touches {platform}-only files ({', '.join(own)}); "
                              f"tag it [{platform}]")
        if tag is None:
            layers = {}
            for t in targets:
                layer = layer_of(t)
                if layer:
                    layers.setdefault(layer, []).append(t)
            if len(layers) > 1:
                detail = "; ".join(f"{k}: {', '.join(v)}" for k, v in sorted(layers.items()))
                errors.append(f"{name}: mixes {' and '.join(sorted(layers))} code ({detail}); "
                              f"split it into one patch per layer")
    return errors


def main():
    errors = []
    errors += check_tree(os.path.join(DESKTOP, "patches"), desktop_platforms(), desktop_layer)
    errors += [f"android: {e}" for e in
               check_tree(os.path.join(REPO, "android", "patches"), [], android_layer)]
    if errors:
        print("\n".join(errors), file=sys.stderr)
        sys.exit(f"{len(errors)} problem(s) in the patch series")
    print("OK: desktop and Android patch series are layered")


if __name__ == "__main__":
    main()

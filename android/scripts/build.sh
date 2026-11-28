#!/usr/bin/env bash
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# Configure and build the Android APK from the tree prepared by fetch.sh.
#
#   android/scripts/build.sh [arm64|x64]   (default arm64; x64 for x64 emulators)
#
# Output: android/build/src/out/<cpu>/apks/ChromePublic.apk, signed with Chromium's public debug
# key. GN args: android/args/android.gn.
# LB_NINJA_FLAGS and LB_CC_WRAPPER work as in desktop/scripts/build.sh.
set -euo pipefail

ANDROID_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REPO="$(dirname "$ANDROID_DIR")"
# shellcheck source=../../desktop/scripts/lib.sh
source "$REPO/desktop/scripts/lib.sh"
SRC="$ANDROID_DIR/build/src"

[ -d "$SRC" ] || die "run android/scripts/fetch.sh first"
CPU="${1:-arm64}"
case "$CPU" in arm64|x64) ;; *) die "usage: build.sh [arm64|x64]" ;; esac
OUT_REL="out/$CPU"
OUT="$SRC/$OUT_REL"
mkdir -p "$OUT"
export PATH="$ANDROID_DIR/third_party/depot_tools:$PATH"

write_gn_args "$OUT" "$CPU" "$ANDROID_DIR/args/android.gn"
gn_build "$SRC" "$OUT_REL" gn third_party/ninja/ninja chrome_public_apk
echo "Built: $OUT/apks/ChromePublic.apk"

#!/usr/bin/env bash
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# Fetch and prepare the Chromium source tree for the Android build: Chromium's own Android app with
# our privacy patches (android/patches/series).
#
#   android/scripts/fetch.sh                 Linux x64, the only host Chromium builds Android on
#   android/scripts/fetch.sh --patches-only  any OS: check that every patch still applies, on a
#                                            sparse checkout of just the files they touch
#
#   1. depot_tools
#   2. a gclient checkout of the Chromium version pinned in desktop/VERSIONS, with the Android
#      dependencies (SDK, NDK, toolchains) and PGO profiles, without git history. The source
#      tarball the desktop build uses has none of them.
#   3. our patches, with `git apply`: no fuzz, so a patch that no longer fits fails here
set -euo pipefail

ANDROID_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REPO="$(dirname "$ANDROID_DIR")"
# shellcheck source=../../desktop/VERSIONS
source "$REPO/desktop/VERSIONS"
# shellcheck source=../../desktop/scripts/lib.sh
source "$REPO/desktop/scripts/lib.sh"
DEPOT_TOOLS="$ANDROID_DIR/third_party/depot_tools"
PATCHES="$ANDROID_DIR/patches"
BUILD="$ANDROID_DIR/build"
SRC="$BUILD/src"

# Paths in the series, without the reasons.
series() { series_for "$PATCHES/series" android; }

apply_patches() {
  local p
  while IFS= read -r p; do
    echo "  $p"
    git -C "$1" apply "$PATCHES/$p"
  done < <(series)
}

if [ "${1:-}" = --patches-only ]; then
  T="$BUILD/patch-check"
  rm -rf "${T:?}"
  log "Patches against Chromium $CHROMIUM_VERSION (sparse checkout of the patched files)"
  # GitHub's mirror of chromium/src serves a shallow, blobless clone in seconds.
  git -c advice.detachedHead=false clone --quiet --filter=blob:none --no-checkout --depth 1 \
    --branch "$CHROMIUM_VERSION" https://github.com/chromium/chromium.git "$T"
  (cd "$PATCHES" && series | xargs sed -n 's|^+++ b/||p') | sort -u |
    sed 's|^|/|' | git -C "$T" sparse-checkout set --no-cone --stdin
  git -C "$T" checkout --quiet
  apply_patches "$T"
  log "Done: every patch applies."
  exit 0
fi

[ "$(uname -s)-$(uname -m)" = Linux-x86_64 ] ||
  die "Chromium builds Android only on Linux x64 (see android/README.md)"
[ -e "$SRC" ] && die "$SRC already exists. Delete $BUILD to prepare it again."

log "depot_tools"
mkdir -p "$ANDROID_DIR/third_party" "$BUILD"
[ -d "$DEPOT_TOOLS/.git" ] ||
  git clone --depth 1 https://chromium.googlesource.com/chromium/tools/depot_tools.git "$DEPOT_TOOLS"
export PATH="$DEPOT_TOOLS:$PATH"

log "Chromium $CHROMIUM_VERSION with Android dependencies (gclient, no history)"
cat > "$BUILD/.gclient" <<EOF
solutions = [{
  "name": "src",
  "url": "https://chromium.googlesource.com/chromium/src.git@refs/tags/$CHROMIUM_VERSION",
  "managed": False,
  "custom_deps": {},
  "custom_vars": {"checkout_pgo_profiles": True},
}]
target_os = ["android"]
EOF
(cd "$BUILD" && gclient sync --no-history --nohooks && gclient runhooks)

log "Patches: Little Browser Android"
apply_patches "$SRC"

log "Done. Source tree: $SRC ($(du -sh "$SRC" | cut -f1)). Next: android/scripts/build.sh"

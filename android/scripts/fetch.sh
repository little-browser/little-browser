#!/usr/bin/env bash
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# Fetch and prepare the Chromium source tree for the Android build: Chromium's own Android app with
# the engine patches every Chromium platform shares (chromium/patches/series) and our Android
# patches (android/patches/series), and our source tree linked in.
#
#   android/scripts/fetch.sh                 Linux x64, the only host Chromium builds Android on
#   android/scripts/fetch.sh --patches-only  any OS: check that every patch still applies, on a
#                                            sparse checkout of just the files they touch
#
#   1. depot_tools
#   2. a gclient checkout of the Chromium version pinned in chromium/VERSIONS, with the Android
#      dependencies (SDK, NDK, toolchains) and PGO profiles, without git history. The source
#      tarball the desktop build uses has none of them.
#   3. our patches, shared then Android, with `git apply`: no fuzz, so a patch that no longer fits
#      fails here
#   4. branding: the product's name in Chromium's strings (chromium/scripts/rebrand_strings.py)
#      and its launcher icon (brand_assets.sh)
#   5. symlinks src/lb → chromium/lb and src/lb_core → core/lb-core, as the desktop's fetch.sh
set -euo pipefail

ANDROID_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REPO="$(dirname "$ANDROID_DIR")"
# shellcheck source=../../chromium/VERSIONS
source "$REPO/chromium/VERSIONS"
# shellcheck source=../../chromium/scripts/lib.sh
source "$REPO/chromium/scripts/lib.sh"
DEPOT_TOOLS="$ANDROID_DIR/third_party/depot_tools"
SHARED_PATCHES="$REPO/chromium/patches"
PATCHES="$ANDROID_DIR/patches"
BUILD="$ANDROID_DIR/build"
SRC="$BUILD/src"

# Every patch to apply, shared series first, as absolute paths, one per line.
series() { series_paths "$SHARED_PATCHES" android && series_paths "$PATCHES" android; }

apply_patches() {
  local p list
  list="$(series)"
  while IFS= read -r p; do
    [ -n "$p" ] || continue
    echo "  ${p#"$REPO"/}"
    git -C "$1" apply "$p"
  done <<< "$list"
}

if [ "${1:-}" = --patches-only ]; then
  T="$BUILD/patch-check"
  rm -rf "${T:?}"
  log "Patches against Chromium $CHROMIUM_VERSION (sparse checkout of the patched files)"
  # GitHub's mirror of chromium/src serves a shallow, blobless clone in seconds.
  git -c advice.detachedHead=false clone --quiet --filter=blob:none --no-checkout --depth 1 \
    --branch "$CHROMIUM_VERSION" https://github.com/chromium/chromium.git "$T"
  series | tr '\n' '\0' | xargs -0 sed -n 's|^+++ b/||p' | sort -u |
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

log "Patches: Little Browser shared and Android"
apply_patches "$SRC"

# Chromium's Android strings call the app "Chrome" as well as "Chromium": rename both in the
# string files the app shows (translations re-keyed).
log "Branding: the product's name in Chromium's strings, the launcher icon"
cp "$REPO/chromium/branding/BRANDING" "$SRC/chrome/app/theme/chromium/BRANDING"
python3 "$REPO/chromium/scripts/rebrand_strings.py" --chrome "$SRC" \
  "$(branding_value PRODUCT_FULLNAME)" "$(branding_value COMPANY_FULLNAME)" \
  chrome/app/chromium_strings.grd chrome/app/generated_resources.grd \
  components/components_chromium_strings.grd components/components_strings.grd \
  chrome/browser/ui/android/strings/android_chrome_strings.grd \
  chrome/android/features/tab_ui/java/strings/android_chrome_tab_ui_strings.grd \
  chrome/android/webapk/strings/android_webapk_strings.grd \
  components/browser_ui/strings/android/browser_ui_strings.grd \
  components/permissions/android/permissions_android_strings.grd \
  components/webapps/browser/android/android_webapps_strings.grd \
  ui/android/java/strings/android_ui_strings.grd
"$ANDROID_DIR/scripts/brand_assets.sh" "$SRC"
ln -sfn "$REPO/chromium/lb" "$SRC/lb"
ln -sfn "$REPO/core/lb-core" "$SRC/lb_core"

log "Done. Source tree: $SRC ($(du -sh "$SRC" | cut -f1)). Next: android/scripts/build.sh"

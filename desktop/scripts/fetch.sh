#!/usr/bin/env bash
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# Fetch and prepare the Chromium source tree for the Little Browser desktop build.
#
#   desktop/scripts/fetch.sh [--platform <name>] [<arch>]   default: this machine's platform and
#                                                            architecture
#   desktop/scripts/fetch.sh --patches-only [--platform <name>]
#                                                  steps 1–3 only, on any OS: CI checks that every
#                                                  patch still applies
#   desktop/scripts/fetch.sh --toolchains-only     step 5's toolchains again, in the existing tree;
#                                                  the next build rebuilds the tools built from
#                                                  them (bindgen, Crubit)
#
#   1. ungoogled-chromium and the platform's repo at the pinned tags
#   2. Chromium source tarball for the pinned version (much smaller than a git checkout)
#   3. prune binaries, apply ungoogled patches (minus ours to skip), platform patches, our patches
#      (those tagged for another platform skipped)
#   4. domain substitution, EXCEPT paths that must keep real Google endpoints (Safe Browsing, Widevine)
#   5. platform toolchains (this machine's, able to build every architecture the platform
#      builds, whichever one <arch> names), branding, link our source tree
#
# What differs per platform lives in its entry, desktop/platforms/<name>.sh (macOS: mac).
set -euo pipefail

DESKTOP="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck source=../VERSIONS
source "$DESKTOP/VERSIONS"
# shellcheck source=lib.sh
source "$DESKTOP/scripts/lib.sh"
UG="$DESKTOP/third_party/ungoogled-chromium"
BUILD="$DESKTOP/build"
SRC="$BUILD/src"

# apply_series <patches dir>: apply its series for this platform, minus ungoogled-exclude.txt,
# via ungoogled's patches.py.
apply_series() {
  local from="$1" name
  name="$(basename "$(dirname "$from")")-$(basename "$from")"
  rm -rf "$BUILD/patches-$name" && cp -R "$from" "$BUILD/patches-$name"
  series_for "$from/series" "$PLATFORM" |
    grep -v -x -F -f <(clean_list "$DESKTOP/patches/ungoogled-exclude.txt") \
      > "$BUILD/patches-$name/series" || true
  if [ -s "$BUILD/patches-$name/series" ]; then
    python3 "$UG/utils/patches.py" apply "$SRC" "$BUILD/patches-$name"
  else
    echo "(no patches in $from)"
  fi
}

PATCHES_ONLY=0
TOOLCHAINS_ONLY=0
CHOSEN_PLATFORM=""
ARCH=""
while [ $# -gt 0 ]; do
  case "$1" in
    --patches-only) PATCHES_ONLY=1 ;;
    --toolchains-only) TOOLCHAINS_ONLY=1 ;;
    --platform) CHOSEN_PLATFORM="${2:?--platform needs a name}"; shift ;;
    -*) die "usage: fetch.sh [--patches-only | --toolchains-only] [--platform <name>] [<arch>]" ;;
    *) ARCH="$1" ;;
  esac
  shift
done
load_platform "$DESKTOP/platforms" "${CHOSEN_PLATFORM:-$(host_platform)}"
PU="$DESKTOP/third_party/$PLATFORM_REPO"
[ "$PATCHES_ONLY" = 1 ] || [ "$(uname -s)" = "$PLATFORM_HOST" ] ||
  die "the $PLATFORM_LABEL build runs on $PLATFORM_HOST (see desktop/README.md)"
HOST="$(host_arch)"
ARCH="${ARCH:-$HOST}"
check_arch "$ARCH"
if [ "$TOOLCHAINS_ONLY" = 1 ]; then
  [ -d "$SRC" ] || die "no source tree at $SRC: run fetch.sh first"
  log "$PLATFORM_LABEL toolchains"
  platform_toolchains
  log "Done. Toolchains replaced in $SRC."
  exit 0
fi
[ -e "$SRC" ] && die "$SRC already exists. Delete $BUILD/src to prepare it again (downloads stay cached)."

log "ungoogled-chromium $UNGOOGLED_TAG + $PLATFORM_LABEL $PLATFORM_TAG"
mkdir -p "$DESKTOP/third_party" "$BUILD"
clone_at https://github.com/ungoogled-software/ungoogled-chromium "$UNGOOGLED_TAG" "$UG"
clone_at "$PLATFORM_REPO_URL" "$PLATFORM_TAG" "$PU"
[ "$(cat "$UG/chromium_version.txt")" = "$CHROMIUM_VERSION" ] ||
  die "ungoogled-chromium in $UG is not for Chromium $CHROMIUM_VERSION"

log "Chromium $CHROMIUM_VERSION source tarball"
platform_fetch_source "$ARCH"

log "Pruning (minus desktop/patches/prune-keep.txt)"
grep -v -x -F -f <(clean_list "$DESKTOP/patches/prune-keep.txt") "$UG/pruning.list" > "$BUILD/pruning.list"
python3 "$UG/utils/prune_binaries.py" "$SRC" "$BUILD/pruning.list"

log "Patches: ungoogled-chromium core, $PLATFORM_LABEL platform, Little Browser"
apply_series "$UG/patches"
apply_series "$PU/patches"
apply_series "$DESKTOP/patches"
if [ "$PATCHES_ONLY" = 1 ]; then log "Done: every patch applies ($PLATFORM_LABEL)."; exit 0; fi

log "Domain substitution (keeping Safe Browsing, Widevine and Chrome Web Store endpoints)"
python3 - "$UG/domain_substitution.list" "$DESKTOP/patches/domsub-exclude.txt" \
  > "$BUILD/domain_substitution.list" <<'PY'
import re, sys
files = open(sys.argv[1]).read().splitlines()
pats = [re.compile(l.split('#', 1)[0].strip()) for l in open(sys.argv[2])
        if l.split('#', 1)[0].strip()]
unmatched = [p.pattern for p in pats if not any(p.search(f) for f in files)]
if unmatched:
    sys.exit("domsub-exclude.txt lines that match no file (renamed upstream?): " +
             ", ".join(unmatched))
kept = [f for f in files if f and not any(p.search(f) for p in pats)]
sys.stderr.write(f"substituting {len(kept)} of {len(files)} files "
                 f"({len(files) - len(kept)} excluded)\n")
print("\n".join(kept))
PY
rm -f "$BUILD/domsubcache.tar.gz"
python3 "$UG/utils/domain_substitution.py" apply \
  -r "$UG/domain_regex.list" -f "$BUILD/domain_substitution.list" \
  -c "$BUILD/domsubcache.tar.gz" "$SRC"

log "$PLATFORM_LABEL toolchains"
platform_toolchains

log "Branding: product strings (translations re-keyed), icons, our source tree"
cp "$DESKTOP/branding/BRANDING" "$SRC/chrome/app/theme/chromium/BRANDING"
PRODUCT="$(sed -n 's/^PRODUCT_FULLNAME=//p' "$DESKTOP/branding/BRANDING")"
COMPANY="$(sed -n 's/^COMPANY_FULLNAME=//p' "$DESKTOP/branding/BRANDING")"
python3 "$DESKTOP/scripts/rebrand_strings.py" "$SRC" "$PRODUCT" "$COMPANY" \
  chrome/app/chromium_strings.grd components/components_chromium_strings.grd
"$DESKTOP/scripts/brand_assets.sh" "$SRC"
platform_branding "$SRC"
ln -sfn "$DESKTOP/lb" "$SRC/lb"
ln -sfn "$DESKTOP/../core/lb-core" "$SRC/lb_core"

log "Done. Source tree: $SRC ($(du -sh "$SRC" | cut -f1)). Next: desktop/scripts/build.sh"

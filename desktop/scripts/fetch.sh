#!/usr/bin/env bash
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# Fetch and prepare the Chromium source tree for the Little Browser desktop build.
#
#   desktop/scripts/fetch.sh [arm64|x86_64]        (macOS; default: host architecture)
#
#   1. ungoogled-chromium (+ platform repo) at the pinned tags
#   2. Chromium source tarball for the pinned version (much smaller than a git checkout)
#   3. prune binaries, apply ungoogled patches (minus ours to skip), platform patches, our patches
#   4. domain substitution, EXCEPT paths that must keep real Google endpoints (Safe Browsing, Widevine)
#   5. platform toolchains (macOS: LLVM, Rust, Node, Crubit), branding, link our source tree
#
# Only macOS is supported; the Windows and Linux platform repos are not used yet.
set -euo pipefail

DESKTOP="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck source=../VERSIONS
source "$DESKTOP/VERSIONS"
UG="$DESKTOP/third_party/ungoogled-chromium"
UGM="$DESKTOP/third_party/ungoogled-chromium-macos"
BUILD="$DESKTOP/build"
SRC="$BUILD/src"

log() { printf '\n\033[1m==> %s\033[0m\n' "$*"; }
die() { printf 'error: %s\n' "$*" >&2; exit 1; }

# Strip comments and blank lines from a list file.
clean_list() { sed -e 's/[[:space:]]*#.*$//' -e '/^[[:space:]]*$/d' "$1"; }

# clone_at <repo url> <tag> <dir>: shallow clone, or verify an existing clone is at <tag>.
clone_at() {
  if [ ! -d "$3/.git" ]; then
    git -c advice.detachedHead=false clone --depth 1 --branch "$2" "$1" "$3"
  elif [ "$(git -C "$3" rev-parse HEAD)" != "$(git -C "$3" rev-parse "refs/tags/$2^{commit}" 2>/dev/null)" ]; then
    die "$3 is not at tag $2 — delete it and re-run."
  fi
}

# apply_series <patches dir>: apply its series minus ungoogled-exclude.txt, via ungoogled's patches.py.
apply_series() {
  local from="$1" name
  name="$(basename "$(dirname "$from")")-$(basename "$from")"
  rm -rf "$BUILD/patches-$name" && cp -R "$from" "$BUILD/patches-$name"
  clean_list "$from/series" |
    grep -v -x -F -f <(clean_list "$DESKTOP/patches/ungoogled-exclude.txt") \
      > "$BUILD/patches-$name/series" || true
  if [ -s "$BUILD/patches-$name/series" ]; then
    python3 "$UG/utils/patches.py" apply "$SRC" "$BUILD/patches-$name"
  else
    echo "(no patches in $from)"
  fi
}

[ "$(uname -s)" = Darwin ] || die "only macOS is wired up so far (see desktop/README.md)"
ARCH="${1:-$(uname -m)}"
case "$ARCH" in arm64|x86_64) ;; *) die "unknown arch $ARCH" ;; esac
[ -e "$SRC" ] && die "$SRC already exists. Delete $BUILD/src to prepare it again (downloads stay cached)."

log "ungoogled-chromium $UNGOOGLED_TAG + macOS $UNGOOGLED_MACOS_TAG"
mkdir -p "$DESKTOP/third_party" "$BUILD"
clone_at https://github.com/ungoogled-software/ungoogled-chromium "$UNGOOGLED_TAG" "$UG"
clone_at https://github.com/ungoogled-software/ungoogled-chromium-macos "$UNGOOGLED_MACOS_TAG" "$UGM"
[ "$(cat "$UG/chromium_version.txt")" = "$CHROMIUM_VERSION" ] ||
  die "ungoogled-chromium in $UG is not for Chromium $CHROMIUM_VERSION"
# The macOS repo's helper scripts expect ./ungoogled-chromium (a submodule) and ./build next to them.
# Point both at ours so there is a single source tree: desktop/build/src.
rm -rf "$UGM/ungoogled-chromium" && ln -s ../ungoogled-chromium "$UGM/ungoogled-chromium"
if [ ! -L "$UGM/build" ]; then rm -rf "$UGM/build" && ln -s ../../build "$UGM/build"; fi

log "Chromium $CHROMIUM_VERSION source tarball"
python3 "$UGM/retrieve_and_unpack_resource.py" --download --generic "$ARCH"

log "Pruning (minus desktop/patches/prune-keep.txt)"
grep -v -x -F -f <(clean_list "$DESKTOP/patches/prune-keep.txt") "$UG/pruning.list" > "$BUILD/pruning.list"
python3 "$UG/utils/prune_binaries.py" "$SRC" "$BUILD/pruning.list"

log "Patches: ungoogled-chromium core, macOS platform, Little Browser"
apply_series "$UG/patches"
apply_series "$UGM/patches"
apply_series "$DESKTOP/patches"

log "Domain substitution (keeping Safe Browsing / Widevine endpoints)"
python3 - "$UG/domain_substitution.list" "$DESKTOP/patches/domsub-exclude.txt" \
  > "$BUILD/domain_substitution.list" <<'PY'
import re, sys
files = open(sys.argv[1]).read().splitlines()
pats = [re.compile(l.split('#', 1)[0].strip()) for l in open(sys.argv[2])
        if l.split('#', 1)[0].strip()]
kept = [f for f in files if f and not any(p.search(f) for p in pats)]
sys.stderr.write(f"substituting {len(kept)} of {len(files)} files "
                 f"({len(files) - len(kept)} excluded)\n")
print("\n".join(kept))
PY
rm -f "$BUILD/domsubcache.tar.gz"
python3 "$UG/utils/domain_substitution.py" apply \
  -r "$UG/domain_regex.list" -f "$BUILD/domain_substitution.list" \
  -c "$BUILD/domsubcache.tar.gz" "$SRC"

log "macOS toolchains (LLVM, Rust, Node.js, Crubit)"
python3 "$UGM/retrieve_and_unpack_resource.py" --platform-specific "$ARCH"

log "Branding: product strings (translations re-keyed), icons, our source tree"
cp "$DESKTOP/branding/BRANDING" "$SRC/chrome/app/theme/chromium/BRANDING"
PRODUCT="$(sed -n 's/^PRODUCT_FULLNAME=//p' "$DESKTOP/branding/BRANDING")"
COMPANY="$(sed -n 's/^COMPANY_FULLNAME=//p' "$DESKTOP/branding/BRANDING")"
python3 "$DESKTOP/scripts/rebrand_strings.py" "$SRC" "$PRODUCT" "$COMPANY" \
  chrome/app/chromium_strings.grd components/components_chromium_strings.grd
"$DESKTOP/scripts/brand_assets.sh" "$SRC"
ln -sfn "$DESKTOP/lb" "$SRC/lb"

log "Done. Source tree: $SRC ($(du -sh "$SRC" | cut -f1)). Next: desktop/scripts/build.sh"

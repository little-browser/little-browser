#!/usr/bin/env bash
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# Configure and build Little Browser (desktop, macOS) from the tree prepared by fetch.sh.
#
#   desktop/scripts/build.sh [--dev] [arm64|x86_64]   configure + build (re-run for incremental builds)
#   desktop/scripts/build.sh --package                ad-hoc sign the release app and make a .dmg
#
#   (default)  release build → out/Default   official, ThinLTO: for packaging and measurements
#   --dev      dev build     → out/Dev       component build, no LTO: fast edit-build-run
#
# GN args = ungoogled flags.gn → macOS flags.macos.gn → desktop/args/common.gn → desktop/args/mac.gn
# (→ desktop/args/dev.gn with --dev); later files win.
# LB_NINJA_FLAGS="-k 0" keeps going after failures to collect every error in one pass.
# LB_CC_WRAPPER=sccache runs every compile through a content-keyed compiler cache (GN cc_wrapper);
# configure the cache itself with sccache's own SCCACHE_* variables.
set -euo pipefail

DESKTOP="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
UG="$DESKTOP/third_party/ungoogled-chromium"
UGM="$DESKTOP/third_party/ungoogled-chromium-macos"
SRC="$DESKTOP/build/src"

[ -d "$SRC" ] || { echo "error: run desktop/scripts/fetch.sh first" >&2; exit 1; }

DEV=0
ARCH="$(uname -m)"
for a in "$@"; do
  case "$a" in
    --package)
      MACOS_AD_HOC_SIGNING=1 CHROMIUM_SRC_DIR="$SRC" "$UGM/sign_and_package_app.sh"
      exit 0 ;;
    --dev) DEV=1 ;;
    arm64|x86_64) ARCH="$a" ;;
    *) echo "usage: build.sh [--dev] [arm64|x86_64] | --package" >&2; exit 1 ;;
  esac
done
case "$ARCH" in arm64) CPU=arm64 ;; x86_64) CPU=x64 ;; esac
OUT_REL="out/Default"
ARG_FILES=("$UG/flags.gn" "$UGM/flags.macos.gn" "$DESKTOP/args/common.gn" "$DESKTOP/args/mac.gn")
if [ "$DEV" = 1 ]; then
  OUT_REL="out/Dev"
  ARG_FILES+=("$DESKTOP/args/dev.gn")
fi
OUT="$SRC/$OUT_REL"
mkdir -p "$OUT"

# ANGLE compiles Metal shaders. `xcrun -f metal` finds a stub even when the toolchain is missing,
# so actually run it.
xcrun metal --version >/dev/null 2>&1 || {
  echo "error: Metal toolchain missing — run: xcodebuild -downloadComponent MetalToolchain" >&2
  exit 1
}

# macOS SDK: the pinned prebuilt LLVM can't read SDKs whose .tbd stubs list targets it doesn't know
# (e.g. "arm64e.x1-macos" in the 27.x SDKs). Use LB_MAC_SDK if set, else the newest installed SDK
# without unknown targets. (Same mechanism as ungoogled-chromium-macos's `build.sh -s`.)
pick_sdk() {
  local sdk dev
  dev="$(xcode-select -p 2>/dev/null || true)"
  while IFS= read -r sdk; do
    [ -d "$sdk" ] && [ ! -L "$sdk" ] || continue   # unmatched globs and version symlinks
    grep -q '\.x1-' "$sdk/usr/lib/libc.tbd" 2>/dev/null || { echo "$sdk"; return; }
  done < <(printf '%s\n' /Library/Developer/CommandLineTools/SDKs/MacOSX[0-9]*.sdk \
             "$dev"/Platforms/MacOSX.platform/Developer/SDKs/MacOSX[0-9]*.sdk | sort -t X -k 3 -V -r)
}
SDK="${LB_MAC_SDK:-$(pick_sdk)}"
[ -f "$SDK/SDKSettings.json" ] || { echo "error: no usable macOS SDK (set LB_MAC_SDK)" >&2; exit 1; }
export SDKROOT="$SDK" # build_bindgen.py asks xcrun for the SDK
echo "macOS SDK: $SDK"

# Merge arg files: "key = value" per line, later files override earlier ones.
python3 - "${ARG_FILES[@]}" > "$OUT/args.gn" <<'PY'
import sys
args = {}
for path in sys.argv[1:]:
    for line in open(path):
        line = line.split('#', 1)[0].strip()
        if '=' in line:
            k, v = (s.strip() for s in line.split('=', 1))
            args[k] = v
print("\n".join(f"{k} = {v}" for k, v in args.items()))
PY
echo "target_cpu = \"$CPU\"" >> "$OUT/args.gn"
if [ -n "${LB_CC_WRAPPER:-}" ]; then
  command -v "$LB_CC_WRAPPER" >/dev/null ||
    { echo "error: LB_CC_WRAPPER=$LB_CC_WRAPPER not found in PATH" >&2; exit 1; }
  echo "cc_wrapper = \"$LB_CC_WRAPPER\"" >> "$OUT/args.gn"
fi
# Chromium's sdk_inputs action requires the SDK path inside root_build_dir.
mkdir -p "$OUT/sdk" && ln -sfn "$SDK" "$OUT/sdk/selected_macos_sdk"
echo "mac_sdk_path = \"//$OUT_REL/sdk/selected_macos_sdk\"" >> "$OUT/args.gn"
echo "args.gn ($OUT_REL):"; sed 's/^/  /' "$OUT/args.gn"

cd "$SRC"

# Tools the macOS recipe builds once (from ungoogled-chromium-macos/build.sh); each step is skipped
# when its output already exists. They live in the source tree / out/Default and serve both builds.

# DevTools frontend needs the Homebrew esbuild binary as a node module.
esbuild_version="$(esbuild --version)"
esbuild_module="$DESKTOP/build/esbuild-node/node_modules/esbuild"
if [ ! -f "$esbuild_module/package.json" ] ||
   [ "$(node -p 'require(process.argv[1]).version' "$esbuild_module/package.json")" != "$esbuild_version" ]; then
  npm install --prefix "$DESKTOP/build/esbuild-node" --no-save --package-lock=false --no-bin-links \
    --ignore-scripts --omit=optional --no-audit --no-fund "esbuild@$esbuild_version"
fi
rm -rf third_party/devtools-frontend/src/node_modules/esbuild
ln -s "$esbuild_module" third_party/devtools-frontend/src/node_modules/esbuild

[ -x out/Default/gn ] || ./tools/gn/bootstrap/bootstrap.py -o out/Default/gn --skip-generate-buildfiles
[ -x third_party/rust-toolchain/bin/bindgen ] || ./tools/rust/build_bindgen.py --skip-test
[ -x third_party/rust-toolchain/crubit/bin/cc_bindings_from_rs ] || "$UGM/build_crubit.sh"

./out/Default/gn gen "$OUT_REL" --fail-on-unused-args
# shellcheck disable=SC2086
ninja ${LB_NINJA_FLAGS:-} -C "$OUT_REL" chrome
if [ "${LB_CC_WRAPPER:-}" = sccache ]; then sccache --show-stats; fi
for app in "$OUT"/*.app; do
  case "$app" in *" Helper"*) ;; *) echo "Built: $app"; break ;; esac
done

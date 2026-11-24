# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# Desktop platform entry: macOS. Sourced by desktop/scripts/fetch.sh and build.sh, which set
# DESKTOP, UG, PU (this platform's ungoogled-chromium repo), BUILD and SRC; see README.md here.
# shellcheck shell=bash
# shellcheck disable=SC2034  # the PLATFORM_* variables are read by fetch.sh and build.sh

PLATFORM_LABEL="macOS"
PLATFORM_HOST="Darwin"
PLATFORM_ARCHS="arm64 x86_64"
PLATFORM_REPO="ungoogled-chromium-macos"
PLATFORM_REPO_URL="https://github.com/ungoogled-software/ungoogled-chromium-macos"
PLATFORM_TAG="$UNGOOGLED_MACOS_TAG"
PLATFORM_FLAGS="flags.macos.gn"
PLATFORM_GN="out/Default/gn"   # bootstrapped by platform_prebuild
PLATFORM_NINJA="ninja"         # Homebrew's
PLATFORM_TARGETS="chrome"

# The Chromium source tarball, unpacked into $SRC.
platform_fetch_source() {
  # The macOS repo's helper scripts expect ./ungoogled-chromium (a submodule) and ./build next to
  # them. Point both at ours so there is a single source tree: desktop/build/src.
  rm -rf "$PU/ungoogled-chromium" && ln -s ../ungoogled-chromium "$PU/ungoogled-chromium"
  if [ ! -L "$PU/build" ]; then rm -rf "$PU/build" && ln -s ../../build "$PU/build"; fi
  python3 "$PU/retrieve_and_unpack_resource.py" --download --generic "$1"
}

# LLVM, Rust, Node.js and Crubit, prebuilt by the macOS repo.
platform_toolchains() {
  python3 "$PU/retrieve_and_unpack_resource.py" --platform-specific "$1"
}

# The icons only macOS uses, after the shared ones (desktop/scripts/brand_assets.sh): the 22 px
# monochrome status-tray logo, app.icns, and Assets.car compiled with Xcode's actool from the
# 1024 px icon (Chromium ships a prebuilt Assets.car; the app uses it as its icon on current macOS).
platform_branding() {
  local ex="$DESKTOP/../branding/exports" t="$1/chrome/app/theme/chromium" tmp set_dir images s k px f
  sips -s format png -z 22 22 "$ex/macos/MenuBarTemplate@2x.png" --out "$t/product_logo_22_mono.png" >/dev/null
  cp "$ex/macos/LittleBrowser.icns" "$t/mac/app.icns"
  tmp="$(mktemp -d)"
  set_dir="$tmp/Assets.xcassets/AppIcon.appiconset"
  mkdir -p "$set_dir"
  images=""
  for s in 16 32 128 256 512; do
    for k in 1 2; do
      px=$((s * k))
      f="appicon_${s}@${k}x.png"
      sips -z "$px" "$px" "$ex/macos/icon-1024.png" --out "$set_dir/$f" >/dev/null
      images+="{\"idiom\":\"mac\",\"size\":\"${s}x${s}\",\"scale\":\"${k}x\",\"filename\":\"$f\"},"
    done
  done
  printf '{"images":[%s],"info":{"version":1,"author":"xcode"}}\n' "${images%,}" > "$set_dir/Contents.json"
  printf '{"info":{"version":1,"author":"xcode"}}\n' > "$tmp/Assets.xcassets/Contents.json"
  mkdir -p "$tmp/out"
  xcrun actool --compile "$tmp/out" --platform macosx --minimum-deployment-target 12.0 \
    --app-icon AppIcon --output-partial-info-plist "$tmp/partial.plist" \
    "$tmp/Assets.xcassets" >/dev/null
  cp "$tmp/out/Assets.car" "$t/mac/Assets.car"
  # Keep the in-tree sources consistent with what we compiled (Chromium's own Icon Composer
  # source is not used by the build and is removed to avoid confusion).
  rm -rf "${t:?}/mac/Assets.xcassets" "${t:?}/mac/AppIcon.icon"
  cp -R "$tmp/Assets.xcassets" "$t/mac/Assets.xcassets"
  rm -rf "${tmp:?}"
}

# macOS SDK: the pinned prebuilt LLVM can't read SDKs whose .tbd stubs list targets it doesn't know
# (e.g. "arm64e.x1-macos" in the 27.x SDKs). LB_MAC_SDK if set, else the newest installed SDK
# without unknown targets. (Same mechanism as ungoogled-chromium-macos's `build.sh -s`.)
mac_pick_sdk() {
  local sdk dev
  dev="$(xcode-select -p 2>/dev/null || true)"
  while IFS= read -r sdk; do
    [ -d "$sdk" ] && [ ! -L "$sdk" ] || continue   # unmatched globs and version symlinks
    grep -q '\.x1-' "$sdk/usr/lib/libc.tbd" 2>/dev/null || { echo "$sdk"; return; }
  done < <(printf '%s\n' /Library/Developer/CommandLineTools/SDKs/MacOSX[0-9]*.sdk \
             "$dev"/Platforms/MacOSX.platform/Developer/SDKs/MacOSX[0-9]*.sdk | sort -t X -k 3 -V -r)
}

# Run in $SRC after args.gn is written, before gn gen: the SDK arg, and the tools the macOS recipe
# builds once (from ungoogled-chromium-macos/build.sh). Each tool is skipped when it already
# exists; they live in the source tree and out/Default and serve both builds.
platform_prebuild() {
  local out_rel="$1" sdk esbuild_version esbuild_module
  # ANGLE compiles Metal shaders. `xcrun -f metal` finds a stub even when the toolchain is
  # missing, so actually run it.
  xcrun metal --version >/dev/null 2>&1 ||
    die "Metal toolchain missing — run: xcodebuild -downloadComponent MetalToolchain"
  sdk="${LB_MAC_SDK:-$(mac_pick_sdk)}"
  [ -f "$sdk/SDKSettings.json" ] || die "no usable macOS SDK (set LB_MAC_SDK)"
  export SDKROOT="$sdk" # build_bindgen.py asks xcrun for the SDK
  echo "macOS SDK: $sdk"
  # Chromium's sdk_inputs action requires the SDK path inside root_build_dir.
  mkdir -p "$out_rel/sdk" && ln -sfn "$sdk" "$out_rel/sdk/selected_macos_sdk"
  echo "mac_sdk_path = \"//$out_rel/sdk/selected_macos_sdk\"" >> "$out_rel/args.gn"

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
  [ -x third_party/rust-toolchain/crubit/bin/cc_bindings_from_rs ] || "$PU/build_crubit.sh"
}

# What the build made.
platform_report() {
  local app
  for app in "$1"/*.app; do
    case "$app" in *" Helper"*) ;; *) echo "Built: $app"; break ;; esac
  done
}

# build.sh --package: an ad-hoc signed .dmg of the release build.
platform_package() {
  exec "$DESKTOP/scripts/package-macos.sh" "$@"
}

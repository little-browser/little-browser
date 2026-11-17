#!/usr/bin/env bash
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# Put Little Browser's icons (branding/exports, see branding/README.md) into a Chromium tree:
#
#   desktop/scripts/brand_assets.sh <chromium src dir>
#
# - macOS: app.icns, and Assets.car compiled with Xcode's actool from the 1024 px icon
#   (Chromium ships a prebuilt Assets.car; the app uses it as its icon on current macOS)
# - product logos used inside the UI (About page, Linux, status tray)
# Windows (.ico) is wired up together with the Windows build.
set -euo pipefail

SRC="$(cd "${1:?usage: brand_assets.sh <chromium src dir>}" && pwd)"
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
EX="$REPO/branding/exports"
T="$SRC/chrome/app/theme/chromium"
png() { echo "$EX/linux/hicolor/$1x$1/apps/little-browser.png"; }

[ -f "$EX/macos/LittleBrowser.icns" ] ||
  { echo "error: no icon exports — run: cd branding && npm install && npm run export" >&2; exit 1; }

# Product logos (UI, Linux) and the 22 px monochrome status-tray icon.
for s in 16 24 48 64 128 256; do cp "$(png $s)" "$T/product_logo_$s.png"; done
for s in 24 48 64 128 256; do cp "$(png $s)" "$T/linux/product_logo_$s.png"; done
cp "$REPO/branding/svg/icon.svg" "$T/product_logo.svg"
# Scaled UI logos (About page "current-channel-logo", menus): 1x and 2x.
TH="$SRC/chrome/app/theme"
for s in 16 32; do
  cp "$(png $s)" "$TH/default_100_percent/chromium/product_logo_$s.png"
  cp "$(png $((s * 2)))" "$TH/default_200_percent/chromium/product_logo_$s.png"
done
sips -s format png -z 22 22 "$EX/macos/MenuBarTemplate@2x.png" --out "$T/product_logo_22_mono.png" >/dev/null

# macOS: .icns + asset catalog.
cp "$EX/macos/LittleBrowser.icns" "$T/mac/app.icns"
tmp="$(mktemp -d)"
trap 'rm -rf "${tmp:?}"' EXIT
set_dir="$tmp/Assets.xcassets/AppIcon.appiconset"
mkdir -p "$set_dir"
images=""
for s in 16 32 128 256 512; do
  for k in 1 2; do
    px=$((s * k))
    f="appicon_${s}@${k}x.png"
    sips -z "$px" "$px" "$EX/macos/icon-1024.png" --out "$set_dir/$f" >/dev/null
    images+="{\"idiom\":\"mac\",\"size\":\"${s}x${s}\",\"scale\":\"${k}x\",\"filename\":\"$f\"},"
  done
done
printf '{"images":[%s],"info":{"version":1,"author":"xcode"}}\n' "${images%,}" > "$set_dir/Contents.json"
printf '{"info":{"version":1,"author":"xcode"}}\n' > "$tmp/Assets.xcassets/Contents.json"
mkdir -p "$tmp/out"
xcrun actool --compile "$tmp/out" --platform macosx --minimum-deployment-target 12.0 \
  --app-icon AppIcon --output-partial-info-plist "$tmp/partial.plist" \
  "$tmp/Assets.xcassets" >/dev/null
cp "$tmp/out/Assets.car" "$T/mac/Assets.car"
# Keep the in-tree sources consistent with what we compiled (Chromium's own Icon Composer
# source is not used by the build and is removed to avoid confusion).
rm -rf "${T:?}/mac/Assets.xcassets" "${T:?}/mac/AppIcon.icon"
cp -R "$tmp/Assets.xcassets" "$T/mac/Assets.xcassets"
echo "brand_assets: icons installed into $T"

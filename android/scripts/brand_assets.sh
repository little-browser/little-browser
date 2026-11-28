#!/usr/bin/env bash
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# Put Little Browser's launcher icon and logos (branding/exports/android, see branding/README.md)
# into a Chromium tree, over Chromium's own, under the names Chromium's resources already list:
#   android/scripts/brand_assets.sh <chromium src dir>
# app_icon is the icon before Android 8; layered_app_icon and layered_app_icon_background are the
# adaptive icon's layers, which android/patches/app-icon.patch puts together (with the
# monochrome layer, a vector there). fre_product_logo is the welcome screen's logo and
# product_logo_name the lockup; its dark-mode copy is new, listed by android/patches/logo-dark.patch.
# The notification icon (ic_chrome) is a vector, so app-icon.patch replaces it, not this script.
set -euo pipefail

SRC="$(cd "${1:?usage: brand_assets.sh <chromium src dir>}" && pwd)"
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
EX="$REPO/branding/exports/android/res"
RES="$SRC/chrome/android/java/res_chromium_base"
LOGOS="$SRC/components/browser_ui/styles/android/java/res_chromium"

[ -f "$EX/mipmap-mdpi/ic_launcher.png" ] ||
  { echo "error: no icon exports — run: cd branding && npm install && npm run export" >&2; exit 1; }

for d in mdpi hdpi xhdpi xxhdpi xxxhdpi; do
  [ -d "$RES/mipmap-$d" ] || { echo "error: $RES/mipmap-$d is missing" >&2; exit 1; }
  cp "$EX/mipmap-$d/ic_launcher.png" "$RES/mipmap-$d/app_icon.png"
  cp "$EX/mipmap-$d/ic_launcher_foreground.png" "$RES/mipmap-$d/layered_app_icon.png"
  cp "$EX/mipmap-$d/ic_launcher_background.png" "$RES/mipmap-$d/layered_app_icon_background.png"
  [ -d "$LOGOS/drawable-$d" ] || { echo "error: $LOGOS/drawable-$d is missing" >&2; exit 1; }
  cp "$EX/drawable-$d/fre_product_logo.png" "$EX/drawable-$d/product_logo_name.png" "$LOGOS/drawable-$d/"
  mkdir -p "$LOGOS/drawable-night-$d"
  cp "$EX/drawable-night-$d/product_logo_name.png" "$LOGOS/drawable-night-$d/"
done
echo "brand_assets: launcher icon and logos installed into $RES and $LOGOS"

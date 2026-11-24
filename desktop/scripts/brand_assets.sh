#!/usr/bin/env bash
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# Put Little Browser's shared icons (branding/exports, see branding/README.md) into a Chromium tree:
#   desktop/scripts/brand_assets.sh <chromium src dir>
# The product logos used inside the UI (About page, menus, Linux). Each platform's own icons
# (macOS: app.icns, Assets.car, the status-tray logo) come from its entry's platform_branding
# (desktop/platforms/).
set -euo pipefail

SRC="$(cd "${1:?usage: brand_assets.sh <chromium src dir>}" && pwd)"
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
EX="$REPO/branding/exports"
T="$SRC/chrome/app/theme/chromium"
png() { echo "$EX/linux/hicolor/$1x$1/apps/little-browser.png"; }

[ -f "$(png 16)" ] ||
  { echo "error: no icon exports — run: cd branding && npm install && npm run export" >&2; exit 1; }

# Product logos (UI, Linux).
for s in 16 24 48 64 128 256; do cp "$(png $s)" "$T/product_logo_$s.png"; done
for s in 24 48 64 128 256; do cp "$(png $s)" "$T/linux/product_logo_$s.png"; done
cp "$REPO/branding/svg/icon.svg" "$T/product_logo.svg"
# Scaled UI logos (About page "current-channel-logo", menus): 1x and 2x.
TH="$SRC/chrome/app/theme"
for s in 16 32; do
  cp "$(png $s)" "$TH/default_100_percent/chromium/product_logo_$s.png"
  cp "$(png $((s * 2)))" "$TH/default_200_percent/chromium/product_logo_$s.png"
done
echo "brand_assets: icons installed into $T"

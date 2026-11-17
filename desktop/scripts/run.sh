#!/usr/bin/env bash
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# Launch a local Little Browser build with a separate, persistent developer profile
# (never your real one).
#
#   desktop/scripts/run.sh [--dev] [--fresh] [extra browser flags / URL...]
#
#   --dev    run out/Dev (fast dev build) instead of out/Default (release)
#   --fresh  start from an empty profile (the old one is kept, renamed to <profile>.old-<timestamp>)
set -euo pipefail

DESKTOP="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="$DESKTOP/build/src/out/Default"
PROFILE="$DESKTOP/build/dev-profile"
args=()
HINT=""
for a in "$@"; do
  case "$a" in
    --dev) OUT="$DESKTOP/build/src/out/Dev"; PROFILE="$DESKTOP/build/dev-profile-dev"; HINT=" --dev" ;;
    --fresh) FRESH=1 ;;
    *) args+=("$a") ;;
  esac
done
if [ "${FRESH:-0}" = 1 ] && [ -d "$PROFILE" ]; then
  mv "$PROFILE" "$PROFILE.old-$(date +%Y%m%d-%H%M%S)"
fi
APP="$OUT/Little Browser.app"
[ -d "$APP" ] || { echo "error: $APP not built — run desktop/scripts/build.sh$HINT" >&2; exit 1; }
# --use-mock-keychain: local builds get a new ad-hoc signature on every rebuild, so macOS would ask
# for the "Safe Storage" keychain item each time ("Always Allow" is tied to the signature). The dev
# profile doesn't need real keychain protection.
exec "$APP/Contents/MacOS/Little Browser" --user-data-dir="$PROFILE" --use-mock-keychain \
  "${args[@]+"${args[@]}"}"

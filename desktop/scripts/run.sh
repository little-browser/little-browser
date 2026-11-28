#!/usr/bin/env bash
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# Launch a local Little Browser build with a separate, persistent developer profile
# (never your real one).
#
#   desktop/scripts/run.sh [--dev] [--arch <arch>] [--fresh] [extra browser flags / URL...]
#
#   --dev    run out/Dev (fast dev build) instead of out/Default (release)
#   --arch   run the build for another architecture (out/Default-x64; macOS runs x64 with Rosetta)
#   --fresh  start from an empty profile (the old one is kept, renamed to <profile>.old-<timestamp>)
set -euo pipefail

DESKTOP="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck source=lib.sh
source "$DESKTOP/scripts/lib.sh"
KIND=Default
HOST="$(host_arch)"
ARCH="$HOST"
PROFILE="$DESKTOP/build/dev-profile"
args=()
HINT=""
while [ $# -gt 0 ]; do
  case "$1" in
    --dev) KIND=Dev; PROFILE="$DESKTOP/build/dev-profile-dev"; HINT+=" --dev" ;;
    --arch) ARCH="${2:?--arch needs an architecture}"; HINT+=" $2"; shift ;;
    --fresh) FRESH=1 ;;
    *) args+=("$1") ;;
  esac
  shift
done
OUT="$DESKTOP/build/src/$(out_dir "$KIND" "$ARCH" "$HOST")"
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

#!/usr/bin/env bash
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# Look for an ungoogled-chromium release newer than chromium/VERSIONS.
#
#   desktop/scripts/upstream.sh           print the newest usable release and whether it is newer
#   desktop/scripts/upstream.sh --write   also rewrite chromium/VERSIONS to it
#
# Usable = an ungoogled-chromium tag "<chromium version>-<rev>" that also has a macOS platform tag
# "<that tag>.<platform rev>". A newer platform revision alone also counts as an update.
# Windows and Linux platform repos join this check when their builds exist.
set -euo pipefail

DESKTOP="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CHROMIUM="$(cd "$DESKTOP/../chromium" && pwd)"
VERSIONS="$CHROMIUM/VERSIONS"
# shellcheck source=../../chromium/VERSIONS
source "$VERSIONS"

UG_REPO=https://github.com/ungoogled-software/ungoogled-chromium
UGM_REPO=https://github.com/ungoogled-software/ungoogled-chromium-macos

WRITE=0
case "${1:-}" in
  --write) WRITE=1 ;;
  "") ;;
  *) echo "usage: upstream.sh [--write]" >&2; exit 1 ;;
esac

tags() { git ls-remote --tags --refs "$1" | sed 's#.*refs/tags/##'; }

ug_tags="$(tags "$UG_REPO" | grep -E '^[0-9]+(\.[0-9]+){3}-[0-9]+$' || true)"
mac_tags="$(tags "$UGM_REPO" | grep -E '^[0-9]+(\.[0-9]+){3}-[0-9]+\.[0-9]+$' || true)"
[ -n "$ug_tags" ] && [ -n "$mac_tags" ] || { echo "error: could not list upstream tags" >&2; exit 1; }

# Newest macOS tag whose ungoogled-chromium tag exists.
best=""
while read -r m; do
  if grep -qxF "${m%.*}" <<<"$ug_tags"; then best="$m"; break; fi
done < <(sort -V -r <<<"$mac_tags")
[ -n "$best" ] || { echo "error: no macOS tag matches an ungoogled-chromium tag" >&2; exit 1; }

new_ug="${best%.*}"
new_chromium="${new_ug%-*}"
echo "pinned:   $UNGOOGLED_MACOS_TAG"
echo "upstream: $best"

newest="$(printf '%s\n%s\n' "$UNGOOGLED_MACOS_TAG" "$best" | sort -V | tail -1)"
if [ "$best" = "$UNGOOGLED_MACOS_TAG" ] || [ "$newest" != "$best" ]; then
  echo "up to date"
  exit 0
fi
echo "update available: Chromium $new_chromium"

if [ "$WRITE" = 1 ]; then
  sed -i.bak \
    -e "s/^CHROMIUM_VERSION=.*/CHROMIUM_VERSION=$new_chromium/" \
    -e "s/^UNGOOGLED_TAG=.*/UNGOOGLED_TAG=$new_ug/" \
    -e "s/^UNGOOGLED_MACOS_TAG=.*/UNGOOGLED_MACOS_TAG=$best/" \
    "$VERSIONS"
  rm -f "$VERSIONS.bak"
  echo "wrote $VERSIONS"
fi

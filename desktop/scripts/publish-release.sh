#!/usr/bin/env bash
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# Publish a release's macOS disk image on downloads.littlebrowser.app: list it in releases.json,
# which the website reads, and offer it to the browser's updater. CI only (the manual `publish`
# job on release tags), with the release role.
#
#   desktop/scripts/publish-release.sh <version> <file.dmg>
#
# <version> is the tag without its "v", e.g. 157.0.8103.78-1. The file is stored twice, never
# replaced:
#   <version>/LittleBrowser-<version>-macos-arm64.dmg          the website's download
#   updates/<version>/LittleBrowser-<version>-macos-arm64.dmg  the updater's copy
# so the download counts (download-stats.sh) tell new downloads from updates. A version that is
# already published stops the script. releases.json lists releases newest first:
#   { "releases": [ { "version", "date", "channel", "chromium", "security",
#                     "files": [ { "platform", "arch", "url", "size", "sha256",
#                                  "update": { "url", "bundle_version",
#                                              "minimum_system_version", "ed_signature" } } ] } ] }
# and updates/macos-arm64.xml, the updater's feed, is written from it (update_feed.py).
#
# The update details come from <file>.update.json, which package-macos.sh writes next to the
# disk image when it signs the update. Without it (no update signing key yet) the release is
# published as a download only, and the feed keeps offering the previous release.
#
# A security release is marked critical in the feed, so the browser says so and asks again
# sooner. LB_SECURITY_RELEASE=1 or 0 decides; unset, a release on a new Chromium version (an
# upstream security update) is one and a revision on the same Chromium version isn't.
#
# Needs LB_DOWNLOADS_BUCKET, LB_DOWNLOADS_DISTRIBUTION and LB_DOWNLOADS_URL (CI/CD variables
# written by the infrastructure repository), aws, jq and python3. LB_DRY_RUN=1 prints the new
# releases.json and feed and uploads nothing.
set -euo pipefail

DESKTOP="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck source=../VERSIONS
source "$DESKTOP/VERSIONS"

die() { echo "error: $*" >&2; exit 1; }

[ $# -eq 2 ] || die "usage: publish-release.sh <version> <file.dmg> (exactly one disk image)"
VERSION="$1"
FILE="$2"
: "${LB_DOWNLOADS_BUCKET:?}" "${LB_DOWNLOADS_DISTRIBUTION:?}" "${LB_DOWNLOADS_URL:?}"
[[ "$VERSION" =~ ^[0-9]+(\.[0-9]+){3}-[0-9]+$ ]] || die "version $VERSION is not <chromium version>-<revision>"
[ -f "$FILE" ] || die "no such file: $FILE"
DRY="${LB_DRY_RUN:-0}"
UPDATE_INFO="${FILE%.dmg}.update.json"
FEED=updates/macos-arm64.xml

NAME="LittleBrowser-$VERSION-macos-arm64.dmg"
KEY="$VERSION/$NAME"
SIZE="$(wc -c < "$FILE" | tr -d ' ')"
SHA256="$( (sha256sum "$FILE" 2>/dev/null || shasum -a 256 "$FILE") | cut -d' ' -f1)"
DATE="$(date -u +%Y-%m-%d)"

if aws s3api head-object --bucket "$LB_DOWNLOADS_BUCKET" --key "$KEY" >/dev/null 2>&1; then
  die "$KEY is already published; a release is never replaced. Tag a new revision instead."
fi

TMP="$(mktemp -d)"
trap 'rm -rf "${TMP:?}"' EXIT
if ! aws s3 cp --only-show-errors "s3://$LB_DOWNLOADS_BUCKET/releases.json" "$TMP/current.json" 2>/dev/null; then
  echo '{"releases":[]}' > "$TMP/current.json"   # the first release
fi

if [ -f "$UPDATE_INFO" ]; then
  jq -e '.bundle_version and .ed_signature' "$UPDATE_INFO" >/dev/null ||
    die "$UPDATE_INFO has no bundle_version or ed_signature"
  UPDATE="$(jq -c --arg u "$LB_DOWNLOADS_URL/updates/$KEY" \
    '{ url: $u, bundle_version, minimum_system_version, ed_signature }' "$UPDATE_INFO")"
else
  echo "warning: no $UPDATE_INFO (no update signing key yet): a download only, not offered as an update" >&2
  UPDATE=null
fi

case "${LB_SECURITY_RELEASE:-}" in
  1) SECURITY=true ;;
  0) SECURITY=false ;;
  "") SECURITY="$(jq --arg c "$CHROMIUM_VERSION" \
        '(.releases[0].chromium // "") != $c' "$TMP/current.json")" ;;
  *) die "LB_SECURITY_RELEASE is 1, 0 or unset, not $LB_SECURITY_RELEASE" ;;
esac

# Add the file to its release (a later platform joins the same version), or the release
# itself at the top of the list.
jq --arg v "$VERSION" --arg d "$DATE" --arg c "$CHROMIUM_VERSION" \
   --arg u "$LB_DOWNLOADS_URL/$KEY" --arg h "$SHA256" --argjson s "$SIZE" \
   --argjson update "$UPDATE" --argjson security "$SECURITY" '
  ({ platform: "macos", arch: "arm64", url: $u, size: $s, sha256: $h }
   + (if $update then { update: $update } else {} end)) as $file
  | if any(.releases[]; .version == $v)
    then .releases |= map(if .version == $v then .files += [$file] else . end)
    else .releases = [ { version: $v, date: $d, channel: "beta", chromium: $c,
                         security: $security, files: [ $file ] } ] + .releases
    end' "$TMP/current.json" > "$TMP/releases.json"
python3 -I "$DESKTOP/scripts/update_feed.py" "$TMP/releases.json" macos arm64 > "$TMP/feed.xml"

echo "publishing $NAME ($SIZE bytes, SHA-256 $SHA256, security release: $SECURITY)"
if [ "$DRY" = 1 ]; then
  echo "dry run: would upload $FILE to s3://$LB_DOWNLOADS_BUCKET/$KEY"
  [ "$UPDATE" = null ] || echo "dry run: and to s3://$LB_DOWNLOADS_BUCKET/updates/$KEY"
  echo "dry run: this releases.json:"
  cat "$TMP/releases.json"
  echo "dry run: this $FEED:"
  cat "$TMP/feed.xml"
  exit 0
fi

# The files first, then the lists that point at them.
upload_dmg() {
  aws s3 cp --only-show-errors "$FILE" "s3://$LB_DOWNLOADS_BUCKET/$1" \
    --content-type application/x-apple-diskimage \
    --cache-control 'public, max-age=31536000, immutable'
}
upload_dmg "$KEY"
[ "$UPDATE" = null ] || upload_dmg "updates/$KEY"
aws s3 cp --only-show-errors "$TMP/releases.json" "s3://$LB_DOWNLOADS_BUCKET/releases.json" \
  --content-type application/json --cache-control 'public, max-age=60'
aws s3 cp --only-show-errors "$TMP/feed.xml" "s3://$LB_DOWNLOADS_BUCKET/$FEED" \
  --content-type application/xml --cache-control 'public, max-age=300'
aws cloudfront create-invalidation --distribution-id "$LB_DOWNLOADS_DISTRIBUTION" \
  --paths /releases.json "/$FEED" --query 'Invalidation.Id' --output text >/dev/null
echo "published $LB_DOWNLOADS_URL/$KEY"
[ "$UPDATE" = null ] || echo "offered as an update: $LB_DOWNLOADS_URL/$FEED"

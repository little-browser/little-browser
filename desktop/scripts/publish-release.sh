#!/usr/bin/env bash
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# Publish a release's macOS disk image on downloads.littlebrowser.app and list it in
# releases.json, which the website reads. CI only (the manual `publish` job on release
# tags), with the release role.
#
#   desktop/scripts/publish-release.sh <version> <file.dmg>
#
# <version> is the tag without its "v", e.g. 157.0.8103.78-1. The file is stored as
# <version>/LittleBrowser-<version>-macos-arm64.dmg and never replaced: a version that is
# already published stops the script. releases.json lists releases newest first:
#   { "releases": [ { "version", "date", "channel", "chromium",
#                     "files": [ { "platform", "arch", "url", "size", "sha256" } ] } ] }
#
# Needs LB_DOWNLOADS_BUCKET, LB_DOWNLOADS_DISTRIBUTION and LB_DOWNLOADS_URL (CI/CD variables
# written by the infrastructure repository), aws and jq. LB_DRY_RUN=1 prints the new
# releases.json and uploads nothing.
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

# Add the file to its release (a later platform joins the same version), or the release
# itself at the top of the list.
jq --arg v "$VERSION" --arg d "$DATE" --arg c "$CHROMIUM_VERSION" \
   --arg u "$LB_DOWNLOADS_URL/$KEY" --arg h "$SHA256" --argjson s "$SIZE" '
  { platform: "macos", arch: "arm64", url: $u, size: $s, sha256: $h } as $file
  | if any(.releases[]; .version == $v)
    then .releases |= map(if .version == $v then .files += [$file] else . end)
    else .releases = [ { version: $v, date: $d, channel: "alpha", chromium: $c, files: [ $file ] } ] + .releases
    end' "$TMP/current.json" > "$TMP/releases.json"

echo "publishing $NAME ($SIZE bytes, SHA-256 $SHA256)"
if [ "$DRY" = 1 ]; then
  echo "dry run: would upload $FILE to s3://$LB_DOWNLOADS_BUCKET/$KEY and this releases.json:"
  cat "$TMP/releases.json"
  exit 0
fi

aws s3 cp --only-show-errors "$FILE" "s3://$LB_DOWNLOADS_BUCKET/$KEY" \
  --content-type application/x-apple-diskimage \
  --cache-control 'public, max-age=31536000, immutable'
aws s3 cp --only-show-errors "$TMP/releases.json" "s3://$LB_DOWNLOADS_BUCKET/releases.json" \
  --content-type application/json --cache-control 'public, max-age=60'
aws cloudfront create-invalidation --distribution-id "$LB_DOWNLOADS_DISTRIBUTION" \
  --paths /releases.json --query 'Invalidation.Id' --output text >/dev/null
echo "published $LB_DOWNLOADS_URL/$KEY"

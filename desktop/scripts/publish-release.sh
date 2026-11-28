#!/usr/bin/env bash
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# Publish a release's macOS disk image on downloads.littlebrowser.app: list it in releases.json,
# which the website reads, and offer it to the browser's updater. CI only (the manual `publish-macos-<arch>`
# job on release tags), with the release role.
#
#   desktop/scripts/publish-release.sh <version> <LittleBrowser-macos-<arch>.dmg>
#
# <version> is the tag without its "v", e.g. 157.0.8103.78-1. <arch> (arm64 or x64) comes from
# the disk image's name, as package-macos.sh writes it. Each architecture is published on its own:
# a later one joins its release. The file is stored twice, never replaced:
#   <version>/LittleBrowser-<version>-macos-<arch>.dmg          the website's download
#   updates/<version>/LittleBrowser-<version>-macos-<arch>.dmg  the updater's copy
# so the download counts (download-stats.sh) tell new downloads from updates. A build already
# published for the version stops the script. releases.json lists releases newest first, by
# version, whichever of their builds was published first:
#   { "releases": [ { "version", "date", "channel", "chromium", "security", "notes": [ "…" ],
#                     "files": [ { "platform", "arch", "url", "size", "sha256",
#                                  "update": { "url", "bundle_version",
#                                              "minimum_system_version", "ed_signature" } } ] } ] }
# and updates/macos-<arch>.xml, that architecture's update feed, is written from it (update_feed.py).
#
# The update details come from <file>.update.json, which package-macos.sh writes next to the
# disk image when it signs the update. Without it (no update signing key yet) the release is
# published as a download only, and the feed keeps offering the previous release.
#
# A security release is marked critical in the feed, so the browser says so and asks again
# sooner. LB_SECURITY_RELEASE=1 or 0 decides; unset, a release on a new Chromium version (an
# upstream security update) is one and a revision on the same Chromium version isn't.
#
# A new release's notes are the Release-note lines of the commits since the release before it
# (by version), after a line about Chromium (release_notes.py); LB_RELEASE_NOTES (one note per
# line, or notes separated by "|") replaces the commits' notes. A release's notes and security
# flag are set by its first published build: a later build joins them, and stops if
# LB_RELEASE_NOTES or LB_SECURITY_RELEASE is set, since they would change nothing. GitLab's
# release for the tag is written afterwards from the published releases.json (gitlab_release.py,
# the `release-macos-<arch>` job); the script waits for CloudFront to serve the new one, and only
# warns if that wait fails, since the build is out by then.
#
# Needs LB_DOWNLOADS_BUCKET, LB_DOWNLOADS_DISTRIBUTION and LB_DOWNLOADS_URL (CI/CD variables
# written by the infrastructure repository), aws (with s3 and cloudfront Create/GetInvalidation),
# jq, python3 and git with the release tags.
# LB_DRY_RUN=1 prints the new releases.json, notes and feed and uploads nothing.
set -euo pipefail

DESKTOP="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck source=../VERSIONS
source "$DESKTOP/VERSIONS"

die() { echo "error: $*" >&2; exit 1; }

[ $# -eq 2 ] || die "usage: publish-release.sh <version> <LittleBrowser-macos-<arch>.dmg> (exactly one disk image)"
VERSION="$1"
FILE="$2"
: "${LB_DOWNLOADS_BUCKET:?}" "${LB_DOWNLOADS_DISTRIBUTION:?}" "${LB_DOWNLOADS_URL:?}"
[[ "$VERSION" =~ ^[0-9]+(\.[0-9]+){3}-[0-9]+$ ]] || die "version $VERSION is not <chromium version>-<revision>"
[ -f "$FILE" ] || die "no such file: $FILE"
DRY="${LB_DRY_RUN:-0}"
UPDATE_INFO="${FILE%.dmg}.update.json"
[[ "$(basename "$FILE")" =~ ^LittleBrowser-macos-(arm64|x64)\.dmg$ ]] ||
  die "$FILE is not LittleBrowser-macos-<arm64 or x64>.dmg (package-macos.sh's name)"
ARCH="${BASH_REMATCH[1]}"
FEED="updates/macos-$ARCH.xml"

NAME="LittleBrowser-$VERSION-macos-$ARCH.dmg"
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

# Releases compare by version, number by number: 157.0.8103.78-10 is after -9.
VERSION_KEY='[.version | scan("[0-9]+") | tonumber]'

if jq -e --arg v "$VERSION" 'any(.releases[]; .version == $v)' "$TMP/current.json" >/dev/null; then
  # A later build joins its release, which keeps the notes and security flag its first build set.
  [ -z "${LB_RELEASE_NOTES:-}" ] && [ -z "${LB_SECURITY_RELEASE:-}" ] ||
    die "$VERSION is already out for another build, whose publish set its notes and security flag: unset LB_RELEASE_NOTES and LB_SECURITY_RELEASE"
  SECURITY="$(jq -r --arg v "$VERSION" '.releases[] | select(.version == $v) | .security == true' \
    "$TMP/current.json")"
  NOTES=null
else
  # The release before this one by version, which the notes and the security default count from;
  # with builds published one at a time, it needn't be the newest one listed.
  PREVIOUS="$(jq -c --arg v "$VERSION" "[.releases[] | select($VERSION_KEY < (\$v | [scan(\"[0-9]+\") | tonumber]))]
    | max_by($VERSION_KEY) // {}" "$TMP/current.json")"
  PUBLISHED="$(jq -r '.version // ""' <<<"$PREVIOUS")"
  PREVIOUS_CHROMIUM="$(jq -r '.chromium // ""' <<<"$PREVIOUS")"
  case "${LB_SECURITY_RELEASE:-}" in
    1) SECURITY=true ;;
    0) SECURITY=false ;;
    "") if [ "$PREVIOUS_CHROMIUM" != "$CHROMIUM_VERSION" ]; then SECURITY=true; else SECURITY=false; fi ;;
    *) die "LB_SECURITY_RELEASE is 1, 0 or unset, not $LB_SECURITY_RELEASE" ;;
  esac
  HEAD_REF="v$VERSION"
  if [ "$DRY" = 1 ] && ! git -C "$DESKTOP" rev-parse --verify --quiet "$HEAD_REF^{commit}" >/dev/null; then
    echo "dry run: no tag $HEAD_REF here, so the notes run up to HEAD" >&2
    HEAD_REF=HEAD
  fi
  NOTES="$(python3 -I "$DESKTOP/scripts/release_notes.py" notes --head "$HEAD_REF" \
    --published "$PUBLISHED" --previous-chromium "$PREVIOUS_CHROMIUM" \
    --chromium "$CHROMIUM_VERSION" --security "$SECURITY")"
fi

# Add the file to its release (a later build joins the same version), or the release itself,
# then keep the list newest first by version.
jq --arg v "$VERSION" --arg d "$DATE" --arg c "$CHROMIUM_VERSION" \
   --arg u "$LB_DOWNLOADS_URL/$KEY" --arg h "$SHA256" --argjson s "$SIZE" \
   --argjson update "$UPDATE" --argjson security "$SECURITY" --argjson notes "$NOTES" --arg a "$ARCH" '
  ({ platform: "macos", arch: $a, url: $u, size: $s, sha256: $h }
   + (if $update then { update: $update } else {} end)) as $file
  | if any(.releases[]; .version == $v)
    then .releases |= map(if .version == $v then .files += [$file] else . end)
    else .releases += [ { version: $v, date: $d, channel: "beta", chromium: $c,
                          security: $security, notes: $notes, files: [ $file ] } ]
    end
  | .releases |= (sort_by('"$VERSION_KEY"') | reverse)' "$TMP/current.json" > "$TMP/releases.json"
python3 -I "$DESKTOP/scripts/update_feed.py" "$TMP/releases.json" macos "$ARCH" > "$TMP/feed.xml"
python3 -I "$DESKTOP/scripts/release_notes.py" markdown "$TMP/releases.json" "$VERSION" \
  > "$TMP/release-notes.md"

echo "publishing $NAME ($SIZE bytes, SHA-256 $SHA256, security release: $SECURITY)"
if [ "$DRY" = 1 ]; then
  echo "dry run: would upload $FILE to s3://$LB_DOWNLOADS_BUCKET/$KEY"
  [ "$UPDATE" = null ] || echo "dry run: and to s3://$LB_DOWNLOADS_BUCKET/updates/$KEY"
  echo "dry run: this releases.json:"
  cat "$TMP/releases.json"
  echo "dry run: GitLab's release would then read:"
  cat "$TMP/release-notes.md"
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
INVALIDATION="$(aws cloudfront create-invalidation --distribution-id "$LB_DOWNLOADS_DISTRIBUTION" \
  --paths /releases.json "/$FEED" --query 'Invalidation.Id' --output text)"
# Until it completes, CloudFront may still serve the old releases.json, which the release job
# reads next (it reads again until the build shows). The build is out either way: a failed wait
# only warns, so the job's failure can't strand the release job.
if ! aws cloudfront wait invalidation-completed --distribution-id "$LB_DOWNLOADS_DISTRIBUTION" \
    --id "$INVALIDATION"; then
  echo "warning: CloudFront's invalidation $INVALIDATION hasn't completed; it serves the new files once it does" >&2
fi
echo "published $LB_DOWNLOADS_URL/$KEY"
[ "$UPDATE" = null ] || echo "offered as an update: $LB_DOWNLOADS_URL/$FEED"

#!/usr/bin/env bash
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# Count the downloads and updates of each release file from downloads.littlebrowser.app's
# CloudFront logs and publish the totals as stats.json there, which the website reads. CI only
# (the scheduled `download-stats` job), with the release role.
#
#   desktop/scripts/download-stats.sh
#
# The logs hold no IP addresses or user agents and are deleted after a week (the infrastructure
# repository sets both up); the counting rules are in download_stats.py.
#
# Needs LB_DOWNLOADS_BUCKET, LB_DOWNLOADS_DISTRIBUTION and LB_DOWNLOADS_LOG_BUCKET (CI/CD
# variables written by the infrastructure repository), aws and python3. LB_DRY_RUN=1 prints the
# new stats.json and uploads nothing.
set -euo pipefail

SCRIPTS="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
: "${LB_DOWNLOADS_BUCKET:?}" "${LB_DOWNLOADS_DISTRIBUTION:?}" "${LB_DOWNLOADS_LOG_BUCKET:?}"

TMP="$(mktemp -d)"
trap 'rm -rf "${TMP:?}"' EXIT

aws s3 cp --only-show-errors "s3://$LB_DOWNLOADS_BUCKET/releases.json" "$TMP/releases.json"
aws s3 cp --only-show-errors "s3://$LB_DOWNLOADS_BUCKET/stats.json" "$TMP/stats.json" 2>/dev/null ||
  rm -f "$TMP/stats.json"   # the first run
mkdir "$TMP/logs"
aws s3 sync --only-show-errors "s3://$LB_DOWNLOADS_LOG_BUCKET/" "$TMP/logs"

python3 -I "$SCRIPTS/download_stats.py" --releases "$TMP/releases.json" --stats "$TMP/stats.json" \
  --logs "$TMP/logs" --today "$(date -u +%Y-%m-%d)" > "$TMP/new.json"

if [ "${LB_DRY_RUN:-0}" = 1 ]; then
  echo "dry run: would upload this stats.json:"
  cat "$TMP/new.json"
  exit 0
fi
aws s3 cp --only-show-errors "$TMP/new.json" "s3://$LB_DOWNLOADS_BUCKET/stats.json" \
  --content-type application/json --cache-control 'public, max-age=3600'
aws cloudfront create-invalidation --distribution-id "$LB_DOWNLOADS_DISTRIBUTION" \
  --paths /stats.json --query 'Invalidation.Id' --output text >/dev/null
echo "published stats.json, counted through $(python3 -I -c \
  'import json,sys; print(json.load(open(sys.argv[1]))["counted_through"])' "$TMP/new.json")"

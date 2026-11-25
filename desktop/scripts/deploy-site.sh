#!/usr/bin/env bash
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# Upload site/ to the littlebrowser.app bucket and clear CloudFront's cache. CI only
# (the `site` job on main), with the release role.
#
#   desktop/scripts/deploy-site.sh
#
# Needs LB_SITE_BUCKET and LB_SITE_DISTRIBUTION (CI/CD variables written by the
# infrastructure repository). LB_DRY_RUN=1 lists what would change and uploads nothing.
set -euo pipefail

SITE="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../site" && pwd)"
: "${LB_SITE_BUCKET:?}" "${LB_SITE_DISTRIBUTION:?}"

DRY=()
[ "${LB_DRY_RUN:-0}" = 1 ] && DRY=(--dryrun)

# Every file in site/ is served (branding/README.md), except dotfiles such as local previews and
# the agent notes (AGENTS.md).
# Short cache lifetime: CloudFront is invalidated below, browsers re-check after 5 minutes.
aws s3 sync "$SITE/" "s3://$LB_SITE_BUCKET/" --delete \
  --exclude '.*' --exclude '*/.*' --exclude 'AGENTS.md' \
  --cache-control 'public, max-age=300' ${DRY[@]+"${DRY[@]}"}

# The AWS CLI guesses content types from file extensions and doesn't know .webmanifest.
aws s3 cp "$SITE/site.webmanifest" "s3://$LB_SITE_BUCKET/site.webmanifest" \
  --content-type application/manifest+json --cache-control 'public, max-age=300' ${DRY[@]+"${DRY[@]}"}

if [ "${LB_DRY_RUN:-0}" = 1 ]; then
  echo "dry run: would invalidate /* on $LB_SITE_DISTRIBUTION"
else
  aws cloudfront create-invalidation --distribution-id "$LB_SITE_DISTRIBUTION" --paths '/*' \
    --query 'Invalidation.[Id,Status]' --output text
fi

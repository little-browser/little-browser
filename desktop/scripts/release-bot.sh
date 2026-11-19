#!/usr/bin/env bash
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# CI release automation. Runs in GitLab CI as the project's bot.
#
#   desktop/scripts/release-bot.sh bump   new upstream release? push bump/<tag> and open a merge request
#   desktop/scripts/release-bot.sh tag    tag the release for the Chromium version in VERSIONS
#
# Needs LB_BOT_TOKEN: a project access token (role Maintainer, scope write_repository). Pushes made
# with CI_JOB_TOKEN can't be used: GitLab doesn't let it push, and its pushes start no pipelines.
# LB_DRY_RUN=1 prints the push instead of running it.
set -euo pipefail

DESKTOP="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

: "${CI_SERVER_HOST:?run in GitLab CI}" "${CI_PROJECT_PATH:?}" "${CI_DEFAULT_BRANCH:?}"
[ "${LB_DRY_RUN:-0}" = 1 ] || : "${LB_BOT_TOKEN:?set the LB_BOT_TOKEN CI/CD variable}"
REMOTE="https://lb-bot:${LB_BOT_TOKEN:-dry-run}@${CI_SERVER_HOST}/${CI_PROJECT_PATH}.git"

git config user.name "${LB_BOT_NAME:-Little Browser bot}"
git config user.email "${LB_BOT_EMAIL:-bot@users.noreply.$CI_SERVER_HOST}"

push() {
  if [ "${LB_DRY_RUN:-0}" = 1 ]; then echo "dry run: git push <remote> $*"; else git push "$REMOTE" "$@"; fi
}

bump() {
  "$DESKTOP/scripts/upstream.sh" --write
  if git diff --quiet -- "$DESKTOP/VERSIONS"; then return 0; fi
  # shellcheck source=../VERSIONS
  source "$DESKTOP/VERSIONS"
  local branch="bump/$UNGOOGLED_MACOS_TAG"
  # The branch exists while its merge request is open; once merged, VERSIONS on main is current.
  if git ls-remote --exit-code --heads "$REMOTE" "refs/heads/$branch" >/dev/null 2>&1; then
    echo "$branch already exists; nothing to do"
    return 0
  fi
  git commit -s -q -m "desktop: Chromium $CHROMIUM_VERSION (ungoogled-chromium $UNGOOGLED_MACOS_TAG)" \
    -m "Opened by the upstream-watch job. Merging it tags the release." -- "$DESKTOP/VERSIONS"
  push "HEAD:refs/heads/$branch" \
    -o merge_request.create \
    -o "merge_request.target=$CI_DEFAULT_BRANCH" \
    -o "merge_request.title=Chromium $CHROMIUM_VERSION" \
    -o merge_request.label=chromium-bump \
    -o merge_request.remove_source_branch
}

# v<CHROMIUM_VERSION>-<revision>: -1 for a new Chromium version, else the next free revision.
tag() {
  # shellcheck source=../VERSIONS
  source "$DESKTOP/VERSIONS"
  local last next
  last="$(git ls-remote --tags --refs "$REMOTE" 2>/dev/null | sed 's#.*refs/tags/##' |
    grep -E "^v${CHROMIUM_VERSION//./\\.}-[0-9]+$" | sed 's/.*-//' | sort -n | tail -1 || true)"
  next="v$CHROMIUM_VERSION-$(( ${last:-0} + 1 ))"
  git tag -a "$next" -m "Little Browser $next (ungoogled-chromium $UNGOOGLED_MACOS_TAG)"
  push "refs/tags/$next"
  echo "tagged $next"
}

case "${1:-}" in
  bump) bump ;;
  tag) tag ;;
  *) echo "usage: release-bot.sh bump|tag" >&2; exit 1 ;;
esac

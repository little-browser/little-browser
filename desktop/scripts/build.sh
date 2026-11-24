#!/usr/bin/env bash
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# Configure and build Little Browser (desktop) from the tree prepared by fetch.sh.
#
#   desktop/scripts/build.sh [--platform <name>] [--dev] [<arch>]   configure + build (re-run for
#                                                                   incremental builds)
#   desktop/scripts/build.sh [--platform <name>] --package          package the release build
#
#   (default)  release build → out/Default   official, ThinLTO: for packaging and measurements
#   --dev      dev build     → out/Dev       component build, no LTO: fast edit-build-run
#
# GN args = ungoogled flags.gn → the platform repo's flags → desktop/args/common.gn →
# desktop/args/<platform>.gn (→ desktop/args/dev.gn with --dev); later files win.
# What differs per platform lives in its entry, desktop/platforms/<name>.sh.
# LB_NINJA_FLAGS="-k 0" keeps going after failures to collect every error in one pass.
# LB_CC_WRAPPER=sccache runs every compile through a content-keyed compiler cache (GN cc_wrapper);
# configure the cache itself with sccache's own SCCACHE_* variables.
set -euo pipefail

DESKTOP="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck source=../VERSIONS
source "$DESKTOP/VERSIONS"
# shellcheck source=lib.sh
source "$DESKTOP/scripts/lib.sh"
UG="$DESKTOP/third_party/ungoogled-chromium"
BUILD="$DESKTOP/build"
SRC="$BUILD/src"

DEV=0
PACKAGE=0
CHOSEN_PLATFORM=""
ARCH=""
while [ $# -gt 0 ]; do
  case "$1" in
    --dev) DEV=1 ;;
    --package) PACKAGE=1 ;;
    --platform) CHOSEN_PLATFORM="${2:?--platform needs a name}"; shift ;;
    -*) die "usage: build.sh [--platform <name>] [--dev] [<arch>] | [--platform <name>] --package" ;;
    *) ARCH="$1" ;;
  esac
  shift
done
load_platform "$DESKTOP/platforms" "${CHOSEN_PLATFORM:-$(host_platform)}"
PU="$DESKTOP/third_party/$PLATFORM_REPO"
if [ "$PACKAGE" = 1 ]; then platform_package; fi

[ -d "$SRC" ] || die "run desktop/scripts/fetch.sh first"
ARCH="${ARCH:-$(uname -m)}"
check_arch "$ARCH"
OUT_REL="out/Default"
ARG_FILES=("$UG/flags.gn" "$PU/$PLATFORM_FLAGS" "$DESKTOP/args/common.gn" "$DESKTOP/args/$PLATFORM.gn")
if [ "$DEV" = 1 ]; then
  OUT_REL="out/Dev"
  ARG_FILES+=("$DESKTOP/args/dev.gn")
fi

write_gn_args "$SRC/$OUT_REL" "$(gn_cpu "$ARCH")" "${ARG_FILES[@]}"
cd "$SRC"
platform_prebuild "$OUT_REL"   # in this shell: what it exports (macOS: SDKROOT) reaches the build
# shellcheck disable=SC2086  # PLATFORM_TARGETS is a list
gn_build "$SRC" "$OUT_REL" "$PLATFORM_GN" "$PLATFORM_NINJA" $PLATFORM_TARGETS
platform_report "$SRC/$OUT_REL"

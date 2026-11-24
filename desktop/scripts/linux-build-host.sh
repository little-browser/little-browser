#!/usr/bin/env bash
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# Prepare a fresh ubuntu:24.04 CI container for a Chromium build on the Linux build runners
# (x86_64: lb-linux-build, lb-linux-release; arm64 on Graviton: lb-linux-arm64-build,
# lb-linux-arm64-release): the tools the fetch scripts need, and sccache. Every Linux-hosted
# Chromium job (Android now, Linux desktop later) runs this first, as root.
#
#   desktop/scripts/linux-build-host.sh
#
# Chromium's own build dependencies come later, from the fetched tree's
# build/install-build-deps.sh, which knows what that Chromium version needs.
#
# sccache keeps its cache in S3 (SCCACHE_BUCKET, SCCACHE_REGION, SCCACHE_S3_KEY_PREFIX, set by the
# CI job), with the host's instance role: merge-request builds write sccache/mr/, protected-ref
# builds sccache/release/ (docs/design.md §14).
set -euo pipefail

# The same version as the macOS and Windows build images (orinta-build's packer templates).
SCCACHE_VERSION=0.19.0

[ "$(id -u)" = 0 ] || { echo "error: run as root (a CI container)" >&2; exit 1; }
case "$(uname -m)" in
  x86_64) arch=x86_64 sha256=aa73c7270c997894038c0d8f0d297865f57569e1687883322bc431713d4bb9dd ;;
  aarch64) arch=aarch64 sha256=3e47fc4a71b1ddd1b4294a0de9d34498922fdf41e3cd0bda5df3954e3d6deb9d ;;
  *) echo "error: no build host setup for $(uname -m)" >&2; exit 1 ;;
esac

export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
apt-get install -y -qq --no-install-recommends \
  ca-certificates curl file git lsb-release patch python3 sudo xz-utils

name="sccache-v$SCCACHE_VERSION-$arch-unknown-linux-musl"
tmp="$(mktemp -d)"
curl -fsSL -o "$tmp/sccache.tar.gz" \
  "https://github.com/mozilla/sccache/releases/download/v$SCCACHE_VERSION/$name.tar.gz"
echo "$sha256  $tmp/sccache.tar.gz" | sha256sum -c --quiet -
tar -xzf "$tmp/sccache.tar.gz" -C "$tmp"
install -m 0755 "$tmp/$name/sccache" /usr/local/bin/sccache
rm -rf "${tmp:?}"
sccache --version

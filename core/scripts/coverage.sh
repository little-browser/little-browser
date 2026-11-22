#!/bin/sh
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# Line coverage of lb-core's tests (CI's core job runs it). Prints llvm-cov's report and fails
# when the total is below LB_MIN_LINE_COVERAGE percent (default 95).
#
# Needs llvm-profdata and llvm-cov from the same LLVM major version as rustc: rustup's llvm-tools
# component (`rustup component add llvm-tools`), or LB_LLVM_BIN set to a directory holding them,
# such as desktop/build/src/third_party/llvm-build/Release+Asserts/bin.
set -eu

MIN=${LB_MIN_LINE_COVERAGE:-95}
cd "$(dirname "$0")/.."
if [ -z "${LB_LLVM_BIN:-}" ]; then
  LB_LLVM_BIN="$(rustc --print sysroot)/lib/rustlib/$(rustc -vV | sed -n 's/^host: //p')/bin"
fi

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
# Its own target directory: instrumented builds would otherwise replace the normal ones.
export RUSTFLAGS="-C instrument-coverage"
export CARGO_TARGET_DIR="$WORK/target"
export LLVM_PROFILE_FILE="$WORK/prof/%p-%m.profraw"

cargo test --quiet
"$LB_LLVM_BIN/llvm-profdata" merge -sparse "$WORK"/prof/*.profraw -o "$WORK/tests.profdata"

# The test binaries cargo just built.
set --
for bin in $(cargo test --no-run --quiet --message-format=json |
  sed -n 's/.*"executable":"\([^"]*\)".*/\1/p'); do
  set -- "$@" --object "$bin"
done

# Only lb-core's own lines count: dependencies' sources live under $CARGO_HOME/registry
# (~/.cargo locally, /usr/local/cargo in CI's rust image) and the toolchain's under rustup.
"$LB_LLVM_BIN/llvm-cov" report --instr-profile="$WORK/tests.profdata" "$@" \
  --ignore-filename-regex='(/rustc/|cargo/registry/|rustup/)' | tee "$WORK/report.txt"

# The TOTAL row's tenth column is line coverage, such as "98.35%".
lines=$(awk '$1 == "TOTAL" { sub(/%/, "", $10); print $10 }' "$WORK/report.txt")
if [ -z "$lines" ]; then
  echo "coverage: no TOTAL row in llvm-cov's report" >&2
  exit 1
fi
if awk -v got="$lines" -v min="$MIN" 'BEGIN { exit !(got + 0 < min + 0) }'; then
  echo "coverage: lines ${lines}% is below the ${MIN}% minimum" >&2
  exit 1
fi
echo "coverage: lines ${lines}% (minimum ${MIN}%)"

# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# Helpers shared by the desktop and Android fetch and build scripts. Source it; don't run it.
# Tested by chromium/scripts/test_lib_sh.py.
# shellcheck shell=bash

log() { printf '\n\033[1m==> %s\033[0m\n' "$*"; }
die() { printf 'error: %s\n' "$*" >&2; exit 1; }

# branding_value <key>: a value from the product's branding, chromium/branding/BRANDING (Chromium's
# own format, which each fetch script also copies into its tree), such as PRODUCT_FULLNAME.
branding_value() {
  local file
  file="$(cd "$(dirname "${BASH_SOURCE[0]}")/../branding" && pwd)/BRANDING"
  [ -r "$file" ] || die "can't read $file"
  sed -n "s/^$1=//p" "$file"
}

# Strip comments and blank lines from a list file.
clean_list() { sed -e 's/[[:space:]]*#.*$//' -e '/^[[:space:]]*$/d' "$1"; }

# series_for <series file> <platform>: the patch paths to apply on <platform>. A line whose
# comment starts with a platform tag, as in `lb/x.patch  # [mac] reason`, is for that platform
# only, and `[desktop]` for every desktop platform; every other line is for all of them. A series
# file that can't be read is an error.
series_for() {
  [ -r "$1" ] || die "can't read the patch series $1"
  awk -v want="$2" '
    { line = $0; sub(/^[[:space:]]+/, "", line) }
    line == "" || substr(line, 1, 1) == "#" { next }
    {
      path = line; sub(/[[:space:]]*#.*$/, "", path)
      tag = ""
      if (match(line, /#[[:space:]]*\[[A-Za-z0-9_-]+\]/)) {
        tag = substr(line, RSTART, RLENGTH); sub(/^#[[:space:]]*\[/, "", tag); sub(/\]$/, "", tag)
      }
      if (tag == "" || tag == want || (tag == "desktop" && want != "android")) print path
    }' "$1"
}

# series_paths <patches dir> <platform>: series_for of <patches dir>/series, each path prefixed
# with <patches dir>/, one per line (any characters in the directory are kept as they are).
series_paths() {
  local dir="$1" p list
  list="$(series_for "$dir/series" "$2")" || return
  while IFS= read -r p; do
    if [ -n "$p" ]; then printf '%s/%s\n' "$dir" "$p"; fi
  done <<< "$list"
}

# version_pins <VERSIONS file>: its KEY=value lines, sorted, without the comments. Two files with
# the same pins build the same release.
version_pins() { grep -E '^[A-Za-z_][A-Za-z0-9_]*=' "$1" | sort; }

# check_tree_links <src> <repo>: the Chromium tree links our code from this checkout (src/lb →
# chromium/lb, src/lb_core → core/lb-core). A tree whose links point elsewhere was fetched for
# another checkout or another layout, and its patches may not be this checkout's: fetch it again.
check_tree_links() {
  local src="$1" repo="$2" link target
  for link in lb:chromium/lb lb_core:core/lb-core; do
    target="$(cd "$repo/${link#*:}" && pwd -P)"
    [ "$(cd "$src/${link%%:*}" 2>/dev/null && pwd -P)" = "$target" ] ||
      die "$src/${link%%:*} doesn't link to $target, so the tree wasn't fetched for this checkout. Delete $src and fetch it again."
  done
}

# clone_at <repo url> <tag> <dir>: shallow clone, or verify an existing clone is at <tag>.
clone_at() {
  if [ ! -d "$3/.git" ]; then
    git -c advice.detachedHead=false clone --depth 1 --branch "$2" "$1" "$3"
  elif [ "$(git -C "$3" rev-parse HEAD)" != "$(git -C "$3" rev-parse "refs/tags/$2^{commit}" 2>/dev/null)" ]; then
    die "$3 is not at tag $2 — delete it and re-run."
  fi
}

# Architectures are arm64 and x64 everywhere: script arguments, platform entries, folders, GN's
# target_cpu, file names and releases.json. A tool that names them otherwise is translated where
# its output is read (arch_from_machine) or where it is called (macOS: mac_arch).

# arch_from_machine <name>: our name for an architecture as uname -m or lipo names it.
arch_from_machine() {
  case "$1" in
    arm64|aarch64) echo arm64 ;;
    x86_64) echo x64 ;;
    *) die "unknown machine architecture $1" ;;
  esac
}

# check_arch_name <arch>: stop unless it is arm64 or x64.
check_arch_name() {
  case "$1" in
    arm64|x64) ;;
    *) die "an architecture is arm64 or x64, not ${1:-nothing}" ;;
  esac
}

# host_arch: this machine's architecture. A shell running under Rosetta reports an Intel chip on
# an Apple silicon Mac, which would fetch and build for the wrong host: stop.
host_arch() {
  [ "$(sysctl -n sysctl.proc_translated 2>/dev/null || true)" != 1 ] ||
    die "this shell runs under Rosetta on Apple silicon; use a native one, and build.sh x64 for Intel"
  arch_from_machine "$(uname -m)"
}

# out_dir <kind> <arch> <host arch>: the output folder, relative to the source tree, for a build of
# <kind> (Default: release, Dev) for <arch> on a machine of <host arch>. The host's own
# architecture keeps the plain name (out/Default); another gets its own folder (out/Default-x64),
# so building one never replaces the other's build.
out_dir() {
  check_arch_name "$2"
  check_arch_name "$3"
  if [ "$2" = "$3" ]; then echo "out/$1"; else echo "out/$1-$2"; fi
}

# merge_gn_args <arg file>...: print the merged "key = value" lines; later files override earlier ones.
merge_gn_args() {
  python3 - "$@" <<'PY'
import sys
args = {}
for path in sys.argv[1:]:
    for line in open(path):
        line = line.split('#', 1)[0].strip()
        if '=' in line:
            k, v = (s.strip() for s in line.split('=', 1))
            args[k] = v
print("\n".join(f"{k} = {v}" for k, v in args.items()))
PY
}

# export_instance_credentials: on a CI build host, the instance role's temporary credentials,
# exported as AWS_ACCESS_KEY_ID, AWS_SECRET_ACCESS_KEY and AWS_SESSION_TOKEN for sccache's S3
# cache. sccache's own lookup of them (IMDSv2, from the job's container) fails now and then,
# and then every compile fails; fetched once here, they are in the environment every sccache
# server it starts inherits. Nothing is printed. Credentials already set, or no metadata service
# (a developer's machine), leave everything as it is.
export_instance_credentials() {
  [ -z "${AWS_ACCESS_KEY_ID:-}" ] || return 0
  command -v curl >/dev/null || return 0
  local imds="${LB_IMDS_URL:-http://169.254.169.254}" token role json key secret session
  token="$(curl -fsS -m 3 -X PUT "$imds/latest/api/token" \
    -H 'X-aws-ec2-metadata-token-ttl-seconds: 21600' 2>/dev/null)" || return 0
  role="$(curl -fsS -m 3 -H "X-aws-ec2-metadata-token: $token" \
    "$imds/latest/meta-data/iam/security-credentials/" 2>/dev/null)" || return 0
  [ -n "$role" ] || return 0
  json="$(curl -fsS -m 3 -H "X-aws-ec2-metadata-token: $token" \
    "$imds/latest/meta-data/iam/security-credentials/$role" 2>/dev/null)" || return 0
  { read -r key; read -r secret; read -r session; } < <(python3 -c '
import json, sys
c = json.load(sys.stdin)
print(c["AccessKeyId"]); print(c["SecretAccessKey"]); print(c["Token"])' <<<"$json" 2>/dev/null) ||
    return 0
  [ -n "$key" ] && [ -n "$secret" ] && [ -n "$session" ] || return 0
  export AWS_ACCESS_KEY_ID="$key" AWS_SECRET_ACCESS_KEY="$secret" AWS_SESSION_TOKEN="$session"
}

# write_gn_args <out dir> <target cpu> <arg file>...: <out dir>/args.gn from the merged files,
# then target_cpu (the only one: an arg file's own is dropped), then cc_wrapper when LB_CC_WRAPPER
# names a compiler cache (sccache). With sccache, Clang modules are off too: sccache caches no
# compile that uses them (-fmodules, and their -Xclang flags, which C files get as well), and
# Chromium turns them on for Android. An sccache that can't start (its cache unreachable: every
# compile would fail) is left out, with a warning: the build is slower, not broken. A folder that
# already holds a build for another target_cpu is left alone: rewriting it would throw that build
# away.
write_gn_args() {
  local out="$1" cpu="$2" had
  shift 2
  mkdir -p "$out"
  had=""
  [ ! -f "$out/args.gn" ] || had="$(sed -n 's/^target_cpu = "\(.*\)"$/\1/p' "$out/args.gn" | tail -1)"
  [ -z "$had" ] || [ "$had" = "$cpu" ] ||
    die "$out holds an $had build, not $cpu: move or delete it to build $cpu there"
  merge_gn_args "$@" | { grep -v '^target_cpu = ' || true; } > "$out/args.gn"
  echo "target_cpu = \"$cpu\"" >> "$out/args.gn"
  if [ -n "${LB_CC_WRAPPER:-}" ]; then
    command -v "$LB_CC_WRAPPER" >/dev/null || die "LB_CC_WRAPPER=$LB_CC_WRAPPER not found in PATH"
    # An S3 cache signs with the host's role (export_instance_credentials); --show-stats starts
    # the server, or reaches the one already running.
    if [ "$LB_CC_WRAPPER" = sccache ] && [ -n "${SCCACHE_BUCKET:-}" ]; then
      export_instance_credentials
    fi
    if [ "$LB_CC_WRAPPER" = sccache ] && ! sccache --show-stats >/dev/null; then
      printf 'warning: sccache could not start (above); building without the compiler cache\n' >&2
      return 0
    fi
    echo "cc_wrapper = \"$LB_CC_WRAPPER\"" >> "$out/args.gn"
    if [ "$LB_CC_WRAPPER" = sccache ]; then echo "use_clang_modules = false" >> "$out/args.gn"; fi
  fi
}

# run_ninja <ninja command>...: in CI, one line per step (~58,000 for a desktop build) overruns
# GitLab's 4 MiB job-log limit, after which the log stops: keep every 500th progress line and
# everything else (warnings, errors) in full.
run_ninja() {
  if [ -z "${CI:-}" ]; then "$@"; return; fi
  "$@" | awk '
    /^\[[0-9]+\/[0-9]+\] / { split(substr($1, 2), p, "/"); if (p[1] % 500 && p[1] != p[2] + 0) next }
    { print; fflush() }'
}

# gn_build <src> <out dir, relative to src> <gn> <ninja> <target>...: gn gen (every arg must be
# one the tree knows), then ninja, with LB_NINJA_FLAGS (e.g. "-k 0" to collect every error in
# one pass). <gn> and <ninja> are commands, relative to <src> or on PATH.
gn_build() {
  local src="$1" out_rel="$2" gn="$3" ninja="$4"
  shift 4
  echo "args.gn ($out_rel):"; sed 's/^/  /' "$src/$out_rel/args.gn"
  (
    cd "$src" || exit
    "$gn" gen "$out_rel" --fail-on-unused-args
    # shellcheck disable=SC2086
    run_ninja "$ninja" ${LB_NINJA_FLAGS:-} -C "$out_rel" "$@"
  )
  if grep -q '^cc_wrapper = "sccache"' "$src/$out_rel/args.gn"; then sccache --show-stats; fi
}

# Desktop platform entries ------------------------------------------------------------------------
# Each desktop platform is one file, desktop/platforms/<name>.sh, that sets these variables and
# defines these functions (desktop/platforms/README.md says what each one is for).

LB_PLATFORM_VARS=(PLATFORM_LABEL PLATFORM_HOST PLATFORM_ARCHS PLATFORM_REPO PLATFORM_REPO_URL
  PLATFORM_TAG PLATFORM_FLAGS PLATFORM_GN PLATFORM_NINJA PLATFORM_TARGETS)
LB_PLATFORM_HOOKS=(platform_fetch_source platform_toolchains platform_branding platform_prebuild
  platform_report platform_package)

# host_platform: the desktop platform this machine builds by default.
host_platform() {
  case "$(uname -s)" in
    Darwin) echo mac ;;
    Linux) echo linux ;;
    MINGW* | MSYS* | CYGWIN*) echo win ;;
    *) die "no desktop platform for $(uname -s)" ;;
  esac
}

# load_platform <platforms dir> <name>: source the entry and check it is complete.
load_platform() {
  local dir="$1" name="$2" v f
  if [ ! -f "$dir/$name.sh" ]; then
    local have=() p
    for p in "$dir"/*.sh; do have+=("$(basename "$p" .sh)"); done
    die "no desktop platform \"$name\" (have: ${have[*]})"
  fi
  # shellcheck source=/dev/null
  source "$dir/$name.sh"
  for v in "${LB_PLATFORM_VARS[@]}"; do
    [ -n "${!v:-}" ] || die "$dir/$name.sh doesn't set $v"
  done
  for f in "${LB_PLATFORM_HOOKS[@]}"; do
    [ "$(type -t "$f")" = function ] || die "$dir/$name.sh doesn't define $f"
  done
  # shellcheck disable=SC2034  # read by the scripts that source this file
  PLATFORM="$name"
}

# check_arch <arch>: the arch is one the loaded platform builds.
check_arch() {
  case " $PLATFORM_ARCHS " in
    *" $1 "*) ;;
    *) die "$PLATFORM_LABEL builds $PLATFORM_ARCHS, not $1" ;;
  esac
}

#!/usr/bin/env bash
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# Sign the release build's Little Browser.app and package it as a disk image,
# desktop/build/LittleBrowser-macos-<arch>.dmg: the app next to an Applications link.
#
#   desktop/scripts/package-macos.sh [--arch <arch>] [<version>]           ad-hoc signature (local
#                                                                          testing)
#   desktop/scripts/package-macos.sh [--arch <arch>] --release <version>   Developer ID signature,
#                                                                          notarised and stapled (CI)
#
# --arch picks the build: this Mac's architecture (out/Default) unless it names another
# (out/Default-x64, build.sh's folder for it).
#
# --release reads the signing material from AWS Secrets Manager (little-browser/signing/
# apple-developer-id and apple-notarization), which only the release builder can read. While the
# certificate secret is empty it falls back to an ad-hoc signature, and without notarisation
# credentials it signs but doesn't notarise; it says so either way.
#
# <version> is the release, e.g. 157.0.8103.78-1. It goes into the app's CFBundleVersion as
# <Chromium build>.<patch>.<revision>, which the updater compares, and turns on the updater:
# Sparkle (pinned below) in Contents/Frameworks, the feed and public key in Info.plist, and
# LittleBrowser-macos-<arch>.update.json next to the disk image with the disk image's EdDSA
# signature, which publish-release.sh puts in the feed. <arch> is arm64 or x64, the names
# releases.json uses, and each architecture's app reads its own feed, updates/macos-<arch>.xml
# (update_feed.py), so it is only ever offered builds for its own chip. The update key ({"private_key",
# "public_key"} from Sparkle's generate_keys) is the little-browser/signing/sparkle-ed25519
# secret with --release, or the JSON file LB_UPDATE_KEY_FILE names otherwise; without one the
# app has no updater, with a warning. LB_UPDATE_FEED_URL replaces the feed's address, for
# testing an update locally.
#
# Signing order and options follow ungoogled-chromium-macos's sign_and_package_app.sh: nested
# code first (libraries, tools, helpers), then the framework, then the app, with the hardened
# runtime and the entitlements Chromium's helpers and app need. Identifiers are the bundles' own
# CFBundleIdentifiers.
set -euo pipefail

DESKTOP="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ENT="$DESKTOP/third_party/ungoogled-chromium-macos/entitlements"
REGION="${LB_AWS_REGION:-ap-southeast-2}"
# Developer ID Certification Authority (G2), valid until 2031-09-17.
APPLE_DEVID_CA_URL=https://www.apple.com/certificateauthority/DeveloperIDG2CA.cer
APPLE_DEVID_CA_SHA256=f16cd3c54c7f83cea4bf1a3e6a0819c8aaa8e4a1528fd144715f350643d2df3a
# The updater (MIT licence): Sparkle's own release build, pinned by checksum.
SPARKLE_VERSION=2.10.1
SPARKLE_SHA256=1a856d2a9edb8e628de88121d7be04cf0e2167f8641650b78fca00b14244cf27
SPARKLE_URL="https://github.com/sparkle-project/Sparkle/releases/download/$SPARKLE_VERSION/Sparkle-$SPARKLE_VERSION.tar.xz"
# Hours between update checks while the browser runs (docs/design.md §7.13).
UPDATE_CHECK_SECONDS=14400

# shellcheck source=lib.sh
source "$DESKTOP/scripts/lib.sh"   # die, host_arch, out_dir, arch_from_machine
warn() { echo "warning: $*" >&2; }
usage() { die "usage: package-macos.sh [--arch <arch>] [--release] [<version>] (--release needs the version)"; }

RELEASE=0
VERSION=""
HOST="$(host_arch)"
ARCH="$HOST"
ARGS=("$@")   # for the re-run in the user's session below
while [ $# -gt 0 ]; do
  case "$1" in
    --release) RELEASE=1 ;;
    --arch) [ $# -gt 1 ] || usage; ARCH="$2"; shift ;;
    -*) usage ;;
    *) [ -z "$VERSION" ] || usage; VERSION="$1" ;;
  esac
  shift
done
[ "$RELEASE" = 0 ] || [ -n "$VERSION" ] || usage
APP="$DESKTOP/build/src/$(out_dir Default "$ARCH" "$HOST")/Little Browser.app"
FW="$APP/Contents/Frameworks/Little Browser Framework.framework"
HELPERS="$FW/Helpers"
[ -d "$APP" ] || die "no release build at $APP"
[ "$(arch_from_machine "$(lipo -archs "$APP/Contents/MacOS/Little Browser")")" = "$ARCH" ] ||
  die "$APP is built for $(lipo -archs "$APP/Contents/MacOS/Little Browser"), not $ARCH"
UPDATE_FEED_URL="${LB_UPDATE_FEED_URL:-https://downloads.littlebrowser.app/updates/macos-$ARCH.xml}"
# shellcheck source=../VERSIONS
source "$DESKTOP/VERSIONS"
if [ -n "$VERSION" ]; then
  [[ "$VERSION" =~ ^[0-9]+(\.[0-9]+){3}-([0-9]+)$ ]] || die "version $VERSION is not <chromium version>-<revision>"
  [ "${VERSION%-*}" = "$CHROMIUM_VERSION" ] ||
    die "version $VERSION is not on this tree's Chromium version, $CHROMIUM_VERSION"
fi

# Keychain identities only count as valid in a user's own launchd session. The GitLab runner is a
# LaunchDaemon, so CI jobs run in the System session, where codesign finds "no identity". Re-run
# in the user's session (ec2-user has passwordless sudo on EC2 Macs).
if [ "$RELEASE" = 1 ] && [ "$(launchctl managername)" = System ] && [ -z "${LB_USER_SESSION:-}" ]; then
  exec sudo -n launchctl asuser "$(id -u)" sudo -n -u "$(id -un)" env LB_USER_SESSION=1 \
    PATH="$PATH" HOME="$HOME" LB_AWS_REGION="$REGION" ${AWS_PROFILE:+AWS_PROFILE="$AWS_PROFILE"} \
    "$0" ${ARGS[@]+"${ARGS[@]}"}
fi
[ -d "$ENT" ] || die "no entitlements in $ENT: run desktop/scripts/fetch.sh first"

IDENTITY=-   # ad-hoc
IDENTITY_NAME=""
TEAM_ID=""
KEYCHAIN=""
NOTARY=()
ORIGINAL_KEYCHAINS=()
TMP="$(mktemp -d)"
# Exits with the script's own status, so nothing in the trap can turn a failure into success.
cleanup() {
  local rc=$?
  if [ -n "$KEYCHAIN" ]; then
    # An empty list can't be set back, and an unchanged one keeps pointing at the deleted keychain.
    if [ "${#ORIGINAL_KEYCHAINS[@]}" -gt 0 ]; then
      security list-keychains -d user -s "${ORIGINAL_KEYCHAINS[@]}" || true
    else
      security list-keychains -d user -s /Library/Keychains/System.keychain || true
    fi
    security delete-keychain "$KEYCHAIN" 2>/dev/null || true
  fi
  rm -rf "${TMP:?}"
  exit "$rc"
}
trap cleanup EXIT

# secret <name>: the secret's value, or nothing if it has none yet. Any other failure (no aws CLI,
# no credentials, no permission) stops the run: a release must never fall back to ad-hoc silently.
secret() {
  local out
  command -v aws >/dev/null || die "--release needs the aws CLI on PATH"
  if out="$(aws --region "$REGION" secretsmanager get-secret-value \
      --secret-id "little-browser/signing/$1" --query SecretString --output text 2>&1)"; then
    printf '%s' "$out"
    return
  fi
  case "$out" in *ResourceNotFoundException*) return ;; esac
  die "can't read little-browser/signing/$1: $out"
}
field() { jq -r --arg k "$2" '.[$k] // empty' <<<"$1" 2>/dev/null || true; }

# --------------------------------------------------------------- signing identity (--release)
if [ "$RELEASE" = 1 ]; then
  id_json="$(secret apple-developer-id)"
  if [ -z "$(field "$id_json" p12_base64)" ]; then
    warn "no Developer ID certificate in Secrets Manager yet: ad-hoc signature, no notarisation"
  else
    # A throwaway keychain holds the identity for this run only; the trap deletes it.
    KEYCHAIN="$TMP/signing.keychain-db"
    kc_password="$(openssl rand -hex 24)"
    # The search list as it is now, before anything can fail: the trap puts it back.
    while IFS= read -r line; do
      line="${line#"${line%%[![:space:]]*}"}"
      ORIGINAL_KEYCHAINS+=("${line//\"/}")
    done < <(security list-keychains -d user)
    security create-keychain -p "$kc_password" "$KEYCHAIN"
    # No auto-lock: notarisation can keep Apple's queue busy for hours before the disk image is signed.
    security set-keychain-settings "$KEYCHAIN"
    security unlock-keychain -p "$kc_password" "$KEYCHAIN"
    field "$id_json" p12_base64 | base64 -d > "$TMP/identity.p12"
    security import "$TMP/identity.p12" -k "$KEYCHAIN" -f pkcs12 \
      -P "$(field "$id_json" password)" -T /usr/bin/codesign >/dev/null
    rm -f "$TMP/identity.p12"
    security set-key-partition-list -S apple-tool:,apple: -s -k "$kc_password" "$KEYCHAIN" >/dev/null
    # Apple's Developer ID intermediate isn't on a fresh Mac, and codesign needs it to build the
    # chain to Apple's root. Public, pinned by fingerprint.
    curl -fsSL --retry 3 -o "$TMP/DeveloperIDG2CA.cer" "$APPLE_DEVID_CA_URL"
    [ "$(shasum -a 256 "$TMP/DeveloperIDG2CA.cer" | cut -d' ' -f1)" = "$APPLE_DEVID_CA_SHA256" ] ||
      die "Apple's Developer ID intermediate certificate doesn't match its pinned fingerprint"
    # macOS refuses the import when one of the user's keychains already has it, which is fine.
    if ! security import "$TMP/DeveloperIDG2CA.cer" -k "$KEYCHAIN" >/dev/null 2>"$TMP/import.err"; then
      grep -q 'already exists' "$TMP/import.err" || die "importing Apple's intermediate: $(cat "$TMP/import.err")"
    fi
    # The CI user has no login keychain, so the list can be empty (and macOS's bash 3.2 treats an
    # empty array as unset under `set -u`).
    security list-keychains -d user -s "$KEYCHAIN" ${ORIGINAL_KEYCHAINS[@]+"${ORIGINAL_KEYCHAINS[@]}"}
    identity_line="$(security find-identity -v -p codesigning "$KEYCHAIN" |
      grep -m 1 '"Developer ID Application: ' || true)"
    [ -n "$identity_line" ] || die "the certificate in Secrets Manager is not a Developer ID Application identity"
    IDENTITY="$(awk '{print $2}' <<<"$identity_line")"   # SHA-1 fingerprint
    IDENTITY_NAME="${identity_line#*\"}"
    IDENTITY_NAME="${IDENTITY_NAME%\"}"
    # "Developer ID Application: <name> (<Team ID>)": the team the designated requirement pins.
    [[ "$IDENTITY_NAME" =~ \(([A-Z0-9]{10})\)$ ]] ||
      die "no Team ID in the signing identity's name: $IDENTITY_NAME"
    TEAM_ID="${BASH_REMATCH[1]}"
    echo "signing as: $IDENTITY_NAME ($IDENTITY)"

    # An App Store Connect API key: notarytool reads it from a file, so no secret ever appears in
    # a command line (which any process on the machine can list).
    notary_json="$(secret apple-notarization)"
    if [ -n "$(field "$notary_json" key_p8)" ]; then
      (umask 077; field "$notary_json" key_p8 > "$TMP/notary-key.p8")
      NOTARY=(--key "$TMP/notary-key.p8" --key-id "$(field "$notary_json" key_id)"
              --issuer "$(field "$notary_json" issuer_id)")
    else
      warn "no notarisation key in Secrets Manager yet: signed but not notarised"
    fi
  fi
fi

# --------------------------------------------------------------- update key (with a version)
UPDATE_KEY_JSON=""
if [ -n "$VERSION" ]; then
  if [ "$RELEASE" = 1 ]; then
    UPDATE_KEY_JSON="$(secret sparkle-ed25519)"
  elif [ -n "${LB_UPDATE_KEY_FILE:-}" ]; then
    UPDATE_KEY_JSON="$(cat "$LB_UPDATE_KEY_FILE")"
  fi
  if [ -z "$(field "$UPDATE_KEY_JSON" private_key)" ] || [ -z "$(field "$UPDATE_KEY_JSON" public_key)" ]; then
    warn "no update signing key: the app has no updater"
    UPDATE_KEY_JSON=""
  fi
fi

# --------------------------------------------------------------- updater
PLIST="$APP/Contents/Info.plist"
SPARKLE="$APP/Contents/Frameworks/Sparkle.framework"
SPARKLE_KEYS=(SUFeedURL SUPublicEDKey SUEnableAutomaticChecks SUScheduledCheckInterval
              SUAllowsAutomaticUpdates SUAutomaticallyUpdate SUShowReleaseNotes
              SUVerifyUpdateBeforeExtraction)
# A clean slate: an earlier run may have added the updater to this build.
rm -rf "$SPARKLE"
for key in "${SPARKLE_KEYS[@]}"; do
  plutil -remove "$key" "$PLIST" 2>/dev/null || true
done
IFS=. read -r _ _ build patch <<<"$CHROMIUM_VERSION"
BUNDLE_VERSION="$build.$patch${VERSION:+.${VERSION##*-}}"
plutil -replace CFBundleVersion -string "$BUNDLE_VERSION" "$PLIST"

if [ -n "$UPDATE_KEY_JSON" ]; then
  tarball="$DESKTOP/build/download_cache/Sparkle-$SPARKLE_VERSION.tar.xz"
  sha() { shasum -a 256 "$1" | cut -d' ' -f1; }
  if [ ! -f "$tarball" ] || [ "$(sha "$tarball")" != "$SPARKLE_SHA256" ]; then
    mkdir -p "$(dirname "$tarball")"
    curl -fsSL --retry 3 -o "$tarball.part" "$SPARKLE_URL"
    mv "$tarball.part" "$tarball"
  fi
  [ "$(sha "$tarball")" = "$SPARKLE_SHA256" ] ||
    die "Sparkle $SPARKLE_VERSION doesn't match its pinned checksum"
  mkdir "$TMP/sparkle"
  tar -xJf "$tarball" -C "$TMP/sparkle"
  ditto "$TMP/sparkle/Sparkle.framework" "$SPARKLE"
  # Its XPC services are only for sandboxed apps, which the browser isn't.
  rm -rf "$SPARKLE/XPCServices" "$SPARKLE/Versions/B/XPCServices"
  # The browser loads Sparkle when these are set (desktop/lb/browser/update/). Checks run on a
  # schedule without asking first; an update is offered, never installed on its own; no release
  # notes, so the prompt stays small; the download's signature is checked before it is opened.
  plutil -replace SUFeedURL -string "$UPDATE_FEED_URL" "$PLIST"
  plutil -replace SUPublicEDKey -string "$(field "$UPDATE_KEY_JSON" public_key)" "$PLIST"
  plutil -replace SUEnableAutomaticChecks -bool YES "$PLIST"
  plutil -replace SUScheduledCheckInterval -integer "$UPDATE_CHECK_SECONDS" "$PLIST"
  plutil -replace SUAllowsAutomaticUpdates -bool NO "$PLIST"
  plutil -replace SUAutomaticallyUpdate -bool NO "$PLIST"
  plutil -replace SUShowReleaseNotes -bool NO "$PLIST"
  plutil -replace SUVerifyUpdateBeforeExtraction -bool YES "$PLIST"
  echo "updater: Sparkle $SPARKLE_VERSION, version $BUNDLE_VERSION, feed $UPDATE_FEED_URL"
fi

# --------------------------------------------------------------- sign
bundle_id() { /usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$1"; }

# sign <identifier> <options or ""> <entitlements file or ""> <path>
# Ad-hoc signatures get no options: the hardened runtime turns on library validation, which
# needs a Team ID (ad-hoc code has none, so the app can't load its own framework), and the
# helpers' code-signing flags stop the GPU process. Notarisation, which needs the hardened
# runtime, only happens with a Developer ID.
sign() {
  local args=(--sign "$IDENTITY" --force --identifier "$1") options="$2"
  [ "$IDENTITY" != - ] || options=""
  [ -z "$options" ] || args+=(--options "$options")
  [ -z "$3" ] || args+=(--entitlements "$3")
  if [ "$IDENTITY" != - ]; then
    args+=(--timestamp --keychain "$KEYCHAIN")
  fi
  codesign "${args[@]}" "$4"
}

APP_ID="$(bundle_id "$APP/Contents/Info.plist")"
xattr -cr "$APP"

for lib in "$FW/Libraries"/*.dylib; do
  [ -e "$lib" ] || continue
  sign "$(basename "$lib" .dylib)" "" "" "$lib"
done
for tool in chrome_crashpad_handler app_mode_loader web_app_shortcut_copier; do
  [ -e "$HELPERS/$tool" ] && sign "$tool" restrict,library,runtime,kill "" "$HELPERS/$tool"
done
for helper in "$HELPERS"/*.app; do
  id="$(bundle_id "$helper/Contents/Info.plist")"
  case "$(basename "$helper" .app)" in
    *"Renderer)") sign "$id" restrict,kill,runtime "$ENT/helper-renderer-entitlements.plist" "$helper" ;;
    *"GPU)")      sign "$id" restrict,kill,runtime "$ENT/helper-gpu-entitlements.plist" "$helper" ;;
    *"Plugin)")   sign "$id" restrict,kill,runtime "$ENT/helper-plugin-entitlements.plist" "$helper" ;;
    *)            sign "$id" restrict,library,runtime,kill "" "$helper" ;;
  esac
done
sign "$(bundle_id "$FW/Resources/Info.plist")" "" "" "$FW"
if [ -d "$SPARKLE" ]; then
  sign org.sparkle-project.Sparkle.Autoupdate runtime "" "$SPARKLE/Versions/B/Autoupdate"
  sign "$(bundle_id "$SPARKLE/Versions/B/Updater.app/Contents/Info.plist")" runtime "" \
    "$SPARKLE/Versions/B/Updater.app"
  sign org.sparkle-project.Sparkle "" "" "$SPARKLE"
fi
if [ "$IDENTITY" = - ]; then
  sign "$APP_ID" restrict,library,runtime,kill "$ENT/app-entitlements.plist" "$APP"
else
  # The designated requirement pins the app to our team's Developer ID certificate, as Chrome's
  # does. macOS trusts whatever meets it with the app's keychain items, such as its Safe Storage
  # password, so without the team any Developer ID could sign an app that passes for ours.
  team_requirement="certificate leaf[subject.OU] = \"$TEAM_ID\""
  codesign --sign "$IDENTITY" --force --timestamp --keychain "$KEYCHAIN" --identifier "$APP_ID" \
    --options restrict,library,runtime,kill --entitlements "$ENT/app-entitlements.plist" \
    --requirements "=designated => identifier \"$APP_ID\" and anchor apple generic and certificate 1[field.1.2.840.113635.100.6.2.6] /* exists */ and certificate leaf[field.1.2.840.113635.100.6.1.13] /* exists */ and $team_requirement" \
    "$APP"
  # codesign prints the team without its quotes.
  codesign -d -r- "$APP" 2>&1 | grep -qE "certificate leaf\[subject\.OU\] = \"?$TEAM_ID\"?( |$)" ||
    die "the app's designated requirement doesn't pin team $TEAM_ID"
  codesign --verify -R="$team_requirement" "$APP" || die "the app isn't signed by team $TEAM_ID"
fi
codesign --verify --deep --strict "$APP"

# --------------------------------------------------------------- notarise
# notarise <file>: submit to Apple's notary service, print its log, staple the ticket.
notarise() {
  local result="$TMP/notary.json" status id
  xcrun notarytool submit "$1" --wait --output-format json "${NOTARY[@]}" > "$result" || true
  status="$(jq -r '.status // empty' "$result")"
  id="$(jq -r '.id // empty' "$result")"
  echo "notarisation of $(basename "$1"): ${status:-no answer} (${id:-no id})"
  if [ -n "$id" ]; then
    xcrun notarytool log "$id" "${NOTARY[@]}" || true
  fi
  [ "$status" = Accepted ] || die "notarisation of $(basename "$1") failed"
}

if [ "${#NOTARY[@]}" -gt 0 ]; then
  ditto -c -k --keepParent "$APP" "$TMP/app.zip"
  notarise "$TMP/app.zip"
  xcrun stapler staple "$APP"
fi

# --------------------------------------------------------------- disk image
DMG="$DESKTOP/build/LittleBrowser-macos-$ARCH.dmg"
UPDATE_INFO="${DMG%.dmg}.update.json"
rm -f "$UPDATE_INFO"
mkdir -p "$TMP/stage"
ditto "$APP" "$TMP/stage/Little Browser.app"
ln -s /Applications "$TMP/stage/Applications"
rm -f "$DMG"
hdiutil create -quiet -volname "Little Browser" -srcfolder "$TMP/stage" -format UDZO -ov "$DMG"
if [ "$IDENTITY" != - ]; then
  security unlock-keychain -p "$kc_password" "$KEYCHAIN"
  codesign --sign "$IDENTITY" --timestamp --keychain "$KEYCHAIN" "$DMG"
fi
if [ "${#NOTARY[@]}" -gt 0 ]; then
  notarise "$DMG"
  xcrun stapler staple "$DMG"
fi

# --------------------------------------------------------------- update signature
# The updater installs only a disk image whose EdDSA signature matches the public key in the
# running app's Info.plist. Signed last: stapling changes the file.
if [ -n "$UPDATE_KEY_JSON" ]; then
  sign_update="$TMP/sparkle/bin/sign_update"
  signature="$(field "$UPDATE_KEY_JSON" private_key | "$sign_update" --ed-key-file - -p "$DMG")"
  [ -n "$signature" ] || die "sign_update printed no signature"
  field "$UPDATE_KEY_JSON" private_key |
    "$sign_update" --ed-key-file - --verify "$DMG" "$signature" >/dev/null ||
    die "the update signature doesn't verify"
  jq -n --arg b "$BUNDLE_VERSION" --arg s "$signature" \
    --arg m "$(/usr/libexec/PlistBuddy -c 'Print :LSMinimumSystemVersion' "$PLIST")" \
    '{ bundle_version: $b, minimum_system_version: $m, ed_signature: $s }' > "$UPDATE_INFO"
  echo "update signature: $UPDATE_INFO"
fi

case "$IDENTITY" in
  -) echo "Packaged (ad-hoc signature): $DMG" ;;
  *) echo "Packaged (${IDENTITY_NAME}${NOTARY[0]:+, notarised}): $DMG" ;;
esac

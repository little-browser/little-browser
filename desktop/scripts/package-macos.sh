#!/usr/bin/env bash
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# Sign out/Default's Little Browser.app and package it as a disk image,
# desktop/build/LittleBrowser-macos-<arch>.dmg: the app next to an Applications link.
#
#   desktop/scripts/package-macos.sh             ad-hoc signature (local testing)
#   desktop/scripts/package-macos.sh --release   Developer ID signature, notarised and stapled (CI)
#
# --release reads the signing material from AWS Secrets Manager (little-browser/signing/
# apple-developer-id and apple-notarization), which only the release builder can read. While the
# certificate secret is empty it falls back to an ad-hoc signature, and without notarisation
# credentials it signs but doesn't notarise; it says so either way.
#
# Signing order and options follow ungoogled-chromium-macos's sign_and_package_app.sh: nested
# code first (libraries, tools, helpers), then the framework, then the app, with the hardened
# runtime and the entitlements Chromium's helpers and app need. Identifiers are the bundles' own
# CFBundleIdentifiers.
set -euo pipefail

DESKTOP="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ENT="$DESKTOP/third_party/ungoogled-chromium-macos/entitlements"
APP="$DESKTOP/build/src/out/Default/Little Browser.app"
FW="$APP/Contents/Frameworks/Little Browser Framework.framework"
HELPERS="$FW/Helpers"
REGION="${LB_AWS_REGION:-ap-southeast-2}"
# Developer ID Certification Authority (G2), valid until 2031-09-17.
APPLE_DEVID_CA_URL=https://www.apple.com/certificateauthority/DeveloperIDG2CA.cer
APPLE_DEVID_CA_SHA256=f16cd3c54c7f83cea4bf1a3e6a0819c8aaa8e4a1528fd144715f350643d2df3a

die() { echo "error: $*" >&2; exit 1; }
warn() { echo "warning: $*" >&2; }

RELEASE=0
case "${1:-}" in
  --release) RELEASE=1 ;;
  "") ;;
  *) die "usage: package-macos.sh [--release]" ;;
esac
[ -d "$APP" ] || die "no release build at $APP"

# Keychain identities only count as valid in a user's own launchd session. The GitLab runner is a
# LaunchDaemon, so CI jobs run in the System session, where codesign finds "no identity". Re-run
# in the user's session (ec2-user has passwordless sudo on EC2 Macs).
if [ "$RELEASE" = 1 ] && [ "$(launchctl managername)" = System ] && [ -z "${LB_USER_SESSION:-}" ]; then
  exec sudo -n launchctl asuser "$(id -u)" sudo -n -u "$(id -un)" env LB_USER_SESSION=1 \
    PATH="$PATH" HOME="$HOME" LB_AWS_REGION="$REGION" ${AWS_PROFILE:+AWS_PROFILE="$AWS_PROFILE"} \
    "$0" "$@"
fi
[ -d "$ENT" ] || die "no entitlements in $ENT: run desktop/scripts/fetch.sh first"

IDENTITY=-   # ad-hoc
IDENTITY_NAME=""
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
    security import "$TMP/DeveloperIDG2CA.cer" -k "$KEYCHAIN" >/dev/null
    while IFS= read -r line; do
      line="${line#"${line%%[![:space:]]*}"}"
      ORIGINAL_KEYCHAINS+=("${line//\"/}")
    done < <(security list-keychains -d user)
    # The CI user has no login keychain, so the list can be empty (and macOS's bash 3.2 treats an
    # empty array as unset under `set -u`).
    security list-keychains -d user -s "$KEYCHAIN" ${ORIGINAL_KEYCHAINS[@]+"${ORIGINAL_KEYCHAINS[@]}"}
    identity_line="$(security find-identity -v -p codesigning "$KEYCHAIN" |
      grep -m 1 '"Developer ID Application: ' || true)"
    [ -n "$identity_line" ] || die "the certificate in Secrets Manager is not a Developer ID Application identity"
    IDENTITY="$(awk '{print $2}' <<<"$identity_line")"   # SHA-1 fingerprint
    IDENTITY_NAME="${identity_line#*\"}"
    IDENTITY_NAME="${IDENTITY_NAME%\"}"
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
if [ "$IDENTITY" = - ]; then
  sign "$APP_ID" restrict,library,runtime,kill "$ENT/app-entitlements.plist" "$APP"
else
  # The designated requirement pins the app to a Developer ID certificate, as Chrome's does.
  codesign --sign "$IDENTITY" --force --timestamp --keychain "$KEYCHAIN" --identifier "$APP_ID" \
    --options restrict,library,runtime,kill --entitlements "$ENT/app-entitlements.plist" \
    --requirements "=designated => identifier \"$APP_ID\" and anchor apple generic and certificate 1[field.1.2.840.113635.100.6.2.6] /* exists */ and certificate leaf[field.1.2.840.113635.100.6.1.13] /* exists */" \
    "$APP"
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
arch="$(lipo -archs "$APP/Contents/MacOS/Little Browser")"
DMG="$DESKTOP/build/LittleBrowser-macos-${arch// /-}.dmg"
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

case "$IDENTITY" in
  -) echo "Packaged (ad-hoc signature): $DMG" ;;
  *) echo "Packaged (${IDENTITY_NAME}${NOTARY[0]:+, notarised}): $DMG" ;;
esac

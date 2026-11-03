#!/bin/bash
# Signs HARDWARIO Monitor.app inside-out, builds the DMG and (release only) notarizes and staples
# the app and the DMG (plan/features/0015 D1-D2, plan/features/0031, ADR-0120, ADR-0126, ADR-0224).
#
#   packaging/macos/sign-and-notarize.sh sign   <app> [identity]   sign the bundle (default "-":
#                                                                   ad-hoc, PR CI) and verify it
#   packaging/macos/sign-and-notarize.sh verify <app>              codesign --verify --strict +
#                                                                   the entitlements of both binaries
#   packaging/macos/sign-and-notarize.sh notarize-app <app>        zip (ditto), notarytool submit
#                                                                   --wait, stapler staple the app,
#                                                                   spctl (release sign job only)
#   packaging/macos/sign-and-notarize.sh dmg    <app> <out.dmg> [identity]
#                                                                   DMG (create-dmg, pinned) and its
#                                                                   signature (not for ad-hoc)
#   packaging/macos/sign-and-notarize.sh notarize <dmg>           notarytool submit --wait, stapler
#                                                                   staple, spctl (release sign job only)
#   packaging/macos/sign-and-notarize.sh verify-dmg <dmg>         the DMG and the app inside it:
#                                                                   signatures, stapled tickets, spctl
#
# Signing order (inside-out): every Mach-O in Contents/Frameworks, Contents/PlugIns and
# Contents/Resources (Qt frameworks, plugins and QML plugins, re-signed with our identity), then
# Contents/MacOS/hwio, then the bundle (whose main executable is hwio-monitor). Executables get the
# hardened runtime and packaging/macos/entitlements.plist; everything gets --timestamp unless ad-hoc.
# The release notarizes and staples the app first, so the copy in /Applications carries its own
# ticket, then builds the DMG from the stapled app and notarizes and staples the DMG.
#
# Notarization uses the App Store Connect API key (ADR-0120, ADR-0224): APPLE_API_KEY_P8 (the .p8
# content), APPLE_API_KEY (its key id), APPLE_API_ISSUER. The signing identity comes from a temporary
# keychain set up by the release workflow (APPLE_CERTIFICATE, APPLE_CERTIFICATE_PASSWORD).
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
entitlements="$here/entitlements.plist"
command="${1:?usage: sign-and-notarize.sh sign|verify|notarize-app|dmg|notarize|verify-dmg ...}"

# One private scratch directory per run (DMG staging, the notarization zip, the API key, the mount
# point), removed on every exit; a mounted DMG is detached first.
scratch="$(mktemp -d)"
mounted=""
cleanup() {
    if [ -n "$mounted" ]; then hdiutil detach -quiet "$mounted" || true; fi
    rm -rf "$scratch"
}
trap cleanup EXIT

timestamp_option() {
    if [ "$1" = "-" ]; then echo "--timestamp=none"; else echo "--timestamp"; fi
}

sign_bundle() {
    local app="$1" identity="${2:--}"
    local ts
    ts="$(timestamp_option "$identity")"
    # 1. Frameworks, plugins, QML plugins and any other Mach-O inside (deepest first).
    while IFS= read -r -d '' file; do
        if [[ "$(file -b "$file")" == *Mach-O* ]]; then
            codesign --force --sign "$identity" --options runtime "$ts" "$file"
        fi
    done < <(find "$app/Contents/Frameworks" "$app/Contents/PlugIns" "$app/Contents/Resources" \
                -type f -print0 2>/dev/null | sort -rz)
    while IFS= read -r -d '' framework; do
        codesign --force --sign "$identity" --options runtime "$ts" "$framework"
    done < <(find "$app/Contents/Frameworks" -maxdepth 1 -name '*.framework' -print0 2>/dev/null)
    # 2. The CLI (keeps its embedded __info_plist, ADR-0086), 3. the bundle.
    codesign --force --sign "$identity" --options runtime "$ts" \
        --entitlements "$entitlements" "$app/Contents/MacOS/hwio"
    codesign --force --sign "$identity" --options runtime "$ts" \
        --entitlements "$entitlements" "$app"
}

verify_bundle() {
    local app="$1"
    codesign --verify --deep --strict --verbose=2 "$app"
    for binary in "$app" "$app/Contents/MacOS/hwio"; do
        local ent
        ent="$(codesign -d --entitlements - --xml "$binary" 2>/dev/null || true)"
        if ! grep -q "com.apple.security.cs.disable-library-validation" <<<"$ent"; then
            echo "Missing disable-library-validation entitlement: $binary" >&2
            exit 1
        fi
        if grep -q "allow-jit\|allow-unsigned-executable-memory\|allow-dyld-environment-variables" <<<"$ent"; then
            echo "Unexpected entitlement in $binary (ADR-0126 allows only disable-library-validation)" >&2
            exit 1
        fi
        # Captured first: with pipefail, grep -q closing the pipe early fails codesign (SIGPIPE).
        local details
        details="$(codesign -d --verbose=2 "$binary" 2>&1)"
        if ! grep -q "flags=.*runtime" <<<"$details"; then
            echo "Hardened runtime not enabled: $binary" >&2
            echo "$details" >&2
            exit 1
        fi
    done
    # The Bluetooth usage description must survive the re-signing (ADR-0086).
    otool -s __TEXT __info_plist "$app/Contents/MacOS/hwio" >/dev/null
    plutil -extract NSBluetoothAlwaysUsageDescription raw "$app/Contents/Info.plist" >/dev/null
    echo "Signature and entitlements OK: $app"
}

make_dmg() {
    local app="$1" dmg="$2" identity="${3:--}"
    local staging="$scratch/dmg"
    mkdir -p "$staging"
    # ditto keeps symlinks, modes, extended attributes and the stapled ticket of the bundle.
    ditto "$app" "$staging/$(basename "$app")"
    rm -f "$dmg"
    # create-dmg (pinned, installed by the workflow): window with the app, an Applications link and
    # the background. --skip-jenkins leaves out the Finder (AppleScript) layout step when no
    # Finder session is available (HWIO_DMG_NO_FINDER=1).
    local finder=()
    if [ -n "${HWIO_DMG_NO_FINDER:-}" ]; then finder=(--skip-jenkins); fi
    create-dmg \
        --volname "HARDWARIO Monitor" \
        --background "$here/dmg/background.png" \
        --window-pos 200 120 --window-size 660 400 --icon-size 128 \
        --icon "$(basename "$app")" 165 190 --hide-extension "$(basename "$app")" \
        --app-drop-link 495 190 \
        --no-internet-enable "${finder[@]}" \
        "$dmg" "$staging"
    rm -rf "$staging"
    if [ "$identity" != "-" ]; then
        codesign --force --sign "$identity" --timestamp "$dmg"
    fi
    ls -l "$dmg"
}

# notarytool submit --wait for a .zip or .dmg; anything but Accepted prints Apple's log and fails.
# The .p8 lives in the scratch directory (mode 600) only while notarytool runs.
submit_for_notarization() {
    local file="$1"
    local key="$scratch/AuthKey_${APPLE_API_KEY:?}.p8"
    local result id status
    (umask 077 && printf '%s\n' "${APPLE_API_KEY_P8:?}" > "$key")
    local auth=(--key "$key" --key-id "$APPLE_API_KEY" --issuer "${APPLE_API_ISSUER:?}")
    # Parsed even when notarytool exits non-zero (Invalid, timeout); errors go to stderr.
    result="$(xcrun notarytool submit "$file" "${auth[@]}" --wait --timeout 60m --output-format json)" || true
    read -r id status < <(python3 -I -c '
import json, sys
try:
    d = json.load(sys.stdin)
except ValueError:
    d = {}
print(d.get("id") or "-", d.get("status") or "-")' <<<"$result")
    echo "notarytool: $(basename "$file"): $status (submission $id)"
    if [ "$status" != "Accepted" ]; then
        echo "$result" >&2
        if [ "$id" != "-" ]; then xcrun notarytool log "$id" "${auth[@]}" >&2 || true; fi
        exit 1
    fi
    rm -f "$key"
}

notarize_app() {
    local app="$1"
    ditto -c -k --keepParent "$app" "$scratch/app.zip"
    submit_for_notarization "$scratch/app.zip"
    rm -f "$scratch/app.zip"
    xcrun stapler staple "$app"
    xcrun stapler validate "$app"
    spctl -a -t exec -vv "$app"
}

notarize_dmg() {
    local dmg="$1"
    submit_for_notarization "$dmg"
    xcrun stapler staple "$dmg"
    spctl -a -t open --context context:primary-signature -v "$dmg"
}

# The released DMG: its signature and ticket, then the app inside it (strict signature, its own
# stapled ticket, Gatekeeper). Read-only mount, detached on exit.
verify_dmg() {
    local dmg="$1"
    local mount="$scratch/mnt" app details
    codesign --verify --strict --verbose=2 "$dmg"
    xcrun stapler validate "$dmg"
    spctl -a -t open --context context:primary-signature -v "$dmg"
    mkdir -p "$mount"
    hdiutil attach -readonly -nobrowse -mountpoint "$mount" "$dmg" >/dev/null
    mounted="$mount"
    app="$mount/HARDWARIO Monitor.app"
    codesign --verify --deep --strict --verbose=2 "$app"
    # Captured first: with pipefail, grep -q closing the pipe early fails codesign (SIGPIPE).
    details="$(codesign -d --verbose=2 "$app" 2>&1)"
    grep -E "^(Authority|TeamIdentifier|Timestamp)=" <<<"$details"
    grep -q "^Authority=Developer ID Application" <<<"$details" || { echo "Not signed with a Developer ID: $app" >&2; exit 1; }
    grep -q "^Timestamp=" <<<"$details" || { echo "No secure timestamp: $app" >&2; exit 1; }
    xcrun stapler validate "$app"
    spctl -a -t exec -vv "$app"
    echo "DMG and app signed, notarized and stapled: $dmg"
}

case "$command" in
    sign) sign_bundle "${2:?app}" "${3:--}"; verify_bundle "$2" ;;
    verify) verify_bundle "${2:?app}" ;;
    notarize-app) notarize_app "${2:?app}" ;;
    dmg) make_dmg "${2:?app}" "${3:?dmg}" "${4:--}" ;;
    notarize) notarize_dmg "${2:?dmg}" ;;
    verify-dmg) verify_dmg "${2:?dmg}" ;;
    *) echo "unknown command: $command" >&2; exit 2 ;;
esac

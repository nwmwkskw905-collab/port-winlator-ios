#!/bin/sh
# inspect_entitlements.sh — read what a BUILT Apple product actually carries, without
# touching personal data.
#
# Why this exists (Fix 06, mandatory entitlement investigation; extended in pass 03): four
# different things get confused with each other, and the script now separates them explicitly
# instead of only answering the product side.
#   level 1  requested in the project  — RuntimePoC/WinlatorPhase02.entitlements plus the
#                                        CODE_SIGN_ENTITLEMENTS wiring state. Readable on ANY
#                                        host, which is why it is printed first.
#   level 2  present before signing    — belongs to the signing step: whether the product
#                                        carries a signature, a CodeResources seal or an
#                                        embedded provisioning profile (presence only).
#   level 3  granted to the signature  — only readable on the built product, with an Apple
#                                        toolchain (this script).
#   level 4  observed at runtime       — the harness records it (MAP_JIT accepted/refused).
#
# This script answers level 3 and must be honest when it cannot: on a Linux host, or on an
# unsigned IPA, it says so instead of guessing.
#
# Privacy: only entitlement KEY NAMES and boolean facts are printed. Certificate subjects,
# team identifiers, application-identifier values, UDIDs and profile contents are never
# echoed, because they are not needed to answer "was JIT granted?".
#
# Usage:  sh ios/tools/inspect_entitlements.sh <product>
#         product = an .app bundle, a Mach-O binary, or an .ipa produced by CI.
# Exit status is always 0: this is a diagnostic, it must never break a build.
set -u

STATUS="UNTESTED"
REASON=""
PRODUCT="${1:-}"
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

say() { printf '%s\n' "$*"; }

if [ -z "$PRODUCT" ]; then
    say "ENTITLEMENT_INSPECTION=UNTESTED REASON=NO_PRODUCT_GIVEN"
    say "usage: sh ios/tools/inspect_entitlements.sh <App.app|binary|ipa>"
    exit 0
fi

# ---------------------------------------------------------------- level 1: repository facts
# These do not depend on the host or on the product, so they are always reported first: a
# reader must never have to guess what the project requests and whether anything applies it.
say "LEVEL1_REQUESTED_IN_REPO=checked"
if [ -f "$ROOT/RuntimePoC/WinlatorPhase02.entitlements" ]; then
    say "  file=RuntimePoC/WinlatorPhase02.entitlements present"
    if grep -q "com.apple.security.cs.allow-jit" "$ROOT/RuntimePoC/WinlatorPhase02.entitlements"; then
        say "  key_names:"
        sed -n 's/.*<key>\(com\.apple\.[A-Za-z0-9._-]*\)<\/key>.*/    - \1/p' \
            "$ROOT/RuntimePoC/WinlatorPhase02.entitlements" | sort -u
    else
        say "  key_names=<none of interest>"
    fi
    if grep -q "WIRING DECISION" "$ROOT/RuntimePoC/WinlatorPhase02.entitlements"; then
        say "  wiring_decision=recorded_in_the_entitlements_file"
    else
        say "  wiring_decision=NOT_RECORDED (an unwired entitlements file must say why)"
    fi
else
    say "  file=ABSENT"
fi
# The wiring state, read off the project file: how many target configurations attach the file.
PBXPROJ="$ROOT/WinlatorPhase02.xcodeproj/project.pbxproj"
if [ -f "$PBXPROJ" ]; then
    WIRED=$(grep -c "CODE_SIGN_ENTITLEMENTS" "$PBXPROJ" || true)
    if [ "${WIRED:-0}" -gt 0 ]; then
        say "  CODE_SIGN_ENTITLEMENTS=set in $WIRED configuration line(s) (level 2 will decide"
        say "    whether the signing step embeds it; a subset of configurations is a defect)"
    else
        say "  CODE_SIGN_ENTITLEMENTS=not set in any configuration: the request is a repository"
        say "    record only. Reason and opt-in are documented in the entitlements file and in"
        say "    tools/build_ios.sh (on iOS every entitlement must be allowlisted by the"
        say "    provisioning profile, so attaching one it cannot carry breaks the signed"
        say "    build/install; the unsigned build embeds nothing either way)"
    fi
fi

if [ ! -e "$PRODUCT" ]; then
    say "ENTITLEMENT_INSPECTION=UNTESTED REASON=PRODUCT_NOT_FOUND product=$PRODUCT"
    say "ENTITLEMENT_SIGNATURE_INSPECTION=UNTESTED REASON=NO_APPLE_TOOLCHAIN"
    exit 0
fi
if [ "$(uname -s)" != "Darwin" ] || ! command -v codesign >/dev/null 2>&1; then
    say "LEVEL2_PRESENT_BEFORE_SIGNING=not readable without an Apple toolchain on the product"
    say "  (the level-1 facts above are host independent and remain valid)"
    say "ENTITLEMENT_INSPECTION=UNTESTED REASON=NO_MACOS_HOST host=$(uname -s)"
    say "ENTITLEMENT_SIGNATURE_INSPECTION=UNTESTED REASON=NO_APPLE_TOOLCHAIN"
    say "note: level 3 of the entitlement investigation cannot be read off Darwin; run this"
    say "      on a macOS host (or in the CI job) over the produced product"
    exit 0
fi

WORK=$(mktemp -d "${TMPDIR:-/tmp}/winlator-entitlements-XXXXXX") || exit 0
trap 'rm -rf "$WORK"' EXIT INT TERM

TARGET="$PRODUCT"
case "$PRODUCT" in
*.ipa)
    if ! command -v unzip >/dev/null 2>&1; then
        say "ENTITLEMENT_INSPECTION=UNTESTED REASON=NO_UNZIP"
        say "ENTITLEMENT_SIGNATURE_INSPECTION=UNTESTED REASON=NO_APPLE_TOOLCHAIN"
        exit 0
    fi
    unzip -qq "$PRODUCT" -d "$WORK" || {
        say "ENTITLEMENT_INSPECTION=UNTESTED REASON=IPA_UNREADABLE product=$PRODUCT"
        say "ENTITLEMENT_SIGNATURE_INSPECTION=UNTESTED REASON=NO_APPLE_TOOLCHAIN"
        exit 0
    }
    TARGET=$(find "$WORK/Payload" -maxdepth 1 -name '*.app' -print 2>/dev/null | head -1)
    if [ -z "$TARGET" ]; then
        say "ENTITLEMENT_INSPECTION=UNTESTED REASON=NO_APP_IN_IPA product=$PRODUCT"
        say "ENTITLEMENT_SIGNATURE_INSPECTION=UNTESTED REASON=NO_APPLE_TOOLCHAIN"
        exit 0
    fi
    ;;
esac
[ -d "$TARGET" ] && BUNDLE="$TARGET" || BUNDLE=$(dirname "$TARGET")
EXEC_NAME=$(basename "$TARGET")
[ -d "$TARGET" ] && EXEC_NAME=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleExecutable' \
                                       "$TARGET/Info.plist" 2>/dev/null || echo "")

say "ENTITLEMENT_INSPECTION=STARTED product=$PRODUCT"

# ------------------------------------------------- level 2: what the product carries at all
# Presence only: a seal, a signature or a profile is a boolean fact here, never content.
if [ -d "$BUNDLE" ] && [ -e "$BUNDLE/_CodeSignature/CodeResources" ]; then
    say "LEVEL2_CODESIGNATURE_SEAL=present_in_bundle"
else
    say "LEVEL2_CODESIGNATURE_SEAL=absent_in_bundle"
fi

# ---------------------------------------------------------------- level 2/3: the signature
SIGN_INFO="$WORK/codesign.txt"
if ! codesign -d --entitlements :- "$TARGET" > "$WORK/entitlements.plist" 2> "$SIGN_INFO"; then
    say "LEVEL3_GRANTED_TO_SIGNATURE=ABSENT (codesign reported no readable signature)"
    say "LEVEL2_PRESENT_BEFORE_SIGNING=see the CI signing step"
    say "  this is the expected answer for the UNSIGNED_IPA this project produces on purpose:"
    say "  an unsigned product grants nothing, so MAP_JIT must be refused, and the refusal is"
    say "  a property of the signature - not of the port"
else
    say "LEVEL3_GRANTED_TO_SIGNATURE=READABLE_ON_THE_PRODUCT"
    # Key names only: values can carry team/app identifiers and are not needed here.
    KEYS=$(/usr/libexec/PlistBuddy -c 'Print' "$WORK/entitlements.plist" 2>/dev/null |
           sed -n 's/^ *\(com\.apple\.[A-Za-z0-9._-]*\) *=.*/\1/p' |
           sort -u)
    if [ -n "$KEYS" ]; then
        say "GRANTED_KEY_NAMES:"
        printf '%s\n' "$KEYS" | sed 's/^/  - /'
    else
        say "GRANTED_KEY_NAMES=<none>"
    fi
    for KEY in com.apple.security.cs.allow-jit com.apple.security.cs.allow-unsigned-executable-memory; do
        if /usr/libexec/PlistBuddy -c "Print :$KEY" "$WORK/entitlements.plist" >/dev/null 2>&1; then
            say "JIT_ENTITLEMENT_KEY=$KEY PRESENT_IN_SIGNATURE"
        else
            say "JIT_ENTITLEMENT_KEY=$KEY absent_from_signature"
        fi
    done
fi

# ------------------------------------------------- level 2: the provisioning profile, keys only
if [ -n "$EXEC_NAME" ] && [ -f "$BUNDLE/embedded.mobileprovision" ]; then
    say "LEVEL2_MOBILEPROVISION=present (parsed for key names and expiry only)"
    if security cms -D -i "$BUNDLE/embedded.mobileprovision" > "$WORK/profile.plist" 2>/dev/null; then
        PROFILE_KEYS=$(/usr/libexec/PlistBuddy -c 'Print :Entitlements' "$WORK/profile.plist" 2>/dev/null |
                       sed -n 's/^ *\([A-Za-z0-9._-]*\) *=.*/\1/p' | sort -u)
        [ -n "$PROFILE_KEYS" ] && printf '%s\n' "$PROFILE_KEYS" | sed 's/^/  profile-key: /'
        if /usr/libexec/PlistBuddy -c 'Print :ExpirationDate' "$WORK/profile.plist" >/dev/null 2>&1; then
            EXPIRY=$(/usr/libexec/PlistBuddy -c 'Print :ExpirationDate' "$WORK/profile.plist")
            if date -j -f '%Y-%m-%dT%H:%M:%SZ' "$EXPIRY" '+%s' >/dev/null 2>&1; then
                if [ "$(date '+%s')" -gt "$(date -j -f '%Y-%m-%dT%H:%M:%SZ' "$EXPIRY" '+%s')" ]; then
                    say "PROFILE_EXPIRED=yes"
                else
                    say "PROFILE_EXPIRED=no"
                fi
            else
                say "PROFILE_EXPIRY=unparsed"
            fi
        fi
    else
        say "MOBILEPROVISION_PARSED=no (security cms refused; only its presence is recorded)"
    fi
else
    say "LEVEL2_MOBILEPROVISION=absent_in_bundle"
fi

say "LEVEL4_OBSERVED_AT_RUNTIME=see the harness record jit.map_jit_probe on the device"
say "ENTITLEMENT_SIGNATURE_INSPECTION=COMPLETE (key names and booleans only)"
say "ENTITLEMENT_INSPECTION=COMPLETE (this script prints key names and booleans only; no"
say "  certificate subject, team identifier, UDID or profile value is ever printed)"
exit 0

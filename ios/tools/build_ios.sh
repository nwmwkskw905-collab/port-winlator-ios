#!/bin/sh
# build_ios.sh — generate the Xcode project and build for Simulator and/or device.
#
# PHASE_02_RECONSTRUCTED_POC.
#
# Order (same as CI): generate -> structural preflight -> `xcodebuild -list` gate ->
# build. The iphoneos build only starts when the real Xcode parser reads the project.
#
#   sh tools/build_ios.sh both        # iphonesimulator + iphoneos
#   sh tools/build_ios.sh device      # iphoneos only, unsigned (CODE_SIGNING_ALLOWED=NO)
#   sh tools/build_ios.sh simulator
#
# With no Apple toolchain the script reports the environment limitation instead of
# pretending to have built anything:
#   IOS_BUILD=UNTESTED REASON=NO_APPLE_TOOLCHAIN
#
# ENTITLEMENT WIRING (level 1 -> level 2), decided and audited in pass 03:
#   RuntimePoC/WinlatorPhase02.entitlements records com.apple.security.cs.allow-jit as
#   REQUESTED_IN_REPO and is deliberately NOT attached to CODE_SIGN_ENTITLEMENTS for this
#   iOS target: on iOS-based platforms every entitlement must be allowlisted by the
#   provisioning profile, and an entitlements file asking for one the profile does not allow
#   makes the signed build/install fail ("provisioning profile does not include the ...
#   entitlement"; device 0xE8008016). Attaching it would break the only install path that can
#   validate anything, and could not obtain MAP_JIT anyway (physical run #2: refused
#   errno=1). If a signing context that CAN carry it is available, opt in explicitly:
#     xcodebuild ... CODE_SIGN_ENTITLEMENTS=RuntimePoC/WinlatorPhase02.entitlements
#   (Debug and Release together, never a single configuration; the audit enforces this).
#   The unsigned build below is unaffected by that setting either way.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
EVIDENCE="$ROOT/Documentation/evidence"
mkdir -p "$EVIDENCE"
MODE=${1:-both}
PROJECT="$ROOT/WinlatorPhase02.xcodeproj"
SCHEME=WinlatorPhase02

echo "[ios] regenerating the Xcode project from the tree"
python3 "$ROOT/tools/generate_xcodeproj.py" | tee "$EVIDENCE/ios_generate.log"
echo "[ios] structural validation"
python3 "$ROOT/tools/validate_xcodeproj.py" | tee "$EVIDENCE/ios_validate.log"
echo "[ios] entitlement wiring (level 1 -> 2)"
python3 "$ROOT/tools/audit_entitlement_config.py" | tee "$EVIDENCE/ios_entitlement_config.log" | tail -3

if ! command -v xcodebuild >/dev/null 2>&1; then
    echo "IOS_BUILD=UNTESTED REASON=NO_APPLE_TOOLCHAIN"
    echo "(the generator and the structural validator ran; no xcodebuild exists here.)"
    exit 0
fi

echo "[ios] toolchain"
{ sw_vers; uname -m; xcodebuild -version; xcode-select -p; \
  xcrun --sdk iphoneos --show-sdk-version; clang --version; swift --version; } \
  > "$EVIDENCE/ios_toolchain.txt" 2>&1 || true
cat "$EVIDENCE/ios_toolchain.txt"

echo "[ios] gate: xcodebuild -list (authoritative parser)"
if ! xcodebuild -project "$PROJECT" -list > "$EVIDENCE/ios_xcodebuild_list.log" 2>&1; then
    echo "xcodebuild cannot read the project; the iphoneos build will NOT start."
    cat "$EVIDENCE/ios_xcodebuild_list.log"
    exit 1
fi
cat "$EVIDENCE/ios_xcodebuild_list.log"
grep -q "WinlatorPhase02" "$EVIDENCE/ios_xcodebuild_list.log" || \
    { echo "FAIL: target missing from -list output"; exit 1; }

case "$MODE" in
    simulator|both)
        echo "[ios] build for iphonesimulator"
        xcodebuild -project "$PROJECT" -scheme "$SCHEME" -configuration Debug \
                   -sdk iphonesimulator -derivedDataPath "$ROOT/build/DerivedData" \
                   CODE_SIGNING_ALLOWED=NO build \
                   > "$EVIDENCE/ios_build_simulator.log" 2>&1 || \
            echo "IOS_SIMULATOR_BUILD=FAILED (see ios_build_simulator.log)"
        ;;
esac

case "$MODE" in
    device|both)
        echo "[ios] build for iphoneos (unsigned)"
        xcodebuild -project "$PROJECT" -scheme "$SCHEME" -configuration Debug \
                   -sdk iphoneos -destination 'generic/platform=iOS' \
                   -derivedDataPath "$ROOT/build/DerivedData" \
                   CODE_SIGNING_ALLOWED=NO build \
                   > "$EVIDENCE/ios_build_device.log" 2>&1 || \
            echo "IOS_DEVICE_BUILD=FAILED (see ios_build_device.log)"
        ;;
esac

echo "[ios] done (release builds: use -configuration Release with the same flags)"

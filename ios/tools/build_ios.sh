#!/bin/sh
# build_ios.sh — generate the Xcode project and build for Simulator and/or device.
#
# PHASE_02_RECONSTRUCTED_POC.
#
#   sh tools/build_ios.sh both        # iphonesimulator + iphoneos
#   sh tools/build_ios.sh device      # iphoneos only, unsigned (CODE_SIGNING_ALLOWED=NO)
#   sh tools/build_ios.sh simulator
#
# With no Apple toolchain the script reports the environment limitation instead of
# pretending to have built anything:
#   IOS_BUILD=UNTESTED REASON=NO_APPLE_TOOLCHAIN
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

echo "[ios] xcodebuild -list"
xcodebuild -project "$PROJECT" -list > "$EVIDENCE/ios_xcodebuild_list.log" 2>&1 || true
cat "$EVIDENCE/ios_xcodebuild_list.log"

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

#!/bin/sh
# collect_evidence.sh — regenerate every evidence artefact of this reconstruction.
#
# PHASE_02_RECONSTRUCTED_POC. Host and AArch64 evidence is produced by running the
# code; nothing is transcribed from the lost Phase 02 report.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
EVIDENCE="$ROOT/Documentation/evidence"
mkdir -p "$EVIDENCE"

echo "[evidence] inventory of the reconstructed tree"
find "$ROOT/RuntimeCore" "$ROOT/Diagnostics" "$ROOT/RuntimePoC" "$ROOT/Tests" "$ROOT/tools" \
     -type f | sort | xargs sha256sum > "$EVIDENCE/inventory_sha256.txt"
wc -l < "$EVIDENCE/inventory_sha256.txt"

echo "[evidence] host build + tests"
sh "$ROOT/tools/build_host_harness.sh"

echo "[evidence] aarch64 cross build + qemu tests"
sh "$ROOT/tools/build_aarch64_cross.sh"

echo "[evidence] Apple SDK include audit (static pre-check)"
python3 "$ROOT/tools/audit_apple_includes.py" 2>&1 | tee "$EVIDENCE/apple_include_audit.txt"

echo "[evidence] Apple API audit (macOS-only symbols)"
python3 "$ROOT/tools/audit_apple_apis.py" 2>&1 | tee "$EVIDENCE/apple_api_audit.txt"

echo "[evidence] xcode project structure"
python3 "$ROOT/tools/generate_xcodeproj.py" > "$EVIDENCE/ios_generate.log"
python3 "$ROOT/tools/validate_xcodeproj.py" > "$EVIDENCE/ios_validate.log" || true
tail -1 "$EVIDENCE/ios_validate.log"

echo "[evidence] host/device distinction"
{
    echo "HOST_TESTS   = CONFIRMADA NO HARNESS HOST (linux) - never an iOS result"
    echo "AARCH64      = AARCH64_QEMU - qemu does not validate iOS"
    echo "IOS_BUILD    = see ios_* logs; without a macOS runner it is"
    echo "               IOS_BUILD=UNTESTED REASON=NO_APPLE_TOOLCHAIN"
    echo "IPHONE       = PHYSICAL_DEVICE_VALIDATION_REQUIRED (iPhone 13 target)"
} > "$EVIDENCE/evidence_classes.txt"
cat "$EVIDENCE/evidence_classes.txt"

#!/bin/sh
# build_aarch64_cross.sh — cross build + qemu run of the same sources.
#
# PHASE_02_RECONSTRUCTED_POC. Results from this script are AARCH64_QEMU. QEMU does
# not validate iOS, and this script never claims it does.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
EVIDENCE="$ROOT/Documentation/evidence"
mkdir -p "$EVIDENCE"
BUILD="$ROOT/build/aarch64"
SYSROOT=/usr/aarch64-linux-gnu

if ! command -v aarch64-linux-gnu-gcc >/dev/null 2>&1; then
    echo "AARCH64_CROSS=UNTESTED REASON=NO_AARCH64_TOOLCHAIN"
    exit 0
fi

echo "[aarch64] cmake configure (toolchain file)"
cmake -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_TOOLCHAIN_FILE="$ROOT/tools/aarch64-linux-toolchain.cmake" \
      > "$EVIDENCE/aarch64_cmake_configure.log" 2>&1
echo "[aarch64] build"
cmake --build "$BUILD" -j"$( (nproc 2>/dev/null) || echo 2)" > "$EVIDENCE/aarch64_build.log" 2>&1
echo "[aarch64] warnings: $(grep -c 'warning:' "$EVIDENCE/aarch64_build.log" || true)"
file "$BUILD/phase02_poc" > "$EVIDENCE/aarch64_binary_arch.txt" 2>&1 || true
cat "$EVIDENCE/aarch64_binary_arch.txt"

if command -v qemu-aarch64 >/dev/null 2>&1; then
    echo "[aarch64] unit tests under qemu"
    (cd "$BUILD" && qemu-aarch64 -L "$SYSROOT" ./phase02_unit_tests) \
        > "$EVIDENCE/aarch64_unit_tests.log" 2>&1 || true
    tail -1 "$EVIDENCE/aarch64_unit_tests.log"
    echo "[aarch64] selected-suite regression under qemu"
    (cd "$BUILD" && qemu-aarch64 -L "$SYSROOT" ./phase02_run_selected_tests) \
        > "$EVIDENCE/aarch64_run_selected_tests.log" 2>&1 || true
    tail -1 "$EVIDENCE/aarch64_run_selected_tests.log"
    echo "[aarch64] full suite under qemu"
    (cd "$BUILD" && qemu-aarch64 -L "$SYSROOT" ./phase02_poc --suite all \
        --export "$EVIDENCE/aarch64_phase02_report.txt") \
        > "$EVIDENCE/aarch64_suite_stdout.log" 2>&1 || true
    tail -1 "$EVIDENCE/aarch64_suite_stdout.log"
else
    echo "AARCH64_QEMU=UNTESTED REASON=NO_QEMU"
fi

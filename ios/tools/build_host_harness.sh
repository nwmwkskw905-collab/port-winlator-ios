#!/bin/sh
# build_host_harness.sh — host build, ctest and the full suite with an exported report.
#
# PHASE_02_RECONSTRUCTED_POC. Everything it prints is a HOST result.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
EVIDENCE="$ROOT/Documentation/evidence"
mkdir -p "$EVIDENCE"
BUILD="$ROOT/build/host"

echo "[host] cmake configure"
cmake -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE=Debug > "$EVIDENCE/host_cmake_configure.log" 2>&1
echo "[host] build"
cmake --build "$BUILD" -j"$( (nproc 2>/dev/null) || echo 2)" > "$EVIDENCE/host_build.log" 2>&1
grep -c "warning:" "$EVIDENCE/host_build.log" > "$EVIDENCE/host_build_warning_count.txt" || true
grep -c "error:" "$EVIDENCE/host_build.log" > "$EVIDENCE/host_build_error_count.txt" || true
echo "[host] warnings: $(cat "$EVIDENCE/host_build_warning_count.txt") errors: $(cat "$EVIDENCE/host_build_error_count.txt")"

echo "[host] ctest"
(cd "$BUILD" && ctest --output-on-failure) > "$EVIDENCE/host_ctest.log" 2>&1 || true
tail -3 "$EVIDENCE/host_ctest.log"

echo "[host] unit tests"
(cd "$BUILD" && ./phase02_unit_tests) > "$EVIDENCE/host_unit_tests.log" 2>&1 || true
tail -1 "$EVIDENCE/host_unit_tests.log"

echo "[host] full suite"
(cd "$BUILD" && ./phase02_poc --suite all --export "$EVIDENCE/host_phase02_report.txt") \
    > "$EVIDENCE/host_suite_stdout.log" 2>&1 || true
tail -1 "$EVIDENCE/host_suite_stdout.log"
echo "[host] report: $EVIDENCE/host_phase02_report.txt"

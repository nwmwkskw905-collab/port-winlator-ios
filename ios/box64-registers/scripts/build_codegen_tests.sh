#!/bin/sh
# ---------------------------------------------------------------------------
# FASE 04: build and run the CODEGEN tests (emitted instruction sequences).
#
#   new  -> -I include/new       patched tree, Strategy A (guest R8 -> host x9)
#   orig -> -I include/orig      upstream v0.4.4 (guest R8 -> host x18)
#
# The tests emit real instructions through box64's own emitter headers and run
# them on the host CPU under qemu-aarch64.  Run from anywhere:
#       sh scripts/build_codegen_tests.sh [fuzz_iterations] [seed]
# ---------------------------------------------------------------------------
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT/harness"

CC=aarch64-linux-gnu-gcc
QEMU=qemu-aarch64
ITERS=${1:-2000}
SEED=${2:-0x5eedf04}

# full warning set requested for this phase; the build must stay silent
WARN="-Wall -Wextra -Wpedantic -Wshadow -Wconversion"
BASE="-O1 -std=gnu11 -static $WARN -ffixed-x18 -I. -I../include/upstream"

echo "=== building codegen harness: patched tree (xR8 = x9) ==="
$CC $BASE -I../include/new  t_codegen.c clobber.S frame_call.S -o artifacts/t_codegen_new
echo "=== building codegen harness: upstream (xR8 = x18) ==="
$CC $BASE -I../include/orig t_codegen.c clobber.S frame_call.S -o artifacts/t_codegen_orig

echo "=== running: patched tree ($ITERS fuzz iterations, seed $SEED) ==="
timeout 180 $QEMU ./artifacts/t_codegen_new . $SEED $ITERS 2>&1 | tee artifacts/t_codegen_new.log || true
echo "=== running: upstream ==="
timeout 180 $QEMU ./artifacts/t_codegen_orig . $SEED $ITERS 2>&1 | tee artifacts/t_codegen_orig.log || true

echo "=== behavioural comparison of the two register models (T3-T9) ==="
sed -n '/^\[T3\]/,/^=== /p' artifacts/t_codegen_new.log  | grep -v '^=== ' > /tmp/codegen_behaviour_new.txt
sed -n '/^\[T3\]/,/^=== /p' artifacts/t_codegen_orig.log | grep -v '^=== ' > /tmp/codegen_behaviour_orig.txt
if diff -q /tmp/codegen_behaviour_new.txt /tmp/codegen_behaviour_orig.txt > /dev/null; then
    echo "identical behavioural output for T3-T9 across both register models (structural checks differ by design)"
else
    echo "DIVERGENCE between register models:"
    diff /tmp/codegen_behaviour_new.txt /tmp/codegen_behaviour_orig.txt
    exit 1
fi

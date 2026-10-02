#!/bin/sh
# ---------------------------------------------------------------------------
# FASE 04: build and run the dynarec FRAME tests (real arm64_prolog/epilog/next).
#
#   new     -> patched/            Strategy A: guest R8 lives in host x9
#   orig    -> include/upstream/   box64 v0.4.4: guest R8 lives in host x18
#   mutant  -> patched prolog/epilog + UPSTREAM next.S, i.e. what the tree would
#              look like if the dispatcher had been forgotten during the port.
#              Mutation test: the suite MUST detect it (T14 must fail), which
#              proves the next.S change is load-bearing and not cosmetic.
#
# The same t_frame.S / t_frame_main.c sources are used everywhere; only the
# R8REG macro differs.  Usage:  sh scripts/build_frame_tests.sh
# ---------------------------------------------------------------------------
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"

CC=aarch64-linux-gnu-gcc
QEMU=qemu-aarch64
WARN="-Wall -Wextra -Wpedantic -Wshadow -Wconversion"
CFLAGS="-O1 -std=gnu11 -static $WARN -ffixed-x18 -I harness"

mkdir -p harness/artifacts/frame/new harness/artifacts/frame/orig harness/artifacts/frame/mutant

echo "=== building frame tests ==="
N=harness/artifacts/frame/new
$CC $CFLAGS -DMODEL_NAME='"patched Strategy A: guest R8 -> host x9"' \
    -c harness/t_frame_main.c -o $N/t_frame_main.o
$CC -c -DR8REG=x9 harness/t_frame.S   -o $N/t_frame.o
$CC -c patched/arm64_prolog.S         -o $N/prolog.o
$CC -c patched/arm64_epilog.S         -o $N/epilog.o
$CC -c patched/arm64_next.S           -o $N/next.o
$CC -static $N/t_frame_main.o $N/t_frame.o $N/prolog.o $N/epilog.o $N/next.o -o $N/t_frame_new

O=harness/artifacts/frame/orig
$CC $CFLAGS -DMODEL_NAME='"upstream v0.4.4: guest R8 -> host x18"' \
    -c harness/t_frame_main.c -o $O/t_frame_main.o
$CC -c -DR8REG=x18 harness/t_frame.S          -o $O/t_frame.o
$CC -c include/upstream/arm64_prolog.S        -o $O/prolog.o
$CC -c include/upstream/arm64_epilog.S        -o $O/epilog.o
$CC -c include/upstream/arm64_next.S          -o $O/next.o
$CC -static $O/t_frame_main.o $O/t_frame.o $O/prolog.o $O/epilog.o $O/next.o -o $O/t_frame_orig

M=harness/artifacts/frame/mutant
$CC $CFLAGS -DMODEL_NAME='"mutant: Strategy A everywhere except next.S"' \
    -c harness/t_frame_main.c -o $M/t_frame_main.o
$CC -c -DR8REG=x9 harness/t_frame.S           -o $M/t_frame.o
$CC -c patched/arm64_prolog.S                 -o $M/prolog.o
$CC -c patched/arm64_epilog.S                 -o $M/epilog.o
$CC -c include/upstream/arm64_next.S          -o $M/next.o
$CC -static $M/t_frame_main.o $M/t_frame.o $M/prolog.o $M/epilog.o $M/next.o -o $M/t_frame_mutant

echo "=== registers written by the compiled C helper (context for the control) ==="
aarch64-linux-gnu-objdump -d $N/t_frame_main.o | sed -n '/<hf_c_poison>:/,/^$/p' > $N/hf_c_poison.dis
printf 'hf_c_poison uses: '; grep -oE '\b(w|x|d)[0-9]+' $N/hf_c_poison.dis | sort -u | tr '\n' ' '; echo

echo "=== running: patched tree ==="
( cd harness && timeout 120 $QEMU ./artifacts/frame/new/t_frame_new ; echo "exit=$?" ) | tee $N/t_frame_new.log

echo "=== running: upstream tree ==="
( cd harness && timeout 120 $QEMU ./artifacts/frame/orig/t_frame_orig ; echo "exit=$?" ) | tee $O/t_frame_orig.log

echo "=== running: mutant (next.S left upstream) ==="
( cd harness && timeout 120 $QEMU ./artifacts/frame/mutant/t_frame_mutant ; echo "exit=$?" ) | tee $M/t_frame_mutant.log

if grep -q "^  FAIL" $M/t_frame_mutant.log; then
    echo "=== mutation detected: next.S change is load-bearing ==="
else
    echo "=== MUTANT NOT DETECTED - the frame suite is insensitive, aborting ==="
    exit 1
fi

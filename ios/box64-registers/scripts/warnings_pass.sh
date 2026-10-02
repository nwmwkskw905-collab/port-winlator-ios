#!/bin/sh
# ---------------------------------------------------------------------------
# FASE 04 / item (l): warnings pass on the changed code.
#
#   -Wall -Wextra -Wpedantic -Wshadow -Wconversion, no global suppression.
#   The three changed C translation units plus the four STEP variants of
#   dynarec_arm64_helper.c are compiled TWICE with the identical flag set:
#     upstream : box64 v0.4.4 sources taken from git HEAD
#     patched  : the tree with Strategy A applied
#   and the warning sets are compared.  A warning introduced by the patch shows
#   up as a difference; pre-existing upstream warnings are not our concern but
#   are counted and reported.
#
#   The upstream sources must already be extracted (see the header of
#   scripts/apply_strategy_a.py for the recipe); this script extracts them
#   itself with `git show` from the reference checkout if needed.
# ---------------------------------------------------------------------------
set -e
BOX64=${BOX64:-/tmp/box64-ref}
OUT=${OUT:-/tmp/p04/warn}
WARN="-Wall -Wextra -Wpedantic -Wshadow -Wconversion"
FLAGS="-c -O2 -std=gnu11 -funwind-tables -fvisibility=hidden $WARN -DDYNAREC -DARM64"
INC="-Isrc/include -Isrc -Isrc/dynarec/arm64"
CC=aarch64-linux-gnu-gcc

mkdir -p "$OUT/inc_up" "$OUT/src_up" "$OUT/log"
cd "$BOX64"
git show HEAD:src/dynarec/arm64/arm64_mapping.h        > "$OUT/inc_up/arm64_mapping.h"
git show HEAD:src/dynarec/arm64/dynarec_arm64_helper.h > "$OUT/inc_up/dynarec_arm64_helper.h"
for f in dynarec_arm64_helper.c dynarec_arm64_functions.c arm64_printer.c \
         dynarec_arm64_emit_tests.c dynarec_arm64_emit_math.c \
         dynarec_arm64_emit_logic.c dynarec_arm64_emit_shift.c; do
    git show HEAD:src/dynarec/arm64/$f > "$OUT/src_up/$f"
done

echo "### warnings pass, differential (upstream HEAD vs patched worktree)"
echo "### $CC $FLAGS $INC"
echo

for s in 0 1 2 3; do
    $CC $FLAGS -DSTEP=$s $INC src/dynarec/arm64/dynarec_arm64_helper.c -o "$OUT/log/p_helper$s.o" \
        2> "$OUT/log/patched_helper$s.txt" || echo "PATCHED helper STEP=$s FAILED TO COMPILE"
    $CC $FLAGS -DSTEP=$s -I"$OUT/inc_up" $INC "$OUT/src_up/dynarec_arm64_helper.c" -o "$OUT/log/u_helper$s.o" \
        2> "$OUT/log/upstream_helper$s.txt" || echo "UPSTREAM helper STEP=$s FAILED TO COMPILE"
done

$CC $FLAGS $INC src/dynarec/arm64/dynarec_arm64_functions.c -o "$OUT/log/p_functions.o" \
    2> "$OUT/log/patched_functions.txt" || echo "PATCHED functions.c FAILED TO COMPILE"
$CC $FLAGS -I"$OUT/inc_up" $INC "$OUT/src_up/dynarec_arm64_functions.c" -o "$OUT/log/u_functions.o" \
    2> "$OUT/log/upstream_functions.txt" || echo "UPSTREAM functions.c FAILED TO COMPILE"

$CC $FLAGS $INC src/dynarec/arm64/arm64_printer.c -o "$OUT/log/p_printer.o" \
    2> "$OUT/log/patched_printer.txt" || echo "PATCHED arm64_printer.c FAILED TO COMPILE"
$CC $FLAGS -I"$OUT/inc_up" $INC "$OUT/src_up/arm64_printer.c" -o "$OUT/log/u_printer.o" \
    2> "$OUT/log/upstream_printer.txt" || echo "UPSTREAM arm64_printer.c FAILED TO COMPILE"

# the four opcode-emitter TUs (pull in arm64_emitter.h + the mapping for the whole ISA)
for f in dynarec_arm64_emit_tests dynarec_arm64_emit_math dynarec_arm64_emit_logic dynarec_arm64_emit_shift; do
    $CC $FLAGS $INC src/dynarec/arm64/$f.c -o "$OUT/log/p_$f.o" \
        2> "$OUT/log/patched_$f.txt" || echo "PATCHED $f FAILED TO COMPILE"
    $CC $FLAGS -I"$OUT/inc_up" $INC "$OUT/src_up/$f.c" -o "$OUT/log/u_$f.o" \
        2> "$OUT/log/upstream_$f.txt" || echo "UPSTREAM $f FAILED TO COMPILE"
done

TUS="helper0 helper1 helper2 helper3 functions printer dynarec_arm64_emit_tests dynarec_arm64_emit_math dynarec_arm64_emit_logic dynarec_arm64_emit_shift"

echo "### per-TU warning counts (upstream -> patched)"
for t in $TUS; do
    u=$(grep -c "warning:" "$OUT/log/upstream_$t.txt" || true)
    p=$(grep -c "warning:" "$OUT/log/patched_$t.txt" || true)
    printf '%-10s %3s -> %3s\n' "$t" "$u" "$p"
done

echo
echo "### warnings present ONLY in the patched build (must be none, or classified)"
for t in $TUS; do
    # strip file:line numbers so the two builds are comparable
    sed 's/^[^:]*:[0-9]*:[0-9]*: //' "$OUT/log/upstream_$t.txt" | sort > "$OUT/log/u_$t.set"
    sed 's/^[^:]*:[0-9]*:[0-9]*: //' "$OUT/log/patched_$t.txt"  | sort > "$OUT/log/p_$t.set"
    d=$(comm -13 "$OUT/log/u_$t.set" "$OUT/log/p_$t.set" | grep -c "warning:" || true)
    printf '%-10s new warnings: %s\n' "$t" "$d"
    if [ "$d" != "0" ]; then
        comm -13 "$OUT/log/u_$t.set" "$OUT/log/p_$t.set" | grep "warning:" | sed 's/^/    /'
    fi
done

echo
echo "### raw patched-build warning text (all of it, for classification)"
for t in $TUS; do
    echo "--- $t ---"
    grep "warning:" "$OUT/log/patched_$t.txt" | sort | uniq -c | sort -rn || true
done

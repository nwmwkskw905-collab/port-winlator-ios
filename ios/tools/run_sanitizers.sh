#!/bin/sh
# run_sanitizers.sh — ASAN and UBSAN over the paths "Run Selected" takes.
#
# PHASE_02_RECONSTRUCTED_POC (pass 04).
#
# The pass-04 deliverable requires ASAN= and UBSAN= to be recorded as PASS/FAIL/UNAVAILABLE.
# "Where available" is taken literally: with no sanitizer-capable compiler this prints
# UNAVAILABLE and says why; with one, the tools really run over the selected-suite entry
# points (the same two functions the bridge calls), the unit tests and the full suite, and a
# non-zero exit from any of them is reported as FAIL - never smoothed over.
#
# What is NOT claimed: these sanitizers run on Linux/x86-64 and (when a cross compiler exists)
# could be aimed at AArch64, but nothing here says anything about iOS. Sanitizers on a host do
# not validate a device, and this script never pretends otherwise.
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
EVIDENCE="$ROOT/Documentation/evidence"
mkdir -p "$EVIDENCE"

SANITIZERS="address undefined"
OVERALL=0

for SANITIZER in $SANITIZERS; do
    case "$SANITIZER" in
        address)   NAME=ASAN; FLAGS="-fsanitize=address -fno-omit-frame-pointer" ;;
        undefined) NAME=UBSAN; FLAGS="-fsanitize=undefined -fno-sanitize-recover=all -fno-omit-frame-pointer" ;;
    esac
    BUILD="$ROOT/build/$SANITIZER"
    LOG="$EVIDENCE/$(echo "$SANITIZER" | tr 'a-z' 'A-Z')_run.log"

    if ! command -v cc >/dev/null 2>&1; then
        echo "LOWER_${NAME}=UNAVAILABLE REASON=NO_COMPILER"
        continue
    fi

    {
        echo "# $NAME: $FLAGS"
        echo "# targets: phase02_unit_tests, phase02_run_selected_tests, phase02_poc (all suites)"
    } > "$LOG"

    rm -rf "$BUILD"
    if ! cmake -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE=Debug \
            -DCMAKE_C_FLAGS="$FLAGS" -DCMAKE_EXE_LINKER_FLAGS="${FLAGS%% *}" \
            >> "$LOG" 2>&1; then
        echo "${NAME}=FAIL REASON=CONFIGURE (see $LOG)"
        OVERALL=1
        continue
    fi
    if ! cmake --build "$BUILD" -j"$( (nproc 2>/dev/null) || echo 2)" >> "$LOG" 2>&1; then
        echo "${NAME}=FAIL REASON=BUILD (see $LOG)"
        OVERALL=1
        continue
    fi

    STATUS=PASS
    WORK=$(mktemp -d)
    for COMMAND in \
        "$BUILD/phase02_unit_tests" \
        "$BUILD/phase02_run_selected_tests" \
        "$BUILD/phase02_poc --suite all --quiet --workdir $WORK" \
        "$BUILD/phase02_poc --suite memory --quiet --journal $WORK/journal.txt" \
        "$BUILD/phase02_poc --suite jit --quiet --journal $WORK/journal.txt" \
        "$BUILD/phase02_poc --suite cpu --quiet --journal $WORK/journal.txt" \
        "$BUILD/phase02_poc --suite threads --quiet --journal $WORK/journal.txt" \
        "$BUILD/phase02_poc --suite loader --quiet --journal $WORK/journal.txt"
    do
        echo "\$ $COMMAND" >> "$LOG"
        # shellcheck disable=SC2086
        if $COMMAND >> "$LOG" 2>&1; then
            echo "--> exit 0" >> "$LOG"
        else
            echo "--> exit $? : FAILED" >> "$LOG"
            STATUS=FAIL
        fi
    done
    if grep -q "runtime error\|ERROR: AddressSanitizer\|ERROR: LeakSanitizer" "$LOG"; then
        echo "# diagnostic text found in the log:" >> "$LOG"
        grep -n "runtime error\|ERROR: AddressSanitizer\|ERROR: LeakSanitizer" "$LOG" >> "$LOG"
        STATUS=FAIL
    fi
    rm -rf "$WORK"

    echo "${NAME}=${STATUS} (log: Documentation/evidence/$(basename "$LOG"))"
    if [ "$STATUS" = "FAIL" ]; then
        OVERALL=1
    fi
done

exit $OVERALL

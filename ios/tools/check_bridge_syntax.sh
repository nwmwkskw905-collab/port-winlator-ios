#!/bin/sh
# check_bridge_syntax.sh — compile the Objective-C bridge on a NON-Apple host.
#
# PHASE_02_RECONSTRUCTED_POC. Apple CI run #4 failed with:
#
#   Phase02Bridge.m:75:69: error: call to undeclared function 'rt_jit_isa'; ISO C99 and
#   later do not support implicit function declarations
#   warning: format specifies type 'char *' but the argument has type 'int'
#
# The Xcode build is the authority, but a whole CI cycle per defect is expensive. This
# script runs clang (the same front-end family) over Phase02Bridge.m with a stub
# Foundation, the strict warning policy of the project, and
# -Werror=implicit-function-declaration. It reproduces that class of diagnostic in
# seconds, on any host.
#
# With no clang available it reports the limitation instead of pretending:
#   BRIDGE_SYNTAX=UNTESTED REASON=NO_CLANG
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
EVIDENCE="$ROOT/Documentation/evidence"
mkdir -p "$EVIDENCE"

if ! command -v clang >/dev/null 2>&1; then
    echo "BRIDGE_SYNTAX=UNTESTED REASON=NO_CLANG"
    echo "(the interface audit still runs; the Apple build remains the authority)"
    ( echo "BRIDGE_SYNTAX=UNTESTED REASON=NO_CLANG" ) > "$EVIDENCE/bridge_syntax.log"
    exit 0
fi

{
    echo "# clang $(clang --version | head -1)"
    echo "# stub Foundation: tools/fake_foundation/Foundation/Foundation.h (never used by the app)"
} > "$EVIDENCE/bridge_syntax.log"

set +e
clang -fsyntax-only -x objective-c -std=gnu11 \
    -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion \
    -Wpointer-arith -Wcast-align -Wstrict-prototypes -Wmissing-prototypes \
    -Werror=implicit-function-declaration -Wformat \
    -I "$ROOT/tools/fake_foundation" \
    -I "$ROOT/RuntimeCore/include" -I "$ROOT/Diagnostics/include" \
    "$ROOT/RuntimePoC/Phase02Bridge.m" >> "$EVIDENCE/bridge_syntax.log" 2>&1
status=$?
set -e

warnings=$(grep -c "warning:" "$EVIDENCE/bridge_syntax.log" || true)
errors=$(grep -c "error:" "$EVIDENCE/bridge_syntax.log" || true)
cat "$EVIDENCE/bridge_syntax.log"
echo "BRIDGE_SYNTAX warnings=$warnings errors=$errors"
if [ "$status" -ne 0 ]; then
    echo "BRIDGE_SYNTAX=FAIL (see $EVIDENCE/bridge_syntax.log)"
    exit 1
fi
echo "BRIDGE_SYNTAX=PASS (no implicit declarations, no format mismatches; "
echo "                    note: this is clang+stub, the Apple SDK remains the authority)"

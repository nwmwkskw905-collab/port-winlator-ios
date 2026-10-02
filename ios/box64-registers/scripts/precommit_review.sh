#!/bin/sh
# ---------------------------------------------------------------------------
# FASE 04 / item (o): pre-commit review.
#
# Hunt for the failure modes the phase prompt names explicitly:
#   * accidental x18 -> x9 substitution somewhere it does not belong
#   * unintended change to the Linux/Android behaviour
#   * ABI break
#   * duplicated register home
#   * dead code
#   * fake fallback (a Darwin path that quietly degrades)
#   * suppressed warnings
#   * tests that do not exercise the real code
#
# Runs against the reference tree (default /tmp/box64-ref) and the workspace
# artifacts.  Usage:  sh scripts/precommit_review.sh > evidence/precommit_review.txt
# ---------------------------------------------------------------------------
BOX64=${BOX64:-/tmp/box64-ref}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"

echo "### FASE 04 pre-commit review"
echo "### box64 reference tree: $BOX64"
echo "### date: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
echo
echo "== 1. working tree state (nothing unexpected may be pending) =="
git -C "$BOX64" status --short
echo "revision under test: $(git -C "$BOX64" rev-parse HEAD)"
echo
echo "== 2. the change set, by size (a mass substitution would be huge) =="
git -C "$BOX64" diff --stat
echo
echo "== 3. is the durable patch in the workspace byte-identical to the applied diff? =="
git -C "$BOX64" diff > /tmp/p04_applied.diff
if diff -q /tmp/p04_applied.diff patch/0001-strategy-a-guest-r8-x18-to-x9.patch > /dev/null; then
    echo "OK: patch/0001-*.patch == the applied diff ($(wc -l < /tmp/p04_applied.diff) lines)"
else
    echo "MISMATCH between patch/0001-*.patch and the tree:"
    diff /tmp/p04_applied.diff patch/0001-strategy-a-guest-r8-x18-to-x9.patch | head -20
fi
echo
echo "== 4. every changed line that mentions x18 or x9, in context =="
git -C "$BOX64" diff -U0 | grep -E "^[+-]" | grep -vE "^(\+\+\+|---)" | grep -nE "x18|x9|w9\b" | sed 's/^/   /'
echo
echo "== 5. x18 occurrence census: upstream vs patched, per changed file =="
printf '   %-38s %8s %8s\n' file upstream patched
for f in src/dynarec/arm64/arm64_prolog.S src/dynarec/arm64/arm64_epilog.S \
         src/dynarec/arm64/arm64_next.S src/dynarec/arm64/arm64_mapping.h \
         src/dynarec/arm64/arm64_printer.c src/dynarec/arm64/dynarec_arm64_functions.c \
         src/dynarec/arm64/dynarec_arm64_helper.c src/dynarec/arm64/dynarec_arm64_helper.h; do
    u=$(git -C "$BOX64" show "HEAD:$f" | grep -cE '\bx18\b')
    p=$(grep -cE '\bx18\b' "$BOX64/$f")
    printf '   %-38s %8s %8s\n' "$(basename $f)" "$u" "$p"
done
echo
echo "   the surviving x18 uses must be exactly the Windows TEB paths and comments:"
grep -rnE '\bx18\b' "$BOX64/src/dynarec/arm64/" --include=*.S --include=*.h --include=*.c \
    | grep -v arm64_printer.c | sed 's|^.*/arm64/|      arm64/|'
echo
echo "== 6. _WIN32 / TEB precedent preserved (win64_teb site count) =="
echo "   patched: $(grep -rc "win64_teb" $BOX64/src/dynarec/arm64/ --include=*.S --include=*.c | grep -v ':0' | tr '\n' ' ')"
echo "   upstream: $(for f in arm64_epilog.S arm64_next.S dynarec_arm64_helper.c dynarec_arm64_helper.h; do echo -n \"$f:$(git -C $BOX64 show HEAD:src/dynarec/arm64/$f | grep -c win64_teb) \"; done)"
echo "   _WIN32 blocks that load the TEB into x18: $(grep -rn -A1 "#ifdef _WIN32" $BOX64/src/dynarec/arm64/*.S | grep -c "ldr     x18, \[x0, 3104\]")"
echo
echo "== 7. no per-platform #ifdef smuggled into the register model =="
echo "   __APPLE__ in the patch:  $(git -C "$BOX64" diff | grep -c '__APPLE__')"
echo "   __APPLE__ in arm64 dir:  $(grep -rc '__APPLE__' $BOX64/src/dynarec/arm64/ | grep -v ':0' | wc -l) files"
echo "   (the mapping must stay platform independent: one register model everywhere)"
echo
echo "== 8. no warning suppression added by the patch =="
echo "   pragma/Wno- in added lines: $(git -C "$BOX64" diff | grep -cE '^\+.*(#pragma (GCC|clang) diagnostic|Wno-)')"
echo
echo "== 9. the affine-function assumptions that the move breaks =="
echo "   upstream TO_NAT:  $(git -C "$BOX64" show HEAD:src/dynarec/arm64/arm64_mapping.h | grep -m1 'define TO_NAT')"
echo "   upstream IS_GPR:  $(git -C "$BOX64" show HEAD:src/dynarec/arm64/arm64_mapping.h | grep -m1 'define IS_GPR')"
echo "   product use sites of IS_GPR (arm64): $(grep -rn 'IS_GPR(' $BOX64/src/dynarec/arm64/*.c $BOX64/src/dynarec/arm64/*.h | grep -vc arm64_mapping.h)"
echo "   product use sites of TO_NAT (arm64): $(grep -rn 'TO_NAT(' $BOX64/src/dynarec/arm64/ | grep -vc arm64_mapping.h)"
echo
echo "== 10. dead code / fake fallback hunt in the added lines =="
HITS=$(git -C "$BOX64" diff | grep -E "^\+" | grep -vE "^\+\+\+" | grep -vE "^\+ *//|^\+ *\*" \
       | grep -cE "#if|#else|#endif|fallback|TODO|XXX|FIXME")
if [ "$HITS" = "0" ]; then
    echo "   none: no conditional, fallback or TODO marker was added (comment-only matches ignored)"
else
    git -C "$BOX64" diff | grep -E "^\+" | grep -vE "^\+\+\+" | grep -vE "^\+ *//|^\+ *\*" \
        | grep -nE "#if|#else|#endif|fallback|TODO|XXX|FIXME" | sed 's/^/   /'
fi
echo
echo "== 11. tests must exercise the real code, not a copy =="
echo "   codegen harness includes: $(grep -m2 '#include "arm64' harness/t_codegen.c | tr -d ' ' | tr '\n' ' ')"
echo "   (include/new is the patched arm64_mapping.h: $(diff -q include/new/arm64_mapping.h patched/arm64_mapping.h >/dev/null && echo identical || echo DIFFERENT))"
echo "   frame harness assembles: $(ls patched/*.S | tr '\n' ' ')"
echo "   (the frame tests link the real patched prolog/epilog/next, and the upstream ones for the"
echo "    differential build; the mutation build uses upstream next.S on purpose)"
echo
echo "== 12. headers under test vs headers in the reference tree =="
for f in arm64_mapping.h dynarec_arm64_helper.h; do
    printf '   %-28s tree == mirror: %s\n' "$f" \
      "$(diff -q $BOX64/src/dynarec/arm64/$f patched/$f >/dev/null && echo yes || echo NO)"
done
for f in arm64_prolog.S arm64_epilog.S arm64_next.S; do
    printf '   %-28s tree == mirror: %s\n' "$f" \
      "$(diff -q $BOX64/src/dynarec/arm64/$f patched/$f >/dev/null && echo yes || echo NO)"
done
for f in dynarec_arm64_helper.c dynarec_arm64_functions.c arm64_printer.c; do
    printf '   %-28s tree == mirror: %s\n' "$f" \
      "$(diff -q $BOX64/src/dynarec/arm64/$f patched/$f >/dev/null && echo yes || echo NO)"
done
printf '   %-28s tree == mirror: %s\n' "arm64_emitter.h (untouched)" \
  "$(diff -q $BOX64/src/dynarec/arm64/arm64_emitter.h include/upstream/arm64_emitter.h >/dev/null && echo yes || echo NO)"

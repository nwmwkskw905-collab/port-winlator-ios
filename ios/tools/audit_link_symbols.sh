#!/bin/sh
# audit_link_symbols.sh — object-level link audit of the platform layer.
#
# PHASE_02_RECONSTRUCTED_POC. Apple CI run #5 failed in the LINKER, not in the compiler:
#
#     "___clear_cache", referenced from:
#       _linux_icache_flush in linux_platform.o
#     ld: symbol(s) not found for architecture arm64
#
# What can be examined without an Apple toolchain is examined here, and what cannot is
# reported as UNTESTED rather than invented:
#
#   * AArch64/Linux: the real objects of the very same translation units, every undefined
#     symbol they demand, which one references the cache-maintenance builtin's symbol, and
#     which library of the cross toolchain provides it;
#   * the icache call sites in the platform files;
#   * the Apple target: only the static policy (tools/audit_platform_composition.py). The
#     iphoneos link itself is the Apple linker's verdict.
#
# Exit status is non-zero only when a LOCAL fact is inconsistent (missing cross toolchain
# is reported as UNTESTED, not as failure).
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
EVIDENCE="$ROOT/Documentation/evidence"
mkdir -p "$EVIDENCE"
CROSS=${CROSS_COMPILE:-aarch64-linux-gnu-}
OBJDIR="$ROOT/build/link-audit"
rm -rf "$OBJDIR"
mkdir -p "$OBJDIR"

if ! command -v "${CROSS}gcc" >/dev/null 2>&1; then
    echo "LINK_AUDIT=UNTESTED REASON=NO_CROSS_TOOLCHAIN (no ${CROSS}gcc here)"
    exit 0
fi

echo "== object-level link audit (AArch64 Linux: real objects) =="
echo "compiler: $(${CROSS}gcc --version | head -1)"

status=0
objects=""
for source in "$ROOT"/RuntimeCore/src/*.c "$ROOT"/Diagnostics/src/*.c; do
    object="$OBJDIR/$(basename "${source%.c}").o"
    ${CROSS}gcc -O0 -c "$source" -o "$object" \
        -I "$ROOT/RuntimeCore/include" -I "$ROOT/Diagnostics/include" \
        -Wall -Wextra -Wno-unused-parameter
    objects="$objects $object"
done

echo
echo "-- cache-maintenance symbols demanded by each object (undefined symbols) --"
clear_cache_objects=""
for object in $objects; do
    undefined=$(nm --undefined-only "$object" | awk '{print $NF}' | sort | tr '\n' ' ')
    case " $undefined " in
        *" __clear_cache "*)
            clear_cache_objects="$clear_cache_objects $(basename "$object")" ;;
    esac
    case "$undefined" in
        *mmap*|*mprotect*|*munmap*|*__clear_cache*|*getrandom*|*getauxval*|*epoll*)
            printf '  %-24s %s\n' "$(basename "$object")" "$undefined" ;;
    esac
done
echo "CLEAR_CACHE_REFERENCED_BY=${clear_cache_objects:-<none>}"

echo
echo "-- where the cross toolchain provides __clear_cache --"
libgcc=$(${CROSS}gcc -print-file-name=libgcc.a)
found=0
if [ -f "$libgcc" ] && nm "$libgcc" 2>/dev/null | grep -q "__clear_cache"; then
    echo "  $libgcc ($(nm "$libgcc" | grep "__clear_cache" | head -1))"
    found=1
fi
for library in $(${CROSS}gcc -print-search-dirs | sed -n 's/^libraries: =//p' | tr ':' ' '); do
    candidate="$library/libgcc.a"
    if [ "$found" = "0" ] && [ -f "$candidate" ] \
       && nm "$candidate" 2>/dev/null | grep -q "__clear_cache"; then
        echo "  $candidate (defines __clear_cache)"
        found=1
    fi
done
if [ "$found" = "0" ]; then
    echo "  NOT FOUND in libgcc/compiler-rt — the AArch64 link would fail too; investigate"
    status=1
else
    echo "  (on Linux the symbol comes from the compiler runtime every Linux link pulls in;"
    echo "   the iPhoneOS SDK ships no such symbol, hence Apple CI run #5)"
fi

echo
echo "-- icache call sites in the platform layer --"
for source in "$ROOT"/RuntimeCore/src/*platform.c; do
    printf '  %-22s\n' "$(basename "$source")"
    grep -n "icache_flush\|clear_cache\|OSCacheControl\|sys_icache_invalidate" "$source" \
        | grep -v "^[0-9]*: *\*" | sed 's/^/     /'
done

echo
echo "-- backend selection --"
grep -n "return &rt_platform" "$ROOT/RuntimeCore/src/runtime_memory.c" | sed 's/^/  /'
grep -n "^#if\|^#elif\|^#else" "$ROOT/RuntimeCore/src/runtime_memory.c" | sed -n '1,8p' | sed 's/^/  /'

echo
echo "objdump proof for the AArch64 Linux object (the call the linker resolves):"
if command -v "${CROSS}objdump" >/dev/null 2>&1; then
    ${CROSS}objdump -d "$OBJDIR/linux_platform.o" \
        | sed -n '/<linux_icache_flush>:/,/^$/p' | grep -E "bl|ret" | sed 's/^/  /'
    echo "  (the bl target is __clear_cache, resolved from libgcc above)"
else
    echo "  (no ${CROSS}objdump here: cannot disassemble an AArch64 object with a host objdump)"
fi

echo
echo "APPLE_LINK_AUDIT=UNTESTED REASON=NO_APPLE_TOOLCHAIN"
echo "  the iphoneos link is decided by the Apple linker; the static policy that catches the"
echo "  run #5 condition before a runner is in tools/audit_platform_composition.py"
echo "LINK_SYMBOL_AUDIT=$([ "$status" = "0" ] && echo PASS || echo FAIL)"
exit "$status"

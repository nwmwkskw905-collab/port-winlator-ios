#!/usr/bin/env python3
"""audit_apple_apis.py — do we call Apple APIs that exist on macOS but are marked
unavailable by the iPhoneOS SDK?

PHASE_02_RECONSTRUCTED_POC. Written after Apple CI run #3 failed with:

    ios/RuntimeCore/src/darwin_platform.c:165:5: error: 'pthread_jit_write_protect_np'
    is unavailable: not available on iOS
    iPhoneOS17.5.sdk/usr/include/pthread.h: '... has been explicitly marked unavailable here'

The include audit (tools/audit_apple_includes.py) prevents "header not found" one at a
time. This audit prevents its sibling: a *symbol* that compiles on macOS and is refused
by the iOS SDK's availability annotations.

Rule enforced
-------------
A reference to a known macOS-only symbol must sit inside a preprocessor branch that
excludes iOS by TARGET, not by `__APPLE__` (which is true on iOS as well). Approved
guards: RT_APPLE_HAS_JIT_WRITE_PROTECT, TARGET_OS_OSX, !TARGET_OS_IPHONE,
!TARGET_OS_SIMULATOR.

The compiler remains the authority: this is a static pre-check that fails in seconds,
with file and line, instead of costing a CI cycle.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

APPLE_SOURCES = (
    sorted((ROOT / "RuntimeCore" / "src").glob("*.c"))
    + sorted((ROOT / "RuntimeCore" / "include").glob("*.h"))
    + sorted((ROOT / "Diagnostics" / "src").glob("*.c"))
    + sorted((ROOT / "Diagnostics" / "include").glob("*.h"))
    + sorted((ROOT / "RuntimePoC").glob("*.m")) + sorted((ROOT / "RuntimePoC").glob("*.h"))
)

# Symbols that compile on macOS and are unavailable (or absent) on iOS.
MACOS_ONLY_SYMBOLS = {
    "pthread_jit_write_protect_np":
        "macOS 11+ only; the iPhoneOS SDK marks it unavailable (observed in CI run #3)",
    "pthread_jit_write_protect_supported_np":
        "macOS only, same family as the call above",
    "proc_pidinfo": "libproc.h — macOS only, header absent from the iOS SDK",
    "proc_pidpath": "libproc.h — macOS only",
    "proc_name": "libproc.h — macOS only",
    "proc_listpids": "libproc.h — macOS only",
    "proc_pid_rusage": "libproc.h — macOS only",
    "setiopolicy_np": "macOS only (sys/resource.h)",
}

# Guards that genuinely exclude iOS from the branch.
APPROVED_GUARD_TOKENS = (
    "RT_APPLE_HAS_JIT_WRITE_PROTECT",   # this project's target-derived gate
    "TARGET_OS_OSX",                    # TargetConditionals.h
    "!TARGET_OS_IPHONE",
    "!TARGET_OS_SIMULATOR",
    "!defined(__APPLE__)",              # the whole Apple path is off, so iOS is too
)

CONDITION_PATTERN = re.compile(r"^\s*#\s*(if|ifdef|ifndef|elif)\s*(.*)$")


def strip_literals(line: str) -> str:
    """Remove comments and string/char literals so that mentioning an API *by name in a
    message* is not mistaken for calling it. Approximation, documented as such: enough
    for a pre-check, and the compiler remains the authority."""
    out: list[str] = []
    index = 0
    length = len(line)
    while index < length:
        char = line[index]
        if char == "/" and index + 1 < length and line[index + 1] == "/":
            break
        if char == "/" and index + 1 < length and line[index + 1] == "*":
            end = line.find("*/", index + 2)
            if end == -1:
                break
            index = end + 2
            continue
        if char in ("\"", "'"):
            quote = char
            index += 1
            while index < length:
                if line[index] == "\\":
                    index += 2
                    continue
                if line[index] == quote:
                    index += 1
                    break
                index += 1
            out.append(" ")
            continue
        out.append(char)
        index += 1
    return "".join(out)

# Apple APIs this project deliberately uses, with why each one is safe on both targets.
AVAILABILITY_NOTES = [
    ("pthread_jit_write_protect_np", "macOS only  — call and capability gated by RT_APPLE_HAS_JIT_WRITE_PROTECT"),
    ("MAP_JIT", "both SDKs define it — gated at RUNTIME by entitlements, so it stays a probe"),
    ("sys_icache_invalidate", "libkern/OSCacheControl.h — available on macOS and iOS"),
    ("arc4random_buf", "stdlib.h — available on macOS and iOS"),
    ("shm_open / shm_unlink", "POSIX — available on both, subject to the iOS sandbox"),
    ("kqueue / kevent", "sys/event.h — available on both"),
    ("mmap/mprotect/munmap", "POSIX — available on both"),
    ("sigaction/sigsetjmp", "POSIX — available on both"),
]


def guard_excludes_ios(expression: str) -> bool:
    text = expression.strip()
    return any(token in text for token in APPROVED_GUARD_TOKENS)


def scan(path: Path) -> list[tuple[int, str, int]]:
    """Return (line, symbol, occurrences) for unguarded references."""
    findings: list[tuple[int, str, int]] = []
    stack: list[bool] = []  # True = this branch excludes iOS
    for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        stripped = line.strip()
        condition = CONDITION_PATTERN.match(line)
        if condition is not None:
            stack.append(guard_excludes_ios(condition.group(2)))
            continue
        if stripped.startswith("#else") and stack:
            stack[-1] = not stack[-1]
            continue
        if stripped.startswith("#endif") and stack:
            stack.pop()
            continue
        if stripped.startswith("//") or stripped.startswith("*") or stripped.startswith("/*"):
            continue
        code = strip_literals(line)   # a message may NAME the API; only code CALLS it
        for symbol in MACOS_ONLY_SYMBOLS:
            if re.search(r"\b" + re.escape(symbol) + r"\b", code) is None:
                continue
            if not any(stack):
                findings.append((number, symbol, code.count(symbol)))
    return findings


def main() -> int:
    if not APPLE_SOURCES:
        print("audit_apple_apis: no sources found", file=sys.stderr)
        return 2

    print("== Apple API audit (macOS-only symbols) ==")
    print(f"files scanned          : {len(APPLE_SOURCES)}")
    print(f"macOS-only symbols     : {len(MACOS_ONLY_SYMBOLS)}")
    print(f"guards accepted as safe: {', '.join(APPROVED_GUARD_TOKENS)}")
    print()

    problems = 0
    for source in APPLE_SOURCES:
        for number, symbol, _count in scan(source):
            problems += 1
            print(f"{source.relative_to(ROOT)!s:<46} {number:<6} {symbol}")
            print(f"{'':<46} {'':<6} -> {MACOS_ONLY_SYMBOLS[symbol]}")

    if problems == 0:
        print("UNGUARDED_MACOS_ONLY_APIS=0 "
              "(every macOS-only symbol sits behind a target-aware guard)")
    else:
        print(f"UNGUARDED_MACOS_ONLY_APIS={problems}")

    print()
    print("Apple APIs this project uses, and why each is safe for the target:")
    for api, note in AVAILABILITY_NOTES:
        print(f"  {api:<32} {note}")
    print()
    print("note: static pre-check only — clang's availability annotations remain the authority.")
    return 1 if problems else 0


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
"""audit_apple_includes.py — will the Apple SDK find every header we include?

PHASE_02_RECONSTRUCTED_POC. Written after the second Apple CI run failed with:

    ios/RuntimeCore/src/runtime_dual_mapping.c:31:10: fatal error:
    'sys/random.h' file not found

Cause: an `#include <sys/random.h>` guarded by `#if defined(__APPLE__)` — i.e. the
Linux/glibc header pulled in exactly where it does not exist, with no use of it in
the file at all. The compiler is the only authority on what an SDK ships; this audit
is a cheap pre-check that runs everywhere so the same class of mistake does not cost
another CI cycle.

Rule: for every source that is compiled into the Apple target, report system includes
that the iphoneos SDK is KNOWN not to provide (Linux-only headers, or headers already
observed missing) when they are not protected by a platform guard.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# Files that the Xcode target compiles.
APPLE_SOURCES = (
    sorted((ROOT / "RuntimeCore" / "src").glob("*.c"))
    + sorted((ROOT / "Diagnostics" / "src").glob("*.c"))
    + sorted((ROOT / "RuntimePoC").glob("*.m"))
    + sorted((ROOT / "RuntimePoC").glob("*.swift"))
)

# Headers the iphoneos SDK does not ship. Kept deliberately short and certain:
# every entry here is either Linux-only by definition or already observed missing.
ABSENT_ON_APPLE = {
    "sys/random.h": "Linux/glibc (getrandom); observed missing in the second Apple CI run",
    "sys/epoll.h": "Linux only",
    "sys/eventfd.h": "Linux only",
    "sys/auxv.h": "Linux/glibc (getauxval)",
    "sys/prctl.h": "Linux only",
    "sys/sendfile.h": "Linux only",
    "asm/hwcap.h": "Linux/AArch64 only",
    "elf.h": "Linux only",
    "linux/": "Linux only (whole subtree)",
}

# A guard counts as protecting Apple when Apple is excluded from the guarded text.
GUARD_PATTERN = re.compile(r"^\s*#\s*(if|ifdef|elif)\s*(.*)$")
INCLUDE_PATTERN = re.compile(r"^\s*#\s*include\s+[<\"]([^>\"]+)[>\"]")


def guard_excludes_apple(expression: str) -> bool:
    text = expression.strip()
    if not text:
        return False
    # `!defined(__APPLE__)`, `defined(__linux__)`, `__linux__`, `__aarch64__`...
    if "!defined(__APPLE__)" in text or "!__APPLE__" in text:
        return True
    if "__linux__" in text and "!defined(__linux__)" not in text:
        return True
    return False


def audit(path: Path) -> list[tuple[int, str, str]]:
    findings: list[tuple[int, str, str]] = []
    stack: list[bool] = []  # True = Apple excluded inside this block
    for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        guard = GUARD_PATTERN.match(line)
        if guard is not None:
            stack.append(guard_excludes_apple(guard.group(2)))
            continue
        stripped = line.strip()
        if stripped.startswith("#else") and stack:
            stack[-1] = not stack[-1]
            continue
        if stripped.startswith("#endif") and stack:
            stack.pop()
            continue
        include = INCLUDE_PATTERN.match(line)
        if include is None:
            continue
        header = include.group(1)
        if header not in ABSENT_ON_APPLE:
            continue
        protected_by_apple = any(stack)
        if not protected_by_apple:
            findings.append((number, header, ABSENT_ON_APPLE[header]))
    return findings


def main() -> int:
    if not APPLE_SOURCES:
        print("audit: no sources found", file=sys.stderr)
        return 2

    problems = 0
    print("== Apple include audit ==")
    print(f"files compiled into the Apple target: {len(APPLE_SOURCES)}")
    print(f"headers known absent on iphoneos  : {len(ABSENT_ON_APPLE)}")
    print(f"{'file':<44} {'line':<6} header")
    for source in APPLE_SOURCES:
        for number, header, reason in audit(source):
            problems += 1
            print(f"{source.relative_to(ROOT)!s:<44} {number:<6} {header}  <- {reason}")
    if problems == 0:
        print("UNGUARDED_LINUX_INCLUDES=0 (no Linux-only header reaches the Apple build)")
        print("note: this is a static pre-check; the compiler remains the authority.")
    else:
        print(f"UGUARDED_LINUX_INCLUDES={problems}")
    return 1 if problems else 0


if __name__ == "__main__":
    raise SystemExit(main())

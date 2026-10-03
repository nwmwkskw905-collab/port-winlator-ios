#!/usr/bin/env python3
"""audit_interfaces.py — is every function we call actually declared where we call it?

PHASE_02_RECONSTRUCTED_POC. Written after Apple CI run #4 failed with:

    ios/RuntimePoC/Phase02Bridge.m:75:69: error: call to undeclared function 'rt_jit_isa';
    ISO C99 and later do not support implicit function declarations
    warning: format specifies type 'char *' but the argument has type 'int'

The declaration existed — in a header the file never included. The compiler is the only
authority, but this check finds the same class of defect in seconds, on any host:

  1. implicit declarations — a project function (rt_*, phase02_*, Phase02*) is called in
     a file where no reachable project header declares it, and it is not defined earlier
     in that file;
  2. layering — app-layer files (RuntimePoC/*.m, the bridging header) must talk only to
     the Diagnostics API; including RuntimeCore headers there is how run #4 happened;
  3. symbols defined but never declared in a header (a definition nothing can call
     without a prototype).

The header search is the same one the Xcode target uses (HEADER_SEARCH_PATHS in
tools/generate_xcodeproj.py), so a file that passes here is compiling the same way.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# Same include directories as the generated project.
HEADER_DIRS = [ROOT / "RuntimeCore" / "include", ROOT / "Diagnostics" / "include"]

APP_LAYER = [ROOT / "RuntimePoC" / "Phase02Bridge.m", ROOT / "RuntimePoC" / "Phase02-Bridging-Header.h"]

COMPILED_SOURCES = (
    sorted((ROOT / "RuntimeCore" / "src").glob("*.c"))
    + sorted((ROOT / "Diagnostics" / "src").glob("*.c"))
    + sorted((ROOT / "RuntimePoC").glob("*.m"))
    + sorted((ROOT / "RuntimePoC").glob("*.c"))
)

# Identifiers we own; anything else is assumed to come from a system header.
OWN_NAMESPACE = re.compile(r"^(rt_|phase02_|Phase02)")

INCLUDE_PATTERN = re.compile(r'^\s*#\s*(?:include|import)\s+"([^"]+)"', re.M)
PROTOTYPE_PATTERN = re.compile(
    r"^[A-Za-z_][A-Za-z0-9_ \t*]*?\b([A-Za-z_][A-Za-z0-9_]*)\s*\([^;{]*\)\s*;",
    re.M,
)
DEFINITION_PATTERN = re.compile(
    r"^(?:static\s+)?[A-Za-z_][A-Za-z0-9_ \t*]*?\b([A-Za-z_][A-Za-z0-9_]*)\s*\([^;]*\)\s*\{",
    re.M,
)
CALL_PATTERN = re.compile(r"\b([A-Za-z_][A-Za-z0-9_]*)\s*\(")

# Words that look like calls but are not.
NOT_CALLS = {
    "if", "for", "while", "switch", "return", "sizeof", "do", "else", "case", "defined",
    "typeof", "__typeof__", "alignof", "_Alignof", "static_assert", "_Static_assert",
    "va_arg", "va_start", "va_end", "va_copy",
}

RESULT_COLOR = {True: "ok  ", False: "FAIL"}


def strip_noise(text: str) -> str:
    """Remove comments and literals so call detection is not fooled by prose."""
    out = []
    i = 0
    n = len(text)
    while i < n:
        c = text[i]
        if c == "/" and i + 1 < n and text[i + 1] == "/":
            j = text.find("\n", i)
            i = n if j == -1 else j
            continue
        if c == "/" and i + 1 < n and text[i + 1] == "*":
            j = text.find("*/", i + 2)
            i = n if j == -1 else j + 2
            continue
        if c in ("'", '"'):
            j = i + 1
            while j < n:
                if text[j] == "\\":
                    j += 2
                    continue
                if text[j] == c:
                    j += 1
                    break
                j += 1
            out.append(" ")
            i = j
            continue
        out.append(c)
        i += 1
    return "".join(out)


def resolve_header(name: str) -> Path | None:
    for directory in HEADER_DIRS:
        candidate = directory / name
        if candidate.is_file():
            return candidate
    return None


def reachable_headers(path: Path, seen: set | None = None) -> set[Path]:
    """Project headers reachable from `path`, following project-to-project includes."""
    seen = set() if seen is None else seen
    text = path.read_text(encoding="utf-8", errors="replace")
    for match in INCLUDE_PATTERN.finditer(text):
        header = resolve_header(match.group(1))
        if header is None or header in seen:
            continue
        seen.add(header)
        reachable_headers(header, seen)
    return seen


def declarations_in(headers: set[Path]) -> set[str]:
    names: set[str] = set()
    for header in headers:
        text = strip_noise(header.read_text(encoding="utf-8", errors="replace"))
        names.update(PROTOTYPE_PATTERN.findall(text))
        # function-like macros that take arguments also make a name "declared"
        names.update(re.findall(r"^#\s*define\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(", text, re.M))
    return names


def check_source(path: Path) -> list[str]:
    problems: list[str] = []
    raw = path.read_text(encoding="utf-8", errors="replace")
    text = strip_noise(raw)

    declared = declarations_in(reachable_headers(path))

    # definitions earlier in the file count as declarations for later calls
    defined: set[str] = set()
    definition_positions = {match.group(1): match.start() for match in DEFINITION_PATTERN.finditer(text)}
    definition_positions.update(
        {match.group(1): match.start() for match in PROTOTYPE_PATTERN.finditer(text)}
    )

    for call in CALL_PATTERN.finditer(text):
        name = call.group(1)
        if not OWN_NAMESPACE.match(name) or name in NOT_CALLS or name in declared:
            continue
        position = definition_positions.get(name)
        if position is not None and position < call.start():
            continue  # defined above the call: nothing implicit
        if position is not None:
            problems.append(f"{path.relative_to(ROOT)}:{text.count(chr(10), 0, call.start()) + 1}: "
                            f"'{name}' is called before any visible declaration")
        else:
            problems.append(f"{path.relative_to(ROOT)}:{text.count(chr(10), 0, call.start()) + 1}: "
                            f"'{name}' has no visible declaration (include the header that declares it)")
    return problems


def check_layering() -> list[str]:
    problems: list[str] = []
    for path in APP_LAYER:
        if not path.is_file():
            continue
        for match in INCLUDE_PATTERN.finditer(path.read_text(encoding="utf-8", errors="replace")):
            header = resolve_header(match.group(1))
            if header is None:
                continue
            if header.parent == ROOT / "RuntimeCore" / "include":
                problems.append(
                    f"{path.relative_to(ROOT)}: includes RuntimeCore header "
                    f"'{match.group(1)}' — the app layer must consume the Diagnostics API "
                    f"(phase02_harness.h / phase02_log.h) instead"
                )
    return problems


def check_definitions_declared() -> list[str]:
    """Every non-static function defined in a .c/.m should be declared in some header,
    otherwise callers in other files cannot use it without a prototype."""
    problems: list[str] = []
    all_headers = set()
    for directory in HEADER_DIRS:
        all_headers.update(directory.glob("*.h"))
    declared = declarations_in(all_headers)

    for path in COMPILED_SOURCES:
        text = strip_noise(path.read_text(encoding="utf-8", errors="replace"))
        for match in re.finditer(r"^([A-Za-z_][A-Za-z0-9_ \t*]*?\b([A-Za-z_][A-Za-z0-9_]*)\s*\([^;{]*\))\s*\{",
                                 text, re.M):
            signature, name = match.group(1), match.group(2)
            if signature.strip().startswith("static") or name in declared:
                continue
            if name.startswith("main"):
                continue
            line = text.count("\n", 0, match.start()) + 1
            problems.append(f"{path.relative_to(ROOT)}:{line}: '{name}' is defined but not "
                            f"declared in any header (unusable from another file)")
    return problems


def main() -> int:
    print("== interface audit ==")
    print(f"compiled sources : {len(COMPILED_SOURCES)}")
    print(f"header directories: {', '.join(str(d.relative_to(ROOT)) for d in HEADER_DIRS)}")
    print(f"app-layer files   : {len(APP_LAYER)}")
    print()

    failures = 0
    for label, problems in (
        ("implicit declarations", [p for source in COMPILED_SOURCES for p in check_source(source)]),
        ("layer violations", check_layering()),
        ("definitions without a declaration", check_definitions_declared()),
    ):
        print(f"--- {label}")
        if problems:
            failures += len(problems)
            for problem in problems:
                print(f"FAIL {problem}")
        else:
            print("ok   none")
    print()
    if failures:
        print(f"INTERFACE_AUDIT=FAIL ({failures} problems)")
    else:
        print("INTERFACE_AUDIT=0 (every project call has a visible declaration; "
              "app layer uses the Diagnostics API only)")
    print("note: static pre-check — clang remains the authority.")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())

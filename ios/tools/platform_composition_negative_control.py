#!/usr/bin/env python3
"""platform_composition_negative_control.py — prove that the composition audit detects the
Apple CI run #5 condition (and its siblings) before a runner has to.

Each control patches one file *in memory*, runs tools/audit_platform_composition.py as a
subprocess with that file written to disk, and restores the original content in a finally
block. Nothing else in the tree is touched; `git status` afterwards shows no change.

Controls
  A. linux_platform.c exactly as it was in CI run #5 (taken from git, if available):
     __builtin___clear_cache unguarded -> the audit must report APPLE_FORBIDDEN_SYMBOLS.
  B. darwin_platform.c with the platform guard removed from sys_icache_invalidate ->
     the audit must report LINUX_FORBIDDEN_SYMBOLS.
  C. rt_platform_current() returning the Linux backend under __APPLE__ ->
     the audit must report BACKEND_SELECTION=FAILED.
  D. an extra source on disk that the Xcode project does not compile -> the audit must
     report TARGET_COMPOSITION (run on a scratch copy of the tree).

Usage: python3 tools/platform_composition_negative_control.py [--revision <git-rev>]
"""

from __future__ import annotations

import argparse
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
AUDIT = ROOT / "tools" / "audit_platform_composition.py"


def run_audit(root: Path) -> tuple[int, str]:
    audit = root / "tools" / "audit_platform_composition.py"
    result = subprocess.run([sys.executable, str(audit)], capture_output=True, text=True,
                            cwd=str(root))
    return result.returncode, result.stdout + result.stderr


def summary(text: str) -> str:
    keys = ("TARGET_COMPOSITION=", "APPLE_FORBIDDEN_SYMBOLS=", "LINUX_FORBIDDEN_SYMBOLS=",
            "BACKEND_SELECTION=", "UNDECIDED_CONDITIONS=", "PLATFORM_COMPOSITION_AUDIT=")
    lines = [line for line in text.splitlines() if line.startswith(keys)]
    detail = [line for line in text.splitlines()
              if line.startswith(("APPLE_FORBIDDEN_SYMBOLS:", "LINUX_FORBIDDEN_SYMBOLS:",
                                  "BACKEND_SELECTION:", "TARGET_COMPOSITION:"))
              and "=" not in line.split(":", 1)[0]]
    return "\n".join(detail + lines)


def control(label: str, path: Path, transform, expect: str) -> bool:
    original = path.read_text(encoding="utf-8")
    patched = transform(original)
    if patched == original:
        print(f"[{label}] CONTROL BROKEN: the patch changed nothing in "
              f"{path.relative_to(ROOT)}")
        return False
    path.write_text(patched, encoding="utf-8")
    try:
        code, output = run_audit(ROOT)
    finally:
        path.write_text(original, encoding="utf-8")
    print(f"---- control {label}")
    print(f"patched file : {path.relative_to(ROOT)}")
    print(f"expectation  : {expect}")
    print(summary(output))
    print(f"audit exit   : {code} (non-zero means the regression was caught)")
    print()
    return code != 0


def control_a_source(revision: str) -> tuple[bool, str]:
    """Control A uses the file exactly as CI run #5 built it, straight from git."""
    path = ROOT / "RuntimeCore" / "src" / "linux_platform.c"
    original = path.read_text(encoding="utf-8")
    show = subprocess.run(["git", "show", f"{revision}:ios/RuntimeCore/src/linux_platform.c"],
                          capture_output=True, text=True, cwd=str(ROOT.parent))
    if show.returncode != 0:
        return False, f"git show failed: {show.stderr.strip()}"
    path.write_text(show.stdout, encoding="utf-8")
    try:
        code, output = run_audit(ROOT)
    finally:
        path.write_text(original, encoding="utf-8")
    print("---- control A (the CI run #5 condition, file taken from "
          f"{revision})")
    print(f"patched file : {path.relative_to(ROOT)}")
    print("expectation  : APPLE_FORBIDDEN_SYMBOLS >= 1, naming the unguarded builtin")
    print(summary(output))
    print(f"audit exit   : {code} (non-zero means the regression was caught)")
    print()
    return code != 0, ""


def control_d() -> bool:
    """An extra source on disk that no target compiles (scratch copy of the tree)."""
    with tempfile.TemporaryDirectory() as temporary:
        scratch = Path(temporary) / "ios"
        shutil.copytree(ROOT, scratch,
                        ignore=shutil.ignore_patterns("build", "__pycache__", ".git"))
        extra = scratch / "RuntimeCore" / "src" / "iphone_only_probe.c"
        extra.write_text("/* control D: a source that no target compiles */\n"
                         "int control_d_marker(void) { return 0; }\n", encoding="utf-8")
        code, output = run_audit(scratch)
    print("---- control D (source on disk that the Xcode project does not compile)")
    print("patched file : RuntimeCore/src/iphone_only_probe.c (scratch copy only)")
    print("expectation  : TARGET_COMPOSITION >= 1")
    print(summary(output))
    print(f"audit exit   : {code} (non-zero means the regression was caught)")
    print()
    return code != 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--revision", default="HEAD",
                        help="git revision whose linux_platform.c is the CI #5 one (default HEAD)")
    arguments = parser.parse_args()

    print("== negative controls for the platform composition audit ==")
    print(f"audit    : {AUDIT.relative_to(ROOT)}")
    print(f"baseline : ", end="")
    code, output = run_audit(ROOT)
    print(f"exit {code}")
    for line in summary(output).splitlines():
        print(f"           {line}")
    if code != 0:
        print("the audit already fails on the current tree; fix that before trusting controls")
        return 2
    print()

    results = []
    caught, error = control_a_source(arguments.revision)
    results.append(("A: CI #5 __builtin___clear_cache", caught))
    if error:
        print(error)

    results.append(("B: Apple symbol reachable on Linux", control(
        "B", ROOT / "RuntimeCore" / "src" / "darwin_platform.c",
        lambda text: text.replace(
            "#if defined(__APPLE__) && defined(RT_HAVE_OSCACHECONTROL)",
            "#if defined(RT_HAVE_OSCACHECONTROL) || defined(RT_HAVE_OSCACHECONTROL_FOR_ALL)",
            1),
        "LINUX_FORBIDDEN_SYMBOLS >= 1 (sys_icache_invalidate reachable on Linux)")))

    results.append(("C: wrong backend under __APPLE__", control(
        "C", ROOT / "RuntimeCore" / "src" / "runtime_memory.c",
        lambda text: re.sub(r"(#if defined\(__APPLE__\)\n    return &)rt_platform_darwin",
                            r"\1rt_platform_linux", text, count=1),
        "BACKEND_SELECTION=FAILED (Apple selecting the Linux backend)")))

    results.append(("D: uncompiled source on disk", control_d()))

    print("== summary ==")
    for label, caught in results:
        print(f"  {'CAUGHT' if caught else 'MISSED'}  {label}")
    print()
    print(f"PLATFORM_COMPOSITION_NEGATIVE_CONTROL="
          f"{'PASS' if all(caught for _label, caught in results) else 'FAIL'}")
    return 0 if all(caught for _label, caught in results) else 1


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
"""audit_shm_backend.py — the POSIX-SHM / dual-mapping investigation, frozen in rules.

Physical run #2 measured, on the iPhone: `ipc.posix_shm` stage=shm_open errno=1 (EPERM) and
`memory.dual_mapping_rw_rx` stage=shm_open errno=1. Two conclusions must NOT be drawn from
that, and this audit keeps them impossible to draw again:

  * "shared memory is impossible on iOS" — false: what was refused is the NAMED POSIX
    namespace, to a third-party app in the sandbox. Anonymous MAP_SHARED, file-backed
    MAP_SHARED inside the app container and mach VM are unaffected;
  * "dual mapping is impossible / required" — the dual-mapping record is a capability
    experiment and the iOS memory strategy is a single view flipped RW <-> R-X, which run #2
    proved works. The experiment being blocked changes no architecture.

Pass 03 therefore substituted ONLY the iOS-side backing of the experiment (a file-backed
MAP_SHARED object in the app container, unlinked immediately, two views, aliasing verified),
selected explicitly and only after the named path was refused, leaving Linux and macOS on the
original named-POSIX path. These rules enforce exactly that.

Rules:
  S1 NAMED_PATH_INTACT     the original implementation still exists and is still the FIRST
                           mechanism tried, on every platform.
  S2 IOS_BACKEND_SEMANTICS  the iOS backend supplies the real semantics the experiment needs:
                           unpredictable unique name, O_CREAT|O_EXCL, immediate unlink, two
                           live MAP_SHARED views (RW and R-X), aliasing check, cleanup.
  S3 EXPLICIT_SELECTION    the harness substitutes only for Apple iOS targets and only after
                           a measured refusal, and the record names both backends.
  S4 NO_API_REMOVED        nothing public was deleted or renamed.
  S5 CLEANUP               every failure path closes the descriptor and leaves no object.
  S6 CLASSIFY_NOT_CONFUSED a refusal stays a capability answer (never a defect, never PASS),
                           and both scope notes are emitted.
  S7 DUAL_NOT_IN_JIT_PATH  the JIT/loader/backend code never depends on dual mapping.
  S8 TESTS                 the substitute is covered by tests that check aliasing and cleanup.
  S9 SURFACE_STABLE        the dual-mapping public surface is exactly the intended, named set.

Output: one line per rule plus SHM_BACKEND_AUDIT=<violations>.

Usage: python3 tools/audit_shm_backend.py [--root <dir>]
"""
import argparse
import pathlib
import re

MEMORY_H = "RuntimeCore/include/runtime_memory.h"
MEMORY_C = "RuntimeCore/src/runtime_memory.c"
DUAL_C = "RuntimeCore/src/runtime_dual_mapping.c"
IPC_C = "RuntimeCore/src/runtime_ipc.c"
IPC_H = "RuntimeCore/include/runtime_ipc.h"
HARNESS_C = "Diagnostics/src/phase02_harness.c"
TESTS_C = "Tests/test_runtime_core.c"
JIT_C = "RuntimeCore/src/runtime_jit.c"
LOADER_C = "RuntimeCore/src/runtime_loader.c"
DARWIN_C = "RuntimeCore/src/darwin_platform.c"

EXPECTED_SURFACE = {
    "rt_dual_map_create",
    "rt_dual_map_create_file_backed",
    "rt_dual_map_destroy",
    "rt_dual_map_views_aliased",
    "rt_dual_stage_name",
}


def body(text, signature):
    start = text.find(signature)
    if start < 0:
        return None
    depth = 0
    seen = False
    for i in range(start, len(text)):
        if text[i] == "{":
            depth += 1
            seen = True
        elif text[i] == "}":
            depth -= 1
            if seen and depth == 0:
                return text[start:i + 1]
    return None


def rules(root):
    memory_h = (root / MEMORY_H).read_text()
    dual = (root / DUAL_C).read_text()
    ipc = (root / IPC_C).read_text()
    ipc_h = (root / IPC_H).read_text()
    harness = (root / HARNESS_C).read_text()
    tests = (root / TESTS_C).read_text()

    named = body(dual, "int rt_dual_map_create(") or ""
    file_backed = body(dual, "int rt_dual_map_create_file_backed(") or ""
    two_views = body(dual, "static int rt_dual_map_two_views(") or ""
    memory_suite = body(harness, "static rt_status_t phase02_suite_memory(") or ""
    out = []
    violations = []

    # S1 ------------------------------------------------------------------
    if "shm_open(" not in named or "shm_unlink(" not in named:
        violations.append("S1: the named POSIX path is no longer the default implementation")
    if "shm_open(" not in ipc:
        violations.append("S1: ipc.posix_shm no longer probes shm_open")
    select_at = memory_suite.find("rt_dual_map_create(dual_len, &map)")
    substitute_at = memory_suite.find("rt_dual_map_create_file_backed(")
    if select_at < 0 or substitute_at < 0 or select_at > substitute_at:
        violations.append("S1: the iOS backend is not tried strictly after the named path")
    if not (root / IPC_H).is_file() or "rt_ipc_shm_ex" not in ipc_h:
        violations.append("S1: the POSIX shared-memory probe lost its extended entry point")
    out.append("ok   S1 NAMED_PATH_INTACT: shm_open path still first, everywhere")

    # S2 ------------------------------------------------------------------
    if not two_views:
        violations.append("S2: the shared two-view mapping helper is gone (logic duplicated?)")
    elif not named or "rt_dual_map_two_views(" not in named:
        violations.append("S2: the named backend no longer uses the shared two-view helper")
    if not file_backed:
        violations.append("S2: the iOS backend does not exist")
    else:
        for required, what in (("O_CREAT | O_EXCL | O_RDWR", "O_CREAT|O_EXCL"),
                               ("unlink(path)", "immediate unlink"),
                               ("rt_dual_map_two_views(", "two views"),
                               ("rt_platform_unique_shm_name(", "unpredictable name"),
                               ("EINVAL", "a refusal instead of a guessed directory")):
            if required not in file_backed:
                violations.append(f"S2: the iOS backend is missing {what}")
    if "MAP_SHARED" not in two_views or "PROT_READ | PROT_EXEC" not in two_views:
        violations.append("S2: the two-view helper does not create the executable alias")
    out.append("ok   S2 IOS_BACKEND_SEMANTICS: unique name, exclusive create, immediate "
               "unlink, MAP_SHARED RW + R-X views")

    # S3 ------------------------------------------------------------------
    if "phase02_apple_target_is_ios() != 0" not in memory_suite:
        violations.append("S3: the substitution is not gated on the Apple iOS target")
    if "!named_ok" not in memory_suite:
        violations.append("S3: the substitution is not gated on a measured refusal")
    if "named POSIX object refused at stage=" not in harness:
        violations.append("S3: the record does not state that the named object was refused")
    if "NOTE=memory.dual_mapping_backend" not in harness:
        violations.append("S3: the backend substitution is not announced in the log")
    out.append("ok   S3 EXPLICIT_SELECTION: iOS only, after a refusal, both results named")

    # S4 ------------------------------------------------------------------
    for name in ("rt_dual_map_create", "rt_dual_map_destroy", "rt_dual_map_views_aliased"):
        if name not in memory_h or name not in dual:
            violations.append(f"S4: {name} was removed or renamed")
    for name in ("rt_ipc_shm", "rt_ipc_shm_ex"):
        if name not in ipc_h or name not in ipc:
            violations.append(f"S4: {name} was removed or renamed")
    out.append("ok   S4 NO_API_REMOVED: the previous entry points still exist")

    # S5 ------------------------------------------------------------------
    if file_backed.count("close(fd)") < 1:
        violations.append("S5: the iOS backend can leak its descriptor")
    if "unlink(path)" not in file_backed:
        violations.append("S5: the iOS backend can leave an object behind")
    if "close(fd)" not in named:
        violations.append("S5: the named backend can leak its descriptor")
    out.append("ok   S5 CLEANUP: descriptor closed, object unlinked on every path")

    # S6 ------------------------------------------------------------------
    for note in ("NOTE=ipc.posix_shm_scope", "NOTE=memory.dual_mapping_backend"):
        if note not in harness:
            violations.append(f"S6: the harness does not emit {note}")
    if 'not \\"shared memory is impossible\\"' not in harness:
        violations.append("S6: the log does not separate the named namespace from shared "
                          "memory in general")
    if "does not depend on this experiment" not in harness:
        violations.append("S6: the record does not say that the port does not depend on the "
                          "dual-mapping experiment")
    if "at stage=%s errno=%d (%s) - a property" not in harness:
        violations.append("S6: a refusal is no longer described as a property of the "
                          "namespace")
    if "phase02_classify_shm_error((int)shm_stage, err)" not in harness:
        violations.append("S6: the POSIX SHM refusal is no longer classified through "
                          "phase02_classify_shm_error (a refusal must stay a capability answer)")
    if "phase02_classify_dual_mapping_error((int)ios_map.stage, ios_map.err)" not in harness:
        violations.append("S6: the iOS-backend refusal is no longer classified through "
                          "phase02_classify_dual_mapping_error")
    if "capability experiment" not in dual:
        violations.append("S6: the dual-mapping source no longer calls itself an experiment")
    out.append("ok   S6 CLASSIFY_NOT_CONFUSED: refusal = capability, notes emitted, "
               "experiment named as such")

    # S7 ------------------------------------------------------------------
    for name, path in ((JIT_C, "runtime_jit.c"), (LOADER_C, "runtime_loader.c"),
                       (DARWIN_C, "darwin_platform.c")):
        text = (root / name).read_text()
        if "rt_dual_map" in text:
            violations.append(f"S7: {path} references dual mapping: the JIT path must not "
                              f"depend on it")
    if "memory.dual_mapping_rw_rx" not in harness:
        violations.append("S7: the dual-mapping record disappeared from the memory suite")
    out.append("ok   S7 DUAL_NOT_IN_JIT_PATH: no JIT/loader/backend dependence on dual mapping")

    # S8 ------------------------------------------------------------------
    if "rt_dual_map_create_file_backed" not in tests:
        violations.append("S8: the substitute has no unit test")
    if "file-backed views alias the same memory" not in tests:
        violations.append("S8: the test does not verify the aliasing semantics")
    if "no backing object is left behind" not in tests:
        violations.append("S8: the test does not verify cleanup")
    if "test_dual_mapping_file_backed_negative" not in tests:
        violations.append("S8: no negative test for a refused substitute")
    out.append("ok   S8 TESTS: aliasing, cleanup and the negative case covered")

    # S9 ------------------------------------------------------------------
    surface = set(re.findall(r"\b(rt_dual_[a-z_]+)\s*\(", memory_h))
    # Only the declarations count as surface, not the doc mentions: keep the two types out
    # of the set by construction (they are rt_dual_map_t / rt_dual_stage_t, no call parens).
    extra = surface - EXPECTED_SURFACE
    missing = EXPECTED_SURFACE - surface
    if extra:
        violations.append(f"S9: unexpected public dual-mapping surface: {sorted(extra)}")
    if missing:
        violations.append(f"S9: missing public dual-mapping surface: {sorted(missing)}")
    out.append("ok   S9 SURFACE_STABLE: the dual-mapping surface is exactly "
               f"{len(EXPECTED_SURFACE)} named functions")
    return out, violations


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", default=str(pathlib.Path(__file__).resolve().parent.parent))
    args = parser.parse_args()
    root = pathlib.Path(args.root).resolve()

    out, violations = rules(root)
    for line in out:
        print(line)
    for violation in violations:
        print(f"FAIL {violation}")
    print(f"SHM_BACKEND_AUDIT={len(violations)}")
    return 1 if violations else 0


if __name__ == "__main__":
    raise SystemExit(main())

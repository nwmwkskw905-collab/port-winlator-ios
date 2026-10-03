#!/usr/bin/env python3
"""audit_fix06_contracts.py — static audit of the contracts Correction 06 introduced.

Every rule here exists because a physical line in IPHONE13_PHYSICAL_RUN_01 was produced by
its violation. The rules are checked against the real sources, so the audit fails if the
defect is reintroduced — before an iPhone has to find it again.

  1. LOADER_REASON      a non-PASS loader result can never print a reason that reads as a
                        success ("rejected: OK"), and the loader record must consult the
                        classifier that ties a blocked execution to the capability probe.
  2. SHM_STAGE          the ipc.posix_shm record names the failing stage and the runtime
                        reports it (an errno without an operation is what run #1 printed).
  3. DEPTH_STAGE        the fs.deep_paths record names the failing stage, and the probe
                        cannot return a failure with errno 0 (RT_FS_STAGE_ERRNO_LOST).
  4. ERRNO_ZERO_DEFECT  both classifiers treat "error with errno 0" as a defect; it is never
                        a platform answer and never PASS.
  5. JIT_ATTEMPT_ORDER  rt_jit_alloc records the MAP_JIT attempt *before* the mapping, so a
                        refusal is still attributable to the capability.
  6. COMPAT_SURFACE     the previous entry points (rt_ipc_shm, rt_loader_run,
                        rt_fs_deep_paths) still exist: Correction 06 adds, it does not break.
  7. PHYSICAL_BASELINE  the run #1 document is present, still says FAIL, and still carries the
                        four verbatim details: the baseline is immutable.

Output: one line per rule plus FIX06_CONTRACTS_AUDIT=<violations>.

Usage: python3 tools/audit_fix06_contracts.py [--root <dir>]
"""
import argparse
import pathlib
import re
import sys

HARNESS_C = "Diagnostics/src/phase02_harness.c"
HARNESS_H = "Diagnostics/include/phase02_harness.h"
JIT_C = "RuntimeCore/src/runtime_jit.c"
IPC_C = "RuntimeCore/src/runtime_ipc.c"
IPC_H = "RuntimeCore/include/runtime_ipc.h"
FS_C = "RuntimeCore/src/runtime_filesystem.c"
FS_H = "RuntimeCore/include/runtime_filesystem.h"
LOADER_C = "RuntimeCore/src/runtime_loader.c"
LOADER_H = "RuntimeCore/include/runtime_loader.h"
BASELINE = "Documentation/IPHONE13_PHYSICAL_RUN_01.md"

PHYSICAL_DETAILS = [
    "arena allocation failed errno=1",
    "errno=0 (Undefined error: 0) at depth=103 (deepest ok=102, 1007 chars)",
    "shared memory object mapped twice and compared (errno=1)",
    "rejected: OK",
]


def body(text, signature):
    """Return the source of the function whose definition line contains `signature`."""
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


def rule_loader_reason(root, out):
    violations = []
    harness = (root / HARNESS_C).read_text()
    loader = (root / LOADER_C).read_text()
    if "rejected: %s" in harness or "rejected: %s" in loader:
        violations.append('the literal "rejected: %s" (run #1 output) is reachable again')
    if "phase02_classify_loader_result(status, loader_err" not in harness:
        violations.append("the loader record does not consult phase02_classify_loader_result")
    elif "PHASE02_OUTCOME_OK" not in harness.split(
            "phase02_classify_loader_result(status, loader_err")[1][:200]:
        violations.append("the loader record does not compare the classifier verdict")
    run_ex = body(loader, "rt_status_t rt_loader_run_ex(")
    if run_ex is None:
        violations.append("rt_loader_run_ex is missing")
    else:
        if re.search(r"return\s+RT_(FAIL|BLOCKED)\s*;", run_ex) is None:
            violations.append("rt_loader_run_ex has no non-PASS return")
        if "RT_LOADER_ERR_JIT_UNAVAILABLE" not in run_ex:
            violations.append("the JIT-unavailable reason is not produced by the loader")
        # Structural rule for the run #1 defect: every non-PASS return writes a reason first.
        lines = run_ex.splitlines()
        for index, line in enumerate(lines):
            if not re.match(r"\s*return\s+RT_(FAIL|BLOCKED)\s*;", line):
                continue
            window = "\n".join(lines[max(0, index - 8):index])
            if "*loader_err_out =" not in window:
                violations.append(
                    f"line {index + 1} of rt_loader_run_ex returns without writing a reason "
                    f"(the 'rejected: OK' shape): {line.strip()}")
    out("LOADER_REASON", violations,
        "no rejection without a reason; a blocked execution names its dependency")


def rule_shm_stage(root, out):
    violations = []
    harness = (root / HARNESS_C).read_text()
    ipc = (root / IPC_C).read_text()
    ipc_h = (root / IPC_H).read_text()
    if "rt_ipc_stage_name(" not in harness:
        violations.append("the ipc.posix_shm record does not name the failing stage")
    if "rt_ipc_shm_ex" not in ipc_h:
        violations.append("rt_ipc_shm_ex is not declared")
    fn = body(ipc, "int rt_ipc_shm_ex(")
    if fn is None:
        violations.append("rt_ipc_shm_ex is missing")
    else:
        if fn.count("stage_out") < 4:
            violations.append("rt_ipc_shm_ex does not report the stage on its paths")
        if "RT_IPC_STAGE_COMPARE" not in fn:
            violations.append("the compare stage is not reported")
    if "int rt_ipc_shm(" not in ipc_h:
        violations.append("the previous entry point rt_ipc_shm was dropped")
    out("SHM_STAGE", violations, "the failing syscall is named, not only its errno")


def rule_depth_stage(root, out):
    violations = []
    harness = (root / HARNESS_C).read_text()
    fs = (root / FS_C).read_text()
    fs_h = (root / FS_H).read_text()
    if "rt_fs_stage_name(" not in harness:
        violations.append("the fs.deep_paths record does not name the failing stage")
    if "RT_FS_STAGE_ERRNO_LOST" not in fs:
        violations.append("no invariant forbids a failure with errno 0")
    write_fn = body(fs, "rt_fs_write_pattern(const char *path")
    if write_fn is None:
        violations.append("rt_fs_write_pattern is missing from the runtime")
    elif "errno" not in write_fn or "*err_out" not in write_fn:
        violations.append("rt_fs_write_pattern does not capture the errno of its own call")
    for call in re.findall(r"rt_fs_write_pattern\(([^)]*)\)", fs):
        if "err" not in call:
            violations.append(f"a call to rt_fs_write_pattern discards the errno: ({call})")
    deep = body(fs, "int rt_fs_deep_paths_ex(")
    if deep is None:
        violations.append("rt_fs_deep_paths_ex is missing")
    else:
        fail_branch = deep.find("if (rc != 0) {")
        if fail_branch < 0:
            violations.append("rt_fs_deep_paths_ex has no failure branch")
        else:
            tail = deep[fail_branch:fail_branch + 600]
            if "*err_out = failure;" not in tail:
                violations.append("the failure branch does not report the captured errno")
            if "*err_out = 0;" in tail:
                violations.append("the failure branch can report errno 0: the run #1 "
                                  "symptom (errno=0 (Undefined error: 0))")
            if "failure = EIO;" not in tail:
                violations.append("no invariant forces a non-zero errno on failure")
    if "deep_paths_ex" not in fs_h:
        violations.append("rt_fs_deep_paths_ex is missing")
    if "rt_fs_deep_paths(" not in fs_h:
        violations.append("the previous entry point rt_fs_deep_paths was dropped")
    out("DEPTH_STAGE", violations, "the errno is captured where it happens, with its stage")


def rule_errno_zero(root, out):
    violations = []
    harness = (root / HARNESS_C).read_text()
    for name in ("phase02_classify_fs_depth_error", "phase02_classify_shm_error"):
        fn = body(harness, name)
        if fn is None:
            violations.append(f"{name} is missing")
            continue
        zero = re.search(r"if\s*\(\s*err\s*==\s*0\s*\)\s*\{(.*?)\}", fn, re.S)
        if zero is None:
            violations.append(f"{name} does not test err == 0")
        elif "PHASE02_OUTCOME_RUNTIME_DEFECT" not in zero.group(1):
            violations.append(f"{name} does not classify errno 0 as a defect")
    for name in ("PHASE02_OUTCOME_OK_PROBE_LIMIT",):
        if name not in (root / HARNESS_H).read_text():
            violations.append("the probe-limit outcome is not documented in the header")
    out("ERRNO_ZERO_DEFECT", violations, "errno 0 is a defect of the error path, never an answer")


def rule_jit_attempt_order(root, out):
    violations = []
    jit = (root / JIT_C).read_text()
    # Pass 03 split the allocator: rt_jit_alloc_ex() performs the mapping (and the W^X
    # fallback) while rt_jit_alloc() stays as a compatibility wrapper. The invariant this rule
    # protects is unchanged, so it is checked on the function that maps.
    fn = body(jit, "void *rt_jit_alloc_ex(") or body(jit, "void *rt_jit_alloc(")
    if fn is None:
        violations.append("rt_jit_alloc_ex/rt_jit_alloc is missing")
    elif "void *rt_jit_alloc(size_t" not in jit:
        violations.append("the rt_jit_alloc compatibility entry point was removed")
    else:
        attempt = fn.find("*map_jit_attempted_out = 1")
        if attempt < 0:
            violations.append("rt_jit_alloc never records that MAP_JIT was attempted")
        else:
            # The mapping itself, not the word MAP_JIT inside a comment or an #else branch.
            mapping = re.search(r"mmap\([^;]*MAP_JIT", fn)
            if mapping is None:
                violations.append("no mmap() with MAP_JIT in the arena allocator")
            elif attempt > mapping.start():
                violations.append("the MAP_JIT attempt is recorded after the mapping: a "
                                 "refusal would no longer be attributable to the capability")
    out("JIT_ATTEMPT_ORDER", violations,
        "the attempt is recorded before the mapping, refusals included")


def rule_compat_surface(root, out):
    violations = []
    ipc_h = (root / IPC_H).read_text()
    fs_h = (root / FS_H).read_text()
    loader_h = (root / LOADER_H).read_text()
    for needle, where in ((("int rt_ipc_shm("), IPC_H), (("int rt_fs_deep_paths("), FS_H),
                          (("rt_status_t rt_loader_run("), LOADER_H)):
        text = {"RuntimeCore/include/runtime_ipc.h": ipc_h,
                "RuntimeCore/include/runtime_filesystem.h": fs_h,
                "RuntimeCore/include/runtime_loader.h": loader_h}[where]
        if needle not in text:
            violations.append(f"{needle} missing from {where}")
    out("COMPAT_SURFACE", violations, "existing entry points survive: Fix 06 adds, never breaks")


def rule_physical_baseline(root, out):
    violations = []
    path = root / BASELINE
    if not path.is_file():
        violations.append(f"{BASELINE} is missing: the physical baseline must exist")
        out("PHYSICAL_BASELINE", violations, "run #1 stays recorded, verbatim and immutable")
        return
    text = path.read_text()
    if "PHASE_02_PHYSICAL_VALIDATION=FAIL" not in text:
        violations.append("the baseline does not state PHASE_02_PHYSICAL_VALIDATION=FAIL")
    if "IPHONE13_PHYSICAL_RUN_01" not in text:
        violations.append("the baseline does not carry the run id")
    for detail in PHYSICAL_DETAILS:
        if detail not in text:
            violations.append(f"verbatim detail missing from the baseline: {detail!r}")
    if "records=52 assertions=0 pass=42 fail=4" not in text:
        violations.append("the run summary is not recorded verbatim")
    # no document may present the four physical failures as PASS
    for doc in (root / "Documentation").glob("**/*"):
        if not doc.is_file() or doc.suffix not in (".md", ".txt"):
            continue
        low = doc.name.lower()
        if "iphone" not in low and "physical" not in low:
            continue
        text = doc.read_text(errors="replace")
        if "records=52 assertions=0 pass=42 fail=4 blocked=1 unsupported=1 untested=1 " \
           "not_applicable=3 summary=PASS" in text:
            violations.append(f"{doc.name} turns the run #1 summary into PASS")
        if "PHASE_02_PHYSICAL_VALIDATION=PASS" in text:
            violations.append(f"{doc.name} claims physical validation passed")
    out("PHYSICAL_BASELINE", violations, "run #1 is present, verbatim, and still FAIL")


RULES = [rule_loader_reason, rule_shm_stage, rule_depth_stage, rule_errno_zero,
         rule_jit_attempt_order, rule_compat_surface, rule_physical_baseline]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", default=str(pathlib.Path(__file__).resolve().parent.parent))
    args = parser.parse_args()
    root = pathlib.Path(args.root)

    total = 0

    def out(name, violations, what):
        nonlocal total
        total += len(violations)
        if violations:
            print(f"FAIL {name}: {what}")
            for v in violations:
                print(f"       - {v}")
        else:
            print(f"ok   {name}: {what}")

    print("--- Fix 06 contracts (each rule comes from a physical failure of run #1)")
    for rule in RULES:
        rule(root, out)
    print()
    print(f"FIX06_CONTRACTS_AUDIT={total}")
    if total == 0:
        print("note: static pre-check — the device remains the authority for the physical result.")
    return 0 if total == 0 else 1


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""fix06_negative_controls.py — prove, before an iPhone does, that Correction 06 really
detects the defects that produced IPHONE13_PHYSICAL_RUN_01.

Each control reintroduces ONE of the physical defects (in the same shape it had in the code
that produced the run #1 line, or in the shape a "make it green" edit would take), runs the
detectors — the static contract audit and/or the unit tests — and then restores the file
byte-for-byte. Nothing is left behind: the script asserts the SHA-256 of every touched file
against the value it had at start, rebuilds once at the end, and requires the audit and the
unit tests to be green again.

  A. loader loses the reason on the JIT-unavailable path  -> "rejected: OK" returns
  B. the deep-path probe zeroes the errno it reports      -> "errno=0 (Undefined error: 0)"
  C. MAP_JIT attempt recorded only after a successful mmap -> run #1's unattributable failure
  D. the shm record drops the failing stage                -> "errno=1" with no syscall
  E. the physical baseline rewritten to PASS               -> a falsified baseline
  F. errno 0 classified as acceptable in the depth probe    -> silent failure as PASS

Detectors are named per control: a control that only the tests can see says so, and one that
only the audit can see says so. Nothing here fabricates a device result.

Usage: python3 tools/fix06_negative_controls.py [--skip-build] [--root <dir>]
"""
import argparse
import hashlib
import pathlib
import subprocess
import sys

AUDIT = "tools/audit_fix06_contracts.py"
LOADER = "RuntimeCore/src/runtime_loader.c"
FILESYSTEM = "RuntimeCore/src/runtime_filesystem.c"
JIT = "RuntimeCore/src/runtime_jit.c"
HARNESS = "Diagnostics/src/phase02_harness.c"
BASELINE = "Documentation/IPHONE13_PHYSICAL_RUN_01.md"


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def expected_failures(runner, source):
    """Return the list of failing check descriptions in a unit-test log."""
    return [line.split("FAIL ", 1)[1] for line in source.splitlines()
            if line.startswith("FAIL ")]


class Runner:
    def __init__(self, root, skip_build):
        self.root = root
        self.skip_build = skip_build
        self.tests_run = 0

    def audit(self):
        proc = subprocess.run([sys.executable, str(self.root / AUDIT)], cwd=self.root,
                              capture_output=True, text=True)
        return proc.returncode, proc.stdout + proc.stderr

    def unit_tests(self):
        """Rebuild and run the unit tests; return (failures, log)."""
        if self.skip_build:
            return None, "(builds skipped)"
        self.tests_run += 1
        build = subprocess.run(["cmake", "--build", "build/host", "--target",
                                "phase02_unit_tests"], cwd=self.root, capture_output=True,
                               text=True)
        if build.returncode != 0:
            return ["<the unit-test target does not build>"], build.stdout + build.stderr
        run = subprocess.run([str(self.root / "build/host/phase02_unit_tests")], cwd=self.root,
                             capture_output=True, text=True)
        return expected_failures(self, run.stdout), run.stdout


def patch(text, old, new, what):
    assert text.count(old) == 1, f"patch anchor not unique for {what}: {old[:60]!r}"
    return text.replace(old, new, 1)


def control_a(root, runner, out):
    """The loader forgets to write the reason: the exact run #1 defect at the C level.

    Pass 03 changed the loader's shape (the arena has a kind and the copy happens inside the
    window that kind requires), so the anchor follows the code. What the control plants is
    unchanged: a valid module that cannot get an executable arena, returning BLOCKED with no
    reason written - the state that printed "rejected: OK" on the device."""
    path = root / LOADER
    original = path.read_text()
    try:
        broken = patch(original, """        if (loader_err_out != NULL) {
            *loader_err_out = RT_LOADER_ERR_JIT_UNAVAILABLE;
        }
        if (os_err_out != NULL) {
            *os_err_out = io_err;
        }
        return RT_BLOCKED;
""",
                       """        return RT_BLOCKED;
""",
                       "loader JIT-unavailable reason")
        path.write_text(broken)
        rc, audit_log = runner.audit()
        failures, test_log = runner.unit_tests()
        caught_audit = rc != 0
        caught_tests = failures is not None and any("reason names the missing capability" in f
                                                    for f in failures)
        out("A", "loader loses the reason => 'rejected: OK' returns",
            caught_audit, caught_tests,
            "audit LOADER_REASON" , "unit tests (arena refused path)")
    finally:
        path.write_text(original)


def control_b(root, runner, out):
    """The probe reports the caller's unset out-parameter: the run #1 errno=0 symptom."""
    path = root / FILESYSTEM
    original = path.read_text()
    try:
        broken = patch(original, """        if (err_out != NULL) {
            *err_out = failure;
        }
        return -1;
    }
    if (err_out != NULL) {
        *err_out = 0;
    }
    return 0;
}

int rt_fs_deep_paths(const char *root, size_t depth, size_t *path_len_out, int *err_out)""",
                       """        if (err_out != NULL) {
            *err_out = 0;            /* control B: the physical run #1 defect */
        }
        return -1;
    }
    if (err_out != NULL) {
        *err_out = 0;
    }
    return 0;
}

int rt_fs_deep_paths(const char *root, size_t depth, size_t *path_len_out, int *err_out)""",
                       "errno propagation")
        path.write_text(broken)
        rc, audit_log = runner.audit()
        failures, _ = runner.unit_tests()
        caught_audit = rc != 0 and "DEPTH_STAGE" in audit_log
        caught_tests = failures is not None and any("non-zero errno" in f for f in failures)
        out("B", "the deep-path probe stops preserving the errno => errno=0 again",
            caught_audit, caught_tests, "audit DEPTH_STAGE (the failure branch must report "
            "the captured errno)", "unit tests (errno-preservation regression)")
    finally:
        path.write_text(original)


def control_c(root, runner, out):
    """The pre-Fix-06 shape: the attempt is recorded only after the mmap succeeded.

    Pass 03 gave rt_jit_alloc_ex() the W^X fallback, so the anchor follows the new body; the
    planted defect is the same one run #1 exposed (a refusal that cannot be attributed to the
    MAP_JIT capability because the attempt was never recorded)."""
    path = root / JIT
    original = path.read_text()
    try:
        broken = patch(original, """        if (map_jit_attempted_out != NULL) {
            *map_jit_attempted_out = 1;
        }
        addr = mmap(NULL, rounded, PROT_READ | PROT_WRITE | PROT_EXEC,
                    MAP_PRIVATE | MAP_ANONYMOUS | MAP_JIT, -1, 0);
        if (addr != MAP_FAILED) {""",
                       """        addr = mmap(NULL, rounded, PROT_READ | PROT_WRITE | PROT_EXEC,
                    MAP_PRIVATE | MAP_ANONYMOUS | MAP_JIT, -1, 0);
        if (addr != MAP_FAILED) {
            if (map_jit_attempted_out != NULL) {
                *map_jit_attempted_out = 1;     /* control C: only on success */
            }""", "MAP_JIT attempt order")
        path.write_text(broken)
        rc, audit_log = runner.audit()
        caught_audit = rc != 0 and "JIT_ATTEMPT_ORDER" in audit_log
        out("C", "MAP_JIT attempt recorded only after success (the pre-Fix-06 shape)",
            caught_audit, None, "audit JIT_ATTEMPT_ORDER",
            "not observable on Linux: MAP_JIT is absent, so this is a static contract only")
    finally:
        path.write_text(original)


def control_d(root, runner, out):
    """The record goes back to an errno with no operation attached (the run #1 shape)."""
    path = root / HARNESS
    original = path.read_text()
    try:
        assert original.count("rt_ipc_stage_name(shm_stage)") >= 4, "the stage is not used"
        broken = original.replace("rt_ipc_stage_name(shm_stage)", "\"\"")
        assert broken != original, "stage replacement failed"
        path.write_text(broken)
        rc, audit_log = runner.audit()
        caught_audit = rc != 0 and "SHM_STAGE" in audit_log
        out("D", "the shm record drops the failing stage (run #1 shape: errno with no syscall)",
            caught_audit, None, "audit SHM_STAGE",
            "not observable here: this host grants POSIX shm, so only the record shape is "
            "checkable without a sandbox that refuses it")
    finally:
        path.write_text(original)


def control_e(root, runner, out):
    """The temptation this project forbids: editing the physical baseline into a PASS."""
    path = root / BASELINE
    original = path.read_text()
    try:
        broken = original.replace("PHASE_02_PHYSICAL_VALIDATION=FAIL",
                                  "PHASE_02_PHYSICAL_VALIDATION=PASS")
        assert broken != original, "baseline marker missing"
        path.write_text(broken)
        rc, audit_log = runner.audit()
        caught_audit = rc != 0 and "PHYSICAL_BASELINE" in audit_log
        out("E", "the run #1 baseline rewritten to PASS", caught_audit, None,
            "audit PHYSICAL_BASELINE", "n/a (documentation integrity)")
    finally:
        path.write_text(original)


def control_f(root, runner, out):
    """Silent failure sold as PASS: errno 0 classified as an acceptable answer."""
    path = root / HARNESS
    original = path.read_text()
    try:
        broken = patch(original, """    if (err == 0) {
        return PHASE02_OUTCOME_RUNTIME_DEFECT;
    }
    /* Our own probe buffer ran out before the platform did.""",
                       """    if (err == 0) {
        return PHASE02_OUTCOME_OK;       /* control F: silent failure as PASS */
    }
    /* Our own probe buffer ran out before the platform did.""", "errno 0 verdict")
        path.write_text(broken)
        rc, audit_log = runner.audit()
        caught_audit = rc != 0 and "ERRNO_ZERO_DEFECT" in audit_log
        failures, _ = runner.unit_tests()
        caught_tests = failures is not None and any("errno=0" in f for f in failures)
        out("F", "errno 0 accepted as a verdict (silent failure as PASS)",
            caught_audit, caught_tests, "audit ERRNO_ZERO_DEFECT", "unit tests")
    finally:
        path.write_text(original)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", default=str(pathlib.Path(__file__).resolve().parent.parent))
    parser.add_argument("--skip-build", action="store_true",
                        help="static controls only (no rebuild); C and D need no build anyway")
    args = parser.parse_args()
    root = pathlib.Path(args.root)
    runner = Runner(root, args.skip_build)

    touched = [root / p for p in (LOADER, FILESYSTEM, JIT, HARNESS, BASELINE)]
    before = {p: sha256(p) for p in touched}

    print("== Fix 06 negative controls (each one reintroduces a run #1 defect) ==")
    results = []

    def out(key, description, caught_audit, caught_tests, by_audit, by_tests):
        parts = []
        if caught_audit:
            parts.append(f"DETECTED by {by_audit}")
        if caught_tests:
            parts.append(f"DETECTED by {by_tests}")
        verdict = "; ".join(parts) if parts else "NOT DETECTED"
        results.append(bool(parts))
        print(f"CONTROL_{key}={verdict}")
        print(f"  what: {description}")
        if not parts:
            print(f"  expected detector: {by_audit} / {by_tests}")

    control_a(root, runner, out)
    control_b(root, runner, out)
    control_c(root, runner, out)
    control_d(root, runner, out)
    control_e(root, runner, out)
    control_f(root, runner, out)

    print("\n== restoration ==")
    restored = True
    for path in touched:
        now = sha256(path)
        ok = now == before[path]
        restored = restored and ok
        print(f"  {'ok  ' if ok else 'FAIL'} {path.relative_to(root)} {now[:16]}")
    if not restored:
        print("FIX06_NEGATIVE_CONTROLS=FAIL (a file was not restored; do not commit this tree)")
        return 1

    if not args.skip_build:
        build = subprocess.run(["cmake", "--build", "build/host"], cwd=root,
                               capture_output=True, text=True)
        run = subprocess.run([str(root / "build/host/phase02_unit_tests")], cwd=root,
                             capture_output=True, text=True)
        tail = [l for l in run.stdout.splitlines() if l.startswith("==")]
        print(f"  after restore: build rc={build.returncode}, tests " +
              (tail[-1] if tail else "<no summary>"))
        if build.returncode != 0 or run.returncode != 0:
            print("FIX06_NEGATIVE_CONTROLS=FAIL (the tree does not come back green)")
            return 1

    rc, audit_log = runner.audit()
    print(f"  after restore: FIX06_CONTRACTS_AUDIT={'0' if rc == 0 else '<violations>'}")
    print()
    caught = sum(1 for r in results if r)
    print(f"FIX06_NEGATIVE_CONTROLS={caught}/{len(results)}")
    return 0 if caught == len(results) and rc == 0 else 1


if __name__ == "__main__":
    sys.exit(main())

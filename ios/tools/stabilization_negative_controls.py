#!/usr/bin/env python3
"""stabilization_negative_controls.py — prove that the stabilization audit catches every
defect it was written for, before the iPhone has to.

Each control puts ONE defect back — in the shape it had when it was found — runs the static
audit (and, where the defect is observable on this host, the unit tests), then restores the
file byte-for-byte and asserts its SHA-256. Nothing is left behind, and the tree must come
back green.

  A. a JIT result discarded again with `(void)` (write window / icache flush)
  B. the arena sized from the first payload only, both payloads in one buffer
  C. a guard that could not be installed reported as a caught fault
  D. the dual mapping back to assuming the executable view failed
  E. EPERM classified as UNSUPPORTED again in the dual-mapping probe
  F. the final shm_unlink result discarded again
  G. a random source allowed to return "nothing" and spin the loop
  H. a truncated DETAIL left unmarked
  I. the saved report written into tmp/ again (nothing in tmp/ is exposed)
  J. the bundle no longer exposing its Documents directory
  K. a visible interface label changed (the pass may fix behaviour, never appearance)
  L. rt_mem_protect without the overflow guard again
  M. the errno captured by rt_fs_write_pattern overwritten again
  N. a short write reported with a stale errno again
  O. a thread round trip returning -1 with *err_out = 0 again
  P. a truncated summary left unmarked again
  Q. the signal disposition restore discarded again
  R. a page size written into the source instead of measured

Usage: python3 tools/stabilization_negative_controls.py [--root <dir>] [--skip-build]
"""
import argparse
import hashlib
import pathlib
import re
import subprocess
import sys

AUDIT = "tools/audit_ios_stabilization.py"
HARNESS = "Diagnostics/src/phase02_harness.c"
JIT_C = "RuntimeCore/src/runtime_jit.c"
LOADER_C = "RuntimeCore/src/runtime_loader.c"
DUAL_C = "RuntimeCore/src/runtime_dual_mapping.c"
IPC_C = "RuntimeCore/src/runtime_ipc.c"
LINUX_C = "RuntimeCore/src/linux_platform.c"
LOG_C = "Diagnostics/src/phase02_log.c"
VIEW = "RuntimePoC/ContentView.swift"
PLIST = "RuntimePoC/Info.plist"
MEMORY_C = "RuntimeCore/src/runtime_memory.c"
FS_C = "RuntimeCore/src/runtime_filesystem.c"
DARWIN_C = "RuntimeCore/src/darwin_platform.c"
CPU_C = "RuntimeCore/src/runtime_cpu_abi.c"
SIGNALS_C = "RuntimeCore/src/runtime_signals.c"
THREADS_C = "RuntimeCore/src/runtime_threads.c"


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


class Runner:
    def __init__(self, root, skip_build):
        self.root = root
        self.skip_build = skip_build

    def audit(self):
        proc = subprocess.run([sys.executable, str(self.root / AUDIT)], cwd=self.root,
                              capture_output=True, text=True)
        return proc.returncode, proc.stdout + proc.stderr

    def unit_tests(self):
        if self.skip_build:
            return None
        build = subprocess.run(["cmake", "--build", "build/host", "--target",
                                "phase02_unit_tests"], cwd=self.root, capture_output=True,
                               text=True)
        if build.returncode != 0:
            return ["<the unit-test target does not build>"]
        run = subprocess.run([str(self.root / "build/host/phase02_unit_tests")], cwd=self.root,
                             capture_output=True, text=True)
        return [line.split("FAIL ", 1)[1] for line in run.stdout.splitlines()
                if line.startswith("FAIL ")]


def patch(text, old, new, what):
    assert text.count(old) == 1, f"anchor not unique for {what}: {old[:60]!r}"
    return text.replace(old, new, 1)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", default=str(pathlib.Path(__file__).resolve().parent.parent))
    parser.add_argument("--skip-build", action="store_true")
    args = parser.parse_args()
    root = pathlib.Path(args.root)
    runner = Runner(root, args.skip_build)

    touched = [root / p for p in (HARNESS, JIT_C, LOADER_C, DUAL_C, IPC_C, LINUX_C, LOG_C,
                                  VIEW, PLIST, MEMORY_C, SIGNALS_C, THREADS_C, DARWIN_C)]
    before = {p: sha256(p) for p in touched}
    results = []

    def out(key, what, caught_audit, caught_tests, by_audit, by_tests):
        parts = []
        if caught_audit:
            parts.append(f"DETECTED by {by_audit}")
        if caught_tests:
            parts.append(f"DETECTED by {by_tests}")
        verdict = "; ".join(parts) if parts else "NOT DETECTED"
        results.append(bool(parts))
        print(f"CONTROL_{key}={verdict}")
        print(f"  what: {what}")
        if not parts:
            print(f"  expected: {by_audit}" + (f" / {by_tests}" if by_tests else ""))

    def run_control(key, path, transform, what, rule, tests_can_see):
        original = path.read_text()
        try:
            broken = transform(original)
            assert broken != original, f"control {key} changed nothing"
            path.write_text(broken)
            rc, audit_log = runner.audit()
            caught_audit = rc != 0 and rule in audit_log
            failures = runner.unit_tests() if tests_can_see else None
            caught_tests = None
            if failures is not None:
                caught_tests = len(failures) > 0
            out(key, what, caught_audit, caught_tests, f"audit {rule}",
                "unit tests" if tests_can_see else None)
        finally:
            path.write_text(original)

    print("== stabilization negative controls (each one puts a found defect back) ==")

    run_control("A", root / HARNESS,
                lambda t: patch(t, """        if (rt_jit_begin_write(arena, arena_len, &err) != 0) {
            phase02_log_record(log, "jit.make_executable", RT_BLOCKED,""",
                                """        (void)rt_jit_begin_write(arena, arena_len, &err);
        if (0) {
            phase02_log_record(log, "jit.make_executable", RT_BLOCKED,""",
                                "write window check"),
                "a JIT result discarded again with (void) (write window)",
                "DISCARDED_RESULTS", False)

    run_control("B", root / HARNESS,
                lambda t: patch(t, "    arena_len = (first_len > second_len) ? first_len : second_len;",
                                "    arena_len = (first_len > second_len) ? first_len : first_len;",
                                "arena sizing"),
                "the arena sized from the first payload only",
                "ARENA_SIZING", False)

    run_control("C", root / LOADER_C,
                lambda t: patch(t, """    if (rc == -2) {
        /* The fault guard could not be installed, so the entry point was never called and no
         * fault happened. The previous code reported this as EXEC_FAULT with si_addr=NULL —
         * a fault that never occurred. This is a defect of our own step. */
        if (os_err_out != NULL) {
            *os_err_out = io_err;
        }
        if (loader_err_out != NULL) {
            *loader_err_out = RT_LOADER_ERR_INTERNAL;
        }
        return RT_FAIL;
    }
    if (rc != 0) {""",
                                """    if (rc != 0) {""", "guard branch"),
                "a guard that could not be installed reported as a caught fault",
                "GUARD_VS_FAULT", True)

    run_control("D", root / DUAL_C,
                lambda t: patch(t, "        out->stage = RT_DUAL_STAGE_MAP_RX;\n",
                                "", "dual-mapping stage record"),
                "the dual mapping back to an unnamed failing step",
                "DUAL_MAP_STAGE", True)

    run_control("E", root / HARNESS,
                lambda t: patch(t, """    if (err == EPERM || err == EACCES) {
        /* Refused, not missing: the mechanism exists and this process may not use it —
         * the same classification the POSIX shared-memory probe applies to EPERM. */
        return PHASE02_OUTCOME_CAPABILITY_MISSING;
    }""",
                                """    if (err == EPERM || err == EACCES) {
        return PHASE02_OUTCOME_UNSUPPORTED;
    }""", "dual-mapping capability classification"),
                "EPERM classified as UNSUPPORTED again in the dual-mapping probe",
                "CAPABILITY_WORDS", True)

    run_control("F", root / IPC_C,
                lambda t: patch(t, """    if (shm_unlink(name) != 0) {""",
                                """    if (0 && shm_unlink(name) != 0) {""", "shm cleanup check"),
                "the final shm_unlink result discarded again",
                "SHM_CLEANUP", True)

    run_control("G", root / LINUX_C,
                lambda t: patch(t, """        if (got == 0) {
            /* getrandom(2) never returns 0 for a non-zero length; a source that answers
             * "nothing" would spin this loop forever. Reported, never looped on. */
            if (err_out != NULL) {
                *err_out = EIO;
            }
            return -1;
        }
""", "", "zero-progress guard"),
                "a random source allowed to return nothing and spin the loop",
                "RANDOM_PROGRESS", False)

    run_control("H", root / LOG_C,
                lambda t: patch(t, '            static const char marker[] = " [DETAIL TRUNCATED at 512 chars]";',
                                '            static const char marker[] = "";', "detail marker"),
                "a truncated DETAIL left unmarked",
                "LOG_INTEGRITY", True)

    run_control("I", root / VIEW,
                lambda t: patch(t, """        guard let documents = FileManager.default.urls(for: .documentDirectory,
                                                      in: .userDomainMask).first else {
            status = "save failed: no Documents directory"
            return
        }
        let url = documents.appendingPathComponent(name)""",
                                """        let url = URL(fileURLWithPath: workdir).appendingPathComponent(name)""",
                                "save destination"),
                "the saved report written into tmp/ again",
                "SAVE_REPORT_TARGET", False)

    run_control("J", root / PLIST,
                lambda t: patch(t, "\t<key>UIFileSharingEnabled</key>\n\t<true/>",
                                "\t<key>UIFileSharingEnabledDISABLED</key>\n\t<true/>",
                                "file-sharing key"),
                "the bundle no longer exposing its Documents directory",
                "SAVE_REPORT_TARGET", False)

    run_control("K", root / VIEW,
                lambda t: patch(t, 'Button("Copy report")', 'Button("Copy")', "visible label"),
                "a visible interface label changed",
                "UI_SURFACE_FROZEN", False)

    run_control("L", root / MEMORY_C,
                lambda t: patch(t, """    /* The same overflow guard rt_mem_reserve() already had: without it, len + page - 1
     * wraps and the rounded length becomes tiny, so the call would protect far fewer bytes
     * than it was asked to while reporting success — a silent protection shortfall on the
     * caller's assumption that the whole region changed. */
    if (len > SIZE_MAX - page) {
        if (err_out != NULL) {
            *err_out = EOVERFLOW;
        }
        return -1;
    }
""", "", "overflow guard"),
                "rt_mem_protect without the overflow guard again",
                "PROTECT_OVERFLOW", True)

    run_control("M", root / FS_C,
                lambda t: patch(t, """        /* rt_fs_write_pattern already wrote the errno of the failing call.""",
                                """        if (err_out != NULL) {
            *err_out = errno;
        }
        /* rt_fs_write_pattern already wrote the errno of the failing call.""",
                                "errno clobber"),
                "the captured errno overwritten again",
                "ERRNO_CAPTURED_NOW", False)

    run_control("N", root / FS_C,
                lambda t: patch(t, "                *err_out = (wrote < 0) ? errno : EIO;",
                                "                *err_out = errno;", "short write errno"),
                "a short write reported with a stale errno again",
                "ERRNO_CAPTURED_NOW", False)

    def break_threads(text):
        pattern = re.compile(
            r"    if \((\w+(?:\.\w+)?) != expected\) \{.*?    \}\n"
            r"    if \(err_out != NULL\) \{\n        \*err_out = 0;\n    \}\n    return 0;",
            re.S)

        def old_tail(match):
            return ("    if (err_out != NULL) {\n        *err_out = 0;\n    }\n"
                    f"    return ({match.group(1)} == expected) ? 0 : -1;")

        broken, count = pattern.subn(old_tail, text)
        assert count == 5, f"expected 5 thread sites, broke {count}"
        return broken

    run_control("O", root / THREADS_C, break_threads,
                "a thread round trip returning -1 with *err_out = 0 again",
                "ERRNO_CAPTURED_NOW", True)

    run_control("P", root / CPU_C,
                lambda t: patch(t, '        static const char truncation[] = " [SUMMARY TRUNCATED]";',
                                '        static const char truncation[] = "";', "summary marker"),
                "a truncated summary left unmarked again",
                "TRUNCATION_MARKED", True)

    run_control("Q", root / SIGNALS_C,
                lambda t: patch(t, """    if (sigaction(signal_number, &saved, NULL) != 0) {
        /* Restoring the previous disposition is part of the round trip. Discarding this
         * result — as the previous code did, with err_out forced to 0 afterwards — reported
         * PASS for a signal disposition left modified. */
        if (err_out != NULL) {
            *err_out = errno;
        }
        return -1;
    }""", """    (void)sigaction(signal_number, &saved, NULL);""", "signal restore"),
                "the signal disposition restore discarded again",
                "ERRNO_CAPTURED_NOW", False)

    run_control("R", root / DARWIN_C,
                lambda t: patch(t, """    long page = sysconf(_SC_PAGESIZE);
    if (page <= 0) {
        return 4096;
    }
    return (int)page;""",
                                """    return 16384;   /* the iPhone 13 page size, presumed instead of measured */""",
                                "page size"),
                "a page size written into the source instead of measured",
                "PAGE_SIZE_MEASURED", False)

    print("\n== restoration ==")
    restored = True
    for path in touched:
        now = sha256(path)
        ok = now == before[path]
        restored = restored and ok
        print(f"  {'ok  ' if ok else 'FAIL'} {path.relative_to(root)} {now[:16]}")
    if not restored:
        print("STABILIZATION_NEGATIVE_CONTROLS=FAIL (a file was not restored)")
        return 1

    if not args.skip_build:
        build = subprocess.run(["cmake", "--build", "build/host"], cwd=root,
                               capture_output=True, text=True)
        run = subprocess.run([str(root / "build/host/phase02_unit_tests")], cwd=root,
                             capture_output=True, text=True)
        tail = [line for line in run.stdout.splitlines() if line.startswith("==")]
        print(f"  after restore: build rc={build.returncode}, tests " +
              (tail[-1] if tail else "<no summary>"))
        if build.returncode != 0 or run.returncode != 0:
            print("STABILIZATION_NEGATIVE_CONTROLS=FAIL (the tree does not come back green)")
            return 1

    rc, audit_log = runner.audit()
    print(f"  after restore: IOS_STABILIZATION_AUDIT={'0' if rc == 0 else '<violations>'}")
    caught = sum(1 for r in results if r)
    print()
    print(f"STABILIZATION_NEGATIVE_CONTROLS={caught}/{len(results)}")
    return 0 if caught == len(results) and rc == 0 else 1


if __name__ == "__main__":
    sys.exit(main())

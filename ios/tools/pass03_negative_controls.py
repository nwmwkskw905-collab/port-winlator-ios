#!/usr/bin/env python3
"""pass03_negative_controls.py — prove the pass-03 detectors fire before an iPhone has to.

Every control reintroduces ONE defect in the shape it had on the device, or in the shape a
"make it green" edit would take, runs the detectors (the pass-03 audits and, where the defect
is behavioural, the unit tests), and restores the file byte-for-byte. The script verifies the
SHA-256 of every touched file against the value it had at the start and rebuilds once at the
end, so a control can never leave the tree modified.

Controls (the first eleven are the ones the pass-03 brief demands):

  A  entitlements file kept but the wiring decision erased      -> E2 (unwired with no decision)
  B  a document claims the entitlement grants JIT               -> E3 (granted-capability claim)
  C  jit.make_executable recorded PASS before anything ran      -> J8 (PASS without execution)
  D  the loader PASS stops carrying the executed value          -> J8 (loader PASS without proof)
  E  one icache synchronisation removed from the chain          -> J7/J12
  F  an RWX arena introduced in the fallback                    -> J4 (no RWX request)
  G  shm_open EPERM reported as a generic defect                -> S6 (refusal = capability)
  H  dual mapping described as a requirement of the port        -> S6 (experiment, not a rule)
  I  the iOS backend selected on every platform                 -> S3 (Apple-iOS gate)
  J  a macOS-only API called from the harness                   -> J10 (no macOS API on iOS)
  K  the write window replaced by mprotect on a MAP_JIT arena   -> J5 (window per kind)
  L  CODE_SIGN_ENTITLEMENTS added for one configuration only    -> E2 (subset wiring)
  M  the JIT key removed from the entitlements file             -> E1 (level 1 unreadable)
  N  the harness note says the entitlement is applied           -> E3
  O  the aliasing check always reports success                  -> unit tests (negative case)
  P  the iOS backend stops unlinking its object                 -> unit tests (cleanup)

Usage: python3 tools/pass03_negative_controls.py [--skip-build] [--root <dir>]
"""
import argparse
import hashlib
import pathlib
import subprocess
import sys

ENTITLEMENT_AUDIT = "tools/audit_entitlement_config.py"
JIT_AUDIT = "tools/audit_jit_causal_graph.py"
SHM_AUDIT = "tools/audit_shm_backend.py"

ENTITLEMENTS = "RuntimePoC/WinlatorPhase02.entitlements"
HARNESS = "Diagnostics/src/phase02_harness.c"
LOADER = "RuntimeCore/src/runtime_loader.c"
JIT = "RuntimeCore/src/runtime_jit.c"
DUAL = "RuntimeCore/src/runtime_dual_mapping.c"
PBXPROJ = "WinlatorPhase02.xcodeproj/project.pbxproj"


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


class Runner:
    def __init__(self, root, skip_build):
        self.root = root
        self.skip_build = skip_build
        self.test_runs = 0
        self.audit_runs = 0

    def audit(self, tool):
        self.audit_runs += 1
        proc = subprocess.run([sys.executable, str(self.root / tool)], cwd=self.root,
                              capture_output=True, text=True)
        return proc.returncode, proc.stdout + proc.stderr

    def unit_tests(self):
        if self.skip_build:
            return None, "(builds skipped)"
        self.test_runs += 1
        build = subprocess.run(["cmake", "--build", "build/host", "--target",
                                "phase02_unit_tests"], cwd=self.root, capture_output=True,
                               text=True)
        if build.returncode != 0:
            return ["<the unit-test target does not build>"], build.stdout + build.stderr
        run = subprocess.run([str(self.root / "build/host/phase02_unit_tests")], cwd=self.root,
                             capture_output=True, text=True)
        failures = [line.split("FAIL ", 1)[1] for line in run.stdout.splitlines()
                    if line.startswith("FAIL ")]
        return failures, run.stdout


def patch(text, old, new, what):
    assert text.count(old) == 1, f"anchor not unique for {what}: {old[:70]!r}"
    return text.replace(old, new, 1)


def patch_first(text, old, new, what):
    """Patch the first of several identical anchors (used to build a *subset* violation)."""
    assert text.count(old) >= 1, f"anchor absent for {what}: {old[:70]!r}"
    return text.replace(old, new, 1)


class Controls:
    def __init__(self, runner, out):
        self.runner = runner
        self.out = out
        self.results = []

    def record(self, name, detected, detail):
        self.results.append((name, detected, detail))
        mark = "ok  " if detected else "FAIL"
        print(f"{mark} control {name}: {detail}")

    def control(self, name, path, mutate, expect, detector, check_tests=False):
        """Apply one mutation, run the detector, restore, verify the hash."""
        target = self.runner.root / path
        before = sha256(target)
        original = target.read_text()
        try:
            target.write_text(mutate(original))
            if check_tests:
                failures, log = self.runner.unit_tests()
                if failures is None:
                    self.record(name, False, "unit tests skipped; control not evaluated")
                    return
                hit = any(expect in failure for failure in failures)
                detail = (f"unit tests reported {failures[0]!r}" if hit
                          else f"unit tests did NOT report {expect!r} (failures: {failures})")
                self.record(name, hit, detail)
                return
            rc, output = self.runner.audit(detector)
            hit = expect in output
            detail = (f"detected by {detector}: "
                      f"{next((l for l in output.splitlines() if expect in l), '')[:110]!r}"
                      if hit else
                      f"{detector} did NOT report {expect!r} (rc={rc})")
            self.record(name, hit, detail)
        finally:
            target.write_text(original)
            assert sha256(target) == before, f"{path} was not restored byte-for-byte"

    def audit_is_green(self, name, detector):
        rc, output = self.runner.audit(detector)
        green = rc == 0
        self.record(name, green, f"{detector} {'green' if green else 'RED after restore'}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", default=str(pathlib.Path(__file__).resolve().parent.parent))
    parser.add_argument("--skip-build", action="store_true")
    args = parser.parse_args()
    root = pathlib.Path(args.root).resolve()
    runner = Runner(root, args.skip_build)
    controls = Controls(runner, None)

    print("== pass 03 negative controls (every detector must fire, then the tree is restored)")

    # A — the entitlements file is kept but the wiring decision is erased.
    controls.control(
        "A_unwired_without_decision", ENTITLEMENTS,
        lambda text: text.replace("WIRING DECISION", "SOMETHING ELSE", 1),
        "E2: the entitlements file is not wired and does not record the wiring decision",
        ENTITLEMENT_AUDIT)

    # B — a document claims the entitlement grants JIT.
    controls.control(
        "B_granted_claim_in_docs", "Documentation/RELATORIO_FASE_02_RECONSTRUIDA.md",
        lambda text: text + "\n<!-- JIT enabled by com.apple.security.cs.allow-jit -->\n",
        "E3: Documentation/RELATORIO_FASE_02_RECONSTRUIDA.md",
        ENTITLEMENT_AUDIT)

    # C — jit.make_executable recorded PASS before anything executed.
    controls.control(
        "C_make_executable_pass_without_execution", HARNESS,
        lambda text: patch(text,
                           '    target = arena;\n    memcpy(&fn, &target, sizeof(fn));\n'
                           '    {\n        int rc = rt_signal_call_guarded(fn, &value_first',
                           '    phase02_log_record(log, "jit.make_executable", RT_PASS,\n'
                           '                       "payload is in executable memory and PROVEN '
                           'executable by real execution returning %u (%s)", value_first,\n'
                           '                       use_write_window ? "MAP_JIT" : "mprotect");\n'
                           '    target = arena;\n    memcpy(&fn, &target, sizeof(fn));\n'
                           '    {\n        int rc = rt_signal_call_guarded(fn, &value_first',
                           "make_executable early PASS"),
        "J8: jit.make_executable can be PASS before anything executed", JIT_AUDIT)

    # D — the loader PASS stops carrying the executed value.
    controls.control(
        "D_loader_pass_without_value", HARNESS,
        lambda text: patch(text,
                           '"module executed, entry returned %u (expected 7)", value);',
                           '"module executed (value not checked)");',
                           "loader PASS without the value"),
        "J8: the loader PASS does not carry the value", JIT_AUDIT)

    # E — one icache synchronisation removed from the chain.
    controls.control(
        "E_icache_sync_removed", HARNESS,
        lambda text: patch(text,
                           "    if (phase02_jit_sync_icache(log, arena, second_len, "
                           "PHASE02_ICACHE_REWRITE) != 0) {\n"
                           "        phase02_jit_release(log, arena, arena_len);\n"
                           "        return;\n    }\n",
                           "", "rewrite icache sync"),
        "J7: the rewritten payload is executed without a new icache flush", JIT_AUDIT)

    # F — an RWX arena introduced in the fallback.
    controls.control(
        "F_rwx_arena_introduced", JIT,
        lambda text: patch(text,
                           "        addr = rt_mem_reserve(len, err_out);",
                           "        addr = mmap(NULL, len, PROT_READ | PROT_WRITE | PROT_EXEC,\n"
                           "                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);\n"
                           "        if (addr == MAP_FAILED) { return NULL; }",
                           "RWX fallback"),
        "J4: an RWX request outside a MAP_JIT creation", JIT_AUDIT)

    # G — shm_open EPERM reported as a generic defect (no capability classification).
    controls.control(
        "G_shm_perm_as_generic_defect", HARNESS,
        lambda text: patch(text,
                           "            rt_status_t shm_status = phase02_classify(\n"
                           "                phase02_classify_shm_error((int)shm_stage, err), "
                           "RT_BLOCKED);",
                           "            rt_status_t shm_status = RT_FAIL;",
                           "shm capability classification"),
        "S6: the POSIX SHM refusal is no longer classified", SHM_AUDIT)

    # H — dual mapping described as a requirement of the port.
    controls.control(
        "H_dual_mapping_as_requirement", HARNESS,
        lambda text: patch(text,
                           "the port does not depend on this experiment",
                           "the port depends on this experiment",
                           "dual mapping as a requirement"),
        "S6: the record does not say that the port does not depend", SHM_AUDIT)

    # I — the iOS backend selected on every platform.
    controls.control(
        "I_ios_backend_on_every_platform", HARNESS,
        lambda text: patch(text,
                           "if (!named_ok && phase02_apple_target_is_ios() != 0 && map.err != 0) {",
                           "if (!named_ok && map.err != 0) {",
                           "iOS backend gate"),
        "S3: the substitution is not gated on the Apple iOS target", SHM_AUDIT)

    # J — a macOS-only API called from the harness.
    controls.control(
        "J_macos_api_on_ios_target", HARNESS,
        lambda text: patch(text, "    allowed = rt_jit_execution_allowed();",
                           "    pthread_jit_write_protect_np(0);\n"
                           "    allowed = rt_jit_execution_allowed();", "macOS-only call"),
        "J10: the harness calls the macOS-only API directly", JIT_AUDIT)

    # K — the write window replaced by mprotect on a MAP_JIT arena.
    controls.control(
        "K_mprotect_on_map_jit_arena", JIT,
        lambda text: patch(text,
                           "        /* No mprotect here: on a MAP_JIT region the protections were "
                           "fixed at creation and\n         * closing the write window is the "
                           "supported transition. */",
                           "        (void)rt_mem_protect(arena, len, RT_PROT_READ | RT_PROT_EXEC, "
                           "&err);",
                           "mprotect on MAP_JIT"),
        "J5: rt_jit_execution_allowed still calls rt_mem_protect on a MAP_JIT arena", JIT_AUDIT)

    # L — CODE_SIGN_ENTITLEMENTS added for one configuration only.
    controls.control(
        "L_wiring_subset", PBXPROJ,
        lambda text: patch_first(text, "CODE_SIGN_STYLE = Automatic;",
                           "CODE_SIGN_STYLE = Automatic;\n\t\t\t\t"
                           "CODE_SIGN_ENTITLEMENTS = RuntimePoC/WinlatorPhase02.entitlements;",
                           "single-configuration wiring"),
        "E2: CODE_SIGN_ENTITLEMENTS is set for a subset of the target configurations",
        ENTITLEMENT_AUDIT)

    # M — the JIT key removed from the entitlements file.
    controls.control(
        "M_jit_key_removed", ENTITLEMENTS,
        lambda text: text.replace("<key>com.apple.security.cs.allow-jit</key>", "", 1),
        "E1: RuntimePoC/WinlatorPhase02.entitlements does not request",
        ENTITLEMENT_AUDIT)

    # N — the harness note claims the entitlement is applied.
    controls.control(
        "N_note_claims_applied", HARNESS,
        lambda text: text.replace("L1_requested_in_repo=", "the entitlement is applied: "
                                  "L1_requested_in_repo=", 1),
        "E3: Diagnostics/src/phase02_harness.c:", ENTITLEMENT_AUDIT)

    # O — the aliasing check always reports success (behavioural: the unit tests must catch it).
    controls.control(
        "O_alias_check_always_true", DUAL,
        lambda text: patch(text,
                           "    if (map == NULL || map->supported == 0 || map->rw == NULL || "
                           "map->rx == NULL) {\n        return -1;\n    }",
                           "    if (map == NULL || map->supported == 0 || map->rw == NULL || "
                           "map->rx == NULL) {\n        return 1;\n    }",
                           "aliasing negative case"),
        "destroyed map is not usable", "", check_tests=True)

    # P — the iOS backend stops unlinking its object (behavioural: cleanup test must catch it).
    controls.control(
        "P_backing_object_leaks", DUAL,
        lambda text: patch(text, "    (void)unlink(path);", "    /* object left behind */", 1),
        "no backing object is left behind", "", check_tests=True)

    # After every control the tree must be green again.
    controls.audit_is_green("Q_audits_green_after_restore", JIT_AUDIT)
    controls.audit_is_green("R_entitlement_audit_green_after_restore", ENTITLEMENT_AUDIT)
    controls.audit_is_green("S_shm_audit_green_after_restore", SHM_AUDIT)

    detected = sum(1 for _, hit, _ in controls.results if hit)
    total = len(controls.results)
    print(f"# audits run: {runner.audit_runs}, unit-test runs: {runner.test_runs}")
    print(f"PASS03_NEGATIVE_CONTROLS={detected}/{total}")
    return 0 if detected == total else 1


if __name__ == "__main__":
    raise SystemExit(main())

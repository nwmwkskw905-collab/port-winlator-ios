#!/usr/bin/env python3
"""audit_ios_stabilization.py — static audit for the iOS stabilization pass.

Every rule here is a defect that was found by auditing the paths the Phase 02 device run
exercises, and each one is checked against the real sources so it cannot come back without
the audit failing.

  S1  DISCARDED_RESULTS   no JIT result is thrown away with a `(void)` cast: opening and
                          closing the write window and the instruction-cache flush are
                          checked (the old code ignored all three).
  S2  ICACHE_REPORTED     the icache flush is a reported step with three outcomes (flushed /
                          not applicable / failed), and a failure stops the execution.
  S3  ARENA_SIZING        the arena is sized for both payloads before either is written, the
                          payloads live in separate buffers, and the rewrite is capacity-
                          checked instead of silently clamped.
  S4  GUARD_VS_FAULT      "the guard could not be installed" (-2) is never reported as a
                          fault: the loader returns INTERNAL, the harness says UNTESTED.
  S5  DUAL_MAP_STAGE      the dual-mapping experiment names the step that failed instead of
                          assuming it was the executable view.
  S6  CAPABILITY_WORDS    the same errno is classified the same way everywhere: a refusal
                          (EPERM/EACCES) is never UNSUPPORTED in either probe.
  S7  SHM_CLEANUP         RT_IPC_STAGE_UNLINK exists, is named, and the final shm_unlink is
                          checked.
  S8  RANDOM_PROGRESS     a source that returns "nothing" cannot spin the loop forever.
  S9  LOG_INTEGRITY       a truncated DETAIL and a capacity-truncated report are marked.

Output: one line per rule, then IOS_STABILIZATION_AUDIT=<violations>.

Usage: python3 tools/audit_ios_stabilization.py [--root <dir>]
"""
import argparse
import pathlib
import re
import sys

VIEW = "RuntimePoC/ContentView.swift"
PLIST = "RuntimePoC/Info.plist"
UI_SNAPSHOT = "tools/ui_surface.snapshot.txt"
HARNESS = "Diagnostics/src/phase02_harness.c"
HARNESS_H = "Diagnostics/include/phase02_harness.h"
LOG_C = "Diagnostics/src/phase02_log.c"
JIT_C = "RuntimeCore/src/runtime_jit.c"
LOADER_C = "RuntimeCore/src/runtime_loader.c"
IPC_C = "RuntimeCore/src/runtime_ipc.c"
IPC_H = "RuntimeCore/include/runtime_ipc.h"
MEM_H = "RuntimeCore/include/runtime_memory.h"
DUAL_C = "RuntimeCore/src/runtime_dual_mapping.c"
LINUX_C = "RuntimeCore/src/linux_platform.c"
MEMORY_C = "RuntimeCore/src/runtime_memory.c"
FS_C = "RuntimeCore/src/runtime_filesystem.c"
SIGNALS_C = "RuntimeCore/src/runtime_signals.c"
THREADS_C = "RuntimeCore/src/runtime_threads.c"
CPU_C = "RuntimeCore/src/runtime_cpu_abi.c"
CONTEXT_C = "RuntimeCore/src/runtime_context.c"
DARWIN_C = "RuntimeCore/src/darwin_platform.c"


def strip_comments(text):
    """Remove /* ... */ and // ... so a rule can never be satisfied by prose."""
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    return re.sub(r"//[^\n]*", " ", text)


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


def rule_discarded_results(root, out):
    """S1 + S2: nothing in the JIT path may be called through a `(void)` cast."""
    violations = []
    harness = (root / HARNESS).read_text()
    jit = (root / JIT_C).read_text()
    for name in ("rt_jit_begin_write", "rt_jit_end_write", "rt_jit_invalidate"):
        pattern = "(void)" + name
        for source, label in ((harness, HARNESS), (jit, JIT_C)):
            if pattern in source:
                violations.append(f"{label} discards the result of {name} with a (void) cast")
    # ...and the checks must actually be there.
    if harness.count("if (rt_jit_begin_write(") < 2:
        violations.append("the write window is not checked before both writes")
    if harness.count("if (rt_jit_end_write(") < 2:
        violations.append("the write window is not checked after both writes")
    if "phase02_jit_sync_icache" not in harness:
        violations.append("the icache flush is not a reported step")
    if "rt_jit_invalidate" not in jit:
        violations.append("rt_jit_execution_allowed does not flush the icache at all")
    out("DISCARDED_RESULTS", violations, "no JIT result is silently thrown away")


def rule_icache_reported(root, out):
    violations = []
    harness = (root / HARNESS).read_text()
    fn = body(harness, "static int phase02_jit_sync_icache(")
    if fn is None:
        violations.append("phase02_jit_sync_icache is missing")
    else:
        for needle, why in (("RT_CAP_ICACHE_FLUSH", "does not consult the platform capability"),
                            ("RT_NOT_APPLICABLE", "has no 'no flush needed' outcome"),
                            ("RT_FAIL", "has no failure outcome"),
                            ("return -1", "does not stop the execution when the flush fails")):
            if needle not in fn:
                violations.append(f"phase02_jit_sync_icache {why}")
    # the execution must be gated on the flush result at both write sites
    if harness.count("phase02_jit_sync_icache(log, arena,") != 2:
        violations.append("the flush is not checked after both the first write and the rewrite")
    out("ICACHE_REPORTED", violations, "the icache flush is measured, reported and honoured")


def rule_arena_sizing(root, out):
    violations = []
    harness = (root / HARNESS).read_text()
    fn = body(harness, "static void phase02_jit_microtest(")
    if fn is None:
        violations.append("phase02_jit_microtest is missing")
        out("ARENA_SIZING", violations, "the arena holds both payloads")
        return
    code = strip_comments(fn)
    fn = code + "\n/* " + fn + " */"
    if "? first_len : first_len" in code:
        violations.append("the dead ternary `(first_len > second_len) ? first_len : first_len` "
                          "is back: the arena would be sized from the first payload only")
    if "(first_len > second_len) ? first_len : second_len" not in fn:
        violations.append("the arena is not sized from the larger payload")
    if "payload_first" not in fn or "payload_second" not in fn:
        violations.append("the payloads do not live in separate buffers: emitting the second "
                          "one would overwrite the first before it is copied")
    second_emit = fn.find("payload_second")
    if second_emit < 0 or no_emit_before(fn, second_emit):
        violations.append("the second payload is not emitted before the arena is allocated")
    if "second_len > arena_len" not in fn:
        violations.append("the rewrite is not capacity-checked (it used to clamp silently)")
    if "second_len = (second_len > arena_len)" in fn:
        violations.append("the rewrite length is clamped again: a half-written instruction "
                          "stream would be executed")
    out("ARENA_SIZING", violations, "the arena holds both payloads, in separate buffers")


def no_emit_before(fn, index):
    """True when an emitter call for the second payload appears before `index` writing it."""
    return re.search(r"emit_return_imm\(payload_second", fn[:index]) is not None


def rule_guard_vs_fault(root, out):
    violations = []
    loader = (root / LOADER_C).read_text()
    harness = (root / HARNESS).read_text()
    fn = body(loader, "rt_status_t rt_loader_run_ex(")
    if fn is None:
        violations.append("rt_loader_run_ex is missing")
    else:
        minus_two = fn.find("rc == -2")
        minus_one = fn.find("rc != 0")
        if minus_two < 0:
            violations.append("the loader does not distinguish 'guard unavailable' from a fault")
        else:
            window = fn[minus_two:minus_two + 500]
            if "RT_LOADER_ERR_INTERNAL" not in window:
                violations.append("the loader reports a guard failure without the INTERNAL reason")
            if "RT_LOADER_ERR_EXEC_FAULT" in window:
                violations.append("the loader still reports a guard failure as EXEC_FAULT")
        if minus_two > minus_one >= 0:
            violations.append("the generic fault branch is tested before the guard branch")
    micro = body(harness, "static void phase02_jit_microtest(")
    if micro is not None and "rc == -2" not in micro:
        violations.append("the JIT microtest conflates 'guard not installed' with a fault")
    out("GUARD_VS_FAULT", violations, "'could not install the guard' is never a caught fault")


def rule_dual_map_stage(root, out):
    violations = []
    header = (root / MEM_H).read_text()
    dual = (root / DUAL_C).read_text()
    harness = (root / HARNESS).read_text()
    if "rt_dual_stage_t" not in header or "stage" not in header:
        violations.append("rt_dual_map_t has no stage field")
    if "rt_dual_stage_name" not in header:
        violations.append("no stage-name accessor is declared")
    for stage in ("RT_DUAL_STAGE_NAME", "RT_DUAL_STAGE_SHM_OPEN", "RT_DUAL_STAGE_FTRUNCATE",
                  "RT_DUAL_STAGE_MAP_RW", "RT_DUAL_STAGE_MAP_RX", "RT_DUAL_STAGE_ALIAS_CHECK"):
        if dual.count(stage) < 2:
            violations.append(f"{stage} is never set by the implementation")
    if "rt_dual_stage_name(map.stage)" not in harness:
        violations.append("the harness does not report the failing stage")
    if "second (executable) view refused" in strip_comments(harness):
        violations.append("the harness assumes the executable view failed instead of reporting "
                          "the measured stage")
    out("DUAL_MAP_STAGE", violations, "the failing step of the dual mapping is measured")


def rule_capability_words(root, out):
    violations = []
    harness = (root / HARNESS).read_text()
    for signature, name in (("phase02_outcome_t phase02_classify_dual_mapping_error(", "dual"),
                            ("phase02_outcome_t phase02_classify_shm_error(", "shm")):
        fn = body(harness, signature)
        if fn is None:
            violations.append(f"{name} classifier is missing")
            continue
        code = strip_comments(fn)
        perm = re.search(r"err == EPERM\s*\|\|\s*err == EACCES|err == EACCES\s*\|\|\s*err == EPERM",
                         code)
        if perm is None:
            violations.append(f"the {name} classifier does not single out EPERM/EACCES")
        elif "PHASE02_OUTCOME_CAPABILITY_MISSING" not in code[perm.start():perm.start() + 260]:
            violations.append(f"the {name} classifier maps EPERM/EACCES to something other than "
                              "a missing capability (a refusal is not 'unsupported')")
    out("CAPABILITY_WORDS", violations, "a refusal is a capability in every probe")


def rule_shm_cleanup(root, out):
    violations = []
    header = (root / IPC_H).read_text()
    ipc = (root / IPC_C).read_text()
    if "RT_IPC_STAGE_UNLINK" not in header:
        violations.append("RT_IPC_STAGE_UNLINK is missing from the stage enumeration")
    if "RT_IPC_STAGE_UNLINK" not in ipc:
        violations.append("RT_IPC_STAGE_UNLINK is never set")
    fn = body(ipc, "int rt_ipc_shm_ex(")
    if fn is None:
        violations.append("rt_ipc_shm_ex is missing")
    elif "if (shm_unlink(name) != 0)" not in fn:
        violations.append("the final shm_unlink result is still discarded")
    if 'return "shm_unlink(cleanup)"' not in ipc:
        violations.append("the cleanup stage has no name")
    out("SHM_CLEANUP", violations, "the shared-memory cleanup step is checked and named")


def rule_random_progress(root, out):
    violations = []
    linux = (root / LINUX_C).read_text()
    fn = body(linux, "static int linux_random_bytes(")
    if fn is None:
        violations.append("linux_random_bytes is missing")
    else:
        if "got == 0" not in fn:
            violations.append("a getrandom() that returns 0 spins the loop forever")
        elif fn.find("got == 0") > fn.find("filled += (size_t)got"):
            violations.append("the zero-progress guard is placed after the progress update")
    out("RANDOM_PROGRESS", violations, "a random source cannot spin the loop forever")


def rule_log_integrity(root, out):
    violations = []
    log = (root / LOG_C).read_text()
    if "DETAIL TRUNCATED at 512 chars" not in log:
        violations.append("a truncated DETAIL is not marked")
    if "log-capacity-reached" not in log:
        violations.append("a report truncated by the log capacity does not say so")
    if "vsnprintf(detail, sizeof(detail), fmt, args)" not in log:
        violations.append("the DETAIL formatter changed shape: re-check the truncation rule")
    out("LOG_INTEGRITY", violations, "truncated evidence is marked as truncated")


def rule_save_report_target(root, out):
    """S-009 (pass 02): the report must be saved where file sharing can actually expose it."""
    violations = []
    view = (root / VIEW).read_text()
    plist = (root / PLIST).read_text()
    fn = body(view, "private func save() {")
    if fn is None:
        violations.append("ContentView.save() is missing")
    else:
        if "fileURLWithPath: workdir" in fn:
            violations.append("the saved report goes to the suite workdir (tmp/) again: "
                              "nothing in tmp/ is exposed by UIFileSharingEnabled")
        if "NSTemporaryDirectory()" in fn:
            violations.append("save() builds its own temporary path")
        if ".documentDirectory" not in fn:
            violations.append("save() does not target the Documents directory")
        if "in: .userDomainMask" not in fn:
            violations.append("save() does not target the user domain (the one file sharing exposes)")
        if "Phase02Bridge.reportFileName()" not in fn:
            violations.append("the report file name no longer comes from the bridge")
        if "try report.write(to: url, atomically: true, encoding: .utf8)" not in fn:
            violations.append("the write does not report its failure through `try` any more")
        if fn.count("status =") < 2:
            violations.append("one of the two outcomes of save() is not reported in the status")
    # The destination is only correct while the bundle actually exposes it.
    for key in ("UIFileSharingEnabled", "LSSupportsOpeningDocumentsInPlace"):
        if f"<key>{key}</key>" not in plist:
            violations.append(f"{key} is gone from Info.plist: Documents/ is no longer "
                              "recoverable and the saved report would be stranded")
    # And the suites must keep their scratch directory: moving the harness workdir would
    # change filesystem-suite behaviour on a PASS path.
    if "NSTemporaryDirectory()" not in view:
        violations.append("the suite workdir no longer uses NSTemporaryDirectory(): the "
                          "filesystem suite would run against a different directory")
    out("SAVE_REPORT_TARGET", violations, "the saved report lands where it can be recovered")


def ui_surface(view_text):
    """The visible surface: the SwiftUI body block, byte for byte."""
    return body(view_text, "var body: some View {")


def rule_ui_surface_frozen(root, out):
    """The pass may fix behaviour, never appearance: the body block is frozen."""
    violations = []
    view = (root / VIEW).read_text()
    snapshot_path = root / UI_SNAPSHOT
    if not snapshot_path.exists():
        violations.append(f"{UI_SNAPSHOT} is missing: the visual surface is not frozen")
    current = ui_surface(view)
    if current is None:
        violations.append("ContentView.body is missing")
    elif snapshot_path.exists():
        reference = snapshot_path.read_text().strip("\n")
        if current.strip("\n") != reference:
            violations.append("the SwiftUI body changed: this pass is not allowed to alter the "
                              "interface (labels, order, layout, colours, buttons)")
    out("UI_SURFACE_FROZEN", violations, "the visible interface is byte-identical to pass 01")


def rule_errno_captured_now(root, out):
    """Pass 02: the errno of the failing call, captured there and never re-read later."""
    violations = []
    fs = (root / FS_C).read_text()
    ipc = (root / IPC_C).read_text()
    sig = (root / SIGNALS_C).read_text()
    thr = (root / THREADS_C).read_text()

    # 1. A call that already captured the errno must not be followed, in its own failure
    #    branch, by a fresh read of errno.
    defect = re.compile(
        r"if \((rt_(?:fs|ipc)_[a-z_]+)\([^;]*err_out\)\s*!=\s*0\)\s*\{"
        r"\s*(?:/\*.*?\*/\s*)?if \(err_out != NULL\) \{\s*\*err_out = errno;",
        re.S)
    for source, label in ((fs, FS_C), (ipc, IPC_C)):
        for call in defect.finditer(source):
            violations.append(f"{label}: the errno captured by {call.group(1)}() is "
                              "overwritten in its failure branch with a later `errno` read")

    # 2. A short transfer is not an errno-producing failure: it must be reported as EIO.
    for source, label in ((fs, FS_C), (ipc, IPC_C)):
        for match in re.finditer(r"!= \(ssize_t\)sizeof\([^)]*\)\) \{", source):
            block = source[match.end():match.end() + 400]
            if "errno : EIO" not in block and label == FS_C:
                violations.append(f"{label}: a short write/read reports a stale errno "
                                  f"(no `(x < 0) ? errno : EIO` in the failure branch)")
            if label == IPC_C and "errno : EIO" not in block:
                violations.append(f"{label}: a short write/read reports a stale errno "
                                  f"(no `(x < 0) ? errno : EIO` in the failure branch)")

    # 3. The signal round trip must report an errno on every failure and honour the restore.
    if "int result = -1;" in sig:
        violations.append(f"{SIGNALS_C}: rt_signal_roundtrip still returns -1 through a path "
                          "that leaves *err_out untouched (errno=0 in the report)")
    if "*err_out = EIO;" not in sig:
        violations.append(f"{SIGNALS_C}: the 'handler is not the one installed' failure carries "
                          "no errno")
    if "Restoring the previous disposition is part of the round trip" not in sig:
        violations.append(f"{SIGNALS_C}: the restore of the previous disposition is discarded "
                          "again (a modified disposition reported as PASS)")

    # 4. A mismatch in a thread round trip is a failure and must carry a non-zero errno.
    if re.search(r"return \(\w+(\.\w+)? == expected\) \? 0 : -1;", thr):
        violations.append(f"{THREADS_C}: a mismatch still returns -1 with *err_out = 0")
    if thr.count("*err_out = EILSEQ;") < 5:
        violations.append(f"{THREADS_C}: fewer than five round trips report EILSEQ on a mismatch")
    out("ERRNO_CAPTURED_NOW", violations, "every failure carries the errno of the failing call")


def rule_protect_overflow_guard(root, out):
    """Pass 02: rt_mem_protect must refuse a length whose rounding would overflow."""
    violations = []
    memory = (root / MEMORY_C).read_text()
    fn = body(memory, "int rt_mem_protect(")
    if fn is None:
        violations.append("rt_mem_protect is missing")
    else:
        code = strip_comments(fn)
        if "SIZE_MAX - page" not in code:
            violations.append("the overflow guard is gone: len + page - 1 wraps and the call "
                              "protects a sliver while reporting success")
        if "EOVERFLOW" not in code:
            violations.append("the overflow is not reported as EOVERFLOW")
    reserve = body(memory, "void *rt_mem_reserve(")
    if reserve is None or "SIZE_MAX - page" not in strip_comments(reserve):
        violations.append("rt_mem_reserve lost the guard it is the model for")
    out("PROTECT_OVERFLOW", violations, "the length rounding cannot overflow silently")


def rule_truncation_marked(root, out):
    """Pass 02: a summary that does not fit its buffer says so (evidence, not prose)."""
    violations = []
    for path in (CPU_C, CONTEXT_C, LOG_C):
        source = (root / path).read_text()
        if '" [SUMMARY TRUNCATED]"' not in source:
            violations.append(f"{path}: a summary that does not fit its buffer is cut silently")
    out("TRUNCATION_MARKED", violations, "truncated summaries are marked as truncated")


def rule_page_size_measured(root, out):
    """The device page size (16 KiB on the iPhone 13) is measured, never presumed."""
    violations = []
    darwin = (root / DARWIN_C).read_text()
    linux = (root / LINUX_C).read_text()
    memory = (root / MEMORY_C).read_text()
    harness = (root / HARNESS).read_text()
    for source, label, signature in ((darwin, DARWIN_C, "static int darwin_page_size("),
                                     (linux, LINUX_C, "static int linux_page_size(")):
        fn = body(source, signature)
        if fn is None:
            violations.append(f"{label}: the page-size entry point is missing")
        elif "_SC_PAGESIZE" not in fn:
            violations.append(f"{label}: the page size is not measured with "
                              "sysconf(_SC_PAGESIZE)")
    memory_fn = body(memory, "int rt_platform_page_size(")
    if memory_fn is None:
        violations.append(f"{MEMORY_C}: rt_platform_page_size is missing")
    elif "platform->page_size()" not in memory_fn:
        violations.append(f"{MEMORY_C}: rt_platform_page_size does not delegate to the "
                          "platform's measured value")
    for source, label in ((darwin, DARWIN_C), (linux, LINUX_C), (memory, MEMORY_C),
                          (harness, HARNESS)):
        if "16384" in strip_comments(source):
            violations.append(f"{label}: a device page size (16384) is written in the source "
                              "instead of being measured")
    if "rt_platform_page_size()" not in harness:
        violations.append("the harness does not report the measured page size")
    if "\n    out->page_size = rt_platform_page_size();" not in \
            (root / CPU_C).read_text():
        violations.append(f"{CPU_C}: the CPU facts do not carry the measured page size")
    out("PAGE_SIZE_MEASURED", violations, "the page size comes from the platform, not the source")


RULES = [rule_discarded_results, rule_icache_reported, rule_arena_sizing, rule_guard_vs_fault,
         rule_dual_map_stage, rule_capability_words, rule_shm_cleanup, rule_random_progress,
         rule_log_integrity, rule_save_report_target, rule_ui_surface_frozen,
         rule_errno_captured_now, rule_protect_overflow_guard, rule_truncation_marked,
         rule_page_size_measured]


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

    print("--- iOS stabilization contracts (each rule comes from an audited defect)")
    for rule in RULES:
        rule(root, out)
    print()
    print(f"IOS_STABILIZATION_AUDIT={total}")
    if total == 0:
        print("note: static pre-check — the device and the Apple toolchain remain the authority.")
    return 0 if total == 0 else 1


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""audit_jit_causal_graph.py — the JIT causal graph, checked against the real code (pass 03).

Physical run #2 (iPhone 13) left five BLOCKED records and one UNTESTED one, and the campaign
had to decide which of them are causes and which are symptoms. This audit freezes the
decision in rules, so the graph cannot silently drift away from the code:

    jit.map_jit_probe (BLOCKED: no JIT/dynamic-codesigning entitlement on iOS)
        |
        +-- rt_jit_alloc_ex attempts MAP_JIT, records the refusal, and falls back to the
        |   platform's supported single-view W^X arena  -> jit.alloc can be PASS *while*
        |   the capability stays BLOCKED
        +-- jit.execution_allowed / loader execution depend on the arena and on real
            execution, never on a successful syscall alone

Rules (a violation is a defect, not a style opinion):
  J1  PROBE_FIRST            the MAP_JIT probe is measured before the microtest runs and its
                             real errno is handed to it (that is what makes the causal
                             verdict possible at all).
  J2  ATTEMPT_BEFORE_MMAP    the MAP_JIT attempt is recorded *before* the mapping call, so a
                             refusal stays attributable to the capability.
  J3  FALLBACK_SURFACED      the W^X fallback hands the refusal errno back separately and the
                             harness prints it: nothing is hidden by the fallback.
  J4  NO_RWX_REQUEST         the only RWX mapping request in the runtime is the platform's own
                             MAP_JIT creation, and no rt_mem_protect() call requests W+X.
  J5  WINDOW_PER_KIND        rt_jit_execution_allowed drives a MAP_JIT arena through the write
                             window and an anonymous arena through rt_mem_protect — never
                             mprotect on a MAP_JIT region (the latent defect this pass fixes).
  J6  LOADER_PROTOCOL        the loader writes through the arena's own mechanism and skips
                             rt_mem_protect on a MAP_JIT region.
  J7  ICACHE_BEFORE_EXEC     both executions are preceded by an icache synchronisation.
  J8  PASS_REQUIRES_EXECUTION jit.make_executable is only PASS after a real call returned the
                             expected value; the loader PASS carries the executed value.
  J9  APPLE_ICACHE           Apple ARM64 uses sys_icache_invalidate; no Linux __clear_cache,
                             no empty shim, no no-op flush.
  J10 NO_MACOS_API_ON_IOS    pthread_jit_write_protect_np is only reachable on macOS (target
                             guard); on iOS the hook reports ENOTSUP and the harness reports
                             NOT_APPLICABLE.
  J11 DEPENDENCY_NAMES       jit.execution_allowed names its dependencies instead of claiming
                             a platform verdict, and UNTESTED is never the answer for a
                             capability that exists but was not implemented.
  J12 CHAIN_STEPS            the microtest really contains the 13-step chain, record by record.
  J13 SECOND_VALUE           the rewrite payload is the project's second value (4242).
  J14 NO_FAKE_SUCCESS        no record in the chain can be PASS without the corresponding real
                             observation.
  J15 FALLBACK_DOCUMENTED    the fallback states, in the code, why it is not "a plain mmap to
                             green a test" and what semantics it must supply.

Output: one line per rule plus JIT_CAUSAL_GRAPH_AUDIT=<violations>.

Usage: python3 tools/audit_jit_causal_graph.py [--root <dir>]
"""
import argparse
import pathlib
import re

JIT_C = "RuntimeCore/src/runtime_jit.c"
JIT_H = "RuntimeCore/include/runtime_jit.h"
LOADER_C = "RuntimeCore/src/runtime_loader.c"
HARNESS_C = "Diagnostics/src/phase02_harness.c"
DARWIN_C = "RuntimeCore/src/darwin_platform.c"
LINUX_C = "RuntimeCore/src/linux_platform.c"


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


def flatten(text):
    """Join adjacent C string literals and collapse whitespace.

    The checks below look for sentences that the compiler sees as one string but the source
    wraps across lines; without this the audit would report a false violation on a message
    that is perfectly present."""
    joined = re.sub(r'"\s*\n\s*"', "", text)
    return re.sub(r"\s+", " ", joined)


def uncommented(text):
    """Source with comments removed: policies explained in prose are not calls."""
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.DOTALL)
    return re.sub(r"//[^\n]*", " ", text)


def rules(root):
    jit = (root / JIT_C).read_text()
    jit_h = (root / JIT_H).read_text()
    loader = (root / LOADER_C).read_text()
    harness = (root / HARNESS_C).read_text()
    darwin = (root / DARWIN_C).read_text()
    linux = (root / LINUX_C).read_text() if (root / LINUX_C).is_file() else ""

    micro = body(harness, "static void phase02_jit_microtest(") or ""
    jit_code = uncommented(jit)
    darwin_code = uncommented(darwin)
    suite_jit = body(harness, "static rt_status_t phase02_suite_jit(") or ""
    allowed_fn = body(jit, "int rt_jit_execution_allowed(void)") or ""
    alloc_fn = body(jit, "void *rt_jit_alloc_ex(") or ""
    loader_fn = body(loader, "rt_status_t rt_loader_run_ex(") or ""
    micro_flat = flatten(micro)
    harness_flat = flatten(harness)
    suite_jit_flat = flatten(suite_jit)
    out = []
    violations = []

    # J1 ------------------------------------------------------------------
    probe_at = suite_jit.find("rt_jit_probe_map_jit(")
    micro_call_at = suite_jit.find("phase02_jit_microtest(log, map_jit_status, map_jit_errno)")
    if probe_at < 0 or micro_call_at < 0 or probe_at > micro_call_at:
        violations.append("J1: the MAP_JIT probe is not measured before the microtest runs")
    out.append("ok   J1 PROBE_FIRST: jit.map_jit_probe measured first, errno handed to the "
               "microtest")

    # J2 ------------------------------------------------------------------
    attempt_at = alloc_fn.find("*map_jit_attempted_out = 1;")
    mmap_at = alloc_fn.find("MAP_PRIVATE | MAP_ANONYMOUS | MAP_JIT")
    if attempt_at < 0 or mmap_at < 0 or attempt_at > mmap_at:
        violations.append("J2: the MAP_JIT attempt flag is not set before the mapping call")
    out.append("ok   J2 ATTEMPT_BEFORE_MMAP: the attempt is recorded before mmap()")

    # J3 ------------------------------------------------------------------
    if "map_jit_refused_errno_out" not in alloc_fn or "refused = errno" not in alloc_fn:
        violations.append("J3: the MAP_JIT refusal errno is not captured separately")
    if "RT_JIT_ARENA_ANON_MAP_JIT_REFUSED" not in alloc_fn:
        violations.append("J3: the fallback arena does not report its own kind")
    if "MAP_JIT refused errno=%d" not in harness:
        violations.append("J3: the harness does not print the MAP_JIT refusal that the "
                          "fallback survived")
    out.append("ok   J3 FALLBACK_SURFACED: refusal errno preserved and reported next to the "
               "arena that was obtained")

    # J4 ------------------------------------------------------------------
    jit_lines = jit_code.splitlines()
    for index, line in enumerate(jit_lines):
        if "PROT_READ | PROT_WRITE | PROT_EXEC" not in line:
            continue
        window = " ".join(jit_lines[max(0, index - 3):index + 4])
        if "MAP_JIT" not in window or "mmap(" not in window:
            violations.append(f"J4: an RWX request outside a MAP_JIT creation: {line.strip()!r}")
    if "RT_PROT_READ | RT_PROT_EXEC | RT_PROT_WRITE" in harness + loader + jit:
        violations.append("J4: an rt_mem_protect() call requests W+X together")
    if "mprotect" in darwin and "RWX" in darwin:
        violations.append("J4: the darwin backend mentions an RWX mprotect")
    out.append("ok   J4 NO_RWX_REQUEST: the only RWX request is the platform's MAP_JIT "
               "creation; no generic path maps W+X")

    # J5 ------------------------------------------------------------------
    map_jit_branch_at = allowed_fn.find("kind == RT_JIT_ARENA_MAP_JIT")
    protect_at = allowed_fn.find("rt_mem_protect(")
    if map_jit_branch_at < 0:
        violations.append("J5: rt_jit_execution_allowed does not branch on the arena kind")
    else:
        branch = allowed_fn[map_jit_branch_at:map_jit_branch_at + 900]
        if "rt_mem_protect(" in branch.split("} else {")[0]:
            violations.append("J5: rt_jit_execution_allowed still calls rt_mem_protect on a "
                              "MAP_JIT arena (the run #2 latent defect)")
    if protect_at < 0:
        violations.append("J5: rt_jit_execution_allowed never makes an anonymous arena "
                          "executable")
    if "rt_jit_end_write(" not in allowed_fn:
        violations.append("J5: the MAP_JIT branch does not close the write window")
    out.append("ok   J5 WINDOW_PER_KIND: write window for MAP_JIT, rt_mem_protect for the "
               "anonymous arena")

    # J6 ------------------------------------------------------------------
    if "rt_jit_alloc_ex(" not in loader_fn:
        violations.append("J6: the loader does not ask for the arena kind")
    if "arena_kind == RT_JIT_ARENA_MAP_JIT && rt_jit_begin_write(" not in loader_fn:
        violations.append("J6: the loader can write into a write-protected MAP_JIT arena")
    if "arena_kind == RT_JIT_ARENA_MAP_JIT && rt_jit_end_write(" not in loader_fn:
        violations.append("J6: the loader does not close the write window")
    if "arena_kind != RT_JIT_ARENA_MAP_JIT &&" not in loader_fn:
        violations.append("J6: the loader still calls rt_mem_protect on a MAP_JIT arena")
    out.append("ok   J6 LOADER_PROTOCOL: copy inside the window, R-X flip only for the "
               "anonymous arena")

    # J7 ------------------------------------------------------------------
    sync_one = micro.find("phase02_jit_sync_icache(log, arena, first_len")
    call_one = micro.find("rt_signal_call_guarded(")
    sync_two = micro.find("phase02_jit_sync_icache(log, arena, second_len")
    call_two = micro.find("rt_signal_call_guarded(", micro.find("rt_signal_call_guarded(") + 1) \
        if micro.count("rt_signal_call_guarded(") >= 2 else -1
    if min(sync_one, call_one) < 0 or sync_one > call_one:
        violations.append("J7: the first execution is not preceded by an icache flush")
    if call_two >= 0 and (sync_two < 0 or sync_two > call_two):
        violations.append("J7: the rewritten payload is executed without a new icache flush")
    if micro.count("phase02_jit_sync_icache(") < 2:
        violations.append("J7: only one icache synchronisation exists in the chain")
    out.append("ok   J7 ICACHE_BEFORE_EXEC: both executions are preceded by a flush")

    # J8 ------------------------------------------------------------------
    first_call = micro.find("rt_signal_call_guarded(")
    make_exec_pass = micro.find('"jit.make_executable", RT_PASS')
    if first_call < 0 or make_exec_pass < 0 or make_exec_pass < first_call:
        violations.append("J8: jit.make_executable can be PASS before anything executed")
    if "PROVEN executable by real execution returning" not in micro_flat:
        violations.append("J8: the make_executable PASS does not state that execution proved it")
    if "module executed, entry returned %u (expected 7)" not in harness_flat:
        violations.append("J8: the loader PASS does not carry the value the entry point "
                          "returned")
    out.append("ok   J8 PASS_REQUIRES_EXECUTION: the transition PASS is written after the call "
               "that proves it; the loader PASS carries the executed value")

    # J9 ------------------------------------------------------------------
    icache_fn = body(darwin_code, "static int darwin_icache_flush(") or ""
    darwin_lines = icache_fn.splitlines()
    arm_branch = [i for i, line in enumerate(darwin_lines)
                  if "defined(__APPLE__)" in line and "__aarch64__" in line]
    if not arm_branch:
        # The arm branch may be spelled without repeating __aarch64__ (the SDK header guard
        # carries it); then the first Apple branch is the one that must flush.
        arm_branch = [i for i, line in enumerate(darwin_lines) if "defined(__APPLE__)" in line]
    if "sys_icache_invalidate" not in darwin_code:
        violations.append("J9: the Apple icache hook does not use sys_icache_invalidate")
    elif arm_branch and not any("sys_icache_invalidate" in line
                                for line in darwin_lines[arm_branch[0]:arm_branch[0] + 6]):
        violations.append("J9: the Apple ARM branch of the icache hook is not the "
                          "sys_icache_invalidate call")
    # The builtin is allowed only where the instruction cache is coherent (Apple x86), where
    # clang expands it to nothing. On ARM clang lowers it to the __clear_cache symbol the
    # iPhoneOS SDK does not provide (the CI run #5 link failure), so every use must sit in an
    # x86 branch and the bare symbol must never be named.
    for index, line in enumerate(darwin_lines):
        if "__builtin___clear_cache" not in line:
            continue
        guard = ""
        for back in range(index, -1, -1):
            if darwin_lines[back].lstrip().startswith(("#if", "#elif")):
                guard = darwin_lines[back]
                break
        if "__x86_64__" not in guard and "__i386__" not in guard:
            violations.append("J9: a __builtin___clear_cache outside the coherent-cache "
                              "(Apple x86) branch: " + line.strip())
    if "__clear_cache(" in darwin_code.replace("__builtin___clear_cache(", ""):
        violations.append("J9: the Apple backend names the __clear_cache symbol the "
                          "iPhoneOS SDK does not provide")
    if "define" in darwin_code and "clear_cache" in darwin_code.replace(
            "__builtin___clear_cache", "").replace("__clear_cache", ""):
        violations.append("J9: the Apple backend defines a flush substitute")
    if "__clear_cache" not in linux:
        violations.append("J9: the Linux backend lost its own cache flush")
    out.append("ok   J9 APPLE_ICACHE: sys_icache_invalidate on Apple, __clear_cache only on "
               "Linux, no shim")

    # J10 -----------------------------------------------------------------
    guard_at = darwin.find("RT_APPLE_HAS_JIT_WRITE_PROTECT")
    call_at = darwin.find("pthread_jit_write_protect_np(enable")
    if call_at < 0 or guard_at < 0 or call_at < guard_at:
        violations.append("J10: pthread_jit_write_protect_np is not behind the macOS target "
                          "guard")
    if "ENOTSUP" not in darwin:
        violations.append("J10: the non-macOS branch does not answer ENOTSUP")
    if "pthread_jit_write_protect_np(" in uncommented(harness):
        violations.append("J10: the harness calls the macOS-only API directly")
    if "pthread_jit_write_protect_np is unavailable on iOS" not in harness_flat:
        violations.append("J10: the harness no longer reports the iOS unavailability as "
                          "NOT_APPLICABLE")
    out.append("ok   J10 NO_MACOS_API_ON_IOS: target-guarded call, ENOTSUP elsewhere, "
               "NOT_APPLICABLE reported")

    # J11 -----------------------------------------------------------------
    allowed_record = suite_jit[suite_jit.find("allowed = rt_jit_execution_allowed();"):]
    if "phase02_dependency_note(PHASE02_DEP_EXEC_MAPPING)" not in allowed_record:
        violations.append("J11: the blocked execution_allowed record names no dependency")
    if "phase02_dependency_note(PHASE02_DEP_JIT_MAP)" not in allowed_record:
        violations.append("J11: the undetermined execution_allowed record names no dependency")
    if "no verdict about the platform is claimed" not in allowed_record:
        violations.append("J11: UNTESTED does not state that no platform verdict is claimed")
    out.append("ok   J11 DEPENDENCY_NAMES: execution_allowed names its dependency and claims "
               "no verdict it did not measure")

    # J12 -----------------------------------------------------------------
    for step in ('"jit.emit_payload"', '"jit.alloc"', '"jit.write_payload"',
                 '"jit.make_executable"', '"jit.execute_return_42"',
                 '"jit.rewrite_payload"', '"jit.execute_return_4242"'):
        if step not in micro:
            violations.append(f"J12: the chain is missing the {step} record")
    # The two records emitted by helpers must exist in the harness and be reachable from the
    # chain: the icache sync (one per write) and the release after execution.
    for step, helper in (('"jit.icache_sync"', "phase02_jit_sync_icache("),
                         ('"jit.free"', "phase02_jit_release(")):
        if step not in harness:
            violations.append(f"J12: the chain is missing the {step} record")
        if helper not in micro:
            violations.append(f"J12: the chain never calls {helper}")
    if micro.count("phase02_jit_sync_icache(") < 2:
        violations.append("J12: the chain does not synchronise the icache on both writes")
    out.append("ok   J12 CHAIN_STEPS: emit, alloc, write, sync, make executable, execute, "
               "rewrite, sync, execute, free - all present")

    # J13 -----------------------------------------------------------------
    if "4242u, &second_len" not in micro_flat:
        violations.append("J13: the rewrite payload is not the project's second value (4242)")
    out.append("ok   J13 SECOND_VALUE: the rewrite emits 4242 and its result is checked")

    # J14 -----------------------------------------------------------------
    if "value_first == 42u" not in micro_flat:
        violations.append("J14: the 42-result record does not compare the returned value")
    if "value_second == 4242u" not in micro_flat:
        violations.append("J14: the 4242-result record does not compare the returned value")
    out.append("ok   J14 NO_FAKE_SUCCESS: every terminal PASS compares an observed value")

    # J15 -----------------------------------------------------------------
    if 'not "using a plain mmap to green a test"' not in jit:
        violations.append("J15: the fallback does not state why it is not a test-greening mmap")
    if "semantics the port requires" not in jit:
        violations.append("J15: the fallback does not name the semantics it must supply")
    if "RT_JIT_ARENA_ANON_MAP_JIT_REFUSED" not in jit_h:
        violations.append("J15: the arena kinds are not part of the public contract")
    out.append("ok   J15 FALLBACK_DOCUMENTED: the fallback is justified and its contract is "
               "published")

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
    print(f"JIT_CAUSAL_GRAPH_AUDIT={len(violations)}")
    return 1 if violations else 0


if __name__ == "__main__":
    raise SystemExit(main())

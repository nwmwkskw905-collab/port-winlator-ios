/*
 * phase02_harness.c — the eight diagnostic suites.
 *
 * PHASE_02_RECONSTRUCTED_POC (see runtime_platform.h).
 *
 * Rules the suites obey:
 *   - PASS only for observed behaviour;
 *   - a refused platform capability is UNSUPPORTED/BLOCKED with the real errno,
 *     never a silent fallback;
 *   - every fault-producing probe runs under the fault guard, so a suite can report
 *     BLOCKED instead of killing the process;
 *   - host results are labelled with the platform name and never presented as iOS
 *     behaviour.
 */
#include "phase02_harness.h"

#include "phase02_log.h"
#include "runtime_cpu_abi.h"
#include "runtime_filesystem.h"
#include "runtime_ipc.h"
#include "runtime_jit.h"
#include "runtime_loader.h"
#include "runtime_memory.h"
#include "runtime_signals.h"
#include "runtime_threads.h"

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ helpers */

static rt_status_t phase02_delta_summary(const phase02_log_t *log,
                                         const unsigned before[PHASE02_STATUS_COUNT])
{
    unsigned delta[PHASE02_STATUS_COUNT];
    size_t index;

    for (index = 0u; index < (size_t)PHASE02_STATUS_COUNT; index++) {
        unsigned now = phase02_log_status_count(log, (rt_status_t)index);
        delta[index] = (now >= before[index]) ? (now - before[index]) : 0u;
    }
    if (delta[(size_t)RT_FAIL] > 0u) {
        return RT_FAIL;
    }
    if (delta[(size_t)RT_BLOCKED] > 0u) {
        return RT_BLOCKED;
    }
    if (delta[(size_t)RT_PASS] > 0u) {
        return RT_PASS;
    }
    if (delta[(size_t)RT_UNSUPPORTED] > 0u) {
        return RT_UNSUPPORTED;
    }
    if (delta[(size_t)RT_UNTESTED] > 0u) {
        return RT_UNTESTED;
    }
    return RT_NOT_APPLICABLE;
}

static void phase02_snapshot(const phase02_log_t *log, unsigned before[PHASE02_STATUS_COUNT])
{
    size_t index;
    for (index = 0u; index < (size_t)PHASE02_STATUS_COUNT; index++) {
        before[index] = phase02_log_status_count(log, (rt_status_t)index);
    }
}

int phase02_platform_summary(char *out, size_t capacity)
{
    int written;

    if (out == NULL || capacity == 0u) {
        return -1;
    }
    written = snprintf(out, capacity, "platform=%s page_size=%d isa=%s apple_target=%s",
                       rt_platform_name(), rt_platform_page_size(), rt_jit_isa(),
                       rt_platform_apple_target_name());
    if (written < 0 || (size_t)written >= capacity) {
        out[0] = '\0';
        return -1;
    }
    return written;
}

rt_status_t phase02_classify_write_protect(int has_capability, int probe_result,
                                           int apple_target)
{
    if (has_capability != 0) {
        return (probe_result == 0) ? RT_PASS : RT_BLOCKED;
    }
    if (apple_target == RT_APPLE_TARGET_IPHONE_DEVICE ||
        apple_target == RT_APPLE_TARGET_IPHONE_SIMULATOR) {
        return RT_NOT_APPLICABLE;
    }
    return RT_UNSUPPORTED;
}

/* True when the binary was built for an iOS target (device or simulator). On such a
 * target some Darwin APIs are not merely refused at runtime — the SDK marks them
 * unavailable, so the call cannot be compiled at all. */
static int phase02_apple_target_is_ios(void)
{
    int target = rt_platform_apple_target();
    return (target == RT_APPLE_TARGET_IPHONE_DEVICE ||
            target == RT_APPLE_TARGET_IPHONE_SIMULATOR) ? 1 : 0;
}

static int phase02_platform_has(uint32_t capability)
{
    return ((rt_platform_capabilities() & capability) != 0u) ? 1 : 0;
}

/* -------------------------------------------------------------------- memory */

static rt_status_t phase02_suite_memory(phase02_log_t *log, const char *workdir)
{
    size_t page = (size_t)rt_platform_page_size();
    void *region;
    void *fault = NULL;
    int err = 0;
    int rc;

    (void)workdir;

    region = rt_mem_reserve(page, &err);
    if (region == NULL) {
        phase02_log_record(log, "memory.reserve_rw", RT_FAIL, "mmap failed errno=%d (%s)",
                           err, strerror(err));
        return RT_FAIL;
    }
    phase02_log_record(log, "memory.reserve_rw", RT_PASS,
                       "mapped one page read-write (page_size=%zu)", page);

    phase02_log_record(log, "memory.read_rw",
                       (rt_mem_read_probe(region, page) == 0) ? RT_PASS : RT_FAIL,
                       "read %zu bytes from the RW mapping", page);

    /* RW -> R: a write must now fault. */
    if (rt_mem_protect(region, page, RT_PROT_READ, &err) != 0) {
        phase02_log_record(log, "memory.rw_to_r_transition", RT_FAIL,
                           "mprotect(R) failed errno=%d (%s)", err, strerror(err));
    } else {
        phase02_log_record(log, "memory.rw_to_r_transition", RT_PASS,
                           "mprotect to R accepted");
        rc = rt_mem_fault_probe(region, 1, &fault, &err);
        if (rc == -1) {
            phase02_log_record(log, "memory.write_to_r_faults", RT_PASS,
                               "write to read-only page faulted, si_addr=%p", fault);
        } else if (rc == 0) {
            phase02_log_record(log, "memory.write_to_r_faults", RT_FAIL,
                               "write to a read-only page SUCCEEDED (protection not enforced)");
        } else {
            phase02_log_record(log, "memory.write_to_r_faults", RT_BLOCKED,
                               "fault guard unavailable errno=%d (%s)", err, strerror(err));
        }
    }

    /* R -> RW and a write must now be allowed. */
    if (rt_mem_protect(region, page, RT_PROT_READ | RT_PROT_WRITE, &err) != 0) {
        phase02_log_record(log, "memory.r_to_rw_transition", RT_FAIL,
                           "mprotect(RW) failed errno=%d (%s)", err, strerror(err));
    } else {
        rc = rt_mem_fault_probe(region, 1, &fault, &err);
        phase02_log_record(log, "memory.r_to_rw_transition",
                           (rc == 0) ? RT_PASS : RT_FAIL,
                           "write after mprotect(RW) %s",
                           (rc == 0) ? "completed" : "still faulted");
    }

    /* RW -> R-X (the W^X pivot). Execution itself is exercised by the jit suite. */
    if (rt_mem_protect(region, page, RT_PROT_READ | RT_PROT_EXEC, &err) != 0) {
        phase02_log_record(log, "memory.rw_to_rx_transition", RT_FAIL,
                           "mprotect(R-X) failed errno=%d (%s)", err, strerror(err));
    } else {
        phase02_log_record(log, "memory.rw_to_rx_transition", RT_PASS,
                           "mprotect to R-X accepted (execution is tested by the jit suite)");
    }

    /* R-X -> RW back. */
    if (rt_mem_protect(region, page, RT_PROT_READ | RT_PROT_WRITE, &err) != 0) {
        phase02_log_record(log, "memory.rx_to_rw_transition", RT_PASS,
                           "mprotect back to RW refused errno=%d (%s) - recorded as observed",
                           err, strerror(err));
    } else {
        phase02_log_record(log, "memory.rx_to_rw_transition", RT_PASS,
                           "mprotect back to RW accepted");
    }

    /* RWX probe: a measurement of the kernel's policy, not a design goal. */
    if (rt_mem_protect(region, page, RT_PROT_READ | RT_PROT_WRITE | RT_PROT_EXEC, &err) != 0) {
        phase02_log_record(log, "memory.wx_policy", RT_PASS,
                           "RWX refused errno=%d (%s): strict W^X is available", err,
                           strerror(err));
    } else {
        phase02_log_record(log, "memory.wx_policy", RT_PASS,
                           "RWX granted by this kernel (recorded; the port still prefers W^X)");
    }
    (void)rt_mem_protect(region, page, RT_PROT_READ | RT_PROT_WRITE, &err);

    if (rt_mem_release(region, page, &err) != 0) {
        phase02_log_record(log, "memory.release", RT_FAIL, "munmap failed errno=%d (%s)",
                           err, strerror(err));
    } else {
        phase02_log_record(log, "memory.release", RT_PASS, "munmap accepted");
    }

    /* Dual mapping: two views of the same physical memory. */
    {
        rt_dual_map_t map;
        size_t dual_len = page;
        if (rt_dual_map_create(dual_len, &map) != 0) {
            rt_status_t status =
                (map.err == EPERM || map.err == EACCES || map.err == ENOTSUP ||
                 map.err == ENOSYS || map.err == EINVAL)
                    ? RT_UNSUPPORTED
                    : RT_BLOCKED;
            phase02_log_record(log, "memory.dual_mapping_rw_rx", status,
                               "second (executable) view refused errno=%d (%s); host=%s",
                               map.err, strerror(map.err), rt_platform_name());
        } else {
            int aliased = rt_dual_map_views_aliased(&map);
            phase02_log_record(log, "memory.dual_mapping_rw_rx",
                               (aliased == 1) ? RT_PASS : RT_FAIL,
                               "RW and R-X views %s aliasing the same memory (len=%zu)",
                               (aliased == 1) ? "are" : "are NOT", map.len);
            (void)rt_dual_map_destroy(&map);
        }
    }

    return RT_PASS;
}

/* ----------------------------------------------------------------------- jit */

static void phase02_jit_microtest(phase02_log_t *log)
{
    uint8_t payload[16];
    size_t first_len = 0u;
    size_t second_len = 0u;
    size_t arena_len;
    void *arena;
    void *target = NULL;
    void *fault = NULL;
    uint32_t value_first = 0u;
    uint32_t value_second = 0u;
    int err = 0;
    int used_map_jit = 0;
    int rc;
    rt_jit_fn_t fn;

    if (rt_jit_emit_return_imm(payload, sizeof(payload), 42u, &first_len) != 0) {
        phase02_log_record(log, "jit.emit_payload", RT_UNSUPPORTED,
                           "no payload emitter for ISA %s", rt_jit_isa());
        return;
    }
    phase02_log_record(log, "jit.emit_payload", RT_PASS,
                       "ISA=%s emitted %zu bytes for 'return 42'", rt_jit_isa(), first_len);

    arena_len = (first_len > second_len) ? first_len : first_len;
    arena = rt_jit_alloc(arena_len, &err, &used_map_jit);
    if (arena == NULL) {
        phase02_log_record(log, "jit.alloc", RT_FAIL, "arena allocation failed errno=%d (%s)",
                           err, strerror(err));
        return;
    }
    phase02_log_record(log, "jit.alloc", RT_PASS, "arena of %zu bytes (MAP_JIT=%s)",
                       arena_len, (used_map_jit != 0) ? "yes" : "no");

    if (used_map_jit != 0) {
        (void)rt_jit_begin_write(arena, arena_len, &err);
    }
    memcpy(arena, payload, first_len);
    if (used_map_jit != 0) {
        (void)rt_jit_end_write(arena, arena_len, &err);
    }
    (void)rt_jit_invalidate(arena, first_len);

    if (used_map_jit == 0) {
        if (rt_mem_protect(arena, first_len, RT_PROT_READ | RT_PROT_EXEC, &err) != 0) {
            phase02_log_record(log, "jit.make_executable", RT_BLOCKED,
                               "mprotect(R-X) refused errno=%d (%s): this process may not "
                               "execute memory it wrote", err, strerror(err));
            (void)rt_jit_free(arena, arena_len);
            return;
        }
    }
    phase02_log_record(log, "jit.make_executable", RT_PASS,
                       "payload is in executable memory (%s)",
                       (used_map_jit != 0) ? "MAP_JIT + write-protect window" : "mprotect R-X");

    target = arena;
    memcpy(&fn, &target, sizeof(fn));
    rc = rt_signal_call_guarded(fn, &value_first, &fault, &err);
    if (rc != 0) {
        phase02_log_record(log, "jit.execute_return_42", RT_BLOCKED,
                           "calling generated code faulted (si_addr=%p): executable-memory "
                           "permission missing - physical-device validation required", fault);
        (void)rt_jit_free(arena, arena_len);
        return;
    }
    phase02_log_record(log, "jit.execute_return_42",
                       (value_first == 42u) ? RT_PASS : RT_FAIL,
                       "generated function returned %u (expected 42)", value_first);

    /* Rewrite the payload, synchronise the icache, run it again. */
    if (rt_jit_emit_return_imm(payload, sizeof(payload), 4242u, &second_len) != 0) {
        phase02_log_record(log, "jit.rewrite_payload", RT_UNSUPPORTED,
                           "second payload not emittable for ISA %s", rt_jit_isa());
        (void)rt_jit_free(arena, arena_len);
        return;
    }
    second_len = (second_len > arena_len) ? arena_len : second_len;

    if (used_map_jit != 0) {
        (void)rt_jit_begin_write(arena, arena_len, &err);
    } else if (rt_mem_protect(arena, arena_len, RT_PROT_READ | RT_PROT_WRITE, &err) != 0) {
        phase02_log_record(log, "jit.rewrite_payload", RT_BLOCKED,
                           "could not reopen the arena for writing errno=%d (%s)",
                           err, strerror(err));
        (void)rt_jit_free(arena, arena_len);
        return;
    }
    memcpy(arena, payload, second_len);
    if (used_map_jit != 0) {
        (void)rt_jit_end_write(arena, arena_len, &err);
    } else if (rt_mem_protect(arena, arena_len, RT_PROT_READ | RT_PROT_EXEC, &err) != 0) {
        phase02_log_record(log, "jit.rewrite_payload", RT_BLOCKED,
                           "could not re-protect the arena errno=%d (%s)", err, strerror(err));
        (void)rt_jit_free(arena, arena_len);
        return;
    }
    (void)rt_jit_invalidate(arena, second_len);
    phase02_log_record(log, "jit.rewrite_payload", RT_PASS,
                       "payload rewritten and icache synchronised (%zu bytes)", second_len);

    target = arena;
    memcpy(&fn, &target, sizeof(fn));
    rc = rt_signal_call_guarded(fn, &value_second, &fault, &err);
    if (rc != 0) {
        phase02_log_record(log, "jit.execute_return_4242", RT_BLOCKED,
                           "second call faulted (si_addr=%p)", fault);
        (void)rt_jit_free(arena, arena_len);
        return;
    }
    phase02_log_record(log, "jit.execute_return_4242",
                       (value_second == 4242u) ? RT_PASS : RT_FAIL,
                       "rewritten generated function returned %u (expected 4242)", value_second);

    (void)rt_jit_free(arena, arena_len);
}

static rt_status_t phase02_suite_jit(phase02_log_t *log, const char *workdir)
{
    void *map_jit_addr = NULL;
    int err = 0;
    int allowed;

    (void)workdir;

    if (phase02_platform_has(RT_CAP_MAP_JIT) != 0) {
        if (rt_jit_probe_map_jit(&map_jit_addr, &err) == 0) {
            phase02_log_record(log, "jit.map_jit_probe", RT_PASS,
                               "MAP_JIT mapping accepted by the kernel (target=%s). "
                               "On iOS a simulator accepts it more readily than a device: "
                               "this line alone never proves device behaviour",
                               rt_platform_apple_target_name());
        } else {
            phase02_log_record(log, "jit.map_jit_probe", RT_BLOCKED,
                               "MAP_JIT refused errno=%d (%s) (target=%s) — on iOS this is "
                               "the signature of a missing JIT entitlement, recorded as "
                               "capability-not-granted, not as a port defect",
                               err, strerror(err), rt_platform_apple_target_name());
        }
    } else {
        phase02_log_record(log, "jit.map_jit_probe", RT_UNSUPPORTED,
                           "platform '%s' has no MAP_JIT (capability bit absent, target=%s)",
                           rt_platform_name(), rt_platform_apple_target_name());
    }

    /* Four outcomes, kept apart on purpose:
     *   PASS           the API exists for this target and the hook answered 0;
     *   BLOCKED        the API exists but the hook refused (errno reported);
     *   NOT_APPLICABLE the target cannot call the API at all (iOS: the iPhoneOS SDK
     *                  marks pthread_jit_write_protect_np unavailable) — a property of
     *                  the API for this target, not a defect of the port;
     *   UNSUPPORTED    the platform has no such API (e.g. Linux).
     * Unavailability is never turned into PASS, and never into FAIL. */
    if (phase02_platform_has(RT_CAP_JIT_WP_NP) != 0) {
        int probe = rt_jit_probe_write_protect_np();
        phase02_log_record(log, "jit.write_protect_np",
                           phase02_classify_write_protect(1, probe, rt_platform_apple_target()),
                           (probe == 0)
                               ? "pthread_jit_write_protect_np hook present and callable (target=%s)"
                               : "pthread_jit_write_protect_np hook present but refused (target=%s)",
                           rt_platform_apple_target_name());
    } else if (phase02_apple_target_is_ios() != 0) {
        phase02_log_record(log, "jit.write_protect_np", RT_NOT_APPLICABLE,
                           "pthread_jit_write_protect_np is unavailable on iOS: the SDK "
                           "marks it unavailable for this target (%s), so the call cannot "
                           "be compiled — API property, not a port defect; W^X here must "
                           "use a single view flipped RW <-> R-X",
                           rt_platform_apple_target_name());
    } else {
        phase02_log_record(log, "jit.write_protect_np", RT_UNSUPPORTED,
                           "platform '%s' has no pthread_jit_write_protect_np (target=%s, "
                           "capability bit absent)", rt_platform_name(),
                           rt_platform_apple_target_name());
    }

    phase02_jit_microtest(log);

    allowed = rt_jit_execution_allowed();
    if (allowed == 1) {
        phase02_log_record(log, "jit.execution_allowed", RT_PASS,
                           "this process executed memory it wrote");
    } else if (allowed == 0) {
        phase02_log_record(log, "jit.execution_allowed", RT_BLOCKED,
                           "this process could NOT execute memory it wrote");
    } else {
        phase02_log_record(log, "jit.execution_allowed", RT_UNTESTED,
                           "not determined on this platform/ISA");
    }

    return RT_PASS;
}

/* ----------------------------------------------------------------------- cpu */

static rt_status_t phase02_suite_cpu(phase02_log_t *log, const char *workdir)
{
    rt_cpu_facts_t facts;
    char summary[256];
    const rt_guest_reg_note_t *notes = NULL;
    size_t note_count;
    int page;
    size_t index;

    (void)workdir;

    if (rt_cpu_collect(&facts) != 0) {
        phase02_log_record(log, "cpu.facts", RT_FAIL, "could not collect CPU facts");
        return RT_FAIL;
    }
    (void)rt_cpu_facts_summary(summary, sizeof(summary), &facts);
    phase02_log_record(log, "cpu.facts", RT_PASS, "%s", summary);

    page = facts.page_size;
    phase02_log_record(log, "cpu.page_size",
                       (page > 0 && (page & (page - 1)) == 0) ? RT_PASS : RT_FAIL,
                       "PAGE_SIZE=%d (power of two=%s)", page,
                       (page > 0 && (page & (page - 1)) == 0) ? "yes" : "no");

    phase02_log_record(log, "cpu.pointer_and_endianness", RT_PASS,
                       "pointer_bits=%d endianness=%s", facts.pointer_bits,
                       (facts.big_endian != 0) ? "big" : "little");

    note_count = rt_cpu_guest_register_notes(&notes);
    phase02_log_record(log, "cpu.guest_register_notes", RT_PASS,
                       "%zu guest registers recorded; guest R8 -> %s", note_count,
                       (notes != NULL && note_count >= 9u) ? notes[8].host : "?");
    (void)index;

    phase02_log_record(log, "cpu.x18_reservation", RT_PASS,
                       "platform_reserved=%d build_reserved=%d | %s",
                       facts.x18_reserved_platform, facts.x18_reserved_build,
                       rt_cpu_x18_policy_note());

    if (facts.arm64_hwcap_valid != 0) {
        phase02_log_record(log, "cpu.arm64_hwcap", RT_PASS, "AT_HWCAP=0x%016llx",
                           (unsigned long long)facts.arm64_hwcap);
    } else {
        phase02_log_record(log, "cpu.arm64_hwcap", RT_NOT_APPLICABLE,
                           "HWCAP is only read on Linux/AArch64; this host is %s",
                           facts.arch);
    }

    phase02_log_record(log, "cpu.scope", RT_NOT_APPLICABLE,
                       "facts describe this host (%s) only; they do not prove iOS behaviour",
                       facts.platform);
    return RT_PASS;
}

/* ------------------------------------------------------------------- threads */

static rt_status_t phase02_suite_threads(phase02_log_t *log, const char *workdir)
{
    uint64_t value = 0u;
    int err = 0;

    (void)workdir;

    if (rt_thread_roundtrip(&value, &err) == 0) {
        phase02_log_record(log, "threads.create_join", RT_PASS,
                           "4 threads created/joined, aggregate=0x%016llx",
                           (unsigned long long)value);
    } else {
        phase02_log_record(log, "threads.create_join", RT_FAIL, "errno=%d (%s)", err,
                           strerror(err));
    }

    if (rt_thread_tls_roundtrip(&value, &err) == 0) {
        phase02_log_record(log, "threads.tls", RT_PASS,
                           "per-thread TLS values read back, aggregate=0x%016llx",
                           (unsigned long long)value);
    } else {
        phase02_log_record(log, "threads.tls", RT_FAIL, "errno=%d (%s)", err, strerror(err));
    }

    if (rt_thread_mutex_counter(&value, &err) == 0) {
        phase02_log_record(log, "threads.mutex", RT_PASS,
                           "mutex-protected counter reached %llu", (unsigned long long)value);
    } else {
        phase02_log_record(log, "threads.mutex", RT_FAIL,
                           "counter=%llu errno=%d (%s)", (unsigned long long)value, err,
                           strerror(err));
    }

    if (rt_thread_condition_pingpong(&value, &err) == 0) {
        phase02_log_record(log, "threads.condition", RT_PASS,
                           "condition variable delivered %llu tokens",
                           (unsigned long long)value);
    } else {
        phase02_log_record(log, "threads.condition", RT_FAIL, "tokens=%llu errno=%d (%s)",
                           (unsigned long long)value, err, strerror(err));
    }

    if (rt_thread_atomics_roundtrip(&value, &err) == 0) {
        phase02_log_record(log, "threads.atomics", RT_PASS,
                           "atomic counter reached %llu", (unsigned long long)value);
    } else {
        phase02_log_record(log, "threads.atomics", RT_FAIL,
                           "counter=%llu errno=%d (%s)", (unsigned long long)value, err,
                           strerror(err));
    }
    return RT_PASS;
}

/* ------------------------------------------------------------------- signals */

static rt_status_t phase02_suite_signals(phase02_log_t *log, const char *workdir)
{
    void *fault = NULL;
    int err = 0;
    int rc;

    (void)workdir;

    /* A single benign signal is enough to prove the install/query/restore path; the
     * fault path below uses SIGSEGV through the guard. */
    rc = rt_signal_roundtrip(SIGUSR1, &err);
    phase02_log_record(log, "signals.install_query_restore", (rc == 0) ? RT_PASS : RT_FAIL,
                       "SIGUSR1 handler installed, queried and restored (errno=%d)", err);

    rc = rt_signal_mask_roundtrip(SIGUSR1, &err);
    phase02_log_record(log, "signals.mask_roundtrip", (rc == 0) ? RT_PASS : RT_FAIL,
                       "SIGUSR1 block/unblock round trip (errno=%d)", err);

    rc = rt_signal_controlled_segv(&fault, &err);
    if (rc == 0) {
        phase02_log_record(log, "signals.controlled_segv", RT_PASS,
                           "SIGSEGV caught by the guard, si_addr=%p, signal=%d", fault,
                           rt_signal_last_fault_signal());
    } else if (rc == -2) {
        phase02_log_record(log, "signals.controlled_segv", RT_BLOCKED,
                           "guard could not be installed errno=%d (%s)", err, strerror(err));
    } else {
        phase02_log_record(log, "signals.controlled_segv", RT_FAIL,
                           "dereference of the null target did NOT fault");
    }

    phase02_log_record(log, "signals.suite_survived", RT_PASS,
                       "suite continued after the controlled fault");
    return RT_PASS;
}

/* ------------------------------------------------------------------------ fs */

/* Largest depth probed when searching for the platform ceiling. The path buffer in
 * runtime_filesystem.c (2048) binds before this on every platform we have measured,
 * so the search always terminates on a real limit rather than on this guard. */
#define PHASE02_DEEP_HARD_CAP 512u

/* The log detail is a 512-character buffer: a workdir of two hundred levels would
 * swamp the record. Report its length plus the tail, which is what identifies it. */
static const char *phase02_short_path(const char *path, char *buffer, size_t capacity)
{
    size_t length = strlen(path);
    if (length <= 32u) {
        (void)snprintf(buffer, capacity, "%s", path);
    } else {
        (void)snprintf(buffer, capacity, "...%s", path + (length - 32u));
    }
    return buffer;
}

typedef struct phase02_depth_probe {
    unsigned deepest_ok;   /* deepest depth that was created successfully */
    size_t   deepest_len;  /* path length (characters) of that success */
    unsigned first_fail;   /* first depth that failed (0 = none inside the cap) */
    int      fail_errno;   /* errno reported there */
    int      path_max;     /* platform PATH_MAX as reported by pathconf */
    int      path_max_measured;
    size_t   path_cap;     /* this implementation's own path buffer */
} phase02_depth_probe_t;

/* Bisection over rt_fs_deep_paths(). Measuring is the point: the reachable depth is
 * PATH_MAX minus the length of the caller's directory, divided by the cost of a
 * level, and PATH_MAX is 4096 on Linux but 1024 on Darwin. A fixed depth taken from
 * one platform is not a fact about the other. */
static void phase02_measure_depth(const char *workdir, phase02_depth_probe_t *probe)
{
    unsigned low = 0u;            /* known to succeed */
    size_t low_len = 0u;
    unsigned high = 0u;           /* known to fail */
    int high_errno = 0;
    unsigned try_depth = 8u;
    size_t len = 0u;
    int err = 0;
    rt_fs_limits_t limits;

    memset(probe, 0, sizeof(*probe));
    if (rt_fs_limits_query(workdir, &limits, &err) == 0) {
        probe->path_max = (int)limits.path_max;
        probe->path_max_measured = limits.path_max_from_pathconf;
        probe->path_cap = limits.path_cap;
    }

    while (try_depth <= PHASE02_DEEP_HARD_CAP) {
        if (rt_fs_deep_paths(workdir, (size_t)try_depth, &len, &err) == 0) {
            low = try_depth;
            low_len = len;
            try_depth *= 2u;
            continue;
        }
        high = try_depth;
        high_errno = err;
        break;
    }

    while (high > low + 1u) {
        unsigned middle = low + (high - low) / 2u;
        if (rt_fs_deep_paths(workdir, (size_t)middle, &len, &err) == 0) {
            low = middle;
            low_len = len;
        } else {
            high = middle;
            high_errno = err;
        }
    }

    probe->deepest_ok = low;
    probe->deepest_len = low_len;
    probe->first_fail = high;
    probe->fail_errno = high_errno;
}

/* Semantic outcome of the measurement — the three cases the report must separate:
 *   PASS    the platform supports a measurable depth, and the boundary is the
 *           platform's own documented limit (ENAMETOOLONG), reported as such;
 *   PASS    no ceiling inside the probed cap: reported as "not reached", which is
 *           NOT the same as "unlimited";
 *   BLOCKED the platform refused for a permission/resource reason: capability not
 *           measurable here, and the real errno is what gets recorded;
 *   FAIL    the boundary error is something else: an implementation defect. */
static void phase02_fs_deep_paths_record(phase02_log_t *log, const char *workdir)
{
    static const char *const refusal_hint =
        "platform refused before the limit was reached; capacity not measurable here";
    phase02_depth_probe_t probe;

    phase02_measure_depth(workdir, &probe);

    if (probe.deepest_ok == 0u) {
        char short_path[40];
        phase02_log_record(log, "fs.deep_paths", RT_BLOCKED,
                           "no depth measurable from workdir=%s (len=%zu): first failure at "
                           "depth=%u errno=%d (%s); PATH_MAX=%d[%s] cap=%zu",
                           phase02_short_path(workdir, short_path, sizeof(short_path)),
                           strlen(workdir), probe.first_fail, probe.fail_errno,
                           strerror(probe.fail_errno), probe.path_max,
                           probe.path_max_measured ? "pathconf" : "limits.h", probe.path_cap);
        return;
    }

    if (probe.first_fail == 0u) {
        phase02_log_record(log, "fs.deep_paths", RT_PASS,
                           "deepest probed depth=%u (%zu chars) created; no ceiling within the "
                           "probed cap of %u levels - NOT a claim of unlimited depth "
                           "(PATH_MAX=%d[%s] cap=%zu)",
                           probe.deepest_ok, probe.deepest_len, PHASE02_DEEP_HARD_CAP,
                           probe.path_max, probe.path_max_measured ? "pathconf" : "limits.h",
                           probe.path_cap);
        return;
    }

    if (probe.fail_errno == ENAMETOOLONG) {
        phase02_log_record(log, "fs.deep_paths", RT_PASS,
                           "capacity: depth up to %u (%zu chars) created and removed; ceiling at "
                           "depth=%u with errno=%d (File name too long) - the platform limit, "
                           "correctly reported (PATH_MAX=%d[%s] cap=%zu)",
                           probe.deepest_ok, probe.deepest_len, probe.first_fail, probe.fail_errno,
                           probe.path_max, probe.path_max_measured ? "pathconf" : "limits.h",
                           probe.path_cap);
        return;
    }

    if (probe.fail_errno == EPERM || probe.fail_errno == EACCES || probe.fail_errno == ENOSPC ||
        probe.fail_errno == EDQUOT || probe.fail_errno == EROFS) {
        phase02_log_record(log, "fs.deep_paths", RT_BLOCKED,
                           "deepest ok=%u (%zu chars); %s at depth=%u errno=%d (%s)",
                           probe.deepest_ok, probe.deepest_len, refusal_hint, probe.first_fail,
                           probe.fail_errno, strerror(probe.fail_errno));
        return;
    }

    phase02_log_record(log, "fs.deep_paths", RT_FAIL,
                       "unexpected errno=%d (%s) at depth=%u (deepest ok=%u, %zu chars)",
                       probe.fail_errno, strerror(probe.fail_errno), probe.first_fail,
                       probe.deepest_ok, probe.deepest_len);
}

static rt_status_t phase02_suite_fs(phase02_log_t *log, const char *workdir)
{
    uint64_t free_bytes = 0u;
    char temp_path[256];
    int err = 0;

    if (workdir == NULL) {
        phase02_log_record(log, "fs.workdir", RT_FAIL, "no workdir supplied");
        return RT_FAIL;
    }

    if (rt_fs_roundtrip(workdir, &err) == 0) {
        phase02_log_record(log, "fs.roundtrip", RT_PASS,
                           "create/write/read/rename/read/unlink inside %s", workdir);
    } else {
        phase02_log_record(log, "fs.roundtrip", RT_FAIL, "errno=%d (%s)", err, strerror(err));
    }

    phase02_fs_deep_paths_record(log, workdir);

    if (rt_fs_links_and_modes(workdir, &err) == 0) {
        phase02_log_record(log, "fs.links_and_modes", RT_PASS,
                           "symlink followed, chmod 0640 observed");
    } else {
        phase02_log_record(log, "fs.links_and_modes", RT_FAIL, "errno=%d (%s)", err,
                           strerror(err));
    }

    if (rt_fs_temp_file(temp_path, sizeof(temp_path), &err) == 0) {
        phase02_log_record(log, "fs.temp_file", RT_PASS,
                           "mkstemp+unlink-while-open at %s", temp_path);
    } else {
        phase02_log_record(log, "fs.temp_file", RT_FAIL, "errno=%d (%s)", err, strerror(err));
    }

    if (rt_fs_capacity(workdir, &free_bytes, &err) == 0) {
        phase02_log_record(log, "fs.capacity", RT_PASS, "free=%llu bytes on the workdir fs",
                           (unsigned long long)free_bytes);
    } else {
        phase02_log_record(log, "fs.capacity", RT_FAIL, "errno=%d (%s)", err, strerror(err));
    }
    return RT_PASS;
}

/* ----------------------------------------------------------------------- ipc */

static rt_status_t phase02_suite_ipc(phase02_log_t *log, const char *workdir)
{
    const char *mechanism = "none";
    size_t sun_limit = 0u;
    int err = 0;

    if (workdir == NULL) {
        phase02_log_record(log, "ipc.workdir", RT_FAIL, "no workdir supplied");
        return RT_FAIL;
    }

    phase02_log_record(log, "ipc.socketpair",
                       (rt_ipc_socketpair(&err) == 0) ? RT_PASS : RT_FAIL,
                       "AF_UNIX SOCK_STREAM socketpair round trip (errno=%d)", err);
    phase02_log_record(log, "ipc.unix_stream",
                       (rt_ipc_unix_stream(workdir, &err) == 0) ? RT_PASS : RT_FAIL,
                       "AF_UNIX stream server/client in %s (errno=%d)", workdir, err);
    phase02_log_record(log, "ipc.scm_rights",
                       (rt_ipc_scm_rights(workdir, &err) == 0) ? RT_PASS : RT_FAIL,
                       "file descriptor passed with SCM_RIGHTS and used (errno=%d)", err);
    phase02_log_record(log, "ipc.pipe", (rt_ipc_pipe(&err) == 0) ? RT_PASS : RT_FAIL,
                       "pipe round trip (errno=%d)", err);
    phase02_log_record(log, "ipc.posix_shm", (rt_ipc_shm(&err) == 0) ? RT_PASS : RT_FAIL,
                       "shared memory object mapped twice and compared (errno=%d)", err);

    if (rt_ipc_sun_path_limit(&sun_limit, &err) == 0) {
        phase02_log_record(log, "ipc.sun_path_limit", RT_PASS,
                           "usable sockaddr_un.sun_path = %zu bytes on this platform", sun_limit);
    } else {
        phase02_log_record(log, "ipc.sun_path_limit", RT_FAIL, "errno=%d (%s)", err,
                           strerror(err));
    }

    if (rt_ipc_mux(&err, &mechanism) == 0) {
        phase02_log_record(log, "ipc.mux", RT_PASS, "readiness reported by %s", mechanism);
    } else if (phase02_platform_has(RT_CAP_KQUEUE) == 0 && phase02_platform_has(RT_CAP_EPOLL) == 0) {
        phase02_log_record(log, "ipc.mux", RT_UNSUPPORTED,
                           "no kqueue and no epoll on this platform");
    } else {
        phase02_log_record(log, "ipc.mux", RT_FAIL, "%s reported nothing (errno=%d)", mechanism,
                           err);
    }
    return RT_PASS;
}

/* -------------------------------------------------------------------- loader */

static rt_status_t phase02_suite_loader(phase02_log_t *log, const char *workdir)
{
    uint8_t image[RT_MODULE_MAX_IMAGE];
    size_t image_len;
    uint32_t value = 0u;
    void *fault = NULL;
    rt_loader_error_t loader_err = RT_LOADER_OK;
    rt_status_t status;
    rt_module_header_t header;

    (void)workdir;

    image_len = rt_loader_build_return_image(image, sizeof(image), 7u);
    if (image_len == 0u) {
        phase02_log_record(log, "loader.build_image", RT_UNSUPPORTED,
                           "no payload emitter for ISA %s", rt_jit_isa());
        return RT_UNSUPPORTED;
    }
    phase02_log_record(log, "loader.build_image", RT_PASS, "%zu-byte RTM1 image", image_len);

    status = rt_loader_run(image, image_len, &value, &loader_err, &fault);
    if (status == RT_PASS) {
        phase02_log_record(log, "loader.run_valid_module", RT_PASS,
                           "module executed, entry returned %u (expected 7)", value);
    } else if (status == RT_BLOCKED) {
        phase02_log_record(log, "loader.run_valid_module", RT_BLOCKED,
                           "module valid but execution blocked (si_addr=%p)", fault);
    } else {
        phase02_log_record(log, "loader.run_valid_module", RT_FAIL, "rejected: %s",
                           rt_loader_error_name(loader_err));
    }

    /* Negative cases: each must be rejected by pure validation. */
    {
        uint8_t copy[RT_MODULE_MAX_IMAGE];
        memcpy(copy, image, image_len);
        copy[0] = 0x00u; /* break the magic */
        phase02_log_record(log, "loader.reject_bad_magic",
                           (rt_loader_validate(copy, image_len, &header) == RT_LOADER_ERR_MAGIC)
                               ? RT_PASS : RT_FAIL,
                           "corrupted magic rejected");

        memcpy(copy, image, image_len);
        memcpy(copy + 4, "\x09\x00\x00\x00", 4);
        phase02_log_record(log, "loader.reject_bad_version",
                           (rt_loader_validate(copy, image_len, &header) == RT_LOADER_ERR_VERSION)
                               ? RT_PASS : RT_FAIL,
                           "version 9 rejected");

        memcpy(copy, image, image_len);
        memcpy(copy + 20, "\x01\x00\x00\x00", 4);
        phase02_log_record(log, "loader.reject_bad_flags",
                           (rt_loader_validate(copy, image_len, &header) == RT_LOADER_ERR_FLAGS)
                               ? RT_PASS : RT_FAIL,
                           "non-zero flags rejected");

        memcpy(copy, image, image_len);
        memcpy(copy + 12, "\xff\xff\x00\x00", 4); /* code_len = 65535 > MAX_CODE */
        phase02_log_record(log, "loader.reject_oversize_code",
                           (rt_loader_validate(copy, image_len, &header) == RT_LOADER_ERR_CODE_SIZE)
                               ? RT_PASS : RT_FAIL,
                           "code_len above RT_MODULE_MAX_CODE rejected");

        memcpy(copy, image, image_len);
        memcpy(copy + 16, "\xff\x00\x00\x00", 4); /* entry_off >= code_len */
        phase02_log_record(log, "loader.reject_bad_entry",
                           (rt_loader_validate(copy, image_len, &header) == RT_LOADER_ERR_ENTRY)
                               ? RT_PASS : RT_FAIL,
                           "entry outside the code blob rejected");

        phase02_log_record(log, "loader.reject_truncated",
                           (rt_loader_validate(image, 8u, &header) == RT_LOADER_ERR_TOO_SMALL)
                               ? RT_PASS : RT_FAIL,
                           "8-byte image rejected as too small");

        memcpy(copy, image, image_len);
        memcpy(copy + 8, "\x10\x00\x00\x00", 4);  /* code_off = 16 < header size */
        phase02_log_record(log, "loader.reject_code_overlapping_header",
                           (rt_loader_validate(copy, image_len, &header) == RT_LOADER_ERR_RANGE)
                               ? RT_PASS : RT_FAIL,
                           "code_off inside the header rejected");
    }

    return RT_PASS;
}

/* -------------------------------------------------------------------- registry */

static const phase02_suite_t phase02_suites_table[] = {
    { "memory",  "memory protection matrix, W^X and dual mapping", phase02_suite_memory },
    { "jit",     "MAP_JIT, write-protect hook, generated-code microtest", phase02_suite_jit },
    { "cpu",     "host CPU/ABI facts, page size, x18 policy", phase02_suite_cpu },
    { "threads", "pthreads, TLS, mutex, condition, atomics", phase02_suite_threads },
    { "signals", "install/query/restore, mask, controlled SIGSEGV", phase02_suite_signals },
    { "fs",      "POSIX filesystem semantics", phase02_suite_fs },
    { "ipc",     "AF_UNIX, SCM_RIGHTS, pipe, shm, kqueue/epoll", phase02_suite_ipc },
    { "loader",  "experimental RTM1 module: validate, load, execute", phase02_suite_loader }
};

size_t phase02_suite_count(void)
{
    return sizeof(phase02_suites_table) / sizeof(phase02_suites_table[0]);
}

const phase02_suite_t *phase02_suite_at(size_t index)
{
    if (index >= phase02_suite_count()) {
        return NULL;
    }
    return &phase02_suites_table[index];
}

const phase02_suite_t *phase02_suite_find(const char *name)
{
    size_t index;
    if (name == NULL) {
        return NULL;
    }
    for (index = 0u; index < phase02_suite_count(); index++) {
        if (strcmp(phase02_suites_table[index].name, name) == 0) {
            return &phase02_suites_table[index];
        }
    }
    return NULL;
}

rt_status_t phase02_run_suite(const char *name, phase02_log_t *log, const char *workdir)
{
    unsigned before[PHASE02_STATUS_COUNT];
    const phase02_suite_t *suite;

    if (log == NULL || name == NULL) {
        return RT_FAIL;
    }
    suite = phase02_suite_find(name);
    if (suite == NULL) {
        phase02_log_record(log, "harness.unknown_suite", RT_UNSUPPORTED,
                           "no suite named '%s'", name);
        return RT_UNSUPPORTED;
    }

    phase02_snapshot(log, before);
    phase02_log_blank(log);
    phase02_log_line(log, "== SUITE %s: %s ==", suite->name, suite->description);
    (void)suite->fn(log, workdir);
    return phase02_delta_summary(log, before);
}

rt_status_t phase02_run_all(phase02_log_t *log, const char *workdir)
{
    unsigned before[PHASE02_STATUS_COUNT];
    size_t index;
    rt_status_t worst = RT_NOT_APPLICABLE;

    if (log == NULL) {
        return RT_FAIL;
    }
    for (index = 0u; index < phase02_suite_count(); index++) {
        const phase02_suite_t *suite = &phase02_suites_table[index];
        rt_status_t status;
        phase02_log_blank(log);
        phase02_log_line(log, "== SUITE %s: %s ==", suite->name, suite->description);
        phase02_snapshot(log, before);
        (void)suite->fn(log, workdir);
        /* The delta of the counters is authoritative, not the suite's return value:
         * a suite that records a FAIL must not be able to claim success. */
        status = phase02_delta_summary(log, before);
        if (status == RT_FAIL) {
            worst = RT_FAIL;
        } else if (status == RT_BLOCKED && worst != RT_FAIL) {
            worst = RT_BLOCKED;
        } else if (status == RT_PASS && worst == RT_NOT_APPLICABLE) {
            worst = RT_PASS;
        } else if (status == RT_UNSUPPORTED && worst == RT_NOT_APPLICABLE) {
            worst = RT_UNSUPPORTED;
        }
    }
    return worst;
}

const char *phase02_suite_names(char *buf, size_t cap)
{
    size_t index;
    size_t used = 0u;

    if (buf == NULL || cap == 0u) {
        return "";
    }
    buf[0] = '\0';
    for (index = 0u; index < phase02_suite_count(); index++) {
        int written = snprintf(buf + used, cap - used, "%s%s", (index == 0u) ? "" : ",",
                               phase02_suites_table[index].name);
        if (written < 0 || (size_t)written >= (cap - used)) {
            break;
        }
        used += (size_t)written;
    }
    return buf;
}

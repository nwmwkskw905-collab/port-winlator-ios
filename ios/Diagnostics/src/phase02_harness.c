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

/* ------------------------------------------------- causal classification (see header) */

rt_status_t phase02_classify(phase02_outcome_t outcome, rt_status_t dependency_status)
{
    switch (outcome) {
    case PHASE02_OUTCOME_OK:
        /* PASS requires observed success: this is the only path to it — plus the case below,
         * which is also an observation (a measured capacity), never an assumption. */
        return RT_PASS;
    case PHASE02_OUTCOME_OK_PROBE_LIMIT:
        return RT_PASS;
    case PHASE02_OUTCOME_RUNTIME_DEFECT:
        /* Including the case where the dependency PROVED the capability was available. */
        return RT_FAIL;
    case PHASE02_OUTCOME_CAPABILITY_MISSING:
        return RT_BLOCKED;
    case PHASE02_OUTCOME_NOT_APPLICABLE:
        return RT_NOT_APPLICABLE;
    case PHASE02_OUTCOME_UNSUPPORTED:
        return RT_UNSUPPORTED;
    case PHASE02_OUTCOME_DEPENDENCY_BLOCKED:
        /* Explicit causality: only a dependency that did NOT pass makes this BLOCKED.
         * If the dependency passed, the capability is present and this is a defect. */
        return (dependency_status == RT_PASS) ? RT_FAIL : RT_BLOCKED;
    case PHASE02_OUTCOME_UNDETERMINED:
        return RT_UNTESTED;
    }
    return RT_UNTESTED;
}

const char *phase02_outcome_name(phase02_outcome_t outcome)
{
    switch (outcome) {
    case PHASE02_OUTCOME_OK:                return "ok";
    case PHASE02_OUTCOME_OK_PROBE_LIMIT:    return "ok(probe-limit)";
    case PHASE02_OUTCOME_RUNTIME_DEFECT:    return "runtime-defect";
    case PHASE02_OUTCOME_CAPABILITY_MISSING:return "capability-missing";
    case PHASE02_OUTCOME_NOT_APPLICABLE:    return "not-applicable";
    case PHASE02_OUTCOME_UNSUPPORTED:       return "unsupported";
    case PHASE02_OUTCOME_DEPENDENCY_BLOCKED:return "dependency-blocked";
    case PHASE02_OUTCOME_UNDETERMINED:      return "undetermined";
    }
    return "undetermined";
}

const char *phase02_dependency_name(phase02_dependency_t dependency)
{
    switch (dependency) {
    case PHASE02_DEP_NONE:                return "none";
    case PHASE02_DEP_JIT_MAP:             return "MAP_JIT";
    case PHASE02_DEP_JIT_WRITE_PROTECT:   return "pthread_jit_write_protect_np";
    case PHASE02_DEP_EXEC_MAPPING:        return "executable mapping";
    case PHASE02_DEP_POSIX_SHM:           return "POSIX shared memory";
    }
    return "none";
}

const char *phase02_dependency_test(phase02_dependency_t dependency)
{
    switch (dependency) {
    case PHASE02_DEP_NONE:                return "<none>";
    case PHASE02_DEP_JIT_MAP:             return "jit.map_jit_probe";
    case PHASE02_DEP_JIT_WRITE_PROTECT:   return "jit.write_protect_np";
    case PHASE02_DEP_EXEC_MAPPING:        return "jit.make_executable";
    case PHASE02_DEP_POSIX_SHM:           return "ipc.posix_shm";
    }
    return "<none>";
}

phase02_outcome_t phase02_classify_jit_alloc(int map_jit_attempted,
                                             rt_status_t map_jit_probe_status,
                                             int map_jit_probe_errno,
                                             int alloc_errno)
{
    /* Not the MAP_JIT path at all: a plain mapping failed. That has nothing to do with the
     * MAP_JIT capability and must be reported as a defect. */
    if (map_jit_attempted == 0) {
        return PHASE02_OUTCOME_RUNTIME_DEFECT;
    }
    /* MAP_JIT was requested and the probe that measures exactly that capability passed:
     * the platform offered it and the allocation still failed -> defect. */
    if (map_jit_probe_status == RT_PASS) {
        return PHASE02_OUTCOME_RUNTIME_DEFECT;
    }
    /* MAP_JIT was requested, the probe was blocked, and both saw the same refusal: one
     * missing capability, one cause. Equality is required so that a *different* failure
     * cannot be folded into the probe's verdict. */
    if (map_jit_probe_status == RT_BLOCKED && alloc_errno != 0 &&
        alloc_errno == map_jit_probe_errno) {
        return PHASE02_OUTCOME_DEPENDENCY_BLOCKED;
    }
    /* Everything else — including a refusal whose errno we could not compare, because it
     * was lost — is reported as a defect, not excused as "the capability". */
    return PHASE02_OUTCOME_RUNTIME_DEFECT;
}

phase02_outcome_t phase02_classify_fs_depth_error(int stage, int err, int longest_path,
                                                  int path_max, int path_max_from_pathconf)
{
    (void)path_max_from_pathconf;

    /* An error whose errno was not preserved is a defect of the error path that reported
     * it: it tells us nothing about the platform, and it is exactly what physical run #1
     * printed as "unexpected errno=0 (Undefined error: 0)". It must never become PASS and
     * must never be excused as a platform limit. */
    if (err == 0) {
        return PHASE02_OUTCOME_RUNTIME_DEFECT;
    }
    /* Our own probe buffer ran out before the platform did. This is checked BEFORE the
     * ENAMETOOLONG rule below, because that branch also reports ENAMETOOLONG: the ceiling
     * that was reached is this probe's, not the platform's. The depth that WAS created is a
     * real measurement, so this is PASS — but the record must say where the ceiling came
     * from, and the caller branches on the stage to say exactly that. Reporting it as "the
     * platform limit" would be a false claim; reporting it as BLOCKED would throw away a
     * measurement that did happen. */
    if (stage == RT_FS_STAGE_PATH_CAP || stage == RT_FS_STAGE_LEAF_JOIN) {
        return PHASE02_OUTCOME_OK_PROBE_LIMIT;
    }
    /* The platform's documented limit: reaching it IS the measurement. */
    if (err == ENAMETOOLONG) {
        return PHASE02_OUTCOME_OK;
    }
    /* The platform refused for a permission/resource reason: capability not granted to
     * this process, not a defect of the port. */
    if (err == EPERM || err == EACCES || err == ENOSPC || err == EDQUOT || err == EROFS) {
        return PHASE02_OUTCOME_CAPABILITY_MISSING;
    }
    if (err == ENOTSUP || err == ENOSYS) {
        return PHASE02_OUTCOME_UNSUPPORTED;
    }
    (void)longest_path;
    (void)path_max;
    return PHASE02_OUTCOME_RUNTIME_DEFECT;
}

phase02_outcome_t phase02_classify_loader_result(rt_status_t status, rt_loader_error_t reason,
                                                 int map_jit_attempted,
                                                 rt_status_t probe_status, int os_err)
{
    if (status == RT_PASS) {
        return PHASE02_OUTCOME_OK;
    }
    if (reason == RT_LOADER_OK) {
        /* The physical run #1 symptom: a non-PASS result carrying no reason. A rejection must
         * always name its cause; "rejected: OK" is a contradiction, not a finding. */
        return PHASE02_OUTCOME_RUNTIME_DEFECT;
    }
    if (reason == RT_LOADER_ERR_JIT_UNAVAILABLE) {
        /* A valid module that could not get an executable arena is the same capability
         * jit.map_jit_probe measures — but only if that probe really saw a refusal in this
         * process and the errno of the failing allocation is known. If the probe PASSED, the
         * capability was granted and the failure is ours. */
        if (map_jit_attempted != 0 && probe_status != RT_PASS && os_err != 0) {
            return PHASE02_OUTCOME_DEPENDENCY_BLOCKED;
        }
        return PHASE02_OUTCOME_RUNTIME_DEFECT;
    }
    if (reason == RT_LOADER_ERR_EXEC_FAULT) {
        /* The module was accepted and mapped and its entry point still could not execute:
         * the platform did not give this process executable memory. */
        return PHASE02_OUTCOME_CAPABILITY_MISSING;
    }
    if (status == RT_FAIL) {
        /* A rejected image: the loader did its job and said why. Correct behaviour. */
        return PHASE02_OUTCOME_OK;
    }
    if (reason == RT_LOADER_ERR_INTERNAL) {
        return PHASE02_OUTCOME_RUNTIME_DEFECT;
    }
    /* BLOCKED with none of the reasons above: cause not established here. */
    return PHASE02_OUTCOME_UNDETERMINED;
}

phase02_outcome_t phase02_classify_dual_mapping_error(int stage, int err)
{
    if (stage == RT_DUAL_STAGE_NONE) {
        return PHASE02_OUTCOME_OK;
    }
    if (err == 0) {
        return PHASE02_OUTCOME_RUNTIME_DEFECT;   /* an error with no errno is a defect */
    }
    if (stage == RT_DUAL_STAGE_ALIAS_CHECK) {
        /* Both mappings were granted and the contents disagree: this is our mapping, not
         * the platform. */
        return PHASE02_OUTCOME_RUNTIME_DEFECT;
    }
    if (err == ENOSYS || err == ENOTSUP || err == EOPNOTSUPP) {
        return PHASE02_OUTCOME_UNSUPPORTED;
    }
    if (err == EPERM || err == EACCES) {
        /* Refused, not missing: the mechanism exists and this process may not use it —
         * the same classification the POSIX shared-memory probe applies to EPERM. */
        return PHASE02_OUTCOME_CAPABILITY_MISSING;
    }
    /* EINVAL, EFAULT, EOVERFLOW …: an errno that fits no contract is not a platform
     * answer, it is a defect of the call. */
    return PHASE02_OUTCOME_RUNTIME_DEFECT;
}

phase02_outcome_t phase02_classify_shm_error(int stage, int err)
{
    if (stage == RT_IPC_STAGE_NONE) {
        return PHASE02_OUTCOME_OK;
    }
    if (stage == RT_IPC_STAGE_UNLINK) {
        /* Only the cleanup failed: the capability was proven by the probe that reached
         * this stage. Reported as a cleanup fact, never as the capability verdict. */
        return PHASE02_OUTCOME_OK;
    }
    if (err == 0) {
        return PHASE02_OUTCOME_RUNTIME_DEFECT;   /* an error with no errno is a defect */
    }
    /* The two views of the same object disagreed: the platform accepted every syscall and
     * our mapping is not actually shared. That is a defect of this PoC, not a limit. */
    if (stage == RT_IPC_STAGE_COMPARE) {
        return PHASE02_OUTCOME_RUNTIME_DEFECT;
    }
    if (err == ENOSYS || err == ENOTSUP || err == EOPNOTSUPP) {
        return PHASE02_OUTCOME_UNSUPPORTED;
    }
    if (err == EPERM || err == EACCES) {
        /* Measured refusal in this sandbox/signing context: the capability was not
         * granted. Not a defect of the port — and not "unsupported" either, because the
         * mechanism exists; this process may not use it. */
        return PHASE02_OUTCOME_CAPABILITY_MISSING;
    }
    return PHASE02_OUTCOME_RUNTIME_DEFECT;
}

/* Composed one-line note that a record can print, so every causal verdict carries the
 * name of the test that measured the capability it depends on. */
static const char *phase02_dependency_note(phase02_dependency_t dependency)
{
    switch (dependency) {
    case PHASE02_DEP_NONE:              return "no dependency";
    case PHASE02_DEP_JIT_MAP:           return "depends on jit.map_jit_probe (MAP_JIT capability)";
    case PHASE02_DEP_JIT_WRITE_PROTECT: return "depends on jit.write_protect_np";
    case PHASE02_DEP_EXEC_MAPPING:      return "depends on jit.make_executable";
    case PHASE02_DEP_POSIX_SHM:         return "depends on ipc.posix_shm";
    }
    return "no dependency";
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

    (void)workdir; /* used by the iOS dual-mapping backend below, see the note there */

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

    /* Dual mapping: two views of the same physical memory.
     *
     * Two facts have to stay separate, because physical run #2 measured the first one and it
     * would be wrong to read it as the second:
     *   - the NAMED POSIX shared-memory namespace is refused to a third-party iOS app
     *     (shm_open -> EPERM). That is a property of that namespace in the app sandbox;
     *   - whether this device can hold two live views of one object at all (one writable,
     *     one executable).
     * On an Apple iOS target the first refusal therefore selects the iOS backend
     * (rt_dual_map_create_file_backed: a regular file in the app container, mapped
     * MAP_SHARED twice, unlinked immediately). Selection is explicit, per platform, and used
     * only after the named path was refused; Linux and macOS keep the named path untouched.
     * This experiment is a CAPABILITY EXPERIMENT, never an architectural requirement: the
     * port's iOS strategy is a single view flipped RW <-> R-X with the instruction cache
     * synchronised, which run #2 already proved works (memory.rw_to_rx_transition = PASS,
     * memory.wx_policy = PASS). A refusal below is recorded with its real stage and errno and
     * changes no architecture. */
    {
        rt_dual_map_t map;
        size_t dual_len = page;
        int named_ok = (rt_dual_map_create(dual_len, &map) == 0);

        if (!named_ok && phase02_apple_target_is_ios() != 0 && map.err != 0) {
            char detail[512];
            rt_dual_map_t ios_map;
            int named_err = map.err;
            rt_dual_stage_t named_stage = map.stage;
            int ios_rc = rt_dual_map_create_file_backed(workdir, dual_len, &ios_map);

            (void)snprintf(detail, sizeof(detail),
                           "named POSIX object refused at stage=%s errno=%d (%s) - a property "
                           "of the named namespace in the app sandbox, NOT \"shared memory is "
                           "impossible\" and not \"dual mapping is impossible\"; the iOS "
                           "backend (file-backed MAP_SHARED object inside the app container) "
                           "was selected for this target",
                           rt_dual_stage_name(named_stage), named_err, strerror(named_err));
            phase02_log_line(log, "[PHASE02] NOTE=memory.dual_mapping_backend %s", detail);

            if (ios_rc == 0) {
                int aliased = rt_dual_map_views_aliased(&ios_map);
                if (aliased == 1) {
                    phase02_log_record(log, "memory.dual_mapping_rw_rx", RT_PASS,
                                       "iOS backend: RW and R-X views of a file-backed "
                                       "MAP_SHARED object in the app container alias the same "
                                       "memory (len=%zu, stage=%s); the named POSIX namespace "
                                       "remains refused (stage=%s errno=%d, %s) and is not "
                                       "used by the iOS backend",
                                       ios_map.len, rt_dual_stage_name(ios_map.stage),
                                       rt_dual_stage_name(named_stage), named_err,
                                       strerror(named_err));
                } else if (aliased == 0) {
                    phase02_log_record(log, "memory.dual_mapping_rw_rx", RT_FAIL,
                                       "iOS backend: both views were granted and they do NOT "
                                       "alias the same memory (stage=%s errno=%d): defect of "
                                       "this mapping, never a platform property",
                                       rt_dual_stage_name(ios_map.stage), ios_map.err);
                } else {
                    phase02_log_record(log, "memory.dual_mapping_rw_rx", RT_FAIL,
                                       "iOS backend reports itself usable but the alias check "
                                       "could not run: defect of this code");
                }
                (void)rt_dual_map_destroy(&ios_map);
            } else if (ios_map.err == 0) {
                phase02_log_record(log, "memory.dual_mapping_rw_rx", RT_FAIL,
                                   "iOS backend stage=%s failed with errno=0 (errno not "
                                   "preserved): defect of the error path, not a platform "
                                   "answer",
                                   rt_dual_stage_name(ios_map.stage));
            } else {
                /* Classified from the iOS backend's own stage/errno. The record carries both
                 * backends so neither result is hidden, and the capability verdict describes
                 * this device's answer to the experiment, not a requirement of the port. */
                rt_status_t status = phase02_classify(
                    phase02_classify_dual_mapping_error((int)ios_map.stage, ios_map.err),
                    RT_BLOCKED);
                phase02_log_record(log, "memory.dual_mapping_rw_rx", status,
                                   "stage=%s (iOS backend) refused errno=%d (%s) after the "
                                   "named object was refused at stage=%s errno=%d (%s) on "
                                   "host=%s: the port does not depend on this experiment "
                                   "(single-view RW <-> R-X is the iOS strategy and is proven "
                                   "separately), and a refusal here is a capability this "
                                   "process was not granted, not a port defect",
                                   rt_dual_stage_name(ios_map.stage), ios_map.err,
                                   strerror(ios_map.err), rt_dual_stage_name(named_stage),
                                   named_err, strerror(named_err), rt_platform_name());
            }
        } else if (!named_ok) {
            /* Non-Apple target (or a failure without an errno): the original path and its
             * classification are unchanged. */
            rt_status_t status = phase02_classify(
                phase02_classify_dual_mapping_error((int)map.stage, map.err), RT_BLOCKED);
            if (map.err == 0) {
                phase02_log_record(log, "memory.dual_mapping_rw_rx", RT_FAIL,
                                   "stage=%s failed with errno=0 (errno not preserved): "
                                   "defect of the error path, not a platform answer",
                                   rt_dual_stage_name(map.stage));
            } else {
                phase02_log_record(log, "memory.dual_mapping_rw_rx", status,
                                   "stage=%s refused errno=%d (%s) on host=%s: %s",
                                   rt_dual_stage_name(map.stage), map.err, strerror(map.err),
                                   rt_platform_name(),
                                   (status == RT_UNSUPPORTED)
                                       ? "no dual-mapping mechanism on this platform"
                                       : "the mechanism exists and this process/signing "
                                         "context was not granted it - capability, not a "
                                         "port defect");
            }
        } else {
            int aliased = rt_dual_map_views_aliased(&map);
            if (aliased == 1) {
                phase02_log_record(log, "memory.dual_mapping_rw_rx", RT_PASS,
                                   "RW and R-X views are aliasing the same memory (len=%zu, "
                                   "stage=%s)", map.len, rt_dual_stage_name(map.stage));
            } else if (aliased == 0) {
                phase02_log_record(log, "memory.dual_mapping_rw_rx", RT_FAIL,
                                   "stage=%s: both views were granted and they are NOT "
                                   "aliasing the same memory (errno=%d): defect of this "
                                   "mapping, never a platform property",
                                   rt_dual_stage_name(map.stage), map.err);
            } else {
                phase02_log_record(log, "memory.dual_mapping_rw_rx", RT_FAIL,
                                   "alias check could not run on a map that reports itself "
                                   "as usable: defect of this code");
            }
            (void)rt_dual_map_destroy(&map);
        }
    }

    return RT_PASS;
}

/* ----------------------------------------------------------------------- jit */

/* `map_jit_status`/`map_jit_errno` are the outcome of jit.map_jit_probe, measured by the
 * caller. They are what makes the causal verdict possible: an allocation failure on the
 * MAP_JIT path with the same errno the probe saw is ONE missing capability, not a second
 * defect (IPHONE13_PHYSICAL_RUN_01 reported it as jit.alloc = FAIL). */
/* Instruction-cache synchronisation, reported instead of discarded.
 *
 * The old code called rt_jit_invalidate() through `(void)`: if the platform could not
 * synchronise the instruction cache, the JIT still executed the payload and the result was
 * presented as if the flush had happened. A no-op flush is exactly what this project
 * forbids, and an unreported failure is the same defect with a nicer face.
 *
 * Three outcomes, kept apart:
 *   PASS            the platform hook flushed the range;
 *   NOT_APPLICABLE  the platform advertises no icache maintenance at all (coherent
 *                   instruction cache, e.g. x86): execution remains meaningful;
 *   FAIL            the platform HAS the capability and the flush failed: executing code
 *                   whose instructions were never synchronised yields untrustworthy
 *                   results, so nothing is executed and the record says why.
 */
typedef enum {
    PHASE02_ICACHE_FIRST_WRITE = 0,
    PHASE02_ICACHE_REWRITE     = 1
} phase02_icache_phase_t;

static int phase02_jit_sync_icache(phase02_log_t *log, void *arena, size_t len,
                                   phase02_icache_phase_t phase)
{
    const char *when = (phase == PHASE02_ICACHE_REWRITE) ? "rewrite" : "first write";
    int rc = rt_jit_invalidate(arena, len);

    if (rc == 0) {
        phase02_log_record(log, "jit.icache_sync", RT_PASS,
                           "%zu bytes flushed through the %s backend hook after the %s",
                           len, rt_platform_name(), when);
        return 0;
    }
    if (phase02_platform_has(RT_CAP_ICACHE_FLUSH) == 0) {
        phase02_log_record(log, "jit.icache_sync", RT_NOT_APPLICABLE,
                           "platform '%s' advertises no instruction-cache maintenance "
                           "(coherent icache): no flush is required after the %s, and the "
                           "execution below is still meaningful on this architecture",
                           rt_platform_name(), when);
        return 0;
    }
    phase02_log_record(log, "jit.icache_sync", RT_FAIL,
                       "the %s backend hook exists (capability bit set) and the flush failed "
                       "after the %s: executing code whose instructions were never "
                       "synchronised would yield an untrustworthy result, so nothing was "
                       "executed", rt_platform_name(), when);
    return -1;
}

static void phase02_jit_release(phase02_log_t *log, void *arena, size_t len)
{
    if (rt_jit_free(arena, len) == 0) {
        phase02_log_record(log, "jit.free", RT_PASS,
                           "arena released after execution (munmap accepted): no generated "
                           "code stays mapped");
    } else {
        phase02_log_record(log, "jit.free", RT_FAIL,
                           "releasing the arena failed: cleanup defect (the mapping may "
                           "outlive this test)");
    }
}

static void phase02_jit_microtest(phase02_log_t *log, rt_status_t map_jit_status,
                                  int map_jit_errno)
{
    uint8_t payload_first[16];
    uint8_t payload_second[16];
    size_t first_len = 0u;
    size_t second_len = 0u;
    size_t arena_len;
    void *arena;
    void *target = NULL;
    void *fault = NULL;
    uint32_t value_first = 0u;
    uint32_t value_second = 0u;
    int err = 0;
    int map_jit_attempted = 0;
    int map_jit_refused = 0;
    int use_write_window;
    int wrote_ok;
    rt_jit_arena_kind_t arena_kind = RT_JIT_ARENA_ANON;
    rt_status_t alloc_status;
    rt_jit_fn_t fn;

    if (rt_jit_emit_return_imm(payload_first, sizeof(payload_first), 42u, &first_len) != 0) {
        phase02_log_record(log, "jit.emit_payload", RT_UNSUPPORTED,
                           "no payload emitter for ISA %s", rt_jit_isa());
        return;
    }
    phase02_log_record(log, "jit.emit_payload", RT_PASS,
                       "ISA=%s emitted %zu bytes for 'return 42'", rt_jit_isa(), first_len);

    /* Both payloads are emitted BEFORE the arena is allocated: the arena has to be
     * large enough for the second one too. The previous expression was
     * `(first_len > second_len) ? first_len : first_len` — it could never choose the
     * second payload, and since `second_len` was still 0 at that point the size was
     * "the first payload" by accident. A longer rewrite payload would then have been
     * memcpy'd past the end of the mapping. */
    if (rt_jit_emit_return_imm(payload_second, sizeof(payload_second), 4242u, &second_len) != 0) {
        phase02_log_record(log, "jit.emit_payload", RT_UNSUPPORTED,
                           "second payload not emittable for ISA %s", rt_jit_isa());
        return;
    }
    arena_len = (first_len > second_len) ? first_len : second_len;
    if (arena_len == 0u) {
        phase02_log_record(log, "jit.alloc", RT_FAIL,
                           "emitters produced a zero-length payload: defect of the emitter");
        return;
    }

    arena = rt_jit_alloc_ex(arena_len, &err, &arena_kind, &map_jit_attempted, &map_jit_refused);
    use_write_window = (arena_kind == RT_JIT_ARENA_MAP_JIT);
    if (arena == NULL) {
        /* Causality, stated and checked: the MAP_JIT path was attempted, the probe that
         * measures that capability was BLOCKED, and the refusals carry the same errno.
         * Only then is this "blocked by jit.map_jit_probe"; every other combination is
         * reported as a defect of this runtime. Never a zero errno presented as a fact. */
        alloc_status = phase02_classify(
            phase02_classify_jit_alloc(map_jit_attempted, map_jit_status, map_jit_errno, err),
            map_jit_status);
        if (alloc_status == RT_BLOCKED) {
            phase02_log_record(log, "jit.alloc", RT_BLOCKED,
                               "arena allocation refused errno=%d (%s); %s (errno=%d, MAP_JIT "
                               "attempted=%s): one missing capability, not a second defect",
                               err, strerror(err),
                               phase02_dependency_note(PHASE02_DEP_JIT_MAP), map_jit_errno,
                               (map_jit_attempted != 0) ? "yes" : "no");
        } else if (err == 0) {
            phase02_log_record(log, "jit.alloc", RT_FAIL,
                               "arena allocation failed with errno=0 (errno not preserved): "
                               "defect of the error path, not a platform answer "
                               "(MAP_JIT attempted=%s)",
                               (map_jit_attempted != 0) ? "yes" : "no");
        } else {
            phase02_log_record(log, "jit.alloc", RT_FAIL,
                               "arena allocation failed errno=%d (%s) with MAP_JIT "
                               "attempted=%s (MAP_JIT refusal errno=%d) while "
                               "jit.map_jit_probe reports %s: both the MAP_JIT path and the "
                               "W^X fallback failed, so this is a defect of the fallback, not "
                               "the capability",
                               err, strerror(err), (map_jit_attempted != 0) ? "yes" : "no",
                               map_jit_refused, rt_status_name(map_jit_status));
        }
        return;
    }
    if (arena_kind == RT_JIT_ARENA_ANON_MAP_JIT_REFUSED) {
        /* The arena is real and its semantics are the ones the port needs, and the refused
         * capability is still visible: jit.map_jit_probe keeps reporting it BLOCKED. This is
         * not "MAP_JIT works" and not a plain mmap added to green a test: the arena is
         * writable now, never writable and executable at once, and the executable step below
         * is verified by real execution. */
        phase02_log_record(log, "jit.alloc", RT_PASS,
                           "MAP_JIT refused errno=%d (%s) -> arena of %zu bytes obtained "
                           "through the platform's supported single-view W^X mechanism "
                           "(anonymous mapping; the payload is flipped to R-X and verified by "
                           "execution). MAP_JIT remains BLOCKED at jit.map_jit_probe; no "
                           "writable+executable mapping exists at any point",
                           map_jit_refused, strerror(map_jit_refused), arena_len);
    } else if (arena_kind == RT_JIT_ARENA_MAP_JIT) {
        phase02_log_record(log, "jit.alloc", RT_PASS,
                           "arena of %zu bytes from MAP_JIT (write-protect window is the "
                           "write protocol for this arena)", arena_len);
    } else {
        phase02_log_record(log, "jit.alloc", RT_PASS,
                           "arena of %zu bytes (anonymous W^X; MAP_JIT is not a capability of "
                           "this platform/target)", arena_len);
    }

    /* ---- step: write the payload through the protocol the arena requires -------------- */
    wrote_ok = 0;
    if (use_write_window) {
        if (rt_jit_begin_write(arena, arena_len, &err) != 0) {
            phase02_log_record(log, "jit.write_payload", RT_BLOCKED,
                               "could not open the JIT write window errno=%d (%s): the payload "
                               "was NOT written (write-protected arena untouched)",
                               err, strerror(err));
            phase02_jit_release(log, arena, arena_len);
            return;
        }
    }
    memcpy(arena, payload_first, first_len);
    wrote_ok = (memcmp(arena, payload_first, first_len) == 0);
    if (use_write_window) {
        if (rt_jit_end_write(arena, arena_len, &err) != 0) {
            phase02_log_record(log, "jit.write_payload", RT_BLOCKED,
                               "could not close the JIT write window errno=%d (%s): the payload "
                               "may not be executable yet (nothing was executed)",
                               err, strerror(err));
            phase02_jit_release(log, arena, arena_len);
            return;
        }
    }
    phase02_log_record(log, "jit.write_payload", wrote_ok ? RT_PASS : RT_FAIL,
                       wrote_ok
                           ? "%zu bytes written %s and read back identical"
                           : "%zu bytes written %s and read back DIFFERENT: defect of this "
                             "runtime, nothing may be executed",
                       first_len,
                       use_write_window ? "inside the MAP_JIT write window" : "into the RW arena");
    if (!wrote_ok) {
        phase02_jit_release(log, arena, arena_len);
        return;
    }

    if (phase02_jit_sync_icache(log, arena, first_len, PHASE02_ICACHE_FIRST_WRITE) != 0) {
        phase02_jit_release(log, arena, arena_len);
        return;
    }

    if (!use_write_window &&
        rt_mem_protect(arena, first_len, RT_PROT_READ | RT_PROT_EXEC, &err) != 0) {
        phase02_log_record(log, "jit.make_executable", RT_BLOCKED,
                           "mprotect(R-X) refused errno=%d (%s): this process may not "
                           "execute memory it wrote", err, strerror(err));
        phase02_jit_release(log, arena, arena_len);
        return;
    }

    /* ---- execute, then decide what "executable" may claim --------------------------
     * The record is written AFTER the call on purpose. On this class of Apple device the
     * kernel can accept mprotect(R-X) and still not honour it (execute is stripped, the page
     * stays r--), so a PASS taken from a successful mprotect() would be exactly the "PASS
     * without execution" this pass must not produce. PASS here means: the payload ran and the
     * value came back from the CPU. */
    target = arena;
    memcpy(&fn, &target, sizeof(fn));
    {
        int rc = rt_signal_call_guarded(fn, &value_first, &fault, &err);
        if (rc == -2) {
            phase02_log_record(log, "jit.make_executable", RT_UNTESTED,
                               "the transition to executable was requested but nothing was "
                               "executed to verify it: the fault guard could not be installed "
                               "(errno=%d, %s)", err, strerror(err));
            phase02_log_record(log, "jit.execute_return_42", RT_UNTESTED,
                               "nothing was executed: the fault guard could not be installed "
                               "(errno=%d, %s) - cause undetermined, this is neither a fault "
                               "nor a permission result", err, strerror(err));
            phase02_jit_release(log, arena, arena_len);
            return;
        }
        if (rc != 0) {
            int inside = ((const uint8_t *)fault >= (const uint8_t *)arena &&
                          (const uint8_t *)fault < (const uint8_t *)arena + arena_len);
            if (inside) {
                phase02_log_record(log, "jit.make_executable", RT_BLOCKED,
                                   "%s accepted the transition to executable but executing "
                                   "the payload faulted inside the arena (si_addr=%p): this "
                                   "device did not honour the permission change, so the "
                                   "payload is NOT executable here (nothing was faked: the "
                                   "value was never returned)",
                                   use_write_window ? "closing the MAP_JIT write window"
                                                    : "mprotect(R-X)",
                                   fault);
                phase02_log_record(log, "jit.execute_return_42", RT_BLOCKED,
                                   "calling generated code faulted (si_addr=%p): %s",
                                   fault, phase02_dependency_note(PHASE02_DEP_EXEC_MAPPING));
            } else {
                phase02_log_record(log, "jit.make_executable", RT_FAIL,
                                   "the transition to executable succeeded, but the payload "
                                   "faulted OUTSIDE the arena (si_addr=%p): defect of the "
                                   "emitted code or of the call, not a permission verdict",
                                   fault);
                phase02_log_record(log, "jit.execute_return_42", RT_FAIL,
                                   "generated code faulted at si_addr=%p (outside the arena)",
                                   fault);
            }
            phase02_jit_release(log, arena, arena_len);
            return;
        }
        phase02_log_record(log, "jit.make_executable", RT_PASS,
                           "payload is in executable memory and PROVEN executable by real "
                           "execution returning %u (%s)", value_first,
                           use_write_window ? "MAP_JIT + write-protect window"
                                            : "mprotect R-X");
        phase02_log_record(log, "jit.execute_return_42",
                           (value_first == 42u) ? RT_PASS : RT_FAIL,
                           "generated function returned %u (expected 42)", value_first);
        if (value_first != 42u) {
            phase02_jit_release(log, arena, arena_len);
            return;
        }
    }

    /* Rewrite the payload, synchronise the icache, run it again. The second payload was
     * emitted before the allocation, so this is a capacity check, not a silent clamp:
     * clamping the length would have executed a half-written instruction stream. */
    if (second_len > arena_len) {
        phase02_log_record(log, "jit.rewrite_payload", RT_FAIL,
                           "rewrite payload (%zu bytes) does not fit the arena (%zu bytes): "
                           "defect of the sizing, nothing was written",
                           second_len, arena_len);
        phase02_jit_release(log, arena, arena_len);
        return;
    }

    if (use_write_window) {
        if (rt_jit_begin_write(arena, arena_len, &err) != 0) {
            phase02_log_record(log, "jit.rewrite_payload", RT_BLOCKED,
                               "could not open the JIT write window for the rewrite "
                               "errno=%d (%s)", err, strerror(err));
            phase02_jit_release(log, arena, arena_len);
            return;
        }
    } else if (rt_mem_protect(arena, arena_len, RT_PROT_READ | RT_PROT_WRITE, &err) != 0) {
        phase02_log_record(log, "jit.rewrite_payload", RT_BLOCKED,
                           "could not reopen the arena for writing errno=%d (%s)",
                           err, strerror(err));
        phase02_jit_release(log, arena, arena_len);
        return;
    }
    memcpy(arena, payload_second, second_len);
    wrote_ok = (memcmp(arena, payload_second, second_len) == 0);
    if (use_write_window) {
        if (rt_jit_end_write(arena, arena_len, &err) != 0) {
            phase02_log_record(log, "jit.rewrite_payload", RT_BLOCKED,
                               "could not close the JIT write window after the rewrite "
                               "errno=%d (%s): the rewritten payload was NOT executed",
                               err, strerror(err));
            phase02_jit_release(log, arena, arena_len);
            return;
        }
    } else if (rt_mem_protect(arena, arena_len, RT_PROT_READ | RT_PROT_EXEC, &err) != 0) {
        phase02_log_record(log, "jit.rewrite_payload", RT_BLOCKED,
                           "could not re-protect the arena errno=%d (%s)", err, strerror(err));
        phase02_jit_release(log, arena, arena_len);
        return;
    }
    if (!wrote_ok) {
        phase02_log_record(log, "jit.rewrite_payload", RT_FAIL,
                           "the rewritten payload did not read back identical (defect of "
                           "this runtime): nothing was executed");
        phase02_jit_release(log, arena, arena_len);
        return;
    }
    if (phase02_jit_sync_icache(log, arena, second_len, PHASE02_ICACHE_REWRITE) != 0) {
        phase02_jit_release(log, arena, arena_len);
        return;
    }
    phase02_log_record(log, "jit.rewrite_payload", RT_PASS,
                       "payload rewritten (%zu bytes), window closed and instruction cache "
                       "synchronised", second_len);

    target = arena;
    memcpy(&fn, &target, sizeof(fn));
    {
        int rc = rt_signal_call_guarded(fn, &value_second, &fault, &err);
        if (rc == -2) {
            phase02_log_record(log, "jit.execute_return_4242", RT_UNTESTED,
                               "nothing was executed: the fault guard could not be installed "
                               "(errno=%d, %s) - cause undetermined", err, strerror(err));
            phase02_jit_release(log, arena, arena_len);
            return;
        }
        if (rc != 0) {
            phase02_log_record(log, "jit.execute_return_4242", RT_BLOCKED,
                               "second call faulted (si_addr=%p): the rewritten payload did "
                               "not become executable", fault);
            phase02_jit_release(log, arena, arena_len);
            return;
        }
        phase02_log_record(log, "jit.execute_return_4242",
                           (value_second == 4242u) ? RT_PASS : RT_FAIL,
                           "rewritten generated function returned %u (expected 4242)",
                           value_second);
    }

    phase02_jit_release(log, arena, arena_len);
}

static rt_status_t phase02_suite_jit(phase02_log_t *log, const char *workdir)
{
    void *map_jit_addr = NULL;
    int err = 0;
    int allowed;
    rt_status_t map_jit_status;
    int map_jit_errno = 0;

    (void)workdir;

    if (phase02_platform_has(RT_CAP_MAP_JIT) != 0) {
        if (rt_jit_probe_map_jit(&map_jit_addr, &err) == 0) {
            map_jit_status = RT_PASS;
            phase02_log_record(log, "jit.map_jit_probe", RT_PASS,
                               "MAP_JIT mapping accepted by the kernel (target=%s). "
                               "On iOS a simulator accepts it more readily than a device: "
                               "this line alone never proves device behaviour",
                               rt_platform_apple_target_name());
        } else {
            map_jit_status = RT_BLOCKED;
            map_jit_errno = err;
            phase02_log_record(log, "jit.map_jit_probe", RT_BLOCKED,
                               "MAP_JIT refused errno=%d (%s) (target=%s) — on iOS this is "
                               "the signature of a missing JIT entitlement, recorded as "
                               "capability-not-granted, not as a port defect; every test "
                               "that needs this capability reports it as BLOCKED with this "
                               "test named as its dependency",
                               err, strerror(err), rt_platform_apple_target_name());
        }
    } else {
        map_jit_status = RT_UNSUPPORTED;
        phase02_log_record(log, "jit.map_jit_probe", RT_UNSUPPORTED,
                           "platform '%s' has no MAP_JIT (capability bit absent, target=%s)",
                           rt_platform_name(), rt_platform_apple_target_name());
    }

    /* Mandatory entitlement investigation (Fix 06), as four separate levels. Nobody may read
     * this as "the app has JIT": level 1 is a repository fact, level 2 belongs to the signing
     * step, level 3 is only readable from a signed product and NOT from inside the process, and
     * level 4 is what this run actually observed. Reported as a NOTE because it is evidence,
     * not a measurement of the runtime — it changes no count and grants nothing. */
    phase02_log_line(log,
                     "[PHASE02] NOTE=jit.entitlement_levels "
                     "L1_requested_in_repo=com.apple.security.cs.allow-jit "
                     "(RuntimePoC/WinlatorPhase02.entitlements) and the wiring decision: the "
                     "file is DELIBERATELY NOT attached to CODE_SIGN_ENTITLEMENTS, because on "
                     "iOS-based platforms every entitlement must be allowlisted by the "
                     "provisioning profile and an entitlements file that requests one the "
                     "profile does not allow makes the signed build/install fail (Xcode: "
                     "\"provisioning profile does not include the ... entitlement\"; device: "
                     "0xE8008016) - attaching it would break the only install path that can "
                     "validate anything, for a capability it cannot obtain. The opt-in for a "
                     "signing context that can carry it is documented in tools/build_ios.sh. "
                     "L2_embedded_at_build=decided by the signing step: this project builds "
                     "UNSIGNED_IPA (CODE_SIGNING_ALLOWED=NO), so the built product embeds "
                     "nothing - a fact, not a failure "
                     "L3_granted_to_signature=NOT observable in-process; read it off the built "
                     "product with tools/inspect_entitlements.sh (key names and booleans only, "
                     "never certificate or profile data) "
                     "L4_observed_at_runtime=MAP_JIT %s%s (target=%s) "
                     "the wiring grants nothing and is never claimed to: only signing, "
                     "installation and a physical run can decide L3/L4",
                     (map_jit_status == RT_PASS) ? "accepted"
                                                 : (map_jit_status == RT_BLOCKED) ? "refused"
                                                                                  : "not applicable (no MAP_JIT on this platform/target)",
                     (map_jit_status != RT_BLOCKED) ? ""
                                                    : (map_jit_errno != 0) ? " with a real errno"
                                                                           : " with errno 0 (defect)",
                     rt_platform_apple_target_name());

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

    phase02_jit_microtest(log, map_jit_status, map_jit_errno);

    allowed = rt_jit_execution_allowed();
    if (allowed == 1) {
        phase02_log_record(log, "jit.execution_allowed", RT_PASS,
                           "this process executed memory it wrote (emitted fetch/return "
                           "payload, made it executable through the arena's own mechanism and "
                           "got 1 back from the CPU)");
    } else if (allowed == 0) {
        phase02_log_record(log, "jit.execution_allowed", RT_BLOCKED,
                           "this process could NOT execute memory it wrote: the payload was "
                           "written through the arena's own protocol and the executable step "
                           "was refused or not honoured (see jit.make_executable and "
                           "jit.execute_return_42); %s",
                           phase02_dependency_note(PHASE02_DEP_EXEC_MAPPING));
    } else {
        phase02_log_record(log, "jit.execution_allowed", RT_UNTESTED,
                           "not determined here: a step of the probe itself did not complete "
                           "(emitter, arena, write window, icache flush or the fault guard) - "
                           "no verdict about the platform is claimed; on this target the "
                           "result also depends on %s",
                           phase02_dependency_note(PHASE02_DEP_JIT_MAP));
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
    int      fail_errno;   /* errno reported there, captured by the probe itself */
    rt_fs_stage_t fail_stage; /* which step failed (see rt_fs_stage_name) */
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
    rt_fs_stage_t stage = RT_FS_STAGE_NONE;
    rt_fs_limits_t limits;

    memset(probe, 0, sizeof(*probe));
    if (rt_fs_limits_query(workdir, &limits, &err) == 0) {
        probe->path_max = (int)limits.path_max;
        probe->path_max_measured = limits.path_max_from_pathconf;
        probe->path_cap = limits.path_cap;
    }

    while (try_depth <= PHASE02_DEEP_HARD_CAP) {
        if (rt_fs_deep_paths_ex(workdir, (size_t)try_depth, &len, &err, &stage) == 0) {
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
        if (rt_fs_deep_paths_ex(workdir, (size_t)middle, &len, &err, &stage) == 0) {
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
    probe->fail_stage = stage;
}

/* Semantic outcome of the measurement — the cases the report must separate:
 *   PASS     the platform supports a measurable depth and the boundary is the platform's own
 *            documented limit (ENAMETOOLONG), reported as such;
 *   PASS     no ceiling inside the probed cap: "not reached", which is NOT "unlimited";
 *   BLOCKED  the platform refused for a permission/resource reason (capability not granted
 *            here) or the ceiling is this probe's own path buffer — capacity beyond it was
 *            not measurable, and saying otherwise would be a guess;
 *   FAIL     a defect: a boundary error with a real errno that is neither of the above, or —
 *            as in IPHONE13_PHYSICAL_RUN_01 — an error whose errno was not preserved, which
 *            tells us nothing about the platform and must never be read as PASS or as a
 *            platform limit.
 * The verdict comes from phase02_classify_fs_depth_error(), which is a pure function and is
 * exercised by the unit tests with the exact device inputs. */
static void phase02_fs_deep_paths_record(phase02_log_t *log, const char *workdir)
{
    phase02_depth_probe_t probe;
    rt_status_t status;

    phase02_measure_depth(workdir, &probe);

    if (probe.deepest_ok == 0u) {
        char short_path[40];
        phase02_log_record(log, "fs.deep_paths", RT_BLOCKED,
                           "no depth measurable from workdir=%s (len=%zu): first failure at "
                           "depth=%u stage=%s errno=%d (%s); PATH_MAX=%d[%s] cap=%zu",
                           phase02_short_path(workdir, short_path, sizeof(short_path)),
                           strlen(workdir), probe.first_fail,
                           rt_fs_stage_name(probe.fail_stage), probe.fail_errno,
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

    status = phase02_classify(
        phase02_classify_fs_depth_error((int)probe.fail_stage, probe.fail_errno,
                                        (int)probe.deepest_len, probe.path_max,
                                        probe.path_max_measured),
        RT_BLOCKED);

    if (status == RT_PASS && probe.fail_stage != RT_FS_STAGE_PATH_CAP &&
        probe.fail_stage != RT_FS_STAGE_LEAF_JOIN) {
        /* The boundary is the platform's documented limit: reaching it IS the finding. */
        phase02_log_record(log, "fs.deep_paths", RT_PASS,
                           "capacity: depth up to %u (%zu chars) created and removed; ceiling "
                           "at depth=%u, stage=%s, errno=%d (%s) - the platform limit, "
                           "correctly reported (PATH_MAX=%d[%s] cap=%zu)",
                           probe.deepest_ok, probe.deepest_len, probe.first_fail,
                           rt_fs_stage_name(probe.fail_stage), probe.fail_errno,
                           strerror(probe.fail_errno), probe.path_max,
                           probe.path_max_measured ? "pathconf" : "limits.h", probe.path_cap);
        return;
    }

    if (status == RT_PASS) {
        /* Capacity demonstrated; the ceiling that stopped the probe is its own path buffer.
         * PASS states exactly that, and claims nothing about the platform beyond the depth
         * that was actually created and removed. */
        phase02_log_record(log, "fs.deep_paths", RT_PASS,
                           "capacity measured: depth up to %u (%zu chars) created and removed; "
                           "the ceiling at depth=%u (stage=%s, errno=%d) is this probe's own "
                           "path buffer (cap=%zu), NOT the platform's (PATH_MAX=%d[%s]): depth "
                           "beyond it was not probed and is not claimed",
                           probe.deepest_ok, probe.deepest_len, probe.first_fail,
                           rt_fs_stage_name(probe.fail_stage), probe.fail_errno,
                           probe.path_cap, probe.path_max,
                           probe.path_max_measured ? "pathconf" : "limits.h");
        return;
    }

    if (status == RT_BLOCKED) {
        const char *why = (probe.fail_stage == RT_FS_STAGE_PATH_CAP ||
                           probe.fail_stage == RT_FS_STAGE_LEAF_JOIN)
                              ? "limit is this probe's own path buffer, not the platform's"
                              : "platform refused before the limit was reached; capacity not "
                                "measurable here";
        phase02_log_record(log, "fs.deep_paths", RT_BLOCKED,
                           "deepest ok=%u (%zu chars); %s at depth=%u stage=%s errno=%d (%s); "
                           "PATH_MAX=%d[%s] cap=%zu",
                           probe.deepest_ok, probe.deepest_len, why, probe.first_fail,
                           rt_fs_stage_name(probe.fail_stage), probe.fail_errno,
                           strerror(probe.fail_errno), probe.path_max,
                           probe.path_max_measured ? "pathconf" : "limits.h", probe.path_cap);
        return;
    }

    if (status == RT_UNTESTED) {
        phase02_log_record(log, "fs.deep_paths", RT_UNTESTED,
                           "deepest ok=%u (%zu chars); failure at depth=%u stage=%s could not "
                           "be attributed: cause undetermined, so this is neither PASS nor FAIL",
                           probe.deepest_ok, probe.deepest_len, probe.first_fail,
                           rt_fs_stage_name(probe.fail_stage));
        return;
    }

    if (probe.fail_errno == 0) {
        /* The case physical run #1 printed as "unexpected errno=0 (Undefined error: 0)". */
        phase02_log_record(log, "fs.deep_paths", RT_FAIL,
                           "error returned with errno=0 at depth=%u (deepest ok=%u, %zu chars, "
                           "stage=%s): the failing call did not preserve errno - a defect of "
                           "the error path, not a platform limit (it is not read as PASS and "
                           "not as a capability)",
                           probe.first_fail, probe.deepest_ok, probe.deepest_len,
                           rt_fs_stage_name(probe.fail_stage));
        return;
    }

    phase02_log_record(log, "fs.deep_paths", RT_FAIL,
                       "unexpected errno=%d (%s) at depth=%u (deepest ok=%u, %zu chars, "
                       "stage=%s): neither the platform limit nor a refusal",
                       probe.fail_errno, strerror(probe.fail_errno), probe.first_fail,
                       probe.deepest_ok, probe.deepest_len,
                       rt_fs_stage_name(probe.fail_stage));
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
    /* POSIX shared memory, with the failing syscall named. Physical run #1 reported
     * "shared memory object mapped twice and compared (errno=1)" — an errno without a
     * stage cannot distinguish "the sandbox refuses shm_open" from "it refuses the second
     * mmap", which are different findings. The stage is part of every record now, and the
     * verdict comes from phase02_classify_shm_error(): a measured refusal is a capability
     * this process was not granted, an absent mechanism is UNSUPPORTED, and two views of
     * the same object disagreeing is a defect of our own mapping. */
    {
        rt_ipc_stage_t shm_stage = RT_IPC_STAGE_NONE;
        int shm_rc = rt_ipc_shm_ex(&err, &shm_stage);
        if (shm_rc != 0 && (err == EPERM || err == EACCES)) {
            /* Scope of this record, stated where the refusal is recorded: it measures the
             * NAMED POSIX namespace, nothing more. Shared memory on such a platform is not
             * "impossible": anonymous MAP_SHARED, file-backed MAP_SHARED objects inside the
             * app container and mach VM are all unaffected, and this runtime depends on none
             * of the named-namespace calls - the iOS memory backend uses single-view W^X and
             * the dual-mapping experiment has its own iOS backend (see
             * NOTE=memory.dual_mapping_backend). */
            phase02_log_line(log,
                             "[PHASE02] NOTE=ipc.posix_shm_scope stage=%s errno=%d (%s): the "
                             "named POSIX namespace is refused to this process/sandbox - "
                             "this is not \"shared memory is impossible\" and not a defect "
                             "of the port; other shared-memory mechanisms are unaffected and "
                             "the runtime does not depend on this namespace",
                             rt_ipc_stage_name(shm_stage), err, strerror(err));
        }
        if (shm_rc == 0 && shm_stage == RT_IPC_STAGE_UNLINK) {
            phase02_log_record(log, "ipc.posix_shm", RT_PASS,
                               "shared memory object created, mapped twice, written through one "
                               "view and read through the other (%zu bytes match) - probe "
                               "succeeded; cleanup: %s failed errno=%d (%s), the object may "
                               "outlive this process",
                               sizeof("phase02-ipc-pattern") - 1u,
                               rt_ipc_stage_name(shm_stage), err, strerror(err));
        } else if (shm_rc == 0) {
            phase02_log_record(log, "ipc.posix_shm", RT_PASS,
                               "shared memory object created, mapped twice (stage=%s), written "
                               "through one view and read through the other (%zu bytes match)",
                               rt_ipc_stage_name(shm_stage), sizeof("phase02-ipc-pattern") - 1u);
        } else {
            rt_status_t shm_status = phase02_classify(
                phase02_classify_shm_error((int)shm_stage, err), RT_BLOCKED);
            if (err == 0) {
                phase02_log_record(log, "ipc.posix_shm", RT_FAIL,
                                   "stage=%s returned an error with errno=0 (errno not "
                                   "preserved): defect of the error path, not a platform "
                                   "answer", rt_ipc_stage_name(shm_stage));
            } else if (shm_status == RT_BLOCKED) {
                phase02_log_record(log, "ipc.posix_shm", RT_BLOCKED,
                                   "stage=%s refused errno=%d (%s): the mechanism exists and "
                                   "this process/sandbox was not granted it (%s) - capability "
                                   "not available here, not a defect of the runtime",
                                   rt_ipc_stage_name(shm_stage), err, strerror(err),
                                   phase02_dependency_note(PHASE02_DEP_POSIX_SHM));
            } else if (shm_status == RT_UNSUPPORTED) {
                phase02_log_record(log, "ipc.posix_shm", RT_UNSUPPORTED,
                                   "stage=%s errno=%d (%s): no POSIX shared-memory mechanism "
                                   "on this platform", rt_ipc_stage_name(shm_stage), err,
                                   strerror(err));
            } else if (shm_stage == RT_IPC_STAGE_COMPARE) {
                phase02_log_record(log, "ipc.posix_shm", RT_FAIL,
                                   "stage=%s: every syscall was accepted and the two views of "
                                   "the same object disagreed (errno=%d): defect of this "
                                   "mapping, never a platform property",
                                   rt_ipc_stage_name(shm_stage), err);
            } else {
                phase02_log_record(log, "ipc.posix_shm", RT_FAIL,
                                   "stage=%s errno=%d (%s): neither a refusal nor an absent "
                                   "mechanism", rt_ipc_stage_name(shm_stage), err,
                                   strerror(err));
            }
        }
    }

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
    int os_err = 0;
    int loader_map_jit = 0;
    int jit_probe_err = 0;
    rt_status_t jit_map_jit_status;
    void *jit_probe_addr = NULL;

    (void)workdir;

    /* Measure the capability this suite depends on, in this same process, so the detail of
     * a blocked execution can name an observed dependency instead of an assumption. */
    if (phase02_platform_has(RT_CAP_MAP_JIT) != 0) {
        jit_map_jit_status = (rt_jit_probe_map_jit(&jit_probe_addr, &jit_probe_err) == 0)
                                 ? RT_PASS : RT_BLOCKED;
    } else {
        jit_map_jit_status = RT_UNSUPPORTED;
    }

    image_len = rt_loader_build_return_image(image, sizeof(image), 7u);
    if (image_len == 0u) {
        phase02_log_record(log, "loader.build_image", RT_UNSUPPORTED,
                           "no payload emitter for ISA %s", rt_jit_isa());
        return RT_UNSUPPORTED;
    }
    phase02_log_record(log, "loader.build_image", RT_PASS, "%zu-byte RTM1 image", image_len);

    status = rt_loader_run_ex(image, image_len, &value, &loader_err, &fault, &os_err,
                              &loader_map_jit);
    if (status == RT_PASS) {
        phase02_log_record(log, "loader.run_valid_module", RT_PASS,
                           "module executed, entry returned %u (expected 7)", value);
    } else if (loader_err == RT_LOADER_ERR_JIT_UNAVAILABLE) {
        /* Valid module, refused executable arena: the same capability jit.map_jit_probe
         * measures. Physical run #1 printed "rejected: OK" here — a rejection with a
         * success reason — because the reason was never written and the errno was lost. */
        phase02_log_record(log, "loader.run_valid_module", RT_BLOCKED,
                           "image validated and accepted, but execution is not possible here: "
                           "%s errno=%d (%s) with MAP_JIT attempted=%s while jit.map_jit_probe "
                           "reports %s — %s, not a rejection of the module",
                           rt_loader_error_name(loader_err), os_err, strerror(os_err),
                           (loader_map_jit != 0) ? "yes" : "no",
                           rt_status_name(jit_map_jit_status),
                           phase02_dependency_note(PHASE02_DEP_JIT_MAP));
    } else if (loader_err == RT_LOADER_ERR_EXEC_FAULT) {
        phase02_log_record(log, "loader.run_valid_module", RT_BLOCKED,
                           "%s: the module validated and was mapped, and its entry point "
                           "faulted (si_addr=%p) — executable-memory permission missing",
                           rt_loader_error_name(loader_err), fault);
    } else if (phase02_classify_loader_result(status, loader_err, loader_map_jit,
                                              jit_map_jit_status, os_err) ==
               PHASE02_OUTCOME_OK) {
        /* A rejection that names its reason is the loader doing its job. This suite feeds a
         * valid image, so reaching here means the image was judged invalid — say it plainly,
         * with the reason, and never without one. */
        phase02_log_record(log, "loader.run_valid_module", RT_FAIL,
                           "image rejected by validation: %s (errno=%d)",
                           rt_loader_error_name(loader_err), os_err);
    } else {
        /* Unreachable by contract; kept so that no combination can print a rejection with a
         * success reason. */
        phase02_log_record(log, "loader.run_valid_module", RT_FAIL,
                           "loader returned %s with reason %s and errno=%d: inconsistent "
                           "status/reason pairing - defect of the status propagation",
                           rt_status_name(status), rt_loader_error_name(loader_err), os_err);
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

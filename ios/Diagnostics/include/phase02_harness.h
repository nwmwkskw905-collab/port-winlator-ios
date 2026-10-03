/*
 * phase02_harness.h — suite registry for the reconstructed Phase 02 PoC.
 *
 * PHASE_02_RECONSTRUCTED_POC (see runtime_platform.h).
 *
 * Suites are exactly the ones documented by the surviving ios/README.md:
 *   memory, jit, cpu, threads, signals, fs, ipc, loader
 */
#ifndef PHASE02_HARNESS_H
#define PHASE02_HARNESS_H

#include <stddef.h>

#include "phase02_log.h"
#include "runtime_loader.h"
#include "runtime_platform.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef rt_status_t (*phase02_suite_fn)(phase02_log_t *log, const char *workdir);

typedef struct phase02_suite {
    const char *name;
    const char *description;
    phase02_suite_fn fn;
} phase02_suite_t;

size_t                  phase02_suite_count(void);
const phase02_suite_t  *phase02_suite_at(size_t index);
const phase02_suite_t  *phase02_suite_find(const char *name);

/* Runs one suite by name ("all" runs every suite). An unknown name is reported as
 * UNSUPPORTED in the log and returns UNSUPPORTED. */
rt_status_t phase02_run_suite(const char *name, phase02_log_t *log, const char *workdir);

/* Semantic classification of the JIT write-protect capability, as a pure function so it
 * can be exercised on any host (the iOS branches included):
 *   capability present and the hook answered 0      -> RT_PASS
 *   capability present and the hook refused         -> RT_BLOCKED
 *   capability absent on an iOS target              -> RT_NOT_APPLICABLE
 *                                                     (the SDK marks the API unavailable
 *                                                     for that target: not a defect)
 *   capability absent anywhere else                 -> RT_UNSUPPORTED
 * Unavailability never becomes RT_PASS. */
rt_status_t phase02_classify_write_protect(int has_capability, int probe_result,
                                           int apple_target);

/* ----------------------------------------------------------------- causal classification
 *
 * Physical run #1 (iPhone 13, 2026-10-02) reported `jit.alloc = FAIL` while
 * `jit.map_jit_probe = BLOCKED` for the *same* missing capability, and
 * `loader.run_valid_module = FAIL` whose detail read "rejected: OK". Both were
 * classification defects, not two extra runtime defects: a test that depends on a
 * capability the platform refused must not be reported as an independent failure, and a
 * rejection must never be printed with a success reason.
 *
 * The rule below makes the causality explicit instead of downgrading statuses globally:
 * the caller states WHAT it observed (an outcome) and WHICH capability it depends on, and
 * the dependency's own status decides between "blocked by the capability" and "a defect
 * after all - the dependency proved the capability was there".
 *
 *   outcome                     dependency           result
 *   -------------------------   ------------------   --------------------------------
 *   PHASE02_OUTCOME_OK          anything             RT_PASS
 *   PHASE02_OUTCOME_OK_PROBE_LIMIT  anything         RT_PASS   (capacity measured; the
 *                                                              ceiling belongs to the probe)
 *   PHASE02_OUTCOME_RUNTIME_DEFECT      anything      RT_FAIL
 *   PHASE02_OUTCOME_CAPABILITY_MISSING  anything      RT_BLOCKED
 *   PHASE02_OUTCOME_NOT_APPLICABLE      anything      RT_NOT_APPLICABLE
 *   PHASE02_OUTCOME_UNSUPPORTED         anything      RT_UNSUPPORTED
 *   PHASE02_OUTCOME_DEPENDENCY_BLOCKED  != RT_PASS    RT_BLOCKED   (blocked by it)
 *   PHASE02_OUTCOME_DEPENDENCY_BLOCKED  == RT_PASS    RT_FAIL      (capability was there)
 *   PHASE02_OUTCOME_UNDETERMINED        anything      RT_UNTESTED
 *
 * RT_UNTESTED is the only honest answer when the cause could not be established (for
 * example a failing call that did not preserve errno): an unknown cause is never PASS and
 * never FAIL. PASS is never returned for anything but observed success.
 */
typedef enum {
    PHASE02_OUTCOME_OK = 0,
    /* Capacity WAS demonstrated, but the ceiling that was found is this probe's own path
     * buffer, not the platform's: PASS with the provenance of the limit stated, because the
     * measurement is real and nothing about the platform is being claimed beyond it. */
    PHASE02_OUTCOME_OK_PROBE_LIMIT,
    PHASE02_OUTCOME_RUNTIME_DEFECT,      /* our own code misbehaved */
    PHASE02_OUTCOME_CAPABILITY_MISSING,  /* the platform refused (EPERM/EACCES/...): not granted here */
    PHASE02_OUTCOME_NOT_APPLICABLE,      /* the API does not exist for this target (iOS write-protect) */
    PHASE02_OUTCOME_UNSUPPORTED,         /* the platform has no such mechanism (ENOSYS/ENOTSUP) */
    PHASE02_OUTCOME_DEPENDENCY_BLOCKED,  /* could not even attempt it: a prerequisite is blocked */
    PHASE02_OUTCOME_UNDETERMINED         /* cause not established: never PASS, never FAIL */
} phase02_outcome_t;

/* Capabilities other tests depend on, each named after the test that measures it. */
typedef enum {
    PHASE02_DEP_NONE = 0,
    PHASE02_DEP_JIT_MAP,            /* jit.map_jit_probe */
    PHASE02_DEP_JIT_WRITE_PROTECT,  /* jit.write_protect_np */
    PHASE02_DEP_EXEC_MAPPING,       /* jit.make_executable / memory.dual_mapping_rw_rx */
    PHASE02_DEP_POSIX_SHM           /* ipc.posix_shm */
} phase02_dependency_t;

rt_status_t phase02_classify(phase02_outcome_t outcome, rt_status_t dependency_status);
const char  *phase02_outcome_name(phase02_outcome_t outcome);
const char  *phase02_dependency_name(phase02_dependency_t dependency);
const char  *phase02_dependency_test(phase02_dependency_t dependency);

/* Pure classifiers for the three physical-run failures. Kept in this layer (and exported)
 * so the exact conditions seen on the device can be replayed on any host by the unit
 * tests, including the ones this environment cannot reproduce natively. */

/* jit.alloc: the allocation is a dependency-blocked case only when the MAP_JIT path was
 * attempted, the probe that measures that same capability was BLOCKED, and the allocation
 * failed with the same errno the probe saw. Anything else is a runtime defect. */
phase02_outcome_t phase02_classify_jit_alloc(int map_jit_attempted,
                                             rt_status_t map_jit_probe_status,
                                             int map_jit_probe_errno,
                                             int alloc_errno);

/* fs.deep_paths: separates "the platform's documented limit" (PASS: the limit is the
 * finding), "the platform refused" (BLOCKED), "our own probe buffer is the limit"
 * (BLOCKED: capacity beyond it was not measurable) and "an error whose errno was not
 * preserved" (RUNTIME_DEFECT: an error path that lost the errno, as seen on the device).
 * `stage` is an rt_fs_stage_t value and `path_max_kind` says whether the platform limit
 * came from pathconf. */
phase02_outcome_t phase02_classify_fs_depth_error(int stage, int err, int longest_path,
                                                  int path_max, int path_max_from_pathconf);

/* ipc.posix_shm: names the failing stage; a refusal (EPERM/EACCES) is a capability the
 * sandbox did not grant, an absent mechanism is UNSUPPORTED, and two views of the same
 * object disagreeing is a defect of our mapping, not of the platform. */
phase02_outcome_t phase02_classify_shm_error(int stage, int err);

/* Verdict for the dual-mapping experiment, from the measured stage and errno. The rule is
 * the same one the shared-memory probe uses, so the same errno is never classified two
 * different ways in the same report: a refusal (EPERM/EACCES) is a capability this process
 * was not granted (BLOCKED); an absent mechanism (ENOSYS/ENOTSUP) is UNSUPPORTED; two views
 * that disagree although every syscall was accepted is a defect of this code; and an errno
 * that fits none of those (EINVAL, EFAULT, …) is a defect too, never a platform limit. */
phase02_outcome_t phase02_classify_dual_mapping_error(int stage, int err);

/* Verdict for a loader result, from the status/reason pair alone. The rule that makes
 * "rejected: OK" impossible: a non-PASS return whose reason is RT_LOADER_OK is a defect of
 * the status propagation, never a rejection and never a capability verdict. A rejection with
 * a real reason (an invalid image) is correct behaviour, not a defect. */
phase02_outcome_t phase02_classify_loader_result(rt_status_t status, rt_loader_error_t reason,
                                                 int map_jit_attempted,
                                                 rt_status_t probe_status, int os_err);

rt_status_t phase02_run_all(phase02_log_t *log, const char *workdir);

/* One-line description of the runtime environment:
 *   "platform=<name> page_size=<n> isa=<isa> apple_target=<target>"
 *
 * This exists so the app layer never has to include RuntimeCore headers: anything the
 * app needs is exposed here. CI run #4 is why — Phase02Bridge.m called rt_jit_isa()
 * while including only runtime_platform.h, and clang rejected the implicit declaration.
 * The fix was not to sprinkle declarations: it was to expose the composed string from
 * the layer that already sees all the RuntimeCore facts.
 *
 * Returns the number of characters written (excluding the terminator), or -1 when the
 * buffer is too small or the arguments are invalid. */
int phase02_platform_summary(char *out, size_t capacity);

/* Comma-separated list of suite names, for --list and the app. */
const char *phase02_suite_names(char *buf, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* PHASE02_HARNESS_H */

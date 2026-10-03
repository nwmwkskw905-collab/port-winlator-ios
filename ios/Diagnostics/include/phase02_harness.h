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

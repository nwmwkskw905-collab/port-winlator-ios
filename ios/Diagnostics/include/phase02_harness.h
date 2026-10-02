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
rt_status_t phase02_run_all(phase02_log_t *log, const char *workdir);

/* Comma-separated list of suite names, for --list and the app. */
const char *phase02_suite_names(char *buf, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* PHASE02_HARNESS_H */

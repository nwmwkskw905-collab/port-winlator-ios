/*
 * phase02_log.h — structured result log for the reconstructed Phase 02 PoC.
 *
 * PHASE_02_RECONSTRUCTED_POC (see runtime_platform.h).
 *
 * Line format (stable, greppable, and what the iOS app displays verbatim):
 *
 *   [PHASE02] TEST=<name> STATUS=<PASS|FAIL|BLOCKED|UNSUPPORTED|UNTESTED|NOT_APPLICABLE> DETAIL=<text>
 *
 * PASS is used only for behaviour that was actually observed.
 */
#ifndef PHASE02_LOG_H
#define PHASE02_LOG_H

#include <stddef.h>
#include <stdint.h>

#include "runtime_platform.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PHASE02_LOG_CAP (192u * 1024u)
#define PHASE02_STATUS_COUNT 6u

#if defined(__GNUC__) || defined(__clang__)
#define PHASE02_PRINTF(fmt_index, arg_index) \
    __attribute__((format(printf, fmt_index, arg_index)))
#else
#define PHASE02_PRINTF(fmt_index, arg_index)
#endif

typedef struct phase02_log {
    char     text[PHASE02_LOG_CAP];
    size_t   len;
    unsigned counts[PHASE02_STATUS_COUNT];
    unsigned records;
    unsigned assertions;
} phase02_log_t;

/* Heap helpers so non-C callers (the Objective-C bridge, Swift) never need to know
 * the struct size. */
phase02_log_t *phase02_log_new(void);
void           phase02_log_free(phase02_log_t *log);

void phase02_log_init(phase02_log_t *log, const char *title);
void phase02_log_line(phase02_log_t *log, const char *fmt, ...) PHASE02_PRINTF(2, 3);
/* Empty separator line (no format string involved). */
void phase02_log_blank(phase02_log_t *log);
void phase02_log_record(phase02_log_t *log, const char *test, rt_status_t status,
                        const char *fmt, ...) PHASE02_PRINTF(4, 5);

/* Records an explicit comparison. Increments the assertion counter and emits a
 * FAIL record when the condition is false. */
void phase02_log_assert(phase02_log_t *log, int condition, const char *name);

unsigned    phase02_log_status_count(const phase02_log_t *log, rt_status_t status);
unsigned    phase02_log_records(const phase02_log_t *log);
unsigned    phase02_log_assertions(const phase02_log_t *log);
const char *phase02_log_text(const phase02_log_t *log);
size_t      phase02_log_length(const phase02_log_t *log);

/* Worst-status summary:
 *   FAIL if any FAIL, else BLOCKED if any BLOCKED, else PASS if any PASS,
 *   else UNSUPPORTED if any UNSUPPORTED, else UNTESTED if any UNTESTED,
 *   else NOT_APPLICABLE. */
rt_status_t phase02_log_summary(const phase02_log_t *log);

int phase02_log_export(const phase02_log_t *log, const char *path);

/* One-line machine-readable summary (used by the CI and the app header). */
const char *phase02_log_summary_line(char *buf, size_t cap, const phase02_log_t *log);

#ifdef __cplusplus
}
#endif

#endif /* PHASE02_LOG_H */

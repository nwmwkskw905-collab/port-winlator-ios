/*
 * phase02_log.c — the result log.
 *
 * PHASE_02_RECONSTRUCTED_POC (see runtime_platform.h).
 */
#include "phase02_log.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t phase02_status_index(rt_status_t status)
{
    size_t index = (size_t)status;
    if (index >= (size_t)PHASE02_STATUS_COUNT) {
        index = 0u;
    }
    return index;
}

phase02_log_t *phase02_log_new(void)
{
    phase02_log_t *log = (phase02_log_t *)malloc(sizeof(phase02_log_t));
    if (log != NULL) {
        memset(log, 0, sizeof(*log));
    }
    return log;
}

void phase02_log_free(phase02_log_t *log)
{
    free(log);
}

void phase02_log_init(phase02_log_t *log, const char *title)
{
    if (log == NULL) {
        return;
    }
    memset(log, 0, sizeof(*log));
    phase02_log_line(log, "# %s", (title != NULL) ? title : "PHASE_02_RECONSTRUCTED_POC");
    phase02_log_line(log, "# platform=%s page_size=%d apple_target=%s",
                     rt_platform_name(), rt_platform_page_size(), rt_platform_apple_target_name());
}

void phase02_log_line(phase02_log_t *log, const char *fmt, ...)
{
    va_list args;
    int written;

    if (log == NULL || fmt == NULL || log->len >= PHASE02_LOG_CAP) {
        return;
    }
    va_start(args, fmt);
    written = vsnprintf(log->text + log->len, (size_t)PHASE02_LOG_CAP - log->len, fmt, args);
    va_end(args);
    if (written < 0) {
        return;
    }
    if ((size_t)written >= ((size_t)PHASE02_LOG_CAP - log->len)) {
        log->len = PHASE02_LOG_CAP - 1u; /* truncated: keep the log well-formed */
        log->text[log->len] = '\0';
        return;
    }
    log->len += (size_t)written;
    if (log->len + 1u < PHASE02_LOG_CAP) {
        log->text[log->len] = '\n';
        log->len++;
        log->text[log->len] = '\0';
    }
}

void phase02_log_blank(phase02_log_t *log)
{
    if (log == NULL || log->len + 2u >= PHASE02_LOG_CAP) {
        return;
    }
    log->text[log->len] = '\n';
    log->len++;
    log->text[log->len] = '\0';
}

void phase02_log_record(phase02_log_t *log, const char *test, rt_status_t status,
                        const char *fmt, ...)
{
    char detail[512];
    va_list args;
    int written;

    if (log == NULL || test == NULL) {
        return;
    }
    detail[0] = '\0';
    if (fmt != NULL) {
        va_start(args, fmt);
        written = vsnprintf(detail, sizeof(detail), fmt, args);
        va_end(args);
        if (written < 0) {
            detail[0] = '\0';
        }
    }
    log->counts[phase02_status_index(status)]++;
    log->records++;
    phase02_log_line(log, "[PHASE02] TEST=%s STATUS=%s DETAIL=%s",
                     test, rt_status_name(status), detail);
}

void phase02_log_assert(phase02_log_t *log, int condition, const char *name)
{
    if (log == NULL) {
        return;
    }
    log->assertions++;
    if (condition == 0) {
        phase02_log_record(log, (name != NULL) ? name : "assertion", RT_FAIL,
                           "assertion failed");
    }
}

unsigned phase02_log_status_count(const phase02_log_t *log, rt_status_t status)
{
    if (log == NULL) {
        return 0u;
    }
    return log->counts[phase02_status_index(status)];
}

unsigned phase02_log_records(const phase02_log_t *log)
{
    return (log != NULL) ? log->records : 0u;
}

unsigned phase02_log_assertions(const phase02_log_t *log)
{
    return (log != NULL) ? log->assertions : 0u;
}

const char *phase02_log_text(const phase02_log_t *log)
{
    return (log != NULL) ? log->text : "";
}

size_t phase02_log_length(const phase02_log_t *log)
{
    return (log != NULL) ? log->len : 0u;
}

rt_status_t phase02_log_summary(const phase02_log_t *log)
{
    if (log == NULL) {
        return RT_UNTESTED;
    }
    if (log->counts[phase02_status_index(RT_FAIL)] > 0u) {
        return RT_FAIL;
    }
    if (log->counts[phase02_status_index(RT_BLOCKED)] > 0u) {
        return RT_BLOCKED;
    }
    if (log->counts[phase02_status_index(RT_PASS)] > 0u) {
        return RT_PASS;
    }
    if (log->counts[phase02_status_index(RT_UNSUPPORTED)] > 0u) {
        return RT_UNSUPPORTED;
    }
    if (log->counts[phase02_status_index(RT_UNTESTED)] > 0u) {
        return RT_UNTESTED;
    }
    return RT_NOT_APPLICABLE;
}

int phase02_log_export(const phase02_log_t *log, const char *path)
{
    FILE *file;
    size_t written;

    if (log == NULL || path == NULL) {
        return -1;
    }
    file = fopen(path, "wb");
    if (file == NULL) {
        return -1;
    }
    written = fwrite(log->text, 1u, log->len, file);
    if (fclose(file) != 0) {
        return -1;
    }
    return (written == log->len) ? 0 : -1;
}

const char *phase02_log_summary_line(char *buf, size_t cap, const phase02_log_t *log)
{
    int written;

    if (buf == NULL || cap == 0u || log == NULL) {
        return "";
    }
    written = snprintf(buf, cap,
                       "records=%u assertions=%u pass=%u fail=%u blocked=%u "
                       "unsupported=%u untested=%u not_applicable=%u summary=%s",
                       log->records, log->assertions,
                       phase02_log_status_count(log, RT_PASS),
                       phase02_log_status_count(log, RT_FAIL),
                       phase02_log_status_count(log, RT_BLOCKED),
                       phase02_log_status_count(log, RT_UNSUPPORTED),
                       phase02_log_status_count(log, RT_UNTESTED),
                       phase02_log_status_count(log, RT_NOT_APPLICABLE),
                       rt_status_name(phase02_log_summary(log)));
    if (written < 0) {
        buf[0] = '\0';
    }
    return buf;
}

/*
 * phase02_progress.h — the flight recorder (pass 04 crash investigation).
 *
 * PHASE_02_RECONSTRUCTED_POC.
 *
 * Why this exists. IPHONE13_PHYSICAL_RUN_03 (commit d83332a) ended with the app closing
 * immediately when a suite was started. A termination like that leaves no report: the process
 * is gone before `phase02_log_*` can be serialised, so nothing in the run says WHICH step was
 * being executed. Guessing from the diff is not evidence.
 *
 * Two mechanisms fix that, and nothing else changes:
 *
 *   1. CHECKPOINTS. Every stage of a run appends one small line to a journal file, using a
 *      single unbuffered write(2) per line. A write(2) that returned has reached the kernel,
 *      so the journal survives a process termination that no signal handler can catch
 *      (SIGKILL, watchdog, platform enforcement). After a crash the journal names the last
 *      checkpoint the process reached — for every suite, not just the risky ones.
 *
 *   2. WRITE-AHEAD. The report accumulated so far is written to a file immediately BEFORE
 *      the operations that can terminate a process on this platform (entering emitted code,
 *      creating an executable mapping of an app-owned object). If those operations kill the
 *      process, the measurements up to that point are already on disk.
 *
 * The journal is diagnostics, never evidence of a capability: it records where the process
 * was, never what it achieved. It changes no test result and is not part of the report.
 *
 * Single-run contract: one journal at a time, the same way the app runs one suite at a time.
 * begin() with an empty/NULL path disables the recorder (used by tests and by hosts that do
 * not want the extra file) — disabled is a normal state, not an error.
 */
#ifndef PHASE02_PROGRESS_H
#define PHASE02_PROGRESS_H

#include <stddef.h>

#include "phase02_log.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Largest event string stored; longer events are marked as truncated, never silently cut. */
#define PHASE02_PROGRESS_EVENT_MAX 160u

/* Starts a journal at `path` (created with 0600, appended). Returns 0 on success, -1 with
 * errno set when the file cannot be opened, and 0 when the path is NULL/empty (disabled). */
int phase02_progress_begin(const char *path);

/* Appends one checkpoint. No-op when disabled. Never blocks, never allocates. */
void phase02_progress_checkpoint(const char *event);

/* Appends a checkpoint built from a name and a suite/operation argument:
 * "SUITE_ENTER suite=cpu". `value` may be NULL. */
void phase02_progress_checkpoint_kv(const char *event, const char *key, const char *value);

/* Writes the report accumulated so far to "<dir>/phase02-report-ahead.txt" and records
 * WRITE_AHEAD_OK / WRITE_AHEAD_FAIL in the journal. Returns 0 when the file was written,
 * -1 otherwise (a failure is reported in the journal and never turns into a test result).
 * `dir` may be NULL: then only the journal line is written. */
int phase02_progress_write_ahead(const phase02_log_t *log, const char *dir);

/* Closes the journal. Safe to call when disabled. */
void phase02_progress_end(void);

/* 1 while a journal is open. */
int phase02_progress_enabled(void);

/* Path of the open journal ("" when disabled). */
const char *phase02_progress_path(void);

/* Directory the journal lives in ("" when disabled): the natural home for the write-ahead
 * report, so the artefact a terminated run leaves behind sits next to its journal. */
const char *phase02_progress_directory(void);

/* Reads the last "event=" value back out of the journal at `path` into `out`.
 * Returns 0 when a value was found, -1 otherwise (missing file, no event, buffer too small).
 * This is what makes the crash diagnosis self-checkable: the test that simulates a hard kill
 * reads the journal exactly the way a human would. */
int phase02_progress_last_event(const char *path, char *out, size_t cap);

/* Absolute path helper used by the app layer and the tests: "<dir>/<name>".
 * Returns 0 on success, -1 when the result does not fit (never a truncated path). */
int phase02_progress_join(const char *dir, const char *name, char *out, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* PHASE02_PROGRESS_H */

/*
 * phase02_progress.c — the flight recorder.
 *
 * PHASE_02_RECONSTRUCTED_POC (see phase02_progress.h for why it exists).
 *
 * Design rules, all of them consequences of the run #3 termination:
 *   - one unbuffered write(2) per checkpoint: a returned write means the kernel has the line,
 *     so the journal survives a SIGKILL that no handler can intercept;
 *   - O_APPEND: two writers (the app thread and a test thread) can never overlap inside a
 *     line, and every line ends with '\n';
 *   - no allocation, no stdio buffering, no locks: nothing here can deadlock a crashing
 *     process or depend on a heap that may already be corrupt;
 *   - failures are silent by contract (a diagnostics aid must never change a diagnostic
 *     result); the one thing that IS observable is `phase02_progress_enabled()`;
 *   - the recorded text is a fixed prefix plus a bounded event string: no user data, no
 *     pointers, no addresses.
 */
#include "phase02_progress.h"

#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static int g_fd = -1;
static char g_path[512];
static char g_directory[512];
static unsigned g_sequence = 0u;

static void progress_append(const char *text, size_t length)
{
    if (g_fd < 0 || length == 0u) {
        return;
    }
    /* One write(2); the result is deliberately not interpreted — see the design rules. */
    (void)write(g_fd, text, length);
}

static int progress_copy_path(const char *path)
{
    const char *slash;
    size_t length;
    size_t directory_length;

    if (path == NULL) {
        return -1;
    }
    length = strlen(path);
    if (length == 0u || length >= sizeof(g_path)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    memcpy(g_path, path, length + 1u);
    /* Keep the containing directory: the write-ahead report is written next to the journal. */
    slash = strrchr(g_path, '/');
    directory_length = (slash != NULL) ? (size_t)(slash - g_path) : 0u;
    if (directory_length >= sizeof(g_directory)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    memcpy(g_directory, g_path, directory_length);
    g_directory[directory_length] = '\0';
    if (directory_length == 0u) {
        memcpy(g_directory, ".", 2u);
    }
    return 0;
}

int phase02_progress_join(const char *dir, const char *name, char *out, size_t cap)
{
    int written;

    if (dir == NULL || name == NULL || out == NULL || cap == 0u) {
        return -1;
    }
    written = snprintf(out, cap, "%s/%s", dir, name);
    if (written < 0 || (size_t)written >= cap) {
        if (cap > 0u) {
            out[0] = '\0';
        }
        errno = ENAMETOOLONG;
        return -1;
    }
    return 0;
}

int phase02_progress_begin(const char *path)
{
    int fd;

    if (path == NULL || path[0] == '\0') {
        return 0;                     /* disabled on purpose: not an error */
    }
    if (phase02_progress_enabled() != 0) {
        phase02_progress_end();       /* one journal at a time */
    }
    if (progress_copy_path(path) != 0) {
        return -1;
    }
    fd = open(g_path, O_WRONLY | O_CREAT | O_APPEND, S_IRUSR | S_IWUSR);
    if (fd < 0) {
        g_path[0] = '\0';
        return -1;                    /* errno from open(2) */
    }
    g_fd = fd;
    g_sequence = 0u;
    phase02_progress_checkpoint("JOURNAL_BEGIN");
    return 0;
}

void phase02_progress_checkpoint(const char *event)
{
    char line[PHASE02_PROGRESS_EVENT_MAX + 48u];
    int written;

    if (event == NULL) {
        event = "<null>";
    }
    written = snprintf(line, sizeof(line), "seq=%u event=%.*s\n", g_sequence,
                       (int)PHASE02_PROGRESS_EVENT_MAX, event);
    if (written <= 0) {
        return;
    }
    if ((size_t)written >= sizeof(line)) {
        /* Bounded by construction: element size and count are both from the format string
         * above, so this is unreachable; kept so a future edit cannot silently overrun. */
        return;
    }
    g_sequence++;
    progress_append(line, (size_t)written);
}

void phase02_progress_checkpoint_kv(const char *event, const char *key, const char *value)
{
    char composed[PHASE02_PROGRESS_EVENT_MAX + 1u];
    int written;

    if (event == NULL) {
        event = "<null>";
    }
    if (key == NULL || value == NULL) {
        phase02_progress_checkpoint(event);
        return;
    }
    written = snprintf(composed, sizeof(composed), "%s %s=%s", event, key, value);
    if (written < 0) {
        phase02_progress_checkpoint(event);
        return;
    }
    phase02_progress_checkpoint(composed);
}

int phase02_progress_write_ahead(const phase02_log_t *log, const char *dir)
{
    char path[512];
    FILE *file;
    size_t length;
    size_t written;
    int ok = -1;

    if (log == NULL) {
        return -1;
    }
    if (dir == NULL || dir[0] == '\0' ||
        phase02_progress_join(dir, "phase02-report-ahead.txt", path, sizeof(path)) != 0) {
        phase02_progress_checkpoint("WRITE_AHEAD_SKIP no-directory");
        return -1;
    }
    length = phase02_log_length(log);
    file = fopen(path, "wb");
    if (file == NULL) {
        phase02_progress_checkpoint_kv("WRITE_AHEAD_FAIL", "errno", "open");
        return -1;
    }
    written = fwrite(phase02_log_text(log), 1u, length, file);
    if (fclose(file) != 0 || written != length) {
        phase02_progress_checkpoint_kv("WRITE_AHEAD_FAIL", "errno", "write");
        return -1;
    }
    /* The journal line is written only after the data is on disk, so a journal that says
     * WRITE_AHEAD_OK is never ahead of reality. */
    phase02_progress_checkpoint("WRITE_AHEAD_OK");
    ok = 0;
    return ok;
}

void phase02_progress_end(void)
{
    if (g_fd >= 0) {
        phase02_progress_checkpoint("JOURNAL_END");
        (void)close(g_fd);
        g_fd = -1;
    }
    g_path[0] = '\0';
    g_directory[0] = '\0';
}

const char *phase02_progress_directory(void)
{
    return (g_fd >= 0) ? g_directory : "";
}

int phase02_progress_enabled(void)
{
    return (g_fd >= 0) ? 1 : 0;
}

const char *phase02_progress_path(void)
{
    return (g_fd >= 0) ? g_path : "";
}

int phase02_progress_last_event(const char *path, char *out, size_t cap)
{
    FILE *file;
    char line[PHASE02_PROGRESS_EVENT_MAX + 64u];
    char last[PHASE02_PROGRESS_EVENT_MAX + 1u];
    size_t last_length = 0u;
    int found = 0;

    if (path == NULL || out == NULL || cap == 0u) {
        return -1;
    }
    out[0] = '\0';
    file = fopen(path, "rb");
    if (file == NULL) {
        return -1;
    }
    while (fgets(line, (int)sizeof(line), file) != NULL) {
        char *marker = strstr(line, "event=");
        size_t length;
        if (marker == NULL) {
            continue;
        }
        marker += 6;                       /* strlen("event=") */
        length = strcspn(marker, "\r\n");
        if (length == 0u || length > PHASE02_PROGRESS_EVENT_MAX) {
            continue;
        }
        memcpy(last, marker, length);
        last[length] = '\0';
        last_length = length;
        found = 1;
    }
    (void)fclose(file);
    if (found == 0 || last_length + 1u > cap) {
        return -1;
    }
    memcpy(out, last, last_length + 1u);
    return 0;
}

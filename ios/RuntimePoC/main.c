/*
 * main.c — command-line harness for hosts and CI (the iOS app uses the same suites
 * through the Objective-C bridge).
 *
 * PHASE_02_RECONSTRUCTED_POC (see runtime_platform.h).
 *
 * Usage:
 *   phase02_poc [--suite <name|all>] [--export FILE] [--workdir DIR] [--list] [--quiet]
 *
 * Exit status: 0 unless the run produced a FAIL record (BLOCKED/UNSUPPORTED/UNTESTED
 * are legitimate results of a diagnostics run and do not fail the process).
 */
#include "phase02_harness.h"
#include "phase02_log.h"
/* Pass 04: the same flight recorder the app uses. Optional here (--journal PATH): the
 * regression tests exercise the selected-suite entry point with a journal so the artefact a
 * terminated run leaves behind is verified on every host, not only on the device. */
#include "phase02_progress.h"

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static void phase02_usage(const char *program)
{
    printf("usage: %s [--suite <name|all>] [--export FILE] [--workdir DIR] [--journal FILE] "
           "[--list] [--quiet]\n", program);
    printf("suites: memory jit cpu threads signals fs ipc loader\n");
}

static int phase02_make_workdir(char *out, size_t cap)
{
    const char *base = getenv("TMPDIR");
    char template_path[512];
    int written;

    if (base == NULL || base[0] == '\0') {
        base = "/tmp";
    }
    written = snprintf(template_path, sizeof(template_path), "%s/phase02_XXXXXX", base);
    if (written < 0 || (size_t)written >= sizeof(template_path)) {
        return -1;
    }
    if (mkdtemp(template_path) == NULL) {
        return -1;
    }
    written = snprintf(out, cap, "%s", template_path);
    if (written < 0 || (size_t)written >= cap) {
        return -1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    const char *suite = "all";
    const char *export_path = NULL;
    const char *workdir_arg = NULL;
    const char *journal_arg = NULL;
    char workdir[512];
    int quiet = 0;
    int list_only = 0;
    int argi;
    phase02_log_t *log;
    rt_status_t summary;
    char summary_line[256];
    int exit_code = 0;

    for (argi = 1; argi < argc; argi++) {
        if (strcmp(argv[argi], "--suite") == 0 && argi + 1 < argc) {
            suite = argv[++argi];
        } else if (strcmp(argv[argi], "--export") == 0 && argi + 1 < argc) {
            export_path = argv[++argi];
        } else if (strcmp(argv[argi], "--workdir") == 0 && argi + 1 < argc) {
            workdir_arg = argv[++argi];
        } else if (strcmp(argv[argi], "--journal") == 0 && argi + 1 < argc) {
            journal_arg = argv[++argi];
        } else if (strcmp(argv[argi], "--quiet") == 0) {
            quiet = 1;
        } else if (strcmp(argv[argi], "--list") == 0) {
            list_only = 1;
        } else if (strcmp(argv[argi], "--help") == 0 || strcmp(argv[argi], "-h") == 0) {
            phase02_usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "unknown argument: %s\n", argv[argi]);
            phase02_usage(argv[0]);
            return 2;
        }
    }

    if (list_only != 0) {
        char names[128];
        printf("suites: %s\n", phase02_suite_names(names, sizeof(names)));
        return 0;
    }

    if (workdir_arg != NULL) {
        int written = snprintf(workdir, sizeof(workdir), "%s", workdir_arg);
        if (written < 0 || (size_t)written >= sizeof(workdir)) {
            fprintf(stderr, "workdir path too long\n");
            return 2;
        }
    } else if (phase02_make_workdir(workdir, sizeof(workdir)) != 0) {
        fprintf(stderr, "could not create a temporary workdir: %s\n", strerror(errno));
        return 2;
    }

    /* Disabled unless asked for: an extra file on disk is a diagnostic, never a side effect
     * of running the suites. A journal that cannot be opened is reported and the run
     * continues (same rule the app follows). */
    if (journal_arg != NULL && phase02_progress_begin(journal_arg) != 0) {
        fprintf(stderr, "could not open the journal %s: %s\n", journal_arg, strerror(errno));
    }

    log = phase02_log_new();
    if (log == NULL) {
        fprintf(stderr, "out of memory\n");
        phase02_progress_end();
        return 2;
    }
    phase02_log_init(log, "PHASE_02_RECONSTRUCTED_POC - runtime PoC diagnostics");
    {
        char environment[192];
        if (phase02_platform_summary(environment, sizeof(environment)) < 0) {
            (void)snprintf(environment, sizeof(environment), "platform=<unavailable>");
        }
        phase02_log_line(log, "# host: %s workdir=%s", environment, workdir);
    }
    phase02_log_line(log, "# note: host results are NOT iOS results; physical-device "
                          "validation stays pending.");

    if (strcmp(suite, "all") == 0) {
        summary = phase02_run_all(log, workdir);
    } else {
        summary = phase02_run_suite(suite, log, workdir);
    }

    (void)phase02_log_summary_line(summary_line, sizeof(summary_line), log);
    phase02_log_blank(log);
    phase02_log_line(log, "# %s", summary_line);

    if (quiet == 0) {
        fputs(phase02_log_text(log), stdout);
    } else {
        printf("%s\n", summary_line);
    }

    if (export_path != NULL) {
        if (phase02_log_export(log, export_path) != 0) {
            fprintf(stderr, "could not export the report to %s: %s\n", export_path,
                    strerror(errno));
            exit_code = 2;
        } else if (quiet == 0) {
            printf("# report exported to %s\n", export_path);
        }
    }

    if (summary == RT_FAIL) {
        exit_code = 1;
    }

    phase02_log_free(log);
    phase02_progress_end();
    /* The temporary workdir is left in place: its path is in the report and the
     * evidence collector copies the report before /tmp is cleaned. */
    return exit_code;
}

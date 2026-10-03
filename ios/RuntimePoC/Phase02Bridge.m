/*
 * Phase02Bridge.m — the bridge implementation.
 *
 * PHASE_02_RECONSTRUCTED_POC.
 *
 * Notes for the physical-device run:
 *   - the workdir is a directory the app owns (the caller passes NSTemporaryDirectory());
 *   - a blocked capability (no MAP_JIT, no executable memory) is reported by the C
 *     layer as BLOCKED/UNSUPPORTED with the real errno. The bridge never turns that
 *     into a success and never catches signals itself.
 */
#import "Phase02Bridge.h"

/* Layering: this file is app glue and talks ONLY to the Diagnostics API
 * (phase02_harness.h / phase02_log.h). Everything it needs from RuntimeCore — the
 * platform name, the page size, the JIT ISA, the Apple target — comes from
 * phase02_platform_summary(). Including RuntimeCore headers here directly is what made
 * CI run #4 fail: the bridge called rt_jit_isa() with only runtime_platform.h in scope,
 * so the call had no visible declaration. */
#include "phase02_harness.h"
#include "phase02_log.h"
/* Pass 04: the flight recorder and the execution policy. The recorder is what makes a
 * termination diagnosable (IPHONE13_PHYSICAL_RUN_03 left no report at all); the policy decides
 * whether this target may be asked to enter freshly written memory. */
#include "phase02_progress.h"
#include "phase02_execution_policy.h"

/* The standard headers this file actually uses. They used to arrive by accident
 * through Foundation.h; a translation unit that names snprintf/strcmp should include
 * their headers, the same way every other file in this project does. */
#include <stdio.h>
#include <string.h>

static BOOL gLastRunHadFailure = NO;

@implementation Phase02Bridge

/* Where the diagnostics artefacts go: the app's Documents directory, which this bundle
 * exposes to the Files app (UIFileSharingEnabled + LSSupportsOpeningDocumentsInPlace). A run
 * that terminates without the app's cooperation leaves its journal and its write-ahead report
 * there, so the next person can read exactly how far the process got without a cable and
 * without a debugger. Falls back to the caller's workdir, and to "disabled" if neither is
 * available: a diagnostics aid must never be the reason a run fails. */
+ (NSString *)diagnosticsDirectoryWithFallback:(const char *)cWorkdir
{
    NSArray<NSString *> *paths = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory,
                                                                     NSUserDomainMask, YES);
    NSString *documents = (paths.count > 0) ? paths.firstObject : nil;
    if (documents.length > 0) {
        return documents;
    }
    if (cWorkdir != NULL && cWorkdir[0] != '\0') {
        return [NSString stringWithUTF8String:cWorkdir];
    }
    return nil;
}

+ (NSString *)runSuite:(NSString *)suiteName workdir:(NSString *)workdir
{
    NSString *name = (suiteName.length > 0) ? suiteName : @"all";
    const char *cName = name.UTF8String;
    const char *cWorkdir = (workdir.length > 0) ? workdir.UTF8String : NULL;
    phase02_log_t *log = phase02_log_new();
    NSString *diagnostics = [self diagnosticsDirectoryWithFallback:cWorkdir];
    char summaryLine[256];
    char summary[192];
    NSString *report;

    if (log == NULL) {
        phase02_progress_checkpoint("HARNESS_INIT_FAIL out-of-memory");
        phase02_progress_end();
        gLastRunHadFailure = YES;
        return @"[PHASE02] TEST=bridge STATUS=FAIL DETAIL=out of memory";
    }

    /* The journal is started before anything else that can go wrong, so every later step is
     * recorded. When the directory is unavailable the recorder stays disabled and the run
     * proceeds exactly as before. */
    if (diagnostics.length > 0) {
        char journalPath[512];
        if (phase02_progress_join(diagnostics.UTF8String, "phase02-run-journal.txt",
                                  journalPath, sizeof(journalPath)) == 0) {
            (void)phase02_progress_begin(journalPath);
        }
    }
    phase02_progress_checkpoint_kv("RUN_SELECTED_ENTER", "suite", cName);
    phase02_progress_checkpoint("HARNESS_INIT_ENTER");

    phase02_log_init(log, "PHASE_02_RECONSTRUCTED_POC - iOS runtime PoC diagnostics");
    if (phase02_platform_summary(summary, sizeof(summary)) < 0) {
        (void)snprintf(summary, sizeof(summary), "platform=<unavailable>");
    }
    {
        char policy[256];
        if (phase02_execution_policy_summary(policy, sizeof(policy)) > 0) {
            phase02_log_line(log, "# execution policy: %s", policy);
        }
    }
    phase02_log_line(log, "# device: %s", summary);
    phase02_log_line(log, "# suite: %s", cName);
    phase02_progress_checkpoint("HARNESS_INIT_OK");
    phase02_progress_checkpoint("REPORT_INITIALISED");
    (void)phase02_progress_write_ahead(log, phase02_progress_directory());

    if (strcmp(cName, "all") == 0) {
        phase02_progress_checkpoint("SUITE_DISPATCH_ENTER suite=all");
        (void)phase02_run_all(log, cWorkdir);
    } else {
        phase02_progress_checkpoint_kv("SUITE_DISPATCH_ENTER", "suite", cName);
        (void)phase02_run_suite(cName, log, cWorkdir);
    }
    phase02_progress_checkpoint("SUITE_DISPATCH_END");

    (void)phase02_log_summary_line(summaryLine, sizeof(summaryLine), log);
    phase02_log_blank(log);
    phase02_log_line(log, "# %s", summaryLine);
    phase02_progress_checkpoint("REPORT_FINALISED");
    (void)phase02_progress_write_ahead(log, phase02_progress_directory());

    gLastRunHadFailure =
        (phase02_log_status_count(log, RT_FAIL) > 0u) ? YES : NO;

    report = [NSString stringWithUTF8String:phase02_log_text(log)];
    phase02_log_free(log);
    phase02_progress_checkpoint("RUN_SELECTED_EXIT");
    phase02_progress_end();
    return (report != nil) ? report : @"";
}

+ (NSString *)runAllWithWorkdir:(NSString *)workdir
{
    return [self runSuite:@"all" workdir:workdir];
}

+ (NSString *)suiteNames
{
    char names[128];
    (void)phase02_suite_names(names, sizeof(names));
    NSString *value = [NSString stringWithUTF8String:names];
    return (value != nil) ? value : @"";
}

+ (NSString *)platformDescription
{
    char summary[192];
    if (phase02_platform_summary(summary, sizeof(summary)) < 0) {
        return @"platform=<unavailable>";
    }
    /* A C string into NSString — no %s formatting for the caller to get wrong, and no
     * cast: the ISA diagnosis stays exactly where it was, now composed by the layer
     * that owns the fact. */
    NSString *value = [NSString stringWithUTF8String:summary];
    return (value != nil) ? value : @"platform=<unavailable>";
}

+ (BOOL)lastRunHadFailure
{
    return gLastRunHadFailure;
}

+ (NSString *)reportFileName
{
    NSDateFormatter *formatter = [[NSDateFormatter alloc] init];
    formatter.dateFormat = @"yyyyMMdd-HHmmss";
    NSString *stamp = [formatter stringFromDate:[NSDate date]];
    return [NSString stringWithFormat:@"phase02-report-%@.txt", stamp];
}

@end

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

/* The standard headers this file actually uses. They used to arrive by accident
 * through Foundation.h; a translation unit that names snprintf/strcmp should include
 * their headers, the same way every other file in this project does. */
#include <stdio.h>
#include <string.h>

static BOOL gLastRunHadFailure = NO;

@implementation Phase02Bridge

+ (NSString *)runSuite:(NSString *)suiteName workdir:(NSString *)workdir
{
    NSString *name = (suiteName.length > 0) ? suiteName : @"all";
    const char *cName = name.UTF8String;
    const char *cWorkdir = (workdir.length > 0) ? workdir.UTF8String : NULL;
    phase02_log_t *log = phase02_log_new();
    char summaryLine[256];
    NSString *report;

    if (log == NULL) {
        gLastRunHadFailure = YES;
        return @"[PHASE02] TEST=bridge STATUS=FAIL DETAIL=out of memory";
    }

    phase02_log_init(log, "PHASE_02_RECONSTRUCTED_POC - iOS runtime PoC diagnostics");
    {
        char summary[192];
        if (phase02_platform_summary(summary, sizeof(summary)) < 0) {
            (void)snprintf(summary, sizeof(summary), "platform=<unavailable>");
        }
        phase02_log_line(log, "# device: %s", summary);
    }
    phase02_log_line(log, "# suite: %s", cName);

    if (strcmp(cName, "all") == 0) {
        (void)phase02_run_all(log, cWorkdir);
    } else {
        (void)phase02_run_suite(cName, log, cWorkdir);
    }

    (void)phase02_log_summary_line(summaryLine, sizeof(summaryLine), log);
    phase02_log_blank(log);
    phase02_log_line(log, "# %s", summaryLine);

    gLastRunHadFailure =
        (phase02_log_status_count(log, RT_FAIL) > 0u) ? YES : NO;

    report = [NSString stringWithUTF8String:phase02_log_text(log)];
    phase02_log_free(log);
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

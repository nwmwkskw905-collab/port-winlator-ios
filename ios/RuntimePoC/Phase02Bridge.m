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

#include "phase02_harness.h"
#include "phase02_log.h"
#include "runtime_platform.h"

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
    phase02_log_line(log, "# device: platform=%s page_size=%d", rt_platform_name(),
                     rt_platform_page_size());
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
    NSString *value = [NSString stringWithFormat:@"platform=%s page_size=%d isa=%s",
                       rt_platform_name(), rt_platform_page_size(), rt_jit_isa()];
    return value;
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

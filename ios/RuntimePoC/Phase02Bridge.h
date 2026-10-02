/*
 * Phase02Bridge.h — Objective-C bridge between the C harness and SwiftUI.
 *
 * PHASE_02_RECONSTRUCTED_POC.
 *
 * The bridge does not re-implement anything: it calls the same phase02_* entry
 * points the CLI harness uses, so the app and the CI cannot disagree.
 */
#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

@interface Phase02Bridge : NSObject

/* Runs one suite ("all" runs every suite) inside `workdir` and returns the full
 * report text, exactly as the C harness produced it. */
+ (NSString *)runSuite:(NSString *)suiteName workdir:(NSString *)workdir;

/* Convenience wrapper for the "Run All Tests" button. */
+ (NSString *)runAllWithWorkdir:(NSString *)workdir;

/* Comma-separated suite names, straight from the registry. */
+ (NSString *)suiteNames;

/* One-line description of the current platform/page size/ISA. */
+ (NSString *)platformDescription;

/* True when the last run recorded at least one FAIL line. */
+ (BOOL)lastRunHadFailure;

/* Suggested file name for the exported report. */
+ (NSString *)reportFileName;

@end

NS_ASSUME_NONNULL_END

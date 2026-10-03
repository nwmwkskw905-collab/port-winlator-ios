/*
 * Foundation.h — a *stub* Foundation, only for syntax checking on non-Apple hosts.
 *
 * PHASE_02_RECONSTRUCTED_POC. This file is NOT Foundation. It exists so that
 * `tools/check_bridge_syntax.sh` can run clang over Phase02Bridge.m on a Linux CI or
 * workstation and reproduce the diagnostics Apple's compiler would give:
 *
 *   - call to undeclared function          (Apple CI run #4: rt_jit_isa)
 *   - format specifies type 'char *' but the argument has type 'int'   (same run)
 *
 * Only what the bridge actually uses is declared. `+stringWithFormat:` carries the
 * printf format attribute, exactly as the real Foundation declares it through
 * NS_FORMAT_FUNCTION, which is what made run #4's format warning appear.
 *
 * It is never compiled into the app: the Xcode build uses the real SDK.
 */
#ifndef PHASE02_FAKE_FOUNDATION_H
#define PHASE02_FAKE_FOUNDATION_H

#define NS_ASSUME_NONNULL_BEGIN
#define NS_ASSUME_NONNULL_END

typedef signed char BOOL;
#define YES ((BOOL)1)
#define NO  ((BOOL)0)

#ifndef nil
#define nil ((id)0)
#endif

typedef struct objc_object *id;
typedef unsigned long NSUInteger;

@interface NSObject
+ (instancetype)alloc;
- (instancetype)init;
- (instancetype)retain;
- (void)release;
- (instancetype)autorelease;
@end

@interface NSString : NSObject
@property (readonly) NSUInteger length;
@property (readonly) const char *UTF8String;
+ (instancetype)stringWithUTF8String:(const char *)bytes;
/* __NSString__ is the format kind Foundation itself uses, so %@ is valid here and
 * '%s' with an int is diagnosed exactly as Apple's compiler diagnoses it. */
+ (instancetype)stringWithFormat:(NSString *)format, ... __attribute__((format(__NSString__, 1, 2)));
@end

@interface NSDate : NSObject
+ (instancetype)date;
@end

@interface NSDateFormatter : NSObject
@property (copy) NSString *dateFormat;
- (instancetype)init;
- (NSString *)stringFromDate:(NSDate *)date;
@end

/* Used by ContentView.swift through the bridging header's Swift surface; declared here
 * so the same stub also covers a syntax pass over app-level C. */
extern NSString *NSTemporaryDirectory(void);

#endif /* PHASE02_FAKE_FOUNDATION_H */

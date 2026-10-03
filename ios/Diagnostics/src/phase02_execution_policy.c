/*
 * phase02_execution_policy.c — see the header for the measured fact and the reasoning.
 *
 * PHASE_02_RECONSTRUCTED_POC (pass 04).
 */
#include "phase02_execution_policy.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "runtime_platform.h"

#ifndef PHASE02_IOS_ALLOW_EXECUTION_ATTEMPT
#define PHASE02_IOS_ALLOW_EXECUTION_ATTEMPT 0
#endif

/* -1 = "no explicit request"; set only by phase02_execution_request_attempt(). */
static int g_attempt_override = -1;

/* Set while a profile is being simulated (see phase02_execution_profile_current). */
static int g_profile_override_active = 0;

/* Documented override used ONLY to exercise the profile of another target on this machine
 * (the fenced iOS rows cannot otherwise be tested at all). It never turns a host run into
 * device evidence: the policy summary names the source, so a report carrying it cannot be
 * mistaken for a physical measurement. Values: ios-device, ios-simulator, apple-host,
 * portable. Unrecognised values are ignored. */
static const char *const g_profile_override_names[] = { "ios-device", "ios-simulator",
                                                        "apple-host", "portable", NULL };

int phase02_execution_decide(int platform_allows, int attempt_enabled)
{
    if (platform_allows == 0 && attempt_enabled == 0) {
        return 0;                     /* the platform cannot, and nobody asked to try */
    }
    return 1;
}

const char *phase02_execution_decision_name(int decision)
{
    return (decision != 0) ? "ATTEMPT" : "DEFERRED";
}

const char *phase02_execution_reason(const phase02_execution_profile_t *profile,
                                     phase02_exec_op_t operation, int decision)
{
    if (decision != 0) {
        return "the operation is attempted here; the result is measured, never assumed";
    }
    switch (operation) {
    case PHASE02_EXEC_OP_ENTER_WRITTEN_MEMORY:
        return "policy deferral on this target (NOT a platform verdict): execute permission is "
               "stripped without any reported error and entering such a page terminated "
               "IPHONE13_PHYSICAL_RUN_03; the attempt is available with the "
               "execution-attempt opt-in";
    case PHASE02_EXEC_OP_OBJECT_EXEC_VIEW:
        return "policy deferral on this target (NOT a platform verdict): the executable view of "
               "an app-owned object reaches the same gate that terminated "
               "IPHONE13_PHYSICAL_RUN_03 (this suite, after run 02 had passed the R-X "
               "transition); the named POSIX path is measured instead and the attempt is "
               "available with the execution-attempt opt-in";
    }
    (void)profile;
    return "deferred by policy";
}

const phase02_execution_profile_t *phase02_execution_profile_current(void)
{
    static const phase02_execution_profile_t ios_profile_override = {
        "ios-device(override)",
        1,
        0,
        0,
        "SIMULATED iOS profile: this run is on a host, using the iOS-class policy to exercise "
        "the deferral path. It is NEVER device evidence."
    };
    static const phase02_execution_profile_t ios_profile = {
        "ios-device",
        1,
        0,
        0,
        "iOS 26 / TXM-SPTM hardware (A15+): MAP_JIT refused (no dynamic-codesigning "
        "entitlement for third-party apps), mprotect(+EXEC) may report success while the page "
        "stays non-executable, and entering such a page terminated the whole process in "
        "IPHONE13_PHYSICAL_RUN_03"
    };
    static const phase02_execution_profile_t ios_sim_profile = {
        "ios-simulator",
        1,
        1,                  /* the simulator is the host kernel: no TXM enforcement */
        0,
        "the simulator inherits the host's memory model: entering written memory works there "
        "and that result never describes the device"
    };
    static const phase02_execution_profile_t apple_host_profile = {
        "apple-host",
        0,
        1,
        1,
        "macOS: MAP_JIT plus the write-protect window is the supported mechanism"
    };
    static const phase02_execution_profile_t generic_profile = {
        "portable",
        0,
        1,
        1,
        "no platform-specific execution gate is known for this target"
    };

    {
        const char *override_name = getenv("PHASE02_TARGET_OVERRIDE");
        int index;
        for (index = 0; override_name != NULL && g_profile_override_names[index] != NULL;
             index++) {
            if (strcmp(override_name, g_profile_override_names[index]) == 0) {
                g_profile_override_active = (index == 0) ? 1 : 0;
                if (index == 0) {
                    return &ios_profile_override;
                }
                if (index == 1) {
                    return &ios_sim_profile;
                }
                if (index == 2) {
                    return &apple_host_profile;
                }
                return &generic_profile;
            }
        }
        g_profile_override_active = 0;
    }
    if (rt_platform_apple_target() == RT_APPLE_TARGET_IPHONE_DEVICE) {
        return &ios_profile;
    }
    if (rt_platform_apple_target() == RT_APPLE_TARGET_IPHONE_SIMULATOR) {
        return &ios_sim_profile;
    }
    if (rt_platform_apple_target() != RT_APPLE_TARGET_NONE) {
        return &apple_host_profile;
    }
    return &generic_profile;
}

int phase02_execution_attempt_enabled(void)
{
    const char *environment;

    /* Precedence: what the operator set in the environment, then a programmatic request,
     * then the compile-time default, then the target default. */
    environment = getenv("PHASE02_ALLOW_EXECUTION");
    if (environment != NULL && environment[0] != '\0') {
        if (environment[0] == '1') {
            return 1;
        }
        if (environment[0] == '0') {
            return 0;
        }
    }
    if (g_attempt_override >= 0) {
        return g_attempt_override;
    }
    if (PHASE02_IOS_ALLOW_EXECUTION_ATTEMPT != 0) {
        return 1;
    }
    /* Nothing asked for an attempt: the target's own default decides. */
    return 0;
}

void phase02_execution_request_attempt(int enable)
{
    g_attempt_override = (enable != 0) ? 1 : 0;
}

int phase02_execution_allowed(phase02_exec_op_t operation)
{
    const phase02_execution_profile_t *profile = phase02_execution_profile_current();
    int platform_allows = (operation == PHASE02_EXEC_OP_ENTER_WRITTEN_MEMORY)
                              ? profile->platform_allows_enter
                              : profile->platform_allows_object_view;

    return phase02_execution_decide(platform_allows, phase02_execution_attempt_enabled());
}

int phase02_execution_policy_summary(char *out, unsigned long cap)
{
    const phase02_execution_profile_t *profile = phase02_execution_profile_current();
    int written;
    int attempt = phase02_execution_attempt_enabled();
    int enter = phase02_execution_decide(profile->platform_allows_enter, attempt);
    int view = phase02_execution_decide(profile->platform_allows_object_view, attempt);

    if (out == NULL || cap == 0u) {
        return -1;
    }
    written = snprintf(out, (size_t)cap,
                       "target=%s enter=%s object-view=%s attempt-opt-in=%d profile-source=%s",
                       profile->name, phase02_execution_decision_name(enter),
                       phase02_execution_decision_name(view), attempt,
                       (g_profile_override_active != 0) ? "override-simulation-not-device-evidence"
                                                        : "native");
    if (written < 0 || (size_t)written >= (size_t)cap) {
        out[0] = '\0';
        return -1;
    }
    return written;
}

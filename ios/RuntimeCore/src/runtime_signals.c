/*
 * runtime_signals.c — guard, benign-signal round trip and mask handling.
 *
 * PHASE_02_RECONSTRUCTED_POC (see runtime_platform.h).
 */
#include "runtime_signals.h"

#include <errno.h>
#include <pthread.h>
#include <setjmp.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Only one guard can be active: faults arrive asynchronously and the handler has to
 * know which frame owns the jump buffer. Attempting a nested guard is EBUSY. */
static rt_fault_guard_t *g_active_guard = NULL;
static volatile sig_atomic_t g_last_fault_signal = 0;

/* Written only through a volatile object so the compiler cannot fold the address
 * into a literal "dereference NULL" warning at -O2; the fault is the point. */
static volatile uintptr_t g_fault_target = 0u;

static void rt_fault_handler(int sig, siginfo_t *info, void *ucontext)
{
    rt_fault_guard_t *guard = g_active_guard;
    (void)ucontext;

    if (guard != NULL && guard->armed != 0) {
        guard->fired = 1;
        guard->fault_addr = (info != NULL) ? info->si_addr : NULL;
        guard->fault_signal = sig;
        guard->armed = 0;
        g_last_fault_signal = (sig_atomic_t)sig;
        siglongjmp(guard->env, 1);
    }

    /* Not ours: restore the default disposition and let the process die honestly
     * instead of looping on a fault we cannot describe. */
    (void)signal(sig, SIG_DFL);
    (void)raise(sig);
}

static void rt_benign_handler(int sig)
{
    (void)sig;
}

int rt_fault_guard_begin(rt_fault_guard_t *guard, int *err_out)
{
    struct sigaction action;

    if (guard == NULL) {
        if (err_out != NULL) {
            *err_out = EINVAL;
        }
        return -1;
    }
    memset(guard, 0, sizeof(*guard));
    if (g_active_guard != NULL) {
        if (err_out != NULL) {
            *err_out = EBUSY;
        }
        return -1;
    }

    memset(&action, 0, sizeof(action));
    action.sa_sigaction = rt_fault_handler;
    action.sa_flags = SA_SIGINFO;
    (void)sigemptyset(&action.sa_mask);

    if (sigaction(SIGSEGV, &action, &guard->saved_segv) != 0) {
        if (err_out != NULL) {
            *err_out = errno;
        }
        return -1;
    }
    if (sigaction(SIGBUS, &action, &guard->saved_bus) != 0) {
        int saved_errno = errno;
        (void)sigaction(SIGSEGV, &guard->saved_segv, NULL);
        if (err_out != NULL) {
            *err_out = saved_errno;
        }
        return -1;
    }

    g_active_guard = guard;
    guard->installed = 1;
    if (err_out != NULL) {
        *err_out = 0;
    }
    return 0;
}

int rt_fault_guard_end(rt_fault_guard_t *guard)
{
    if (guard == NULL || guard->installed == 0) {
        return -1;
    }
    (void)sigaction(SIGSEGV, &guard->saved_segv, NULL);
    (void)sigaction(SIGBUS, &guard->saved_bus, NULL);
    guard->installed = 0;
    if (g_active_guard == guard) {
        g_active_guard = NULL;
    }
    return 0;
}

int rt_fault_guard_fired(const rt_fault_guard_t *guard)
{
    return (guard != NULL && guard->fired != 0) ? 1 : 0;
}

void *rt_fault_guard_addr(const rt_fault_guard_t *guard)
{
    return (guard != NULL) ? guard->fault_addr : NULL;
}

int rt_fault_guard_signal(const rt_fault_guard_t *guard)
{
    return (guard != NULL) ? guard->fault_signal : 0;
}

int rt_signal_last_fault_signal(void)
{
    return (int)g_last_fault_signal;
}

int rt_signal_roundtrip(int signal_number, int *err_out)
{
    struct sigaction action;
    struct sigaction queried;
    struct sigaction saved;

    memset(&action, 0, sizeof(action));
    action.sa_handler = rt_benign_handler;
    (void)sigemptyset(&action.sa_mask);

    if (sigaction(signal_number, &action, &saved) != 0) {
        if (err_out != NULL) {
            *err_out = errno;
        }
        return -1;
    }
    if (sigaction(signal_number, NULL, &queried) != 0) {
        int saved_errno = errno;
        (void)sigaction(signal_number, &saved, NULL);
        if (err_out != NULL) {
            *err_out = saved_errno;
        }
        return -1;
    }
    if (queried.sa_handler != rt_benign_handler) {
        /* The handler that came back is not the one just installed: a failure of this
         * function, and a failure always carries an errno. Returning -1 with the caller's
         * err_out untouched published "errno=0" and read like a platform answer. */
        (void)sigaction(signal_number, &saved, NULL);
        if (err_out != NULL) {
            *err_out = EIO;
        }
        return -1;
    }
    if (sigaction(signal_number, &saved, NULL) != 0) {
        /* Restoring the previous disposition is part of the round trip. Discarding this
         * result — as the previous code did, with err_out forced to 0 afterwards — reported
         * PASS for a signal disposition left modified. */
        if (err_out != NULL) {
            *err_out = errno;
        }
        return -1;
    }
    if (err_out != NULL) {
        *err_out = 0;
    }
    return 0;
}

int rt_signal_mask_roundtrip(int signal_number, int *err_out)
{
    sigset_t set;
    sigset_t previous;
    sigset_t pending;
    int blocked;
    int rc = -1;

    if (sigemptyset(&set) != 0 || sigaddset(&set, signal_number) != 0) {
        if (err_out != NULL) {
            *err_out = errno;
        }
        return -1;
    }
    if (pthread_sigmask(SIG_BLOCK, &set, &previous) != 0) {
        if (err_out != NULL) {
            *err_out = errno;
        }
        return -1;
    }
    if (sigpending(&pending) != 0) {
        if (err_out != NULL) {
            *err_out = errno;
        }
        (void)pthread_sigmask(SIG_SETMASK, &previous, NULL);
        return -1;
    }
    blocked = sigismember(&pending, signal_number);
    if (blocked == 0) {
        /* The mask query answered: the signal is reportable, which is all we need.
         * blocked == 0 is expected because this signal was never raised. */
        rc = 0;
    } else if (err_out != NULL) {
        *err_out = EINVAL;
    }
    if (pthread_sigmask(SIG_SETMASK, &previous, NULL) != 0) {
        if (err_out != NULL) {
            *err_out = errno;
        }
        rc = -1;
    }
    return rc;
}

int rt_signal_controlled_segv(void **fault_addr_out, int *err_out)
{
    rt_fault_guard_t guard;

    if (fault_addr_out != NULL) {
        *fault_addr_out = NULL;
    }
    if (rt_fault_guard_begin(&guard, err_out) != 0) {
        return -2;
    }
    guard.armed = 1;
    if (RT_FAULT_GUARD_TRY(&guard)) {
        volatile int *target = (volatile int *)g_fault_target;
        *target = 1; /* expected to fault; the guard catches it */
        (void)rt_fault_guard_end(&guard);
        return -1; /* no fault happened: the probe itself failed */
    }
    if (fault_addr_out != NULL) {
        *fault_addr_out = rt_fault_guard_addr(&guard);
    }
    (void)rt_fault_guard_end(&guard);
    return 0;
}

int rt_signal_call_guarded(rt_guarded_fn_t fn, uint32_t *value_out,
                           void **fault_addr_out, int *err_out)
{
    rt_fault_guard_t guard;

    if (fault_addr_out != NULL) {
        *fault_addr_out = NULL;
    }
    if (fn == NULL) {
        if (err_out != NULL) {
            *err_out = EINVAL;
        }
        return -2;
    }
    if (rt_fault_guard_begin(&guard, err_out) != 0) {
        return -2;
    }
    guard.armed = 1;
    if (RT_FAULT_GUARD_TRY(&guard)) {
        uint32_t value = fn();
        if (value_out != NULL) {
            *value_out = value;
        }
        (void)rt_fault_guard_end(&guard);
        return 0;
    }
    if (fault_addr_out != NULL) {
        *fault_addr_out = rt_fault_guard_addr(&guard);
    }
    (void)rt_fault_guard_end(&guard);
    return -1;
}

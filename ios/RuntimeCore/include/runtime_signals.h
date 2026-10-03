/*
 * runtime_signals.h — controlled signal handling for the harness.
 *
 * PHASE_02_RECONSTRUCTED_POC (see runtime_platform.h).
 *
 * Safety rule from the specification: a diagnostic test must never take the whole
 * suite down. Every fault-producing test runs inside a guard that records si_addr
 * and siglongjmp()s back into the calling frame.
 *
 * Guard mechanics (only one guard may be active at a time — documented, enforced
 * by `active`):
 *
 *     rt_fault_guard_t guard;
 *     int err = 0;
 *     if (rt_fault_guard_begin(&guard, &err) != 0) { ... -2 ... }
 *     if (RT_FAULT_GUARD_TRY(&guard)) {   // sigsetjmp in the *caller's* frame
 *         ... risky access ...
 *         rt_fault_guard_end(&guard);
 *     } else {
 *         void *addr = rt_fault_guard_addr(&guard);   // si_addr
 *         rt_fault_guard_end(&guard);
 *     }
 */
#ifndef RUNTIME_SIGNALS_H
#define RUNTIME_SIGNALS_H

#include <setjmp.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>

#include "runtime_platform.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct rt_fault_guard {
    sigjmp_buf            env;
    volatile sig_atomic_t armed;
    volatile sig_atomic_t fired;
    void                 *fault_addr;
    int                   fault_signal;
    struct sigaction      saved_segv;
    struct sigaction      saved_bus;
    int                   installed;
} rt_fault_guard_t;

/* Installs temporary SIGSEGV/SIGBUS handlers. Returns 0, or -1 with *err_out. */
int rt_fault_guard_begin(rt_fault_guard_t *guard, int *err_out);

/* Restores the previous handlers. Always call it, including on the fault path. */
int rt_fault_guard_end(rt_fault_guard_t *guard);

/* True when the last jump taken by this guard was caused by a fault. */
int rt_fault_guard_fired(const rt_fault_guard_t *guard);
void *rt_fault_guard_addr(const rt_fault_guard_t *guard);
int rt_fault_guard_signal(const rt_fault_guard_t *guard);

/* sigsetjmp()==0 means "try the risky work". The macro exists so the jump target
 * stays in the caller's frame, which siglongjmp() requires. */
#define RT_FAULT_GUARD_TRY(guard) (sigsetjmp((guard)->env, 1) == 0)

/* install -> query -> restore round trip for a benign signal (e.g. SIGUSR1). */
int rt_signal_roundtrip(int signal_number, int *err_out);

/* Blocks then unblocks a signal and verifies the mask reports it again. */
int rt_signal_mask_roundtrip(int signal_number, int *err_out);

/*
 * Produces ONE deliberate fault inside a guarded scope and reports it: the probe writes to a
 * page this process owns and has made inaccessible (RT_PROT_NONE), never through a null
 * pointer — a null-pointer store is undefined behaviour that UBSAN flags and an optimiser may
 * remove, so it cannot be the basis of a test. The signal, the guard and the si_addr
 * semantics are unchanged.
 * Returns 0 when the fault was observed (*fault_addr_out = si_addr),
 * -1 when no fault was observed (a failure of the probe itself),
 * -2 when the probe could not be prepared (guard or fault page; errno in *err_out).
 */
int rt_signal_controlled_segv(void **fault_addr_out, int *err_out);

/*
 * Runs `fn` (generated code, typically) under the same guard.
 * Returns:
 *    0  the call returned; *value_out holds its result and *fault_addr_out is NULL;
 *   -1  a fault was caught; *fault_addr_out holds si_addr;
 *   -2  the guard could not be installed (errno in *err_out).
 * This is what lets the JIT suite report BLOCKED instead of dying when the process
 * has no permission to execute generated memory.
 */
typedef uint32_t (*rt_guarded_fn_t)(void);
int rt_signal_call_guarded(rt_guarded_fn_t fn, uint32_t *value_out,
                           void **fault_addr_out, int *err_out);

/* Signal number recorded by the last guarded fault (0 when none). */
int rt_signal_last_fault_signal(void);

#ifdef __cplusplus
}
#endif

#endif /* RUNTIME_SIGNALS_H */

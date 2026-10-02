/*
 * runtime_threads.h — pthreads, TLS, mutex, condition variable and atomics.
 *
 * PHASE_02_RECONSTRUCTED_POC (see runtime_platform.h).
 *
 * Every function returns 0 on success or -1 on failure with *err_out set to the
 * failing errno. Results must be deterministic: no timing-dependent assertions.
 */
#ifndef RUNTIME_THREADS_H
#define RUNTIME_THREADS_H

#include <stddef.h>
#include <stdint.h>

#include "runtime_platform.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RT_THREADS_WORKERS 4u
#define RT_THREADS_ITERATIONS 1000u

/* create/join: every worker adds its own index into a shared slot; the joined
 * value must equal the sum of all indices. */
int rt_thread_roundtrip(uint64_t *value_out, int *err_out);

/* one TLS key per worker, filled by the thread and read back after join. */
int rt_thread_tls_roundtrip(uint64_t *value_out, int *err_out);

/* RT_THREADS_WORKERS threads increment a mutex-protected counter
 * RT_THREADS_ITERATIONS times each. */
int rt_thread_mutex_counter(uint64_t *value_out, int *err_out);

/* condition variable: one producer hands RT_THREADS_WORKERS tokens to one
 * consumer; the consumer must receive exactly that many. */
int rt_thread_condition_pingpong(uint64_t *value_out, int *err_out);

/* C11 atomics: relaxed fetch_add from all workers, sequentially consistent
 * final read. */
int rt_thread_atomics_roundtrip(uint64_t *value_out, int *err_out);

#ifdef __cplusplus
}
#endif

#endif /* RUNTIME_THREADS_H */

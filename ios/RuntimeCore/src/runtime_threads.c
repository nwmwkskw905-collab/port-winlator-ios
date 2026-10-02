/*
 * runtime_threads.c — pthreads, TLS, mutex, condition variable, atomics.
 *
 * PHASE_02_RECONSTRUCTED_POC (see runtime_platform.h).
 */
#include "runtime_threads.h"

#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

/* ------------------------------------------------------------ create / join */

typedef struct {
    uint64_t slot;
    unsigned index;
} rt_worker_arg_t;

static void *rt_worker_store(void *raw)
{
    rt_worker_arg_t *arg = (rt_worker_arg_t *)raw;
    arg->slot = 0x1000000000000000ULL + (uint64_t)arg->index;
    return NULL;
}

int rt_thread_roundtrip(uint64_t *value_out, int *err_out)
{
    pthread_t threads[RT_THREADS_WORKERS];
    rt_worker_arg_t args[RT_THREADS_WORKERS];
    uint64_t expected = 0u;
    uint64_t observed = 0u;
    unsigned i;
    int rc;

    for (i = 0u; i < RT_THREADS_WORKERS; i++) {
        args[i].slot = 0u;
        args[i].index = i;
        expected += 0x1000000000000000ULL + (uint64_t)i;
        rc = pthread_create(&threads[i], NULL, rt_worker_store, &args[i]);
        if (rc != 0) {
            unsigned joined;
            for (joined = 0u; joined < i; joined++) {
                (void)pthread_join(threads[joined], NULL);
            }
            if (err_out != NULL) {
                *err_out = rc;
            }
            return -1;
        }
    }
    for (i = 0u; i < RT_THREADS_WORKERS; i++) {
        rc = pthread_join(threads[i], NULL);
        if (rc != 0) {
            if (err_out != NULL) {
                *err_out = rc;
            }
            return -1;
        }
        observed += args[i].slot;
    }
    if (value_out != NULL) {
        *value_out = observed;
    }
    if (err_out != NULL) {
        *err_out = 0;
    }
    return (observed == expected) ? 0 : -1;
}

/* ---------------------------------------------------------------------- TLS */

static pthread_key_t rt_tls_key;
static int rt_tls_key_ready = 0;

typedef struct {
    uint64_t observed;
    unsigned index;
} rt_tls_arg_t;

static void *rt_worker_tls(void *raw)
{
    rt_tls_arg_t *arg = (rt_tls_arg_t *)raw;
    uint64_t token = 0x2000u + (uint64_t)arg->index * 0x11u;
    if (pthread_setspecific(rt_tls_key, (void *)(uintptr_t)token) != 0) {
        arg->observed = 0u;
        return NULL;
    }
    arg->observed = (uint64_t)(uintptr_t)pthread_getspecific(rt_tls_key);
    return NULL;
}

int rt_thread_tls_roundtrip(uint64_t *value_out, int *err_out)
{
    pthread_t threads[RT_THREADS_WORKERS];
    rt_tls_arg_t args[RT_THREADS_WORKERS];
    uint64_t observed = 0u;
    uint64_t expected = 0u;
    unsigned i;
    int rc;

    if (rt_tls_key_ready == 0) {
        rc = pthread_key_create(&rt_tls_key, NULL);
        if (rc != 0) {
            if (err_out != NULL) {
                *err_out = rc;
            }
            return -1;
        }
        rt_tls_key_ready = 1;
    }

    for (i = 0u; i < RT_THREADS_WORKERS; i++) {
        args[i].observed = 0u;
        args[i].index = i;
        rc = pthread_create(&threads[i], NULL, rt_worker_tls, &args[i]);
        if (rc != 0) {
            unsigned joined;
            for (joined = 0u; joined < i; joined++) {
                (void)pthread_join(threads[joined], NULL);
            }
            if (err_out != NULL) {
                *err_out = rc;
            }
            return -1;
        }
    }
    for (i = 0u; i < RT_THREADS_WORKERS; i++) {
        if (pthread_join(threads[i], NULL) != 0) {
            if (err_out != NULL) {
                *err_out = EIO;
            }
            return -1;
        }
        observed += args[i].observed;
        expected += 0x2000u + (uint64_t)i * 0x11u;
    }
    if (value_out != NULL) {
        *value_out = observed;
    }
    if (err_out != NULL) {
        *err_out = 0;
    }
    return (observed == expected) ? 0 : -1;
}

/* ------------------------------------------------------------------- mutex */

typedef struct {
    pthread_mutex_t lock;
    uint64_t counter;
} rt_mutex_box_t;

static void *rt_worker_mutex(void *raw)
{
    rt_mutex_box_t *box = (rt_mutex_box_t *)raw;
    unsigned i;
    for (i = 0u; i < RT_THREADS_ITERATIONS; i++) {
        (void)pthread_mutex_lock(&box->lock);
        box->counter++;
        (void)pthread_mutex_unlock(&box->lock);
    }
    return NULL;
}

int rt_thread_mutex_counter(uint64_t *value_out, int *err_out)
{
    pthread_t threads[RT_THREADS_WORKERS];
    rt_mutex_box_t box;
    uint64_t expected = (uint64_t)RT_THREADS_WORKERS * (uint64_t)RT_THREADS_ITERATIONS;
    unsigned i;

    box.counter = 0u;
    if (pthread_mutex_init(&box.lock, NULL) != 0) {
        if (err_out != NULL) {
            *err_out = EAGAIN;
        }
        return -1;
    }
    for (i = 0u; i < RT_THREADS_WORKERS; i++) {
        if (pthread_create(&threads[i], NULL, rt_worker_mutex, &box) != 0) {
            unsigned joined;
            for (joined = 0u; joined < i; joined++) {
                (void)pthread_join(threads[joined], NULL);
            }
            (void)pthread_mutex_destroy(&box.lock);
            if (err_out != NULL) {
                *err_out = EAGAIN;
            }
            return -1;
        }
    }
    for (i = 0u; i < RT_THREADS_WORKERS; i++) {
        (void)pthread_join(threads[i], NULL);
    }
    (void)pthread_mutex_destroy(&box.lock);
    if (value_out != NULL) {
        *value_out = box.counter;
    }
    if (err_out != NULL) {
        *err_out = 0;
    }
    return (box.counter == expected) ? 0 : -1;
}

/* -------------------------------------------------------------- condition */

typedef struct {
    pthread_mutex_t lock;
    pthread_cond_t  cond;
    uint64_t        tokens;
} rt_pingpong_t;

static void *rt_worker_producer(void *raw)
{
    rt_pingpong_t *box = (rt_pingpong_t *)raw;
    unsigned i;
    for (i = 0u; i < RT_THREADS_WORKERS; i++) {
        (void)pthread_mutex_lock(&box->lock);
        box->tokens++;
        (void)pthread_cond_signal(&box->cond);
        (void)pthread_mutex_unlock(&box->lock);
    }
    return NULL;
}

int rt_thread_condition_pingpong(uint64_t *value_out, int *err_out)
{
    rt_pingpong_t box;
    pthread_t producer;
    uint64_t expected = (uint64_t)RT_THREADS_WORKERS;

    box.tokens = 0u;
    if (pthread_mutex_init(&box.lock, NULL) != 0 || pthread_cond_init(&box.cond, NULL) != 0) {
        if (err_out != NULL) {
            *err_out = EAGAIN;
        }
        return -1;
    }
    if (pthread_create(&producer, NULL, rt_worker_producer, &box) != 0) {
        (void)pthread_cond_destroy(&box.cond);
        (void)pthread_mutex_destroy(&box.lock);
        if (err_out != NULL) {
            *err_out = EAGAIN;
        }
        return -1;
    }

    (void)pthread_mutex_lock(&box.lock);
    while (box.tokens < expected) {
        (void)pthread_cond_wait(&box.cond, &box.lock);
    }
    (void)pthread_mutex_unlock(&box.lock);

    (void)pthread_join(producer, NULL);
    (void)pthread_cond_destroy(&box.cond);
    (void)pthread_mutex_destroy(&box.lock);

    if (value_out != NULL) {
        *value_out = box.tokens;
    }
    if (err_out != NULL) {
        *err_out = 0;
    }
    return (box.tokens == expected) ? 0 : -1;
}

/* ------------------------------------------------------------------ atomics */

static atomic_uint_fast64_t rt_atomic_counter;

static void *rt_worker_atomics(void *unused)
{
    unsigned i;
    (void)unused;
    for (i = 0u; i < RT_THREADS_ITERATIONS; i++) {
        (void)atomic_fetch_add_explicit(&rt_atomic_counter, 1u, memory_order_relaxed);
    }
    return NULL;
}

int rt_thread_atomics_roundtrip(uint64_t *value_out, int *err_out)
{
    pthread_t threads[RT_THREADS_WORKERS];
    uint64_t expected = (uint64_t)RT_THREADS_WORKERS * (uint64_t)RT_THREADS_ITERATIONS;
    uint64_t observed;
    unsigned i;

    atomic_store_explicit(&rt_atomic_counter, 0u, memory_order_seq_cst);
    for (i = 0u; i < RT_THREADS_WORKERS; i++) {
        if (pthread_create(&threads[i], NULL, rt_worker_atomics, NULL) != 0) {
            unsigned joined;
            for (joined = 0u; joined < i; joined++) {
                (void)pthread_join(threads[joined], NULL);
            }
            if (err_out != NULL) {
                *err_out = EAGAIN;
            }
            return -1;
        }
    }
    for (i = 0u; i < RT_THREADS_WORKERS; i++) {
        (void)pthread_join(threads[i], NULL);
    }
    observed = (uint64_t)atomic_load_explicit(&rt_atomic_counter, memory_order_seq_cst);
    if (value_out != NULL) {
        *value_out = observed;
    }
    if (err_out != NULL) {
        *err_out = 0;
    }
    return (observed == expected) ? 0 : -1;
}

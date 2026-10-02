/*
 * runtime_memory.h — page-level memory API and the dual-mapping experiment.
 *
 * PHASE_02_RECONSTRUCTED_POC (see runtime_platform.h).
 *
 * Architecture rule: strict W^X is preferred. No API here maps a page writable and
 * executable at the same time through the generic path; the JIT arena (runtime_jit.h)
 * is the only place that deals with MAP_JIT, and it does so explicitly.
 */
#ifndef RUNTIME_MEMORY_H
#define RUNTIME_MEMORY_H

#include <stddef.h>
#include <stdint.h>

#include "runtime_platform.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Reserve `len` bytes of anonymous memory, readable and writable. Rounds up to a
 * whole number of pages. Returns NULL and sets *err_out to the real errno on failure. */
void *rt_mem_reserve(size_t len, int *err_out);

/* Change the protection of a region previously returned by rt_mem_reserve(). */
int rt_mem_protect(void *addr, size_t len, rt_prot_t prot, int *err_out);

/* Release a region. */
int rt_mem_release(void *addr, size_t len, int *err_out);

/* Read `len` bytes from `addr` (one volatile read per page). Returns 0 on success. */
int rt_mem_read_probe(const void *addr, size_t len);

/*
 * Attempt an access at `addr` and report whether the CPU refused it.
 * write != 0 writes a byte, otherwise reads one.
 * Returns:
 *    0  access completed without a fault;
 *   -1  a fault was observed (SIGSEGV/SIGBUS caught by a temporary handler),
 *       *fault_addr_out receives si_addr;
 *   -2  this process cannot install the temporary handler (errno in *err_out).
 * The mapping is never modified by this function.
 */
int rt_mem_fault_probe(void *addr, int write, void **fault_addr_out, int *err_out);

/*
 * Dual mapping experiment: two views of the same physical memory, one writable and
 * one executable/readable. Implemented with a POSIX shared-memory object mapped twice.
 * On platforms that refuse PROT_EXEC on such a mapping (expected on iOS without the
 * right signing context) creation fails with the real errno and `supported` stays 0.
 * Success here is a *host* observation only; iOS behaviour is UNTESTED until a device
 * runs the same code.
 */
typedef struct rt_dual_map {
    void  *rw;        /* writable view */
    void  *rx;        /* executable/readable view */
    size_t len;       /* page-rounded length */
    int    err;       /* errno of the failing step, when supported == 0 */
    int    supported; /* 1 only when both views exist */
} rt_dual_map_t;

int rt_dual_map_create(size_t len, rt_dual_map_t *out);
int rt_dual_map_destroy(rt_dual_map_t *map);

/* Writes a known pattern through `rw` and reads it back through `rx`.
 * Returns 1 when the two views alias the same memory, 0 when they do not,
 * -1 when the map is not usable. */
int rt_dual_map_views_aliased(rt_dual_map_t *map);

#ifdef __cplusplus
}
#endif

#endif /* RUNTIME_MEMORY_H */

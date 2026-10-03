/*
 * runtime_jit.h — generated-code payloads, icache maintenance and a callable entry point.
 *
 * PHASE_02_RECONSTRUCTED_POC (see runtime_platform.h).
 *
 * The microtest contract (documented, deliberately tiny):
 *   emit a function that takes no arguments and returns a 32-bit immediate;
 *   - first payload returns 42;
 *   - the same buffer is rewritten to return 4242, the instruction cache is
 *     synchronised and the function is called again.
 * Nothing here simulates a result: the value comes back from real execution of the
 * generated instructions, and on iOS the caller must expect the call to fault when
 * the process has no usable executable memory.
 */
#ifndef RUNTIME_JIT_H
#define RUNTIME_JIT_H

#include <stddef.h>
#include <stdint.h>

#include "runtime_platform.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Signature of the generated function. */
typedef uint32_t (*rt_jit_fn_t)(void);

/* ISA the emitter targets in this build ("x86-64", "aarch64", ...). */
const char *rt_jit_isa(void);

/* Writes `mov <ret>, imm ; ret` for the current ISA into buf.
 * Returns 0 and sets *len_out, or -1 (buffer too small) / -2 (ISA not implemented). */
int rt_jit_emit_return_imm(uint8_t *buf, size_t cap, uint32_t imm, size_t *len_out);

/* Instruction-cache synchronisation after code modification.
 * Applies to the whole range; returns 0 (no platform here can report failure). */
int rt_jit_invalidate(void *addr, size_t len);

/* Calls generated code. The buffer must be executable when this is called. */
uint32_t rt_jit_call_u32(const void *code);

/*
 * Executable arena. On Apple platforms with MAP_JIT this reserves a W^X-managed
 * region that must be opened/closed around writes with the two calls below.
 * Without MAP_JIT the arena is a plain anonymous mapping that the caller must
 * flip with rt_mem_protect(); the write-window calls then return -1/ENOTSUP.
 */
/* Which mechanism actually backs an arena returned by the allocator. The caller needs this
 * because the two mechanisms have *different* write protocols (see the header of
 * runtime_jit.c): a MAP_JIT region must be written inside the write-protect window, while an
 * anonymous W^X arena must be flipped with rt_mem_protect(). */
typedef enum {
    RT_JIT_ARENA_ANON = 0,             /* anonymous RW mapping; caller flips W^X */
    RT_JIT_ARENA_MAP_JIT = 1,          /* MAP_JIT region; write window required */
    /* MAP_JIT was attempted and REFUSED by the kernel, and the arena was obtained through
     * the platform's supported single-view W^X mechanism instead (the mechanism this port
     * uses wherever MAP_JIT is absent). The refusal is never hidden: the errno is handed
     * back separately, and jit.map_jit_probe still reports the capability as BLOCKED. On iOS
     * this is the only mechanism a third-party app can have (physical run #2: MAP_JIT
     * refused errno=1, while memory.rw_to_rx_transition and the memory W^X policy pass), so
     * without it the port would have no JIT path at all — but a fallback that could not be
     * written, flipped and executed would still be reported as a failed chain. */
    RT_JIT_ARENA_ANON_MAP_JIT_REFUSED = 2
} rt_jit_arena_kind_t;

/* Executable arena.
 *
 * map_jit_attempted_out reports whether the MAP_JIT path was TAKEN — it is 1 even when
 * that mmap() was refused, because that is precisely the fact a caller needs to explain a
 * failure: on iOS a refusal here is the same missing capability the MAP_JIT probe measures,
 * not a second, independent defect (physical run #1 classified it as one because the
 * information was not available). It is 0 when the allocation did not use MAP_JIT at all.
 *
 * arena_kind_out receives the mechanism that produced the arena (RT_JIT_ARENA_*), and
 * map_jit_refused_errno_out receives the errno of a refused MAP_JIT attempt (0 when no
 * attempt was refused). Both are optional.
 *
 * On failure (NULL return) *err_out is the errno of the failing mmap(2), captured
 * immediately; when MAP_JIT was refused and the fallback also failed, err_out carries the
 * errno of the *fallback* failure and map_jit_refused_errno_out carries the first refusal,
 * so no refusal is ever silently overwritten. */
void *rt_jit_alloc_ex(size_t len, int *err_out, rt_jit_arena_kind_t *arena_kind_out,
                      int *map_jit_attempted_out, int *map_jit_refused_errno_out);
void *rt_jit_alloc(size_t len, int *err_out, int *map_jit_attempted_out);
int   rt_jit_free(void *addr, size_t len);
int   rt_jit_begin_write(void *addr, size_t len, int *err_out);
int   rt_jit_end_write(void *addr, size_t len, int *err_out);

/* Probes (each returns 0 on success and sets the out parameters; -1 with *err_out
 * when the platform refuses). These answer "can this process do it", not "is the
 * platform capable in general". */
int rt_jit_probe_map_jit(void **addr_out, int *err_out);
int rt_jit_probe_write_protect_np(void);

/* True when this process may execute memory it wrote. UNKNOWN is not "true":
 * callers get 1 (yes), 0 (no) or -1 (not determined). */
int rt_jit_execution_allowed(void);

#ifdef __cplusplus
}
#endif

#endif /* RUNTIME_JIT_H */

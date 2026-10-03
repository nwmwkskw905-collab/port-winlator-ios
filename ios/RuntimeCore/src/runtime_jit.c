/*
 * runtime_jit.c — payload emission, executable arena, icache maintenance, probing.
 *
 * PHASE_02_RECONSTRUCTED_POC (see runtime_platform.h).
 */
#include "runtime_jit.h"
#include "runtime_memory.h"
#include "runtime_platform.h"
#include "runtime_signals.h"

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#if defined(__APPLE__) && defined(__aarch64__)
#include <sys/mman.h>
#endif

const char *rt_jit_isa(void)
{
#if defined(__x86_64__)
    return "x86-64";
#elif defined(__i386__)
    return "i386";
#elif defined(__aarch64__)
    return "aarch64";
#elif defined(__arm__)
    return "arm32";
#else
    return "unknown";
#endif
}

int rt_jit_emit_return_imm(uint8_t *buf, size_t cap, uint32_t imm, size_t *len_out)
{
#if defined(__x86_64__) || defined(__i386__)
    if (buf == NULL || cap < 6u) {
        return -1;
    }
    buf[0] = 0xB8u;                 /* mov eax, imm32 */
    memcpy(buf + 1, &imm, 4u);
    buf[5] = 0xC3u;                 /* ret */
    if (len_out != NULL) {
        *len_out = 6u;
    }
    return 0;
#elif defined(__aarch64__)
    uint32_t insn;
    size_t offset = 0u;

    if (buf == NULL || cap < 8u) {
        return -1;
    }
    if (imm <= 0xFFFFu) {
        insn = 0x52800000u | (imm << 5);             /* MOVZ w0, #imm16 */
        memcpy(buf + offset, &insn, 4u);
        offset += 4u;
    } else {
        uint32_t lo = imm & 0xFFFFu;
        uint32_t hi = (imm >> 16) & 0xFFFFu;
        insn = 0x52800000u | (lo << 5);              /* MOVZ w0, #lo */
        memcpy(buf + offset, &insn, 4u);
        offset += 4u;
        insn = 0x72A00000u | (hi << 5);              /* MOVK w0, #hi, LSL #16 */
        memcpy(buf + offset, &insn, 4u);
        offset += 4u;
    }
    insn = 0xD65F03C0u;                              /* RET */
    memcpy(buf + offset, &insn, 4u);
    offset += 4u;
    if (len_out != NULL) {
        *len_out = offset;
    }
    return 0;
#else
    (void)buf;
    (void)cap;
    (void)imm;
    (void)len_out;
    return -2; /* ISA not implemented: callers must report UNSUPPORTED */
#endif
}

int rt_jit_invalidate(void *addr, size_t len)
{
    const rt_platform_t *platform = rt_platform_current();
    if (addr == NULL || len == 0u) {
        return -1;
    }
    if (platform->icache_flush == NULL) {
        return -1;
    }
    return platform->icache_flush(addr, len);
}

uint32_t rt_jit_call_u32(const void *code)
{
    rt_jit_fn_t fn;
    void *target = NULL;

    /* Copy the address into a function pointer: casting object pointers to function
     * pointers is not portable C, memcpy is. */
    memcpy(&target, &code, sizeof(target));
    memcpy(&fn, &target, sizeof(fn));
    return fn();
}

void *rt_jit_alloc_ex(size_t len, int *err_out, rt_jit_arena_kind_t *arena_kind_out,
                      int *map_jit_attempted_out, int *map_jit_refused_errno_out)
{
    if (err_out != NULL) {
        *err_out = 0;
    }
    if (arena_kind_out != NULL) {
        *arena_kind_out = RT_JIT_ARENA_ANON;
    }
    if (map_jit_attempted_out != NULL) {
        *map_jit_attempted_out = 0;
    }
    if (map_jit_refused_errno_out != NULL) {
        *map_jit_refused_errno_out = 0;
    }
#if defined(__APPLE__) && defined(__aarch64__) && defined(MAP_JIT)
    {
        size_t page = (size_t)rt_platform_page_size();
        size_t rounded = ((len + page - 1u) / page) * page;
        void *addr;
        int refused = 0;
        /* The MAP_JIT path is taken (and reported as taken) before the call is made, so a
         * refusal can be attributed to it instead of looking like a generic failure. */
        if (map_jit_attempted_out != NULL) {
            *map_jit_attempted_out = 1;
        }
        addr = mmap(NULL, rounded, PROT_READ | PROT_WRITE | PROT_EXEC,
                    MAP_PRIVATE | MAP_ANONYMOUS | MAP_JIT, -1, 0);
        if (addr != MAP_FAILED) {
            if (arena_kind_out != NULL) {
                *arena_kind_out = RT_JIT_ARENA_MAP_JIT;
            }
            return addr;
        }
        refused = errno;                /* captured immediately, never overwritten */
        if (map_jit_refused_errno_out != NULL) {
            *map_jit_refused_errno_out = refused;
        }
        /* MAP_JIT was refused. Falling back is not "using a plain mmap to green a test": the
         * arena handed back here has exactly the semantics the port requires — writable now,
         * never writable and executable at the same time, flipped to R-X by the caller after
         * the instruction-cache flush, and every step verified by real execution. It is the
         * same mechanism the port already uses on every platform without MAP_JIT, and on iOS
         * it is the ONLY mechanism a third-party app can have (the entitlement that gates
         * MAP_JIT is not obtainable; physical run #2 measured the refusal). The refusal stays
         * visible through *map_jit_refused_errno_out and through jit.map_jit_probe, which
         * keeps reporting the capability as BLOCKED. A caller that needs MAP_JIT specifically
         * must not use this function. */
        addr = rt_mem_reserve(len, err_out);
        if (addr == NULL) {
            return NULL;               /* err_out: the fallback's own errno */
        }
        if (arena_kind_out != NULL) {
            *arena_kind_out = RT_JIT_ARENA_ANON_MAP_JIT_REFUSED;
        }
        return addr;
    }
#else
    /* No MAP_JIT on this platform/target: a plain RW mapping that the caller must flip with
     * rt_mem_protect(). This is deliberately not presented as a JIT arena, and
     * map_jit_attempted_out stays 0 — a failure of this path is not the MAP_JIT
     * capability. */
    {
        void *addr = rt_mem_reserve(len, err_out);
        if (addr == NULL) {
            return NULL;
        }
        if (arena_kind_out != NULL) {
            *arena_kind_out = RT_JIT_ARENA_ANON;
        }
        return addr;
    }
#endif
}

void *rt_jit_alloc(size_t len, int *err_out, int *map_jit_attempted_out)
{
    /* Compatibility wrapper: callers that do not act on the arena mechanism (or that only
     * need the MAP_JIT-attempt fact) keep their old signature. */
    return rt_jit_alloc_ex(len, err_out, NULL, map_jit_attempted_out, NULL);
}

int rt_jit_free(void *addr, size_t len)
{
    return rt_mem_release(addr, len, NULL);
}

int rt_jit_begin_write(void *addr, size_t len, int *err_out)
{
    const rt_platform_t *platform = rt_platform_current();
    if (platform->jit_write_protect_set == NULL) {
        if (err_out != NULL) {
            *err_out = ENOTSUP;
        }
        return -1;
    }
    /* enable == 0 opens the write window on Apple (pthread_jit_write_protect_np(0)). */
    return platform->jit_write_protect_set(addr, len, 0, err_out);
}

int rt_jit_end_write(void *addr, size_t len, int *err_out)
{
    const rt_platform_t *platform = rt_platform_current();
    if (platform->jit_write_protect_set == NULL) {
        if (err_out != NULL) {
            *err_out = ENOTSUP;
        }
        return -1;
    }
    /* enable == 1 closes the window and makes the region executable again. */
    return platform->jit_write_protect_set(addr, len, 1, err_out);
}

int rt_jit_probe_map_jit(void **addr_out, int *err_out)
{
    if (addr_out != NULL) {
        *addr_out = NULL;
    }
    if (err_out != NULL) {
        *err_out = 0;
    }
#if defined(__APPLE__) && defined(__aarch64__) && defined(MAP_JIT)
    {
        void *addr = mmap(NULL, (size_t)rt_platform_page_size(),
                          PROT_READ | PROT_WRITE | PROT_EXEC,
                          MAP_PRIVATE | MAP_ANONYMOUS | MAP_JIT, -1, 0);
        if (addr == MAP_FAILED) {
            if (err_out != NULL) {
                *err_out = errno;
            }
            return -1;
        }
        if (munmap(addr, (size_t)rt_platform_page_size()) != 0 && err_out != NULL) {
            *err_out = errno;
        }
        if (addr_out != NULL) {
            *addr_out = NULL; /* the probe does not hand out a mapping */
        }
        return 0;
    }
#else
    if (err_out != NULL) {
        *err_out = ENOTSUP; /* no MAP_JIT on this platform */
    }
    return -1;
#endif
}

int rt_jit_probe_write_protect_np(void)
{
    const rt_platform_t *platform = rt_platform_current();
    if (platform->jit_write_protect_set == NULL) {
        return -1;
    }
    /* Without a mapping there is nothing to toggle, so the meaningful answer is the
     * capability bit of the current platform plus the presence of the hook.
     *
     * The bit itself is target-aware (runtime_platform.h): macOS advertises it, iOS
     * never does, because the iPhoneOS SDK marks pthread_jit_write_protect_np
     * unavailable. A -1 here therefore means "not available to this platform/target"
     * and says nothing about the port — the JIT suite classifies it as UNSUPPORTED or
     * NOT_APPLICABLE, never as PASS and never as FAIL. */
    if ((rt_platform_capabilities() & RT_CAP_JIT_WP_NP) == 0u) {
        return -1;
    }
    return 0;
}

int rt_jit_execution_allowed(void)
{
    /* Answerable only by trying: emit a payload, make it executable, call it under
     * the fault guard. Returns 1 (yes), 0 (no) or -1 (not determined here).
     *
     * Every step is checked, and the two arena mechanisms are driven the way each one
     * requires — this is the defect physical run #2 exposed: the MAP_JIT branch opened the
     * write window correctly, but the executable step then called rt_mem_protect(R-X) on the
     * MAP_JIT region *and* the anonymous branch relied on rt_mem_protect alone. On a MAP_JIT
     * region the page protections are set at creation (PROT_MAX-style) and mprotect() is the
     * wrong tool: the supported operation is closing the write window, which is what
     * rt_jit_end_write() does. A failure of that step must not be reported as "this process
     * may not execute memory it wrote" when the write was never actually made executable.
     * Failure of any step below is "not determined" (-1), never a false verdict. */
    uint8_t payload[16];
    size_t len = 0u;
    void *arena = NULL;
    void *target = NULL;
    void *fault = NULL;
    uint32_t value = 0u;
    int err = 0;
    int map_jit_attempted = 0;
    rt_jit_arena_kind_t kind = RT_JIT_ARENA_ANON;
    int result;
    rt_jit_fn_t fn;

    if (rt_jit_emit_return_imm(payload, sizeof(payload), 1u, &len) != 0) {
        return -1;
    }
    arena = rt_jit_alloc_ex(len, &err, &kind, &map_jit_attempted, NULL);
    (void)map_jit_attempted;
    if (arena == NULL) {
        return -1;
    }
    if (kind == RT_JIT_ARENA_MAP_JIT) {
        /* MAP_JIT region: the payload only exists inside the write window. */
        if (rt_jit_begin_write(arena, len, &err) != 0) {
            (void)rt_jit_free(arena, len);
            return -1; /* no write window: nothing was written, nothing was measured */
        }
        memcpy(arena, payload, len);
        if (rt_jit_end_write(arena, len, &err) != 0) {
            (void)rt_jit_free(arena, len);
            return -1; /* the window never closed: the payload is not executable yet */
        }
        /* No mprotect here: on a MAP_JIT region the protections were fixed at creation and
         * closing the write window is the supported transition. */
    } else {
        memcpy(arena, payload, len);
        if (rt_mem_protect(arena, len, RT_PROT_READ | RT_PROT_EXEC, &err) != 0) {
            (void)rt_jit_free(arena, len);
            return 0; /* writable but not executable: this process may not execute it */
        }
    }
    if (rt_jit_invalidate(arena, len) != 0 && (rt_platform_capabilities() & RT_CAP_ICACHE_FLUSH) != 0u) {
        /* The platform can flush and the flush failed: executing would be untrustworthy. */
        (void)rt_jit_free(arena, len);
        return -1;
    }

    target = arena;
    memcpy(&fn, &target, sizeof(fn));
    result = rt_signal_call_guarded(fn, &value, &fault, &err);
    (void)rt_jit_free(arena, len);

    if (result == -2) {
        return -1; /* the guard could not be installed: nothing was executed */
    }
    if (result != 0) {
        return 0; /* faulted: no permission, or the payload was refused */
    }
    return (value == 1u) ? 1 : -1;
}

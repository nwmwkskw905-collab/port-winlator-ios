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

void *rt_jit_alloc(size_t len, int *err_out, int *map_jit_attempted_out)
{
    if (map_jit_attempted_out != NULL) {
        *map_jit_attempted_out = 0;
    }
#if defined(__APPLE__) && defined(__aarch64__) && defined(MAP_JIT)
    {
        size_t page = (size_t)rt_platform_page_size();
        size_t rounded = ((len + page - 1u) / page) * page;
        void *addr;
        /* The MAP_JIT path is taken (and reported as taken) before the call is made, so a
         * refusal can be attributed to it instead of looking like a generic failure. */
        if (map_jit_attempted_out != NULL) {
            *map_jit_attempted_out = 1;
        }
        addr = mmap(NULL, rounded, PROT_READ | PROT_WRITE | PROT_EXEC,
                    MAP_PRIVATE | MAP_ANONYMOUS | MAP_JIT, -1, 0);
        if (addr == MAP_FAILED) {
            if (err_out != NULL) {
                *err_out = errno;       /* captured immediately */
            }
            return NULL;
        }
        if (err_out != NULL) {
            *err_out = 0;
        }
        return addr;
    }
#else
    /* No MAP_JIT here: a plain RW mapping that the caller must flip with
     * rt_mem_protect(). This is deliberately not presented as a JIT arena, and
     * map_jit_attempted_out stays 0 — a failure of this path is not the MAP_JIT
     * capability. */
    return rt_mem_reserve(len, err_out);
#endif
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
     * Every step is checked, and the MAP_JIT path is driven the way the platform requires:
     * when the arena came from a MAP_JIT mapping, the payload is written inside the
     * write-protect window (begin/end write) instead of straight into a possibly
     * write-protected region, and the instruction-cache flush is honoured. Failure of any
     * of those is "not determined" (-1), never a false "this process may not execute
     * memory it wrote". */
    uint8_t payload[16];
    size_t len = 0u;
    void *arena = NULL;
    void *target = NULL;
    void *fault = NULL;
    uint32_t value = 0u;
    int err = 0;
    int used_map_jit = 0;
    int result;
    rt_jit_fn_t fn;

    if (rt_jit_emit_return_imm(payload, sizeof(payload), 1u, &len) != 0) {
        return -1;
    }
    arena = rt_jit_alloc(len, &err, &used_map_jit);
    if (arena == NULL) {
        return -1;
    }
    if (used_map_jit != 0 && rt_jit_begin_write(arena, len, &err) != 0) {
        (void)rt_jit_free(arena, len);
        return -1; /* no write window: nothing was written, nothing was measured */
    }
    memcpy(arena, payload, len);
    if (used_map_jit != 0 && rt_jit_end_write(arena, len, &err) != 0) {
        (void)rt_jit_free(arena, len);
        return -1;
    }
    if (rt_jit_invalidate(arena, len) != 0 && (rt_platform_capabilities() & RT_CAP_ICACHE_FLUSH) != 0u) {
        /* The platform can flush and the flush failed: executing would be untrustworthy. */
        (void)rt_jit_free(arena, len);
        return -1;
    }

    if (rt_mem_protect(arena, len, RT_PROT_READ | RT_PROT_EXEC, &err) != 0) {
        (void)rt_jit_free(arena, len);
        return 0; /* writable but not executable: this process may not execute it */
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

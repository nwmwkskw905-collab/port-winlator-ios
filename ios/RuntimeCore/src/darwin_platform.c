/*
 * darwin_platform.c — Darwin/iOS backend.
 *
 * PHASE_02_RECONSTRUCTED_POC (see runtime_platform.h).
 *
 * This file compiles on every host (so the harness can report "what Darwin would
 * do"), but it only *acts* on Apple platforms. On a non-Apple host every entry
 * point fails with ENOTSUP and the capability mask is zero: the backend is not
 * allowed to pretend that a Linux box is Darwin.
 *
 * On Apple platforms the real behaviour is:
 *   - MAP_JIT is requested for executable mappings on arm64 (required by the
 *     hardened runtime / AMFI before a write+execute mapping is granted);
 *   - pthread_jit_write_protect_np() opens and closes the write window instead of
 *     flipping page protections, which is the supported W^X pattern on Apple
 *     Silicon;
 *   - sys_icache_invalidate() synchronises the instruction cache.
 *
 * Whether the *process* may use any of it (entitlement, sandbox, code signing)
 * is a runtime question: the probes in runtime_jit.c answer that, and the report
 * keeps capability separate from permission.
 */
#include "runtime_platform.h"

#include <errno.h>
#include <stddef.h>
#include <stdint.h>

#if defined(__APPLE__)
#include <pthread.h>
#include <sys/mman.h>
#include <unistd.h>
#if __has_include(<libkern/OSCacheControl.h>)
#include <libkern/OSCacheControl.h>
#define RT_HAVE_OSCACHECONTROL 1
#endif
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

#if defined(__APPLE__)
static int rt_darwin_prot_to_native(rt_prot_t prot)
{
    int native = PROT_NONE;
    if ((prot & RT_PROT_READ) != 0)  native |= PROT_READ;
    if ((prot & RT_PROT_WRITE) != 0) native |= PROT_WRITE;
    if ((prot & RT_PROT_EXEC) != 0)  native |= PROT_EXEC;
    return native;
}
#endif

static int darwin_page_size(void)
{
    long page = sysconf(_SC_PAGESIZE);
    if (page <= 0) {
        return 4096;
    }
    return (int)page;
}

static void *darwin_mem_map(size_t len, rt_prot_t prot, int *err_out)
{
#if defined(__APPLE__)
    int native = rt_darwin_prot_to_native(prot);
    int flags = MAP_PRIVATE | MAP_ANONYMOUS;
#if defined(__aarch64__) && defined(MAP_JIT)
    if ((prot & RT_PROT_EXEC) != 0) {
        flags |= MAP_JIT;
    }
#endif
    void *addr = mmap(NULL, len, native, flags, -1, 0);
    if (addr == MAP_FAILED) {
        if (err_out != NULL) {
            *err_out = errno;
        }
        return NULL;
    }
    if (err_out != NULL) {
        *err_out = 0;
    }
    return addr;
#else
    (void)len;
    (void)prot;
    if (err_out != NULL) {
        *err_out = ENOTSUP;
    }
    return NULL;
#endif
}

static int darwin_mem_protect(void *addr, size_t len, rt_prot_t prot, int *err_out)
{
#if defined(__APPLE__)
    if (mprotect(addr, len, rt_darwin_prot_to_native(prot)) != 0) {
        if (err_out != NULL) {
            *err_out = errno;
        }
        return -1;
    }
    if (err_out != NULL) {
        *err_out = 0;
    }
    return 0;
#else
    (void)addr;
    (void)len;
    (void)prot;
    if (err_out != NULL) {
        *err_out = ENOTSUP;
    }
    return -1;
#endif
}

static int darwin_mem_unmap(void *addr, size_t len, int *err_out)
{
#if defined(__APPLE__)
    if (munmap(addr, len) != 0) {
        if (err_out != NULL) {
            *err_out = errno;
        }
        return -1;
    }
    if (err_out != NULL) {
        *err_out = 0;
    }
    return 0;
#else
    (void)addr;
    (void)len;
    if (err_out != NULL) {
        *err_out = ENOTSUP;
    }
    return -1;
#endif
}

static uint32_t darwin_capabilities(void)
{
#if defined(__APPLE__)
    uint32_t caps = RT_CAP_MMAP_ANON | RT_CAP_ICACHE_FLUSH | RT_CAP_KQUEUE |
                    RT_CAP_POSIX_SHM | RT_CAP_SCM_RIGHTS;
#if defined(MAP_JIT)
    caps |= RT_CAP_MAP_JIT;
#endif
#if defined(__aarch64__)
    caps |= RT_CAP_JIT_WP_NP;
#endif
    return caps;
#else
    /* Not an Apple host: claim nothing. */
    return 0u;
#endif
}

static int darwin_jit_write_protect_set(void *addr, size_t len, int enable, int *err_out)
{
    (void)addr;
    (void)len;
#if defined(__APPLE__) && defined(__aarch64__)
    pthread_jit_write_protect_np(enable != 0 ? 1 : 0);
    if (err_out != NULL) {
        *err_out = 0;
    }
    return 0;
#else
    (void)enable;
    if (err_out != NULL) {
        *err_out = ENOTSUP;
    }
    return -1;
#endif
}

static int darwin_icache_flush(void *addr, size_t len)
{
#if defined(__APPLE__) && defined(RT_HAVE_OSCACHECONTROL)
    sys_icache_invalidate(addr, len);
    return 0;
#elif defined(__APPLE__)
    __builtin___clear_cache((char *)addr, (char *)addr + len);
    return 0;
#else
    (void)addr;
    (void)len;
    return -1;
#endif
}

const rt_platform_t rt_platform_darwin = {
    "darwin",
    darwin_page_size,
    darwin_mem_map,
    darwin_mem_protect,
    darwin_mem_unmap,
    darwin_capabilities,
    darwin_jit_write_protect_set,
    darwin_icache_flush
};

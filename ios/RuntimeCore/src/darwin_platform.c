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
 *
 * macOS and iOS are NOT the same target (see runtime_platform.h). Concretely:
 *   - pthread_jit_write_protect_np exists on macOS and is marked unavailable by the
 *     iPhoneOS SDK, so the call below is compiled only for macOS;
 *   - MAP_JIT is defined by both SDKs but is gated at runtime by entitlements, so it
 *     stays a runtime probe on both;
 *   - sys_icache_invalidate (libkern/OSCacheControl.h) is available on both.
 */
#include "runtime_platform.h"

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>    /* snprintf in rt_platform_unique_shm_name (all hosts) */

#if defined(__APPLE__)
#include <pthread.h>
#include <stdlib.h>   /* arc4random_buf(3) */
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
#if defined(__aarch64__) && RT_APPLE_HAS_JIT_WRITE_PROTECT
    /* macOS only. On iOS the API is unavailable to the target, so advertising the
     * capability would be a false claim — and the harness reads exactly this bit. */
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
#if defined(__APPLE__) && defined(__aarch64__) && RT_APPLE_HAS_JIT_WRITE_PROTECT
    /* macOS (arm64): the supported W^X pattern on Apple Silicon — open and close the
     * write window instead of flipping page protections. */
    pthread_jit_write_protect_np(enable != 0 ? 1 : 0);
    if (err_out != NULL) {
        *err_out = 0;
    }
    return 0;
#else
    /* iOS (device or simulator): the iPhoneOS SDK marks pthread_jit_write_protect_np
     * as unavailable, so this target cannot legally call it and this code is not even
     * compiled here — the call is not "skipped", it is absent. ENOTSUP is the honest
     * answer: the platform cannot perform the operation. A version check would be
     * meaningless, because the restriction is a property of the API for this target,
     * not of the OS version running on the device. Callers must treat -1/ENOTSUP as
     * "not available here" (the JIT suite reports NOT_APPLICABLE and never PASS), and
     * W^X on iOS has to use a single view flipped between RW and R-X. */
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

static int darwin_random_bytes(void *buffer, size_t length, int *err_out)
{
#if defined(__APPLE__)
    if (buffer == NULL && length != 0u) {
        if (err_out != NULL) {
            *err_out = EINVAL;
        }
        return -1;
    }
    /* arc4random_buf(3): Apple's CSPRNG, available on macOS and iOS alike, needs no
     * entitlement, cannot fail, and lives in <stdlib.h>. The glibc/Linux route
     * (getrandom via <sys/random.h>) is deliberately NOT used here: that header is
     * absent from the iphoneos SDK — including it under __APPLE__ is what made the
     * first iphoneos build of runtime_dual_mapping.c fail. */
    arc4random_buf(buffer, length);
    if (err_out != NULL) {
        *err_out = 0;
    }
    return 0;
#else
    (void)buffer;
    (void)length;
    if (err_out != NULL) {
        *err_out = ENOTSUP;
    }
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
    darwin_icache_flush,
    darwin_random_bytes
};

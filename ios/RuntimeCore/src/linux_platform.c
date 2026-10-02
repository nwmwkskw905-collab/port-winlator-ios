/*
 * linux_platform.c — Linux backend.
 *
 * PHASE_02_RECONSTRUCTED_POC (see runtime_platform.h).
 *
 * Honest-capability rule: this backend reports MAP_JIT and
 * pthread_jit_write_protect_np as absent because Linux has no such API. There is no
 * emulation of either, and the write-protect hook returns -1/ENOTSUP so callers are
 * forced to handle its absence explicitly.
 */
#include "runtime_platform.h"

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/mman.h>
#include <unistd.h>

#if defined(__linux__)
#include <sys/auxv.h>
#endif

static int rt_prot_to_native(rt_prot_t prot)
{
    int native = PROT_NONE;
    if ((prot & RT_PROT_READ) != 0)  native |= PROT_READ;
    if ((prot & RT_PROT_WRITE) != 0) native |= PROT_WRITE;
    if ((prot & RT_PROT_EXEC) != 0)  native |= PROT_EXEC;
    return native;
}

static int linux_page_size(void)
{
    long page = sysconf(_SC_PAGESIZE);
    if (page <= 0) {
        return 4096;
    }
    return (int)page;
}

static void *linux_mem_map(size_t len, rt_prot_t prot, int *err_out)
{
    int native = rt_prot_to_native(prot);
    void *addr = mmap(NULL, len, native, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
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
}

static int linux_mem_protect(void *addr, size_t len, rt_prot_t prot, int *err_out)
{
    if (mprotect(addr, len, rt_prot_to_native(prot)) != 0) {
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

static int linux_mem_unmap(void *addr, size_t len, int *err_out)
{
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
}

static uint32_t linux_capabilities(void)
{
    uint32_t caps = RT_CAP_MMAP_ANON | RT_CAP_ICACHE_FLUSH | RT_CAP_EPOLL |
                    RT_CAP_POSIX_SHM | RT_CAP_SCM_RIGHTS;
    /* No MAP_JIT, no pthread_jit_write_protect_np: Linux has neither API. */
    return caps;
}

static int linux_jit_write_protect_set(void *addr, size_t len, int enable, int *err_out)
{
    (void)addr;
    (void)len;
    (void)enable;
    if (err_out != NULL) {
        *err_out = ENOTSUP;
    }
    return -1;
}

static int linux_icache_flush(void *addr, size_t len)
{
    /* GCC/Clang expand this to the architecture's cache maintenance sequence
     * (dc cvau / ic ivau on AArch64, nothing on x86 where icache is coherent). */
    __builtin___clear_cache((char *)addr, (char *)addr + len);
    return 0;
}

const rt_platform_t rt_platform_linux = {
    "linux",
    linux_page_size,
    linux_mem_map,
    linux_mem_protect,
    linux_mem_unmap,
    linux_capabilities,
    linux_jit_write_protect_set,
    linux_icache_flush
};

#if defined(__linux__) && defined(__aarch64__)
/* HWCAP is only meaningful on the AArch64 Linux build; runtime_cpu_abi.c asks for it
 * through this accessor so the platform file stays the only place that touches auxv. */
uint64_t rt_linux_arm64_hwcap(void)
{
    return (uint64_t)getauxval(AT_HWCAP);
}
#endif

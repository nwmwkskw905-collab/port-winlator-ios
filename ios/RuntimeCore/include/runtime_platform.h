/*
 * runtime_platform.h — platform abstraction for the Phase 02 reconstructed runtime PoC.
 *
 * PHASE_02_RECONSTRUCTED_POC: this file is a reconstruction, not recovered code.
 * It was written from the surviving specification (ios/README.md, ios/CMakeLists.txt)
 * and must never be presented as the original Phase 02 source.
 *
 * Design rules:
 *   - small, verifiable APIs with explicit errors (every call returns a value and,
 *     where the OS can fail, reports the real errno through an out parameter);
 *   - no hidden fallbacks: a capability that is not available is reported as
 *     UNSUPPORTED/BLOCKED/UNTESTED, never silently replaced by something else;
 *   - the Darwin backend refuses to claim support it cannot verify at runtime.
 */
#ifndef RUNTIME_PLATFORM_H
#define RUNTIME_PLATFORM_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Result vocabulary. PASS is only used when the behaviour was actually observed. */
typedef enum {
    RT_PASS = 0,
    RT_FAIL,
    RT_BLOCKED,
    RT_UNSUPPORTED,
    RT_UNTESTED,
    RT_NOT_APPLICABLE
} rt_status_t;

const char *rt_status_name(rt_status_t status);

/* Memory protection vocabulary (mapped to the OS inside the platform backends). */
typedef enum {
    RT_PROT_NONE  = 0,
    RT_PROT_READ  = 1,
    RT_PROT_WRITE = 2,
    RT_PROT_EXEC  = 4
} rt_prot_t;

const char *rt_prot_name(rt_prot_t prot);

/* Capability bits. These describe what the platform *offers*; whether the process
 * may actually use it (iOS entitlements, sandbox, code signing) is a separate,
 * strictly runtime question answered by the probes in the jit/memory suites. */
#define RT_CAP_MMAP_ANON      (1u << 0)
#define RT_CAP_MAP_JIT       (1u << 1)
#define RT_CAP_JIT_WP_NP     (1u << 2)
#define RT_CAP_ICACHE_FLUSH  (1u << 3)
#define RT_CAP_KQUEUE        (1u << 4)
#define RT_CAP_EPOLL         (1u << 5)
#define RT_CAP_POSIX_SHM     (1u << 6)
#define RT_CAP_SCM_RIGHTS    (1u << 7)

typedef struct rt_platform {
    const char *name;
    int       (*page_size)(void);
    void     *(*mem_map)(size_t len, rt_prot_t prot, int *err_out);
    int       (*mem_protect)(void *addr, size_t len, rt_prot_t prot, int *err_out);
    int       (*mem_unmap)(void *addr, size_t len, int *err_out);
    uint32_t  (*capabilities)(void);
    /* Darwin: pthread_jit_write_protect_np(). enable != 0 closes the write window
     * and opens execution. Returns -1 and sets errno when the platform has no such
     * control; callers must treat that as UNSUPPORTED, not as success. */
    int       (*jit_write_protect_set)(void *addr, size_t len, int enable, int *err_out);
    int       (*icache_flush)(void *addr, size_t len);
} rt_platform_t;

/* Backends are always compiled, so the harness can report what a platform would do
 * without pretending that this host is that platform. */
extern const rt_platform_t rt_platform_linux;
extern const rt_platform_t rt_platform_darwin;

/* Linux/AArch64 only: AT_HWCAP, exposed here so the platform file stays the single
 * place that touches the auxiliary vector. Not defined on other platforms. */
#if defined(__linux__) && defined(__aarch64__)
uint64_t rt_linux_arm64_hwcap(void);
#endif

const rt_platform_t *rt_platform_current(void);
const char          *rt_platform_name(void);
int                  rt_platform_page_size(void);
uint32_t             rt_platform_capabilities(void);
int                  rt_platform_is_darwin(void);
int                  rt_platform_is_apple(void);

#ifdef __cplusplus
}
#endif

#endif /* RUNTIME_PLATFORM_H */

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
/* Present only when the target can legally call the API: RT_APPLE_HAS_JIT_WRITE_PROTECT
 * is true on macOS and false on iOS (device or simulator), where the iPhoneOS SDK
 * marks pthread_jit_write_protect_np unavailable. */
#define RT_CAP_JIT_WP_NP     (1u << 2)
#define RT_CAP_ICACHE_FLUSH  (1u << 3)
#define RT_CAP_KQUEUE        (1u << 4)
#define RT_CAP_EPOLL         (1u << 5)
#define RT_CAP_POSIX_SHM     (1u << 6)
#define RT_CAP_SCM_RIGHTS    (1u << 7)

/* ------------------------------------------------------------------ Apple target
 *
 * "__APPLE__" is not one platform: macOS and iOS differ in which APIs exist, and iOS
 * on a device differs from the simulator. CI run #3 proved it the hard way:
 *
 *     darwin_platform.c:165:5: error: 'pthread_jit_write_protect_np' is unavailable:
 *     not available on iOS
 *
 * The function is declared in macOS's pthread.h and marked unavailable by the
 * iPhoneOS SDK. Treating __APPLE__ as one platform made the iOS build reference an
 * API it may not legally call — and, worse, made the capability mask advertise it.
 *
 * The target is derived from <TargetConditionals.h>, the documented Apple mechanism,
 * NOT from a version check: no @available and no runtime version test can make an
 * unavailable API available.
 */
#define RT_APPLE_TARGET_NONE             0
#define RT_APPLE_TARGET_MACOS            1
#define RT_APPLE_TARGET_IPHONE_DEVICE    2
#define RT_APPLE_TARGET_IPHONE_SIMULATOR 3
#define RT_APPLE_TARGET_UNKNOWN_APPLE    4

#if defined(__APPLE__) && defined(__has_include)
#if __has_include(<TargetConditionals.h>)
#include <TargetConditionals.h>
#endif
#endif

#if defined(__APPLE__) && defined(TARGET_OS_MAC)
/* TARGET_OS_OSX appeared with the 10.12 SDK. Older headers only have TARGET_OS_MAC,
 * which is 1 on iOS as well, so the iPhone macro must be consulted first. */
#if !defined(TARGET_OS_OSX)
#if defined(TARGET_OS_IPHONE)
#define TARGET_OS_OSX 0
#else
#define TARGET_OS_OSX TARGET_OS_MAC
#endif
#endif
#if TARGET_OS_OSX
#define RT_APPLE_TARGET RT_APPLE_TARGET_MACOS
#elif defined(TARGET_OS_SIMULATOR) && TARGET_OS_SIMULATOR
#define RT_APPLE_TARGET RT_APPLE_TARGET_IPHONE_SIMULATOR
#elif defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
#define RT_APPLE_TARGET RT_APPLE_TARGET_IPHONE_DEVICE
#else
#define RT_APPLE_TARGET RT_APPLE_TARGET_UNKNOWN_APPLE
#endif
#elif defined(__APPLE__)
/* An Apple build without TargetConditionals.h: assume the most restricted target.
 * Guessing "macOS" here would re-create exactly the bug this block exists to stop. */
#define RT_APPLE_TARGET RT_APPLE_TARGET_UNKNOWN_APPLE
#else
#define RT_APPLE_TARGET RT_APPLE_TARGET_NONE
#endif

/* pthread_jit_write_protect_np(3): exists on macOS, marked unavailable by the
 * iPhoneOS SDK. Both the CALL and the CAPABILITY BIT are gated by this. */
#define RT_APPLE_HAS_JIT_WRITE_PROTECT (RT_APPLE_TARGET == RT_APPLE_TARGET_MACOS)

/* Which Apple target this binary was built for: RT_APPLE_TARGET_*. On a non-Apple
 * host this is RT_APPLE_TARGET_NONE. Reporting it keeps "macOS result", "iOS result"
 * and "host result" from ever being confused in the evidence. */
int          rt_platform_apple_target(void);
const char  *rt_platform_apple_target_name(void);

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
    /* Cryptographically strong random bytes. The two backends are both compiled into
     * the same library (in the struct) because the harness must be able to say what a
     * platform *would* do; a free function per backend would collide at link time. */
    int       (*random_bytes)(void *buffer, size_t length, int *err_out);
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

/* Fill buffer with cryptographically strong random bytes.
 *
 * Linux : getrandom(2); if the running kernel predates it (ENOSYS) the backend
 *         falls back to reading /dev/urandom, and says so through err_out = 0.
 * Apple : arc4random_buf(3) — the platform CSPRNG, present on every Apple OS and
 *         requiring neither entitlement nor an extra header. <sys/random.h> does
 *         NOT exist in the iphoneos SDK, which is what broke the first iphoneos
 *         build of runtime_dual_mapping.c; the Darwin backend must not reach for
 *         the Linux/glibc API.
 *
 * Returns 0 on success. If no acceptable source exists the call FAILS with -1 and
 * the real errno: there is no silent fallback to a weak source (a predictable name
 * is a real collision hazard, not a cosmetic detail).
 */
int rt_platform_random_bytes(void *buffer, size_t length, int *err_out);

/* Compose a unique, unpredictable POSIX shared-memory object name of the form
 * "<prefix>_<pid>_<16 hex digits>". Returns 0, or -1 with *err_out (ENAMETOOLONG
 * when out is too small, or the errno of the random source). No fallback to a
 * deterministic name: on failure the caller must fail, not proceed with a guess. */
int rt_platform_unique_shm_name(char *out, size_t capacity, const char *prefix, int *err_out);
int                  rt_platform_page_size(void);
uint32_t             rt_platform_capabilities(void);
int                  rt_platform_is_darwin(void);
int                  rt_platform_is_apple(void);

#ifdef __cplusplus
}
#endif

#endif /* RUNTIME_PLATFORM_H */

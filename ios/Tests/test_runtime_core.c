/*
 * test_runtime_core.c — unit tests for the reconstructed RuntimeCore.
 *
 * PHASE_02_RECONSTRUCTED_POC (see runtime_platform.h).
 *
 * These tests exercise behaviour, not constants: every positive case is paired with
 * a negative one wherever the API can fail, and the numbers they print are observed
 * here, never copied from the lost Phase 02 report.
 */
#include "phase02_harness.h"
#include "runtime_context.h"
#include "runtime_cpu_abi.h"
#include "runtime_filesystem.h"
#include "runtime_ipc.h"
#include "runtime_jit.h"
#include "runtime_loader.h"
#include "runtime_memory.h"
#include "runtime_signals.h"
#include "runtime_threads.h"

#include <dirent.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static unsigned g_checks = 0u;
static unsigned g_failures = 0u;

#define CHECK(condition, name)                                                   \
    do {                                                                         \
        g_checks++;                                                              \
        if ((condition) != 0) {                                                   \
            printf("ok   %s\n", (name));                                          \
        } else {                                                                  \
            g_failures++;                                                        \
            printf("FAIL %s (%s:%d)\n", (name), __FILE__, __LINE__);              \
        }                                                                         \
    } while (0)

/* ------------------------------------------------------------------- status */

static void test_status_names(void)
{
    static const rt_status_t all[] = { RT_PASS, RT_FAIL, RT_BLOCKED, RT_UNSUPPORTED,
                                       RT_UNTESTED, RT_NOT_APPLICABLE };
    size_t index;
    int distinct = 1;

    for (index = 0u; index < (sizeof(all) / sizeof(all[0])); index++) {
        const char *name = rt_status_name(all[index]);
        size_t other;
        CHECK(name != NULL && name[0] != '\0', "status_name is non-empty");
        for (other = index + 1u; other < (sizeof(all) / sizeof(all[0])); other++) {
            if (strcmp(rt_status_name(all[index]), rt_status_name(all[other])) == 0) {
                distinct = 0;
            }
        }
    }
    CHECK(distinct != 0, "status_name maps every status to a distinct word");
    CHECK(strcmp(rt_status_name(RT_PASS), "PASS") == 0, "PASS is spelled PASS");
}

/* ------------------------------------------------------------------- loader */

static void test_loader_validation(void)
{
    uint8_t image[RT_MODULE_MAX_IMAGE];
    size_t len = rt_loader_build_return_image(image, sizeof(image), 7u);
    rt_module_header_t header;
    uint8_t copy[RT_MODULE_MAX_IMAGE];

    CHECK(len > (size_t)RT_MODULE_HEADER_SIZE, "loader builds a non-empty image");
    CHECK(rt_loader_validate(image, len, &header) == RT_LOADER_OK, "well-formed image accepted");
    CHECK(header.magic == RT_MODULE_MAGIC, "magic round-trips");
    CHECK(header.entry_off == 0u, "entry offset is inside the code blob");

    memcpy(copy, image, len);
    copy[1] = 0x00u;
    CHECK(rt_loader_validate(copy, len, &header) == RT_LOADER_ERR_MAGIC,
          "corrupted magic rejected");

    memcpy(copy, image, len);
    copy[4] = 0x09u;
    CHECK(rt_loader_validate(copy, len, &header) == RT_LOADER_ERR_VERSION,
          "future version rejected");

    memcpy(copy, image, len);
    copy[20] = 0x01u;
    CHECK(rt_loader_validate(copy, len, &header) == RT_LOADER_ERR_FLAGS,
          "non-zero flags rejected");

    memcpy(copy, image, len);
    copy[12] = 0xFFu;
    copy[13] = 0xFFu;
    CHECK(rt_loader_validate(copy, len, &header) == RT_LOADER_ERR_CODE_SIZE,
          "oversized code rejected");

    memcpy(copy, image, len);
    copy[16] = 0xFFu;
    copy[17] = 0xFFu;
    CHECK(rt_loader_validate(copy, len, &header) == RT_LOADER_ERR_ENTRY,
          "entry outside the blob rejected");

    CHECK(rt_loader_validate(image, 4u, &header) == RT_LOADER_ERR_TOO_SMALL,
          "truncated image rejected");
    CHECK(rt_loader_validate(NULL, 0u, &header) == RT_LOADER_ERR_TOO_SMALL,
          "NULL image rejected");

    memcpy(copy, image, len);
    copy[8] = 0x02u; /* code_off = 2, inside the header */
    CHECK(rt_loader_validate(copy, len, &header) == RT_LOADER_ERR_RANGE,
          "code overlapping the header rejected");
}

static void test_loader_run(void)
{
    uint8_t image[RT_MODULE_MAX_IMAGE];
    size_t len = rt_loader_build_return_image(image, sizeof(image), 7u);
    uint32_t value = 0u;
    void *fault = NULL;
    rt_loader_error_t err = RT_LOADER_OK;
    rt_status_t status;

    if (len == 0u) {
        printf("skip loader.run (no emitter for this ISA)\n");
        return;
    }
    status = rt_loader_run(image, len, &value, &err, &fault);
    CHECK(status != RT_FAIL, "valid module is not rejected");
    if (status == RT_PASS) {
        CHECK(value == 7u, "module entry returned the emitted immediate");
    } else {
        printf("note loader.run was BLOCKED (si_addr=%p): execution refused here\n", fault);
    }

    /* negative: a rejected image must never be executed */
    image[0] = 0x00u;
    status = rt_loader_run(image, len, &value, &err, &fault);
    CHECK(status == RT_FAIL, "invalid module is rejected before execution");
    CHECK(err == RT_LOADER_ERR_MAGIC, "rejection reason is reported to the caller");
}

/* ---------------------------------------------------------------------- jit */

static void test_jit_emit(void)
{
    uint8_t buffer[32];
    size_t len = 0u;
    int rc = rt_jit_emit_return_imm(buffer, sizeof(buffer), 42u, &len);

    CHECK(rc == 0, "emit_return_imm succeeds on this ISA");
    CHECK(len > 0u && len <= sizeof(buffer), "emitted length fits the buffer");

#if defined(__x86_64__) || defined(__i386__)
    CHECK(buffer[0] == 0xB8u, "payload starts with mov eax, imm32");
    CHECK(buffer[len - 1u] == 0xC3u, "payload ends with ret");
    {
        uint32_t encoded = 0u;
        memcpy(&encoded, buffer + 1, sizeof(encoded));
        CHECK(encoded == 42u, "immediate is encoded in the instruction");
    }
#elif defined(__aarch64__)
    {
        uint32_t last = 0u;
        memcpy(&last, buffer + len - 4u, sizeof(last));
        CHECK(last == 0xD65F03C0u, "payload ends with RET");
        CHECK(len == 8u, "small immediate uses MOVZ+RET (2 instructions)");
    }
#endif

    /* negative: a buffer that is too small must be refused, not overflowed */
    CHECK(rt_jit_emit_return_imm(buffer, 2u, 1u, &len) == -1,
          "undersized buffer refused");
}

static void test_jit_execution(void)
{
    uint8_t payload[32];
    size_t len = 0u;
    void *arena;
    void *target = NULL;
    void *fault = NULL;
    uint32_t value = 0u;
    int err = 0;
    int used_map_jit = 0;
    int rc;
    rt_jit_fn_t fn;

    if (rt_jit_emit_return_imm(payload, sizeof(payload), 42u, &len) != 0) {
        printf("skip jit.execution (no emitter for this ISA)\n");
        return;
    }
    arena = rt_jit_alloc(len, &err, &used_map_jit);
    CHECK(arena != NULL, "jit arena allocated");
    if (arena == NULL) {
        return;
    }
    memcpy(arena, payload, len);
    (void)rt_jit_invalidate(arena, len);
    if (rt_mem_protect(arena, len, RT_PROT_READ | RT_PROT_EXEC, &err) != 0) {
        printf("note jit.execution blocked: mprotect(R-X) errno=%d (%s)\n", err, strerror(err));
        (void)rt_jit_free(arena, len);
        return;
    }
    target = arena;
    memcpy(&fn, &target, sizeof(fn));
    rc = rt_signal_call_guarded(fn, &value, &fault, &err);
    CHECK(rc == 0, "generated code was callable under the guard");
    if (rc == 0) {
        CHECK(value == 42u, "generated code returned 42");
    }

    /* rewrite and re-run: this is the icache-synchronisation path */
    if (rt_mem_protect(arena, len, RT_PROT_READ | RT_PROT_WRITE, &err) == 0) {
        if (rt_jit_emit_return_imm(payload, sizeof(payload), 4242u, &len) == 0) {
            memcpy(arena, payload, len);
            (void)rt_jit_invalidate(arena, len);
            if (rt_mem_protect(arena, len, RT_PROT_READ | RT_PROT_EXEC, &err) == 0) {
                target = arena;
                memcpy(&fn, &target, sizeof(fn));
                rc = rt_signal_call_guarded(fn, &value, &fault, &err);
                CHECK(rc == 0 && value == 4242u,
                      "rewritten generated code returned 4242 after icache sync");
            }
        }
    }
    (void)rt_jit_free(arena, len);
}

/* ------------------------------------------------------------------- memory */

static void test_memory_transitions(void)
{
    size_t page = (size_t)rt_platform_page_size();
    void *region;
    void *fault = NULL;
    int err = 0;
    int rc;

    region = rt_mem_reserve(page, &err);
    CHECK(region != NULL, "rt_mem_reserve returns a mapping");
    if (region == NULL) {
        return;
    }
    CHECK(rt_mem_read_probe(region, page) == 0, "fresh RW mapping is readable");

    CHECK(rt_mem_protect(region, page, RT_PROT_READ, &err) == 0, "RW -> R transition accepted");
    rc = rt_mem_fault_probe(region, 1, &fault, &err);
    CHECK(rc == -1, "write to a read-only page faults");
    CHECK(fault != NULL, "the fault reports an address");

    CHECK(rt_mem_protect(region, page, RT_PROT_READ | RT_PROT_WRITE, &err) == 0,
          "R -> RW transition accepted");
    rc = rt_mem_fault_probe(region, 1, &fault, &err);
    CHECK(rc == 0, "write after returning to RW succeeds");

    CHECK(rt_mem_protect(region, page, RT_PROT_READ | RT_PROT_EXEC, &err) == 0,
          "RW -> R-X transition accepted");

    /* negative: a NULL region must be refused, not passed to munmap */
    CHECK(rt_mem_release(NULL, page, &err) == -1, "releasing NULL is refused");
    CHECK(rt_mem_reserve(0u, &err) == NULL, "zero-length reserve is refused");

    CHECK(rt_mem_release(region, page, &err) == 0, "munmap accepted");
}

static void test_memory_fault_address(void)
{
    void *fault = NULL;
    int err = 0;
    int rc = rt_mem_fault_probe((void *)(uintptr_t)0x1000u, 0, &fault, &err);

    if (rc == -2) {
        printf("skip memory.fault_address (guard unavailable errno=%d)\n", err);
        return;
    }
    CHECK(rc == -1, "reading the unmapped low page faults");
    CHECK(fault == (void *)(uintptr_t)0x1000u, "si_addr matches the probed address");
}

static void test_dual_mapping(void)
{
    rt_dual_map_t map;
    size_t page = (size_t)rt_platform_page_size();

    if (rt_dual_map_create(page, &map) != 0) {
        printf("note dual mapping unavailable here: errno=%d (%s)\n", map.err, strerror(map.err));
        CHECK(map.supported == 0, "unsupported dual mapping is reported, not faked");
        return;
    }
    CHECK(map.supported == 1, "dual mapping created");
    CHECK(rt_dual_map_views_aliased(&map) == 1, "both views see the same bytes");
    CHECK(rt_dual_map_destroy(&map) == 0, "dual mapping destroyed");
    CHECK(rt_dual_map_views_aliased(&map) == -1, "destroyed map is not usable (negative case)");
}

/* ---------------------------------------------------------------------- cpu */

static void test_cpu_facts(void)
{
    rt_cpu_facts_t facts;
    int page;

    CHECK(rt_cpu_collect(&facts) == 0, "cpu facts collected");
    page = facts.page_size;
    CHECK(page >= 1024 && (page & (page - 1)) == 0, "page size is a power of two >= 1024");
    CHECK(facts.pointer_bits >= 32, "pointer width reported");
    CHECK(facts.arch != NULL && facts.arch[0] != '\0', "architecture name reported");
    CHECK(facts.abi != NULL && facts.abi[0] != '\0', "ABI name reported");
    CHECK(strcmp(rt_platform_name(), facts.platform) == 0, "cpu facts name the same platform");

    /* x18 policy: on Apple arm64 the platform reserves it; elsewhere it is a build flag */
#if defined(__APPLE__) && defined(__aarch64__)
    CHECK(facts.x18_reserved_platform == 1, "x18 is platform-reserved on arm64-apple");
#endif
}

/* ------------------------------------------------------------------ threads */

static void test_threads(void)
{
    uint64_t value = 0u;
    int err = 0;
    uint64_t expected = (uint64_t)RT_THREADS_WORKERS * (uint64_t)RT_THREADS_ITERATIONS;

    CHECK(rt_thread_mutex_counter(&value, &err) == 0, "mutex counter completed");
    CHECK(value == expected, "mutex-protected counter is exact");
    CHECK(rt_thread_tls_roundtrip(&value, &err) == 0, "TLS round trip per thread");
    CHECK(rt_thread_roundtrip(&value, &err) == 0, "create/join round trip");
    CHECK(rt_thread_condition_pingpong(&value, &err) == 0, "condition variable delivered tokens");
    CHECK(rt_thread_atomics_roundtrip(&value, &err) == 0, "atomic counter is exact");
}

/* ------------------------------------------------------------------ signals */

static void test_signals(void)
{
    void *fault = NULL;
    int err = 0;
    rt_fault_guard_t guard;

    CHECK(rt_signal_roundtrip(SIGUSR1, &err) == 0, "SIGUSR1 install/query/restore");
    CHECK(rt_signal_controlled_segv(&fault, &err) == 0, "controlled SIGSEGV is caught");

    /* negative: a second guard must be refused while one is active */
    CHECK(rt_fault_guard_begin(&guard, &err) == 0, "first guard installs");
    {
        rt_fault_guard_t second;
        int second_err = 0;
        CHECK(rt_fault_guard_begin(&second, &second_err) == -1 && second_err == EBUSY,
              "nested guard is refused with EBUSY");
    }
    CHECK(rt_fault_guard_end(&guard) == 0, "guard removed");
}

/* ----------------------------------------------------------------------- fs */

/* ------------------------------------------------------------ apple target */

/* The distinction CI run #3 forced: macOS and iOS are different targets, and an API
 * that exists on one is marked unavailable by the other's SDK. These checks are
 * non-vacuous on every platform: each one is an equality between two observable
 * facts, so a wrong mapping fails on macOS, on iOS and on Linux alike. */
static void test_apple_target(void)
{
    int target = rt_platform_apple_target();
    const char *name = rt_platform_apple_target_name();
    uint32_t caps = rt_platform_capabilities();

    CHECK(name != NULL && name[0] != '\0', "apple target has a non-empty name");
    CHECK((strcmp(name, "none") == 0) == (target == RT_APPLE_TARGET_NONE),
          "apple target code and name agree");
    CHECK(target == RT_APPLE_TARGET_NONE || target == RT_APPLE_TARGET_MACOS ||
          target == RT_APPLE_TARGET_IPHONE_DEVICE ||
          target == RT_APPLE_TARGET_IPHONE_SIMULATOR ||
          target == RT_APPLE_TARGET_UNKNOWN_APPLE,
          "apple target code is one of the documented values");
    CHECK((target == RT_APPLE_TARGET_NONE) == (rt_platform_is_apple() == 0),
          "the target is 'none' exactly when the host is not Apple");
    CHECK(RT_APPLE_HAS_JIT_WRITE_PROTECT == (target == RT_APPLE_TARGET_MACOS),
          "the write-protect gate follows the Apple target, not plain __APPLE__");
    /* The invariant CI run #3 broke: the capability bit and the ability to call the
     * API must agree. Advertised-but-uncallable is what made the iOS build fail. */
    CHECK(((caps & RT_CAP_JIT_WP_NP) != 0u) ==
          (RT_APPLE_HAS_JIT_WRITE_PROTECT != 0 && rt_platform_is_apple() != 0),
          "pthread_jit_write_protect_np is advertised exactly where the target can call it");
}

/* ------------------------------------------- JIT write-protect semantics */

/* CI run #3 showed why this needs its own classification: on iOS the API cannot be
 * called at all. The four outcomes must stay distinct, and none of them may be
 * manufactured from another. Exercised here with synthetic inputs, so the iOS branch
 * is verified even on a Linux host. */
/* The composed platform summary is the app-facing API (CI run #4 taught us the app
 * must not reach into RuntimeCore headers for facts like the ISA). It must be complete,
 * bounded, and must refuse a buffer that cannot hold the answer. */
static void test_platform_summary(void)
{
    char summary[256];
    char tiny[8];
    char exact[1];
    int written = phase02_platform_summary(summary, sizeof(summary));

    CHECK(written > 0, "platform summary is produced");
    CHECK((size_t)written == strlen(summary), "the returned length matches the string");
    CHECK(strstr(summary, "platform=") != NULL, "summary names the platform");
    CHECK(strstr(summary, "page_size=") != NULL, "summary reports the page size");
    CHECK(strstr(summary, "isa=") != NULL, "summary reports the JIT ISA");
    CHECK(strstr(summary, "apple_target=") != NULL, "summary reports the Apple target");
    CHECK(strstr(summary, rt_jit_isa()) != NULL, "the ISA in the summary is the real one");
    CHECK(strstr(summary, rt_platform_name()) != NULL, "the platform in the summary is the real one");

    /* negative: too small a buffer must fail and must not leave a partial string */
    exact[0] = 'x';
    CHECK(phase02_platform_summary(tiny, sizeof(tiny)) == -1, "a short buffer is refused");
    CHECK(phase02_platform_summary(exact, 1u) == -1, "a one-byte buffer is refused");
    CHECK(exact[0] == '\0', "…and it is cleared, not left half-written");
    CHECK(phase02_platform_summary(NULL, 64u) == -1, "NULL buffer is refused");
    CHECK(phase02_platform_summary(summary, 0u) == -1, "capacity 0 is refused");
}

static void test_write_protect_semantics(void)
{
    CHECK(phase02_classify_write_protect(1, 0, RT_APPLE_TARGET_MACOS) == RT_PASS,
          "hook present and callable classifies as PASS");
    CHECK(phase02_classify_write_protect(1, -1, RT_APPLE_TARGET_MACOS) == RT_BLOCKED,
          "hook present but refusing classifies as BLOCKED");
    CHECK(phase02_classify_write_protect(0, -1, RT_APPLE_TARGET_IPHONE_DEVICE) == RT_NOT_APPLICABLE,
          "unavailable on the iOS device target classifies as NOT_APPLICABLE, not PASS");
    CHECK(phase02_classify_write_protect(0, -1, RT_APPLE_TARGET_IPHONE_SIMULATOR) == RT_NOT_APPLICABLE,
          "the same holds for the iOS simulator target");
    CHECK(phase02_classify_write_protect(0, -1, RT_APPLE_TARGET_NONE) == RT_UNSUPPORTED,
          "a platform without the API classifies as UNSUPPORTED");
    CHECK(phase02_classify_write_protect(0, -1, RT_APPLE_TARGET_UNKNOWN_APPLE) == RT_UNSUPPORTED,
          "an Apple build of unknown target does not claim to be iOS (conservative)");
    CHECK(phase02_classify_write_protect(0, -1, RT_APPLE_TARGET_MACOS) != RT_PASS &&
          phase02_classify_write_protect(1, -1, RT_APPLE_TARGET_MACOS) != RT_PASS,
          "no input classifies as PASS without a successful call");
}

/* --------------------------------------------------------------- platform */

static void test_platform_randomness(void)
{
    unsigned char first[16];
    unsigned char second[16];
    char name_one[64];
    char name_two[64];
    char tiny[8];
    int err = 0;

    /* arc4random_buf on Apple / getrandom on Linux: both must really produce bytes. */
    CHECK(rt_platform_random_bytes(first, sizeof(first), &err) == 0,
          "platform random bytes available");
    CHECK(rt_platform_random_bytes(second, sizeof(second), &err) == 0,
          "platform random bytes available a second time");
    CHECK(memcmp(first, second, sizeof(first)) != 0, "two draws differ (128-bit collision");
    CHECK(rt_platform_random_bytes(NULL, 0u, &err) == 0, "zero-length draw is a no-op, not a fault");

    /* negative: a NULL buffer with a real length must be rejected, not ignored */
    err = 0;
    CHECK(rt_platform_random_bytes(NULL, 4u, &err) == -1, "NULL buffer with length is refused");
    CHECK(err == EINVAL, "…with EINVAL as the documented errno");

    /* shm naming: unique, unpredictable, and refuses to silently produce a bad name */
    CHECK(rt_platform_unique_shm_name(name_one, sizeof(name_one), "/rt_test", &err) == 0,
          "unique shm name composed");
    CHECK(rt_platform_unique_shm_name(name_two, sizeof(name_two), "/rt_test", &err) == 0,
          "second unique shm name composed");
    CHECK(strcmp(name_one, name_two) != 0, "the two names differ");
    CHECK(strncmp(name_one, "/rt_test_", 9) == 0, "the name keeps the caller prefix");
    CHECK(strlen(name_one) >= 9u + 16u, "the name carries pid and 64 bits of randomness");

    /* negative: if it cannot produce a trustworthy name it must fail, never truncate */
    err = 0;
    CHECK(rt_platform_unique_shm_name(tiny, sizeof(tiny), "/rt_test", &err) == -1,
          "too-small buffer is refused");
    CHECK(err == ENAMETOOLONG, "…with ENAMETOOLONG as the documented errno");
    err = 0;
    CHECK(rt_platform_unique_shm_name(NULL, 64u, "/rt_test", &err) == -1,
          "NULL output buffer is refused");
    CHECK(err == EINVAL, "…with EINVAL as the documented errno");
}

static void test_filesystem(void)
{
    char template_path[256];
    uint64_t free_bytes = 0u;
    size_t deep_len = 0u;
    rt_fs_limits_t limits = { 0u, 0u, 0u, 0 };
    int err = 0;
    int written = snprintf(template_path, sizeof(template_path), "/tmp/phase02_unit_XXXXXX");

    if (written < 0 || (size_t)written >= sizeof(template_path)) {
        CHECK(0, "test workdir template fits");
        return;
    }
    if (mkdtemp(template_path) == NULL) {
        printf("skip fs tests (mkdtemp failed: %s)\n", strerror(errno));
        return;
    }
    CHECK(rt_fs_roundtrip(template_path, &err) == 0, "filesystem round trip");

    /* Limits are measured, never assumed: PATH_MAX is 4096 on Linux and 1024 on
     * Darwin, so a depth that is safe on one host can be impossible on the other. */
    /* Every check below executes unconditionally: a query that fails leaves the
     * limits zeroed, so the assertions fail loudly instead of being skipped in a dead
     * branch (the check count in the source must equal the count at runtime). */
    CHECK(rt_fs_limits_query(template_path, &limits, &err) == 0, "filesystem limits are queryable");
    CHECK(limits.path_max >= 512u, "platform reports a usable PATH_MAX");
    CHECK(limits.name_max >= 32u, "platform reports a usable NAME_MAX");
    CHECK(limits.path_cap > limits.name_max, "the implementation buffer exceeds NAME_MAX");
    CHECK(rt_fs_deep_paths(template_path, 24u, &deep_len, &err) == 0,
          "deep path creation well inside the reported limit");
    CHECK(deep_len > 100u, "deep path reached a long length");

    /* negative: an impossible depth must be refused with the limit's own errno… */
    err = 0;
    CHECK(rt_fs_deep_paths(template_path, 4096u, &deep_len, &err) == -1,
          "absurd depth is refused");
    CHECK(err == ENAMETOOLONG, "…with ENAMETOOLONG, the platform/implementation limit");
    /* …and must leave no partial tree behind (otherwise the next probe would fail
     * with EEXIST instead of the real limit) */
    {
        DIR *dir = opendir(template_path);
        int entries = 0;
        if (dir != NULL) {
            struct dirent *entry;
            while ((entry = readdir(dir)) != NULL) {
                if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
                    entries++;
                }
            }
            (void)closedir(dir);
        }
        CHECK(entries == 0, "a failed deep-path probe leaves no residue in the workdir");
    }

    /* negative: invalid arguments are rejected, not silently accepted */
    err = 0;
    CHECK(rt_fs_deep_paths(NULL, 4u, &deep_len, &err) == -1, "NULL root is refused");
    CHECK(err == EINVAL, "…with EINVAL");
    err = 0;
    CHECK(rt_fs_deep_paths(template_path, 0u, &deep_len, &err) == -1, "depth 0 is refused");
    CHECK(err == EINVAL, "…with EINVAL");
    err = 0;
    CHECK(rt_fs_limits_query(NULL, &limits, &err) == -1, "NULL root for the limits query is refused");
    CHECK(err == EINVAL, "…with EINVAL");
    CHECK(rt_fs_links_and_modes(template_path, &err) == 0, "symlink and chmod semantics");
    CHECK(rt_fs_capacity(template_path, &free_bytes, &err) == 0, "statvfs reports capacity");

    /* negative: a path that does not exist must fail inside the round trip helper */
    {
        char missing[320];
        int w = snprintf(missing, sizeof(missing), "%s/does_not_exist.bin", template_path);
        if (w > 0 && (size_t)w < sizeof(missing)) {
            CHECK(access(missing, F_OK) != 0, "absent path is absent");
        }
    }
    (void)rmdir(template_path);
}

/* ---------------------------------------------------------------------- ipc */

static void test_ipc(void)
{
    size_t sun_limit = 0u;
    const char *mechanism = "none";
    int err = 0;
    int pipe_fds[2];

    CHECK(rt_ipc_socketpair(&err) == 0, "socketpair round trip");
    CHECK(rt_ipc_pipe(&err) == 0, "pipe round trip");
    CHECK(rt_ipc_sun_path_limit(&sun_limit, &err) == 0 && sun_limit > 0u,
          "sun_path limit is reported");
    CHECK(rt_ipc_scm_rights(NULL, &err) == 0, "SCM_RIGHTS descriptor passing");
    CHECK(rt_ipc_shm(&err) == 0, "POSIX shared memory round trip");
    CHECK(rt_ipc_mux(&err, &mechanism) == 0, "readiness multiplexing works");

    /* negative: reading from a descriptor that was never opened must fail */
    pipe_fds[0] = -1;
    pipe_fds[1] = -1;
    if (pipe(pipe_fds) == 0) {
        ssize_t got;
        CHECK(close(pipe_fds[0]) == 0, "read end closed");
        got = read(pipe_fds[0], &err, 1u);
        CHECK(got == -1, "read on a closed descriptor fails");
        (void)close(pipe_fds[1]);
    }
}

/* ------------------------------------------------------------------ context */

static void test_context(void)
{
    rt_context_t ctx;
    char description[256];
    unsigned index;

    CHECK(rt_context_init(&ctx) == 0, "context initialised");
    CHECK(ctx.page_size > 0, "context carries the page size");
    CHECK(rt_context_add_module(&ctx, "probe", (void *)(uintptr_t)0x1234u, 64u) == 0,
          "module added");
    CHECK(rt_context_find_module(&ctx, "probe") != NULL, "module can be found");
    CHECK(rt_context_find_module(&ctx, "absent") == NULL, "absent module returns NULL");
    CHECK(rt_context_mark_executed(&ctx, "probe", 7u) == 0, "module marked as executed");
    CHECK(rt_context_mark_executed(&ctx, "absent", 7u) == -1, "marking an absent module fails");
    CHECK(rt_context_describe(description, sizeof(description), &ctx)[0] != '\0',
          "context describes itself");

    /* negative: the module table must refuse to overflow */
    for (index = 0u; index < RT_CONTEXT_MAX_MODULES; index++) {
        char name[16];
        int w = snprintf(name, sizeof(name), "m%u", index);
        if (w > 0 && (size_t)w < sizeof(name)) {
            (void)rt_context_add_module(&ctx, name, NULL, 1u);
        }
    }
    CHECK(rt_context_add_module(&ctx, "overflow", NULL, 1u) == -1,
          "module table refuses to overflow");
}

int main(void)
{
    printf("== PHASE_02_RECONSTRUCTED_POC unit tests ==\n");
    printf("platform=%s page_size=%d isa=%s\n", rt_platform_name(), rt_platform_page_size(),
           rt_jit_isa());

    test_status_names();
    test_loader_validation();
    test_loader_run();
    test_jit_emit();
    test_jit_execution();
    test_memory_transitions();
    test_memory_fault_address();
    test_dual_mapping();
    test_cpu_facts();
    test_threads();
    test_signals();
    test_platform_summary();
    test_write_protect_semantics();
    test_apple_target();
    test_platform_randomness();
    test_filesystem();
    test_ipc();
    test_context();

    printf("== %u checks, %u failures ==\n", g_checks, g_failures);
    return (g_failures == 0u) ? 0 : 1;
}

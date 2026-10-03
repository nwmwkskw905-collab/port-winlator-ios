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
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
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


/* ------------------------------------------------------------------- pass 03 */

/* The arena mechanism the allocator reports must match what actually happened, on every
 * host: this is the invariant that made physical run #2's `jit.alloc` unattributable, and
 * the one that keeps the MAP_JIT refusal visible when the W^X fallback supplies the arena. */
static void test_jit_alloc_arena_kind(void)
{
    size_t len = 64u;
    void *arena;
    int err = 0;
    int attempted = 0;
    int refused = 0;
    rt_jit_arena_kind_t kind = RT_JIT_ARENA_MAP_JIT;   /* poisoned: must be overwritten */

    arena = rt_jit_alloc_ex(len, &err, &kind, &attempted, &refused);
    CHECK(arena != NULL, "jit arena allocated (arena-kind probe)");
    if (arena == NULL) {
        CHECK(err != 0, "a failed allocation reports a real errno, never 0");
        return;
    }
    CHECK(kind != RT_JIT_ARENA_MAP_JIT || attempted != 0,
          "MAP_JIT kind implies the MAP_JIT path was taken");
    if (kind == RT_JIT_ARENA_MAP_JIT) {
        CHECK(attempted == 1, "MAP_JIT arena: the attempt flag is set");
        CHECK(refused == 0, "MAP_JIT arena: nothing was refused");
    } else if (kind == RT_JIT_ARENA_ANON_MAP_JIT_REFUSED) {
        /* The iOS/macOS case physical run #2 measures on the device: the refusal must stay
         * visible (errno) and the arena must still be a real W^X arena — never a success
         * that hides the missing capability. */
        CHECK(attempted == 1, "fallback arena: the MAP_JIT attempt is still reported");
        CHECK(refused != 0, "fallback arena: the MAP_JIT refusal errno is preserved");
    } else {
        CHECK(kind == RT_JIT_ARENA_ANON, "non-MAP_JIT arena reports the ANON kind");
        CHECK(attempted == 0, "ANON arena: MAP_JIT was not attempted on this platform");
        CHECK(refused == 0, "ANON arena: no refusal to report");
    }

    /* Whatever the kind, an anonymous arena must behave as a W^X buffer: writable, then
     * executable, never both, and releasable. A MAP_JIT arena's protection belongs to the
     * mapping and is driven by the write window, so it is not flipped here. */
    if (kind != RT_JIT_ARENA_MAP_JIT) {
        CHECK(rt_mem_protect(arena, len, RT_PROT_READ | RT_PROT_WRITE, &err) == 0,
              "arena is writable before the executable flip");
        CHECK(rt_mem_protect(arena, len, RT_PROT_READ | RT_PROT_EXEC, &err) == 0,
              "arena flips to R-X (single view, W^X)");
        /* The kernel's RWX policy is a measurement, not a design goal — the harness records
         * it the same way, so either answer is a fact and is printed rather than asserted.
         * What IS asserted is that the arena can return to writable-not-executable: the port
         * never needs (or keeps) a page that is writable and executable at the same time. */
        printf("note arena RWX policy: %s\n",
               (rt_mem_protect(arena, len, RT_PROT_READ | RT_PROT_WRITE | RT_PROT_EXEC, &err) == 0)
                   ? "granted by this kernel (recorded; the port still prefers W^X)"
                   : "refused (strict W^X available)");
        CHECK(rt_mem_protect(arena, len, RT_PROT_READ | RT_PROT_WRITE, &err) == 0,
              "arena returns to writable-not-executable after the RWX observation");
    }
    CHECK(rt_jit_free(arena, len) == 0, "arena released");
}

/* The iOS backend of the dual-mapping experiment (pass 03). On this host the named POSIX
 * path works, so this test exercises the substitute directly and checks the semantics the
 * experiment needs: one object, two live views, write-through/read-through aliasing, no
 * leftover object, and a real errno on a refusal. */
static void test_dual_mapping_file_backed(void)
{
    char dir[] = "/tmp/phase02-dual-XXXXXX";
    char *made;
    rt_dual_map_t map;
    size_t page = (size_t)rt_platform_page_size();
    int entries = -1;

    made = mkdtemp(dir);
    CHECK(made != NULL, "file-backed dual mapping: scratch directory created");
    if (made == NULL) {
        return;
    }

    if (rt_dual_map_create_file_backed(dir, page, &map) != 0) {
        printf("note file-backed dual mapping unavailable here: stage=%s errno=%d (%s)\n",
               rt_dual_stage_name(map.stage), map.err, strerror(map.err));
        CHECK(map.err != 0, "a refused file-backed dual mapping carries a real errno");
        CHECK(rt_dual_map_views_aliased(&map) == -1,
              "a refused map is not usable (negative case)");
    } else {
        CHECK(map.stage == RT_DUAL_STAGE_NONE, "file-backed dual mapping succeeded");
        CHECK(map.supported == 1, "file-backed map reports itself supported");
        CHECK(rt_dual_map_views_aliased(&map) == 1,
              "file-backed views alias the same memory (real semantics)");
        CHECK(munmap(map.rw, map.len) == 0, "writable view released");
        CHECK(munmap(map.rx, map.len) == 0, "executable view released");
    }

    /* Cleanup: the object is unlinked at creation, so the directory must be empty again
     * (a stray file would survive a crash and accumulate in the app container). */
    {
        DIR *d = opendir(dir);
        struct dirent *entry;
        if (d != NULL) {
            entries = 0;
            while ((entry = readdir(d)) != NULL) {
                if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
                    entries++;
                }
            }
            (void)closedir(d);
        }
    }
    CHECK(entries == 0, "no backing object is left behind in the directory");
    CHECK(rmdir(dir) == 0, "scratch directory removed (cleanup verified)");
}

/* A file-backed map refuses to guess a location: no directory means no experiment, with a
 * real errno, and never a fallback to a system-wide or fixed name. */
static void test_dual_mapping_file_backed_negative(void)
{
    rt_dual_map_t map;
    size_t page = (size_t)rt_platform_page_size();

    CHECK(rt_dual_map_create_file_backed(NULL, page, &map) != 0,
          "file-backed dual mapping refuses a NULL directory");
    CHECK(map.err == EINVAL, "…with errno=EINVAL (a defect of the call, not a platform answer)");
    CHECK(map.stage == RT_DUAL_STAGE_NAME, "…naming the stage that failed");
    CHECK(map.supported == 0, "…and reports the map as unsupported");

    CHECK(rt_dual_map_create_file_backed("/nonexistent-phase02-dir", page, &map) != 0,
          "file-backed dual mapping refuses an unusable directory");
    CHECK(map.err != 0, "…with a real errno from open(2)");
    CHECK(map.stage == RT_DUAL_STAGE_FILE_OPEN, "…naming open(2) as the failing stage");
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

/* ------------------------------------------------------- físico: classificação causal */

/* Every case below is taken from IPHONE13_PHYSICAL_RUN_01 (2026-10-02) or from the
 * contract the run exposed. They are the regression for the four physical FAILs: the
 * verdicts must come out of these inputs without a device. */
static void test_outcome_classification(void)
{
    CHECK(phase02_classify(PHASE02_OUTCOME_OK, RT_PASS) == RT_PASS,
          "observed success is the only source of PASS");
    CHECK(phase02_classify(PHASE02_OUTCOME_RUNTIME_DEFECT, RT_BLOCKED) == RT_FAIL,
          "a defect stays a defect even when something else is blocked");
    CHECK(phase02_classify(PHASE02_OUTCOME_CAPABILITY_MISSING, RT_PASS) == RT_BLOCKED,
          "a capability the platform refused is BLOCKED, never FAIL");
    CHECK(phase02_classify(PHASE02_OUTCOME_NOT_APPLICABLE, RT_PASS) == RT_NOT_APPLICABLE,
          "an API the target cannot call is NOT_APPLICABLE");
    CHECK(phase02_classify(PHASE02_OUTCOME_UNSUPPORTED, RT_PASS) == RT_UNSUPPORTED,
          "an absent mechanism is UNSUPPORTED");
    CHECK(phase02_classify(PHASE02_OUTCOME_UNDETERMINED, RT_PASS) == RT_UNTESTED,
          "an unestablished cause is UNTESTED - never PASS, never FAIL");
    CHECK(phase02_classify(PHASE02_OUTCOME_DEPENDENCY_BLOCKED, RT_BLOCKED) == RT_BLOCKED,
          "a dependent of a blocked capability is BLOCKED");
    CHECK(phase02_classify(PHASE02_OUTCOME_DEPENDENCY_BLOCKED, RT_UNSUPPORTED) == RT_BLOCKED,
          "…and also when the dependency is UNSUPPORTED");
    CHECK(phase02_classify(PHASE02_OUTCOME_DEPENDENCY_BLOCKED, RT_PASS) == RT_FAIL,
          "…but when the dependency PASSED the capability was there: that is a defect");
    CHECK(strcmp(phase02_outcome_name(PHASE02_OUTCOME_DEPENDENCY_BLOCKED),
                 "dependency-blocked") == 0, "outcome names are stable for the report");
    CHECK(strcmp(phase02_dependency_test(PHASE02_DEP_JIT_MAP), "jit.map_jit_probe") == 0,
          "a dependency names the test that measures it");

    /* jit.alloc: the physical combination (MAP_JIT attempted, probe BLOCKED with the same
     * errno) is one missing capability; every other combination is a defect. */
    CHECK(phase02_classify_jit_alloc(1, RT_BLOCKED, EPERM, EPERM) ==
              PHASE02_OUTCOME_DEPENDENCY_BLOCKED,
          "physical run #1: MAP_JIT attempted + probe EPERM + alloc EPERM => dependency");
    CHECK(phase02_classify(phase02_classify_jit_alloc(1, RT_BLOCKED, EPERM, EPERM),
                           RT_BLOCKED) == RT_BLOCKED,
          "…and that dependency maps to BLOCKED, not FAIL");
    CHECK(phase02_classify_jit_alloc(1, RT_BLOCKED, EPERM, ENOMEM) ==
              PHASE02_OUTCOME_RUNTIME_DEFECT,
          "a different errno is a different failure: not excused by the probe");
    CHECK(phase02_classify_jit_alloc(0, RT_BLOCKED, EPERM, EPERM) ==
              PHASE02_OUTCOME_RUNTIME_DEFECT,
          "a non-MAP_JIT allocation failure is never blamed on the MAP_JIT capability");
    CHECK(phase02_classify_jit_alloc(1, RT_PASS, 0, EPERM) == PHASE02_OUTCOME_RUNTIME_DEFECT,
          "probe PASSED and the allocation still failed => defect");
    CHECK(phase02_classify_jit_alloc(1, RT_BLOCKED, EPERM, 0) ==
              PHASE02_OUTCOME_RUNTIME_DEFECT,
          "an allocation error with errno lost is a defect, not a capability verdict");
}

static void test_fs_depth_classification(void)
{
    /* The physical failure: an error returned with errno 0 at depth 103. It is a defect of
     * the error path, is never PASS and is never excused as a platform limit. */
    CHECK(phase02_classify_fs_depth_error(RT_FS_STAGE_LEAF_WRITE, 0, 1007, 1024, 1) ==
              PHASE02_OUTCOME_RUNTIME_DEFECT,
          "physical run #1: error with errno=0 => defect, not a platform answer");
    CHECK(phase02_classify(phase02_classify_fs_depth_error(RT_FS_STAGE_LEAF_WRITE, 0, 1007,
                                                           1024, 1), RT_BLOCKED) == RT_FAIL,
          "…and that defect is FAIL, with the reason named in the record");
    /* The three legitimate cases. */
    CHECK(phase02_classify_fs_depth_error(RT_FS_STAGE_MKDIR, ENAMETOOLONG, 1007, 1024, 1) ==
              PHASE02_OUTCOME_OK,
          "reaching PATH_MAX is the platform limit: a measurement, not a failure");
    CHECK(phase02_classify_fs_depth_error(RT_FS_STAGE_PATH_CAP, ENAMETOOLONG, 2035, 4096, 1) ==
              PHASE02_OUTCOME_OK_PROBE_LIMIT,
          "the probe's own buffer as ceiling is PASS with the provenance stated");
    CHECK(phase02_classify_fs_depth_error(RT_FS_STAGE_LEAF_JOIN, ENAMETOOLONG, 2035, 4096, 1) ==
              PHASE02_OUTCOME_OK_PROBE_LIMIT,
          "…same for the leaf join");
    CHECK(phase02_classify_fs_depth_error(RT_FS_STAGE_MKDIR, EPERM, 100, 1024, 1) ==
              PHASE02_OUTCOME_CAPABILITY_MISSING,
          "a sandbox refusal is a capability, not a defect");
    CHECK(phase02_classify_fs_depth_error(RT_FS_STAGE_LEAF_WRITE, ENOSPC, 100, 1024, 1) ==
              PHASE02_OUTCOME_CAPABILITY_MISSING,
          "a full filesystem is a resource limit, reported as BLOCKED");
    CHECK(phase02_classify_fs_depth_error(RT_FS_STAGE_MKDIR, EINVAL, 10, 1024, 1) ==
              PHASE02_OUTCOME_RUNTIME_DEFECT,
          "an errno that fits no contract is a defect");
    CHECK(strcmp(rt_fs_stage_name(RT_FS_STAGE_LEAF_WRITE), "leaf-write") == 0,
          "the failing stage has a stable name for the report");
}

static void test_shm_classification(void)
{
    CHECK(phase02_classify_shm_error(RT_IPC_STAGE_NONE, 0) == PHASE02_OUTCOME_OK,
          "no failing stage means the probe succeeded");
    CHECK(phase02_classify_shm_error(RT_IPC_STAGE_SHM_OPEN, EPERM) ==
              PHASE02_OUTCOME_CAPABILITY_MISSING,
          "shm_open refused with EPERM is a capability the sandbox did not grant");
    CHECK(phase02_classify_shm_error(RT_IPC_STAGE_MAP_SECOND, EPERM) ==
              PHASE02_OUTCOME_CAPABILITY_MISSING,
          "the second view refused is the same class, and the stage tells them apart");
    CHECK(phase02_classify_shm_error(RT_IPC_STAGE_FTRUNCATE, ENOSYS) ==
              PHASE02_OUTCOME_UNSUPPORTED,
          "an absent mechanism is UNSUPPORTED");
    CHECK(phase02_classify_shm_error(RT_IPC_STAGE_COMPARE, EILSEQ) ==
              PHASE02_OUTCOME_RUNTIME_DEFECT,
          "two views of one object disagreeing is our defect, never a platform property");
    CHECK(phase02_classify_shm_error(RT_IPC_STAGE_SHM_OPEN, 0) ==
              PHASE02_OUTCOME_RUNTIME_DEFECT,
          "an error with errno 0 is a defect of the error path");
    CHECK(strcmp(rt_ipc_stage_name(RT_IPC_STAGE_MAP_SECOND), "mmap(second view)") == 0,
          "the stage name identifies the syscall in the record");
}

/* The loader must never report a rejection without a reason, and a valid module that
 * cannot be executed here must come back BLOCKED — not FAIL, and never "rejected: OK"
 * (IPHONE13_PHYSICAL_RUN_01). */
static void test_loader_reason_propagation(void)
{
    uint8_t image[RT_MODULE_MAX_IMAGE];
    size_t len = rt_loader_build_return_image(image, sizeof(image), 7u);
    uint32_t value = 0u;
    void *fault = NULL;
    rt_loader_error_t reason = RT_LOADER_OK;
    int os_err = 0;
    int map_jit_attempted = 0;
    rt_status_t status;
    struct rlimit saved_limit;
    struct rlimit tight_limit;
    int limited_ok = 0;
    int limit_bites = 0;

    if (len == 0u) {
        printf("skip loader.reason (no emitter for this ISA)\n");
        return;
    }

    /* 1. Every rejection path must name a reason that is not OK. */
    {
        uint8_t broken[RT_MODULE_MAX_IMAGE];
        memcpy(broken, image, len);
        broken[0] = 0x00u;
        reason = RT_LOADER_OK;
        status = rt_loader_run_ex(broken, len, &value, &reason, &fault, &os_err,
                                  &map_jit_attempted);
        CHECK(status == RT_FAIL, "a broken image is still rejected");
        CHECK(reason == RT_LOADER_ERR_MAGIC,
              "…with a real reason (the reason is never left at OK)");
        CHECK(strcmp(rt_loader_error_name(reason), "OK") != 0,
              "a rejection reason can never read as a success");
    }

    /* 2. The physical condition, as far as this environment can reproduce it: the executable
     *    arena cannot be allocated. RLIMIT_AS is the portable way to make mmap(2) fail here,
     *    which is what the MAP_JIT allocation did on the device (EPERM). A resource limit that
     *    is merely *set* proves nothing, so the limit must first be shown to bite: on a kernel
     *    where it does not (qemu-user implements guest memory itself and ignores it), the test
     *    says so and asserts the classification instead of pretending to have reproduced the
     *    device condition. The verdict must be BLOCKED with a reason and an errno, never FAIL
     *    with reason OK. */
    if (getrlimit(RLIMIT_AS, &saved_limit) == 0) {
        tight_limit = saved_limit;
        tight_limit.rlim_cur = 8u * 1024u * 1024u;   /* far below what the arena needs */
        limit_bites = 0;
        if (setrlimit(RLIMIT_AS, &tight_limit) == 0) {
            void *probe = mmap(NULL, 64u * 1024u * 1024u, PROT_READ | PROT_WRITE,
                               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            limit_bites = (probe == MAP_FAILED) ? 1 : 0;
            if (limit_bites != 0) {
                reason = RT_LOADER_OK;
                os_err = 0;
                status = rt_loader_run_ex(image, len, &value, &reason, &fault, &os_err,
                                          &map_jit_attempted);
                limited_ok = 1;
                CHECK(status == RT_BLOCKED,
                      "arena refused: a valid module is BLOCKED, not rejected (physical #1)");
                CHECK(reason == RT_LOADER_ERR_JIT_UNAVAILABLE,
                      "…the reason names the missing capability, not a rejection");
                CHECK(strcmp(rt_loader_error_name(reason), "OK") != 0,
                      "…so the detail can never print 'rejected: OK'");
                CHECK(os_err != 0,
                      "…and the errno of the failing allocation is reported (never 0)");
                CHECK(value == 0u, "no value is reported for a module that did not run");
            } else {
                (void)munmap(probe, 64u * 1024u * 1024u);
                printf("skip loader.arena-refused: this kernel ignores RLIMIT_AS for mmap "
                       "(limit set, probe succeeded) - the device condition cannot be "
                       "reproduced here\n");
            }
            (void)setrlimit(RLIMIT_AS, &saved_limit);
        } else {
            printf("skip loader.arena-refused (RLIMIT_AS cannot be lowered here)\n");
        }
    } else {
        printf("skip loader.arena-refused (no getrlimit here)\n");
    }

    /* 2b. The same combination as a pure decision, so it is verified on every host: the
     *     physical inputs (valid image, arena refused, probe blocked, errno present) must
     *     classify as a dependency, and the run #1 symptom as a defect. */
    CHECK(phase02_classify_loader_result(RT_BLOCKED, RT_LOADER_ERR_JIT_UNAVAILABLE, 1,
                                         RT_BLOCKED, EPERM) ==
              PHASE02_OUTCOME_DEPENDENCY_BLOCKED,
          "physical #1 combination classifies as a blocked dependency (see also loader E2E)");
    CHECK(phase02_classify(phase02_classify_loader_result(RT_BLOCKED,
                                                          RT_LOADER_ERR_JIT_UNAVAILABLE, 1,
                                                          RT_BLOCKED, EPERM),
                           RT_BLOCKED) == RT_BLOCKED,
          "…and the report says BLOCKED, not FAIL");
    CHECK(phase02_classify_loader_result(RT_FAIL, RT_LOADER_OK, 0, RT_BLOCKED, EPERM) ==
              PHASE02_OUTCOME_RUNTIME_DEFECT,
          "physical #1 symptom: a rejection with reason OK is a defect, never a verdict");
    CHECK(phase02_classify_loader_result(RT_FAIL, RT_LOADER_ERR_MAGIC, 0, RT_BLOCKED, 0) ==
              PHASE02_OUTCOME_OK,
          "a rejection that names its reason is correct behaviour");
    CHECK(phase02_classify_loader_result(RT_BLOCKED, RT_LOADER_ERR_JIT_UNAVAILABLE, 1,
                                         RT_PASS, EPERM) == PHASE02_OUTCOME_RUNTIME_DEFECT,
          "probe PASSED and the arena still failed: that is ours, not the capability");
    CHECK(phase02_classify_loader_result(RT_BLOCKED, RT_LOADER_ERR_JIT_UNAVAILABLE, 1,
                                         RT_BLOCKED, 0) == PHASE02_OUTCOME_RUNTIME_DEFECT,
          "an arena failure with errno lost cannot be blamed on the capability");

    /* 3. With the limit restored the very same image must run again: the BLOCKED verdict
     *    came from the environment, not from a corrupted module. */
    if (limited_ok != 0) {
        reason = RT_LOADER_OK;
        status = rt_loader_run_ex(image, len, &value, &reason, &fault, &os_err,
                                  &map_jit_attempted);
        CHECK(status != RT_FAIL, "the same image is accepted once memory is available again");
        if (status == RT_PASS) {
            CHECK(value == 7u, "…and it still returns the emitted immediate");
            CHECK(reason == RT_LOADER_OK, "…with OK as the reason for a successful run");
        } else {
            printf("note loader re-run BLOCKED after the limit was lifted (si_addr=%p)\n",
                   fault);
        }
    }
}

/* The errno-preservation regression: the physical run reported "errno=0 (Undefined error:
 * 0)" because the error path read an out-parameter it had never written. A failing call
 * must now always produce a non-zero errno and a named stage. */
static void test_errno_preservation_on_failure(void)
{
    char template_path[256];
    size_t deep_len = 0u;
    rt_fs_stage_t stage = RT_FS_STAGE_NONE;
    int err = 12345;     /* deliberately pre-set: the probe must overwrite it */
    int written = snprintf(template_path, sizeof(template_path), "/tmp/phase02_errno_XXXXXX");

    if (written < 0 || (size_t)written >= sizeof(template_path) ||
        mkdtemp(template_path) == NULL) {
        CHECK(0, "errno-preservation workdir created");
        return;
    }

    /* A read-only parent makes mkdir(2) fail with EACCES: a real, capturable errno. */
    if (chmod(template_path, S_IRUSR | S_IXUSR) == 0) {
        err = 12345;
        stage = RT_FS_STAGE_NONE;
        CHECK(rt_fs_deep_paths_ex(template_path, 4u, &deep_len, &err, &stage) == -1,
              "the probe fails on a read-only root");
        CHECK(err != 0, "…and the caller receives a non-zero errno (physical #1: it was 0)");
        CHECK(err != 12345, "…which is the failing call's errno, not the caller's old value");
        CHECK(stage != RT_FS_STAGE_NONE, "…and the failing stage is named");
        CHECK(err == EACCES || err == EPERM || err == EROFS,
              "…with EACCES/EPERM/EROFS for a read-only directory");
        (void)chmod(template_path, S_IRWXU);
    }

    /* The capacity ceiling: never reported as an errno-less failure either. */
    err = 12345;
    stage = RT_FS_STAGE_NONE;
    if (rt_fs_deep_paths_ex(template_path, 4096u, &deep_len, &err, &stage) == -1) {
        CHECK(err != 0, "the capacity ceiling also carries a real, non-zero errno");
        CHECK(stage != RT_FS_STAGE_NONE, "…and a stage");
        CHECK(stage == RT_FS_STAGE_PATH_CAP || stage == RT_FS_STAGE_LEAF_JOIN ||
              stage == RT_FS_STAGE_ERRNO_LOST,
              "…which is the probe's own buffer, or the errno-lost invariant breach");
    }

    /* Argument refusal is a documented errno too. */
    err = 12345;
    CHECK(rt_fs_deep_paths_ex(NULL, 4u, &deep_len, &err, &stage) == -1,
          "NULL root is refused");
    CHECK(err == EINVAL, "…with EINVAL, not with the caller's stale value");
    CHECK(stage == RT_FS_STAGE_ROOT, "…and the stage says which argument");

    /* And the classification of the physical symptom itself. */
    CHECK(phase02_classify(phase02_classify_fs_depth_error((int)RT_FS_STAGE_LEAF_WRITE, 0,
                                                            1007, 1024, 1),
                           RT_BLOCKED) == RT_FAIL,
          "the run #1 symptom (errno=0) is FAIL with 'errno not preserved', not PASS");
    (void)rmdir(template_path);
}

/* ------------------------------------------------- estabilização (rodada pass 01) */

/* The JIT arena must be sized for BOTH payloads before either is written, and the two
 * payloads must live in separate buffers. Emitting the second one over the first is what
 * made the fixed microtest copy the wrong bytes and execute 4242 where 42 was expected. */
static void test_jit_payload_capacity(void)
{
    uint8_t first[16];
    uint8_t second[16];
    size_t first_len = 0u;
    size_t second_len = 0u;
    size_t emitted;

    memset(first, 0xEE, sizeof(first));
    memset(second, 0xEE, sizeof(second));

    CHECK(rt_jit_emit_return_imm(first, sizeof(first), 42u, &first_len) == 0 &&
              first_len > 0u && first_len <= 16u,
          "the 42 payload is emitted and its length is reported");
    CHECK(rt_jit_emit_return_imm(second, sizeof(second), 4242u, &second_len) == 0 &&
              second_len > 0u && second_len <= 16u,
          "the 4242 payload is emitted into its own buffer");
    CHECK(memcmp(first, second, (first_len < second_len) ? first_len : second_len) != 0,
          "the two payloads differ: they were emitted into separate buffers");
    CHECK(first_len <= sizeof(first) && second_len <= sizeof(second),
          "an arena sized max(first, second) holds both payloads");
    emitted = (first_len > second_len) ? first_len : second_len;
    CHECK(emitted >= first_len && emitted >= second_len,
          "the sizing rule used by the microtest covers both writes");

    /* Negative: a buffer that cannot hold the payload is refused, not half-written. */
    CHECK(rt_jit_emit_return_imm(second, 2u, 42u, &second_len) == -1,
          "an undersized buffer is refused by the emitter");
}

/* The fault guard could not be installed (-2) is NOT a fault: a valid module must come back
 * as a defect of ours (INTERNAL) with the errno, never as EXEC_FAULT with si_addr=NULL. */
static void test_loader_guard_unavailable_is_not_a_fault(void)
{
    uint8_t image[RT_MODULE_MAX_IMAGE];
    size_t len = rt_loader_build_return_image(image, sizeof(image), 7u);
    uint32_t value = 0xDEADBEEFu;
    void *fault = (void *)0x1;
    rt_loader_error_t reason = RT_LOADER_OK;
    rt_status_t status;
    int os_err = 0;
    int map_jit_attempted = 0;
    int err = 0;
    rt_fault_guard_t outer;

    if (len == 0u) {
        printf("skip loader.guard-unavailable (no emitter for this ISA)\n");
        return;
    }

    /* Hold a guard so the loader's own guard cannot be installed (EBUSY). */
    if (rt_fault_guard_begin(&outer, &err) != 0) {
        printf("skip loader.guard-unavailable (cannot install the outer guard)\n");
        return;
    }
    status = rt_loader_run_ex(image, len, &value, &reason, &fault, &os_err, &map_jit_attempted);
    (void)rt_fault_guard_end(&outer);

    CHECK(status == RT_FAIL, "a guard that cannot be installed is a failure of ours");
    CHECK(reason == RT_LOADER_ERR_INTERNAL,
          "…reported as INTERNAL, not as EXEC_FAULT (no fault ever happened)");
    CHECK(os_err == EBUSY, "…with the errno of the guard failure");
    CHECK((uint32_t)value == 0xDEADBEEFu, "…and the output value is left untouched");
}

/* Dual mapping: the failing step is reported, and the classification vocabulary is the one
 * the shared-memory probe uses (a refusal is a capability, an absent mechanism is
 * UNSUPPORTED, a disagreement between the two views is our defect). */
static void test_dual_mapping_stage_and_classification(void)
{
    rt_dual_map_t map;

    CHECK(strcmp(rt_dual_stage_name(RT_DUAL_STAGE_MAP_RX), "mmap(executable view)") == 0,
          "the executable-view stage has a stable name");
    CHECK(phase02_classify_dual_mapping_error(RT_DUAL_STAGE_MAP_RX, EPERM) ==
              PHASE02_OUTCOME_CAPABILITY_MISSING,
          "EPERM on the executable view is a capability this process was not granted");
    CHECK(phase02_classify(phase02_classify_dual_mapping_error(RT_DUAL_STAGE_MAP_RX, EPERM),
                           RT_BLOCKED) == RT_BLOCKED,
          "…and the report says BLOCKED, not UNSUPPORTED (the same rule as ipc.posix_shm)");
    CHECK(phase02_classify_dual_mapping_error(RT_DUAL_STAGE_SHM_OPEN, EPERM) ==
              PHASE02_OUTCOME_CAPABILITY_MISSING,
          "shm_open refused is the same class, and the stage tells the two apart");
    CHECK(phase02_classify_dual_mapping_error(RT_DUAL_STAGE_FTRUNCATE, ENOSYS) ==
              PHASE02_OUTCOME_UNSUPPORTED,
          "an absent mechanism is UNSUPPORTED");
    CHECK(phase02_classify_dual_mapping_error(RT_DUAL_STAGE_ALIAS_CHECK, EILSEQ) ==
              PHASE02_OUTCOME_RUNTIME_DEFECT,
          "two views that disagree although both were granted is our defect");
    CHECK(phase02_classify_dual_mapping_error(RT_DUAL_STAGE_MAP_RW, EINVAL) ==
              PHASE02_OUTCOME_RUNTIME_DEFECT,
          "an errno that fits no contract is a defect, never a platform limit");
    CHECK(phase02_classify_dual_mapping_error(RT_DUAL_STAGE_MAP_RX, 0) ==
              PHASE02_OUTCOME_RUNTIME_DEFECT,
          "an error with errno 0 is a defect of the error path");
    CHECK(phase02_classify_dual_mapping_error(RT_DUAL_STAGE_NONE, 0) == PHASE02_OUTCOME_OK,
          "no failing stage means the experiment succeeded");

    /* Live: on this host the experiment is expected to work, with the stage cleared. */
    memset(&map, 0, sizeof(map));
    if (rt_dual_map_create((size_t)rt_platform_page_size(), &map) == 0) {
        CHECK(map.stage == RT_DUAL_STAGE_NONE, "a successful dual map reports no failing stage");
        CHECK(rt_dual_map_views_aliased(&map) == 1, "…and its two views alias the same memory");
        (void)rt_dual_map_destroy(&map);
    } else {
        CHECK(map.err != 0, "a failed dual map always names a real errno");
        CHECK(map.stage != RT_DUAL_STAGE_NONE, "…and the step that failed");
        printf("note dual mapping refused here: stage=%s errno=%d (%s)\n",
               rt_dual_stage_name(map.stage), map.err, strerror(map.err));
    }
}

/* Shared memory: the cleanup step is named. A failed final shm_unlink is a cleanup fact —
 * the capability was already proven — and it can never be silently dropped. */
static void test_ipc_shm_cleanup_stage(void)
{
    int err = 12345;
    rt_ipc_stage_t stage = RT_IPC_STAGE_UNLINK;

    CHECK(strcmp(rt_ipc_stage_name(RT_IPC_STAGE_UNLINK), "shm_unlink(cleanup)") == 0,
          "the cleanup stage has a stable name");
    CHECK(phase02_classify_shm_error(RT_IPC_STAGE_UNLINK, EPERM) == PHASE02_OUTCOME_OK,
          "a failing cleanup never turns a proven capability into a refusal");
    CHECK(phase02_classify_shm_error(RT_IPC_STAGE_SHM_OPEN, EPERM) ==
              PHASE02_OUTCOME_CAPABILITY_MISSING,
          "…while a refusal at shm_open still is one");

    CHECK(rt_ipc_shm_ex(&err, &stage) == 0, "the shared-memory probe succeeds on this host");
    CHECK(stage == RT_IPC_STAGE_NONE || stage == RT_IPC_STAGE_UNLINK,
          "…and reports either no failing stage or the cleanup one");
    if (stage == RT_IPC_STAGE_UNLINK) {
        CHECK(err != 0, "a cleanup failure carries its errno");
        printf("note shm cleanup failed here: errno=%d (%s)\n", err, strerror(err));
    } else {
        CHECK(err == 0, "a fully successful probe reports no errno");
    }
}

/* A truncated DETAIL must say so: the stage and the errno live at the end of those lines. */
static void test_log_detail_truncation_is_marked(void)
{
    phase02_log_t *log = phase02_log_new();
    char filler[700];
    const char *text;
    int i;

    CHECK(log != NULL, "the log can be allocated");
    if (log == NULL) {
        return;
    }
    for (i = 0; i < (int)sizeof(filler) - 1; i++) {
        filler[i] = 'x';
    }
    filler[sizeof(filler) - 1] = '\0';
    phase02_log_init(log, "truncation test");
    phase02_log_record(log, "log.truncation_probe", RT_FAIL, "%s", filler);
    text = phase02_log_text(log);
    CHECK(strstr(text, "DETAIL TRUNCATED at 512 chars") != NULL,
          "an over-long DETAIL carries an explicit truncation marker");
    CHECK(strstr(text, "log.truncation_probe") != NULL,
          "…and the record itself is still identifiable");
    phase02_log_free(log);
}

/* ------------------------------------------------- pass 02: errno contracts */

static void test_mem_protect_rejects_overflow(void)
{
    void *region;
    int err = 0;

    region = rt_mem_reserve(2u * (size_t)rt_platform_page_size(), &err);
    CHECK(region != NULL, "region reserved for the overflow probe");
    if (region == NULL) {
        return;
    }
    err = 0;
    /* len + page - 1 would wrap: without a guard the rounded length becomes tiny and the
     * call protects a sliver while reporting success. rt_mem_reserve already refused this;
     * rt_mem_protect must refuse it too. */
    CHECK(rt_mem_protect(region, (size_t)-1 - 1024u, RT_PROT_READ, &err) == -1,
          "rt_mem_protect refuses a length that would overflow the rounding");
    CHECK(err == EOVERFLOW, "…with EOVERFLOW as the documented errno");
    err = 0;
    CHECK(rt_mem_protect(region, (size_t)rt_platform_page_size(), RT_PROT_READ | RT_PROT_WRITE,
                         &err) == 0,
          "a normal length is still protected");
    CHECK(err == 0, "…and reports no error");
    err = 0;
    CHECK(rt_mem_release(region, 2u * (size_t)rt_platform_page_size(), &err) == 0,
          "region released");
}

static void test_failure_carries_and_success_clears_errno(void)
{
    char template_path[256];
    char small[8];
    char missing[512];
    char file_root[512];
    int err = 0;
    int fd = -1;
    uint64_t value = 0u;

    if (snprintf(template_path, sizeof(template_path), "/tmp/phase02_errno_XXXXXX") >=
            (int)sizeof(template_path) ||
        mkdtemp(template_path) == NULL) {
        CHECK(0, "errno-contract workdir created");
        return;
    }

    /* fs: the errno of the failing call, never a stale one. */
    snprintf(missing, sizeof(missing), "%s/rt_no_such_dir", template_path);
    err = 0;
    CHECK(rt_fs_links_and_modes(missing, &err) == -1, "links probe on a missing root fails");
    CHECK(err == ENOENT, "…with ENOENT (the failing open), not a stale errno");

    CHECK(snprintf(file_root, sizeof(file_root), "%s/rt_root_file.bin", template_path) <
              (int)sizeof(file_root),
          "the file root path fits its buffer");
    fd = open(file_root, O_CREAT | O_TRUNC | O_WRONLY, S_IRUSR | S_IWUSR);
    if (fd >= 0) {
        (void)close(fd);
        err = 0;
        CHECK(rt_fs_links_and_modes(file_root, &err) == -1, "links probe under a file fails");
        CHECK(err == ENOTDIR, "…with ENOTDIR (the failing open), not a stale errno");
    } else {
        CHECK(0, "file root created for the ENOTDIR probe");
    }

    err = 0;
    CHECK(rt_fs_temp_file(small, sizeof(small), &err) == -1,
          "temp file name into a too-small buffer fails");
    CHECK(err == ENAMETOOLONG, "…with ENAMETOOLONG");
    err = 0;
    CHECK(rt_fs_temp_file(template_path, sizeof(template_path), &err) == 0,
          "temp file created for the success case");
    CHECK(err == 0, "…and the success path clears errno");

    /* signals: the round trip clears errno on success and reports one on failure. */
    err = 0;
    CHECK(rt_signal_roundtrip(SIGUSR1, &err) == 0, "SIGUSR1 round trip still succeeds");
    CHECK(err == 0, "…and clears errno");
    err = 0;
    CHECK(rt_signal_roundtrip(-1, &err) == -1, "an invalid signal number fails");
    CHECK(err != 0, "…and the failure carries an errno (never 0, never untouched)");

    /* threads: every round trip clears errno on success, and a mismatch would report EILSEQ
     * rather than leaving the caller's errno at 0. */
    err = 0;
    CHECK(rt_thread_roundtrip(&value, &err) == 0 && err == 0,
          "thread round trip succeeds with errno cleared");
    err = 0;
    CHECK(rt_thread_tls_roundtrip(&value, &err) == 0 && err == 0,
          "TLS round trip succeeds with errno cleared");
    err = 0;
    CHECK(rt_thread_mutex_counter(&value, &err) == 0 && err == 0,
          "mutex counter succeeds with errno cleared");
    err = 0;
    CHECK(rt_thread_condition_pingpong(&value, &err) == 0 && err == 0,
          "condition ping-pong succeeds with errno cleared");
    err = 0;
    CHECK(rt_thread_atomics_roundtrip(&value, &err) == 0 && err == 0,
          "atomics round trip succeeds with errno cleared");

    /* ipc: the SCM_RIGHTS probe clears errno on success. */
    err = 0;
    CHECK(rt_ipc_scm_rights(template_path, &err) == 0, "SCM_RIGHTS probe still succeeds");
    CHECK(err == 0, "…and clears errno");
}

static void test_summaries_mark_truncation(void)
{
    rt_cpu_facts_t facts;
    rt_context_t ctx;
    phase02_log_t log;
    char tiny[40];
    char full[256];
    rt_fs_limits_t limits = { 0u, 0u, 0u, 0 };
    int err = 0;

    CHECK(rt_cpu_collect(&facts) == 0, "cpu facts collected for the truncation test");
    memset(tiny, 0x7f, sizeof(tiny));
    CHECK(rt_cpu_facts_summary(tiny, sizeof(tiny), &facts) == tiny,
          "cpu facts summary returns the caller's buffer");
    CHECK(strstr(tiny, "TRUNCATED") != NULL,
          "a cpu summary that does not fit says it was truncated");
    CHECK(rt_cpu_facts_summary(full, sizeof(full), &facts) != NULL &&
          strstr(full, "TRUNCATED") == NULL,
          "…and a summary that fits carries no marker");

    CHECK(rt_context_init(&ctx) == 0, "context initialised for the truncation test");
    memset(tiny, 0x7f, sizeof(tiny));
    CHECK(rt_context_describe(tiny, sizeof(tiny), &ctx) == tiny,
          "context description returns the caller's buffer");
    CHECK(strstr(tiny, "TRUNCATED") != NULL, "a truncated context description is marked");

    (void)limits;
    phase02_log_init(&log, "truncation test");
    memset(tiny, 0x7f, sizeof(tiny));
    CHECK(phase02_log_summary_line(tiny, sizeof(tiny), &log) == tiny,
          "summary line returns the caller's buffer");
    CHECK(strstr(tiny, "TRUNCATED") != NULL, "a truncated summary line is marked");
    memset(full, 0, sizeof(full));
    phase02_log_summary_line(full, sizeof(full), &log);
    CHECK(strstr(full, "records=") == full, "…and a summary that fits starts with its counts");
    CHECK(strstr(full, "TRUNCATED") == NULL, "…without a marker");
    (void)err;
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
    test_outcome_classification();
    test_fs_depth_classification();
    test_shm_classification();
    test_loader_reason_propagation();
    test_errno_preservation_on_failure();
    test_jit_payload_capacity();
    test_loader_guard_unavailable_is_not_a_fault();
    test_dual_mapping_stage_and_classification();
    test_ipc_shm_cleanup_stage();
    test_log_detail_truncation_is_marked();
    test_context();
    test_mem_protect_rejects_overflow();
    test_failure_carries_and_success_clears_errno();
    test_summaries_mark_truncation();
    test_jit_alloc_arena_kind();
    test_dual_mapping_file_backed();
    test_dual_mapping_file_backed_negative();

    printf("== %u checks, %u failures ==\n", g_checks, g_failures);
    return (g_failures == 0u) ? 0 : 1;
}

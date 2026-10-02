/*
 * runtime_cpu_abi.c — host facts (CPU, ABI, page size, x18 policy) and the register note.
 *
 * PHASE_02_RECONSTRUCTED_POC (see runtime_platform.h).
 *
 * Every value here is measured or compile-time known for *this* process. The report
 * prints the platform name beside each sample; a Linux sample is never presented as
 * a Darwin/iOS result.
 */
#include "runtime_cpu_abi.h"
#include "runtime_platform.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int rt_cpu_detect_big_endian(void)
{
    const uint16_t probe = 0x0102u;
    unsigned char bytes[2];
    memcpy(bytes, &probe, sizeof(bytes));
    return (bytes[0] == 0x01u) ? 1 : 0;
}

static const char *rt_cpu_arch_name(void)
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

static const char *rt_cpu_abi_name(void)
{
#if defined(__APPLE__) && defined(__aarch64__)
    return "arm64-apple";
#elif defined(__APPLE__) && defined(__x86_64__)
    return "x86_64-apple";
#elif defined(__linux__) && defined(__aarch64__)
    return "aapcs64-linux";
#elif defined(__linux__) && defined(__x86_64__)
    return "sysv-amd64-linux";
#else
    return "unknown";
#endif
}

int rt_cpu_collect(rt_cpu_facts_t *out)
{
    if (out == NULL) {
        return -1;
    }
    memset(out, 0, sizeof(*out));
    out->arch = rt_cpu_arch_name();
    out->abi = rt_cpu_abi_name();
    out->platform = rt_platform_name();
    out->pointer_bits = (int)(sizeof(void *) * 8u);
    out->page_size = rt_platform_page_size();
    out->big_endian = rt_cpu_detect_big_endian();

    /* x18: the Apple arm64 ABI reserves it outright; elsewhere only if the build
     * asked the compiler to keep off it. Both facts are recorded separately so the
     * report never confuses "platform reserves" with "we told the compiler to". */
#if defined(__APPLE__) && defined(__aarch64__)
    out->x18_reserved_platform = 1;
#else
    out->x18_reserved_platform = 0;
#endif
    out->x18_reserved_build = RT_X18_RESERVED_BUILD;

    out->jit_capable = (rt_platform_capabilities() != 0u) ? 1 : 0;

#if defined(__linux__) && defined(__aarch64__)
    out->arm64_hwcap = rt_linux_arm64_hwcap();
    out->arm64_hwcap_valid = 1;
#else
    out->arm64_hwcap = 0u;
    out->arm64_hwcap_valid = 0;
#endif

#if defined(_SC_LEVEL1_DCACHE_LINESIZE)
    {
        long line = sysconf(_SC_LEVEL1_DCACHE_LINESIZE);
        out->cache_line = (line > 0) ? (int)line : 0;
    }
#else
    out->cache_line = 0;
#endif
    return 0;
}

const char *rt_cpu_facts_summary(char *buf, size_t cap, const rt_cpu_facts_t *facts)
{
    int written;

    if (buf == NULL || cap == 0u || facts == NULL) {
        return "";
    }
    written = snprintf(buf, cap,
                       "arch=%s abi=%s platform=%s ptr_bits=%d page=%d endian=%s "
                       "x18_platform_reserved=%d x18_build_reserved=%d hwcap=%s cache_line=%d",
                       facts->arch, facts->abi, facts->platform, facts->pointer_bits,
                       facts->page_size, facts->big_endian != 0 ? "big" : "little",
                       facts->x18_reserved_platform, facts->x18_reserved_build,
                       facts->arm64_hwcap_valid != 0 ? "available" : "n/a",
                       facts->cache_line);
    if (written < 0) {
        buf[0] = '\0';
    }
    return buf;
}

/* The port's register decision, recorded for the following phases. Fase 04 keeps
 * guest R8 in host x9; x18 is the platform register and is never a guest home. */
static const rt_guest_reg_note_t rt_guest_notes[] = {
    { "rax", "x0",  "return value / scratch" },
    { "rcx", "x1",  "scratch" },
    { "rdx", "x2",  "scratch" },
    { "rbx", "x3",  "callee-saved guest register" },
    { "rsp", "x4",  "guest stack pointer" },
    { "rbp", "x5",  "guest frame pointer" },
    { "rsi", "x6",  "scratch" },
    { "rdi", "x7",  "scratch" },
    { "r8",  "x9",  "moved out of x18 by Strategy A (Fase 04)" },
    { "r9",  "x10", "scratch" },
    { "r10", "x11", "scratch" },
    { "r11", "x12", "scratch" },
    { "r12", "x13", "scratch" },
    { "r13", "x14", "callee-saved guest register" },
    { "r14", "x15", "callee-saved guest register" },
    { "r15", "x16", "callee-saved guest register" },
    { "rip", "x17", "guest instruction pointer" }
};

size_t rt_cpu_guest_register_notes(const rt_guest_reg_note_t **notes)
{
    if (notes != NULL) {
        *notes = rt_guest_notes;
    }
    return sizeof(rt_guest_notes) / sizeof(rt_guest_notes[0]);
}

const char *rt_cpu_x18_policy_note(void)
{
    return "x18 is the platform register: reserved by arm64-apple-ios, the Windows TEB "
           "under _WIN32, unused on Linux. It is not a guest register in this port.";
}

/*
 * runtime_cpu_abi.h — host facts needed by the following phases (CPU, ABI, x18).
 *
 * PHASE_02_RECONSTRUCTED_POC (see runtime_platform.h).
 *
 * Scope warning that is part of the API contract: everything reported here describes
 * the machine this process runs on. A Linux AArch64 result never proves Darwin/iOS
 * behaviour; the harness records the platform name next to every sample for that
 * reason.
 */
#ifndef RUNTIME_CPU_ABI_H
#define RUNTIME_CPU_ABI_H

#include <stddef.h>
#include <stdint.h>

#include "runtime_platform.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Build-time fact: was the compiler told to keep its hands off x18?
 * Set by the build system (-DRT_X18_RESERVED_BUILD=1) when -ffixed-x18 is used. */
#ifndef RT_X18_RESERVED_BUILD
#define RT_X18_RESERVED_BUILD 0
#endif

typedef struct rt_cpu_facts {
    const char *arch;        /* compile-time architecture name */
    const char *abi;         /* coarse ABI name, e.g. "aapcs64-linux" / "arm64-apple" */
    const char *platform;    /* "linux" / "darwin" / "unknown" */
    int         pointer_bits;
    int         page_size;   /* measured with sysconf/_SC_PAGESIZE at runtime */
    int         big_endian;
    int         x18_reserved_platform; /* 1 when the platform reserves x18 outright */
    int         x18_reserved_build;    /* from RT_X18_RESERVED_BUILD */
    uint64_t    arm64_hwcap; /* Linux AArch64: AT_HWCAP; 0 elsewhere */
    int         arm64_hwcap_valid;
    int         jit_capable;  /* from rt_platform_capabilities(), not a promise */
    int         cache_line;   /* 0 when unknown */
} rt_cpu_facts_t;

int         rt_cpu_collect(rt_cpu_facts_t *out);
const char *rt_cpu_facts_summary(char *buf, size_t cap, const rt_cpu_facts_t *facts);

/* Inventory of the 16 guest GPRs as used by the ARM64 dynarec, plus the platform
 * register x18. This is a *report* of the mapping the port adopted (Fase 04 keeps
 * guest R8 in x9); it is not a register allocator. */
typedef struct rt_guest_reg_note {
    const char *guest;   /* "rax", "r8", ... */
    const char *host;    /* "x0", "x9", ... */
    const char *note;
} rt_guest_reg_note_t;

size_t                  rt_cpu_guest_register_notes(const rt_guest_reg_note_t **notes);
const char             *rt_cpu_x18_policy_note(void);

#ifdef __cplusplus
}
#endif

#endif /* RUNTIME_CPU_ABI_H */

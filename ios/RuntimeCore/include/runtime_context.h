/*
 * runtime_context.h — the minimum execution context the PoC needs to diagnose state.
 *
 * PHASE_02_RECONSTRUCTED_POC (see runtime_platform.h).
 *
 * Deliberately small: this is NOT a Linux-user emulation layer and does not try to
 * become one. It groups the facts and modules a run depends on so the report can
 * say "these facts, this platform, these modules" instead of anonymous numbers.
 */
#ifndef RUNTIME_CONTEXT_H
#define RUNTIME_CONTEXT_H

#include <stddef.h>
#include <stdint.h>

#include "runtime_platform.h"
#include "runtime_cpu_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RT_CONTEXT_MAX_MODULES 8u

typedef struct rt_context_module {
    char     name[64];
    void    *base;
    size_t   size;
    uint32_t entry_value;   /* what the module entry returned, when executed */
    int      executed;      /* 1 when entry_value is a real observation */
} rt_context_module_t;

typedef struct rt_context {
    const rt_platform_t *platform;
    rt_cpu_facts_t       facts;
    int                  page_size;
    unsigned             module_count;
    rt_context_module_t  modules[RT_CONTEXT_MAX_MODULES];
} rt_context_t;

int  rt_context_init(rt_context_t *ctx);
int  rt_context_add_module(rt_context_t *ctx, const char *name, void *base, size_t size);
int  rt_context_mark_executed(rt_context_t *ctx, const char *name, uint32_t value);
const rt_context_module_t *rt_context_find_module(const rt_context_t *ctx, const char *name);
const char *rt_context_describe(char *buf, size_t cap, const rt_context_t *ctx);

#ifdef __cplusplus
}
#endif

#endif /* RUNTIME_CONTEXT_H */

/*
 * runtime_context.c — the small execution-context object used by the report.
 *
 * PHASE_02_RECONSTRUCTED_POC (see runtime_platform.h).
 *
 * This is deliberately not a Linux-user emulation layer: it groups the facts and
 * the modules a run depends on so the report can name them instead of printing
 * anonymous numbers.
 */
#include "runtime_context.h"
#include "runtime_platform.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

int rt_context_init(rt_context_t *ctx)
{
    if (ctx == NULL) {
        return -1;
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->platform = rt_platform_current();
    if (rt_cpu_collect(&ctx->facts) != 0) {
        return -1;
    }
    ctx->page_size = ctx->facts.page_size;
    return 0;
}

int rt_context_add_module(rt_context_t *ctx, const char *name, void *base, size_t size)
{
    rt_context_module_t *slot;
    int written;

    if (ctx == NULL || name == NULL || ctx->module_count >= RT_CONTEXT_MAX_MODULES) {
        return -1;
    }
    slot = &ctx->modules[ctx->module_count];
    written = snprintf(slot->name, sizeof(slot->name), "%s", name);
    if (written < 0 || (size_t)written >= sizeof(slot->name)) {
        return -1;
    }
    slot->base = base;
    slot->size = size;
    slot->entry_value = 0u;
    slot->executed = 0;
    ctx->module_count++;
    return 0;
}

int rt_context_mark_executed(rt_context_t *ctx, const char *name, uint32_t value)
{
    unsigned index;

    if (ctx == NULL || name == NULL) {
        return -1;
    }
    for (index = 0u; index < ctx->module_count; index++) {
        if (strcmp(ctx->modules[index].name, name) == 0) {
            ctx->modules[index].entry_value = value;
            ctx->modules[index].executed = 1;
            return 0;
        }
    }
    return -1;
}

const rt_context_module_t *rt_context_find_module(const rt_context_t *ctx, const char *name)
{
    unsigned index;

    if (ctx == NULL || name == NULL) {
        return NULL;
    }
    for (index = 0u; index < ctx->module_count; index++) {
        if (strcmp(ctx->modules[index].name, name) == 0) {
            return &ctx->modules[index];
        }
    }
    return NULL;
}

const char *rt_context_describe(char *buf, size_t cap, const rt_context_t *ctx)
{
    char facts[256];
    int written;

    if (buf == NULL || cap == 0u || ctx == NULL) {
        return "";
    }
    (void)rt_cpu_facts_summary(facts, sizeof(facts), &ctx->facts);
    written = snprintf(buf, cap, "platform=%s modules=%u page=%d %s",
                       (ctx->platform != NULL) ? ctx->platform->name : "unknown",
                       ctx->module_count, ctx->page_size, facts);
    if (written < 0) {
        buf[0] = '\0';
    }
    return buf;
}

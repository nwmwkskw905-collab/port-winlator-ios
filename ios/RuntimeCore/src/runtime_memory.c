/*
 * runtime_memory.c — page API, fault probe and the platform selector.
 *
 * PHASE_02_RECONSTRUCTED_POC (see runtime_platform.h).
 */
#include "runtime_memory.h"
#include "runtime_signals.h"

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* ------------------------------------------------------------------ names */

const char *rt_status_name(rt_status_t status)
{
    switch (status) {
    case RT_PASS:           return "PASS";
    case RT_FAIL:           return "FAIL";
    case RT_BLOCKED:        return "BLOCKED";
    case RT_UNSUPPORTED:    return "UNSUPPORTED";
    case RT_UNTESTED:       return "UNTESTED";
    case RT_NOT_APPLICABLE: return "NOT_APPLICABLE";
    }
    return "UNKNOWN";
}

const char *rt_prot_name(rt_prot_t prot)
{
    if (prot == RT_PROT_NONE)  return "NONE";
    if (prot == RT_PROT_READ)  return "R";
    if (prot == RT_PROT_WRITE) return "W";
    if (prot == RT_PROT_EXEC)  return "X";
    if (prot == (RT_PROT_READ | RT_PROT_WRITE)) return "RW";
    if (prot == (RT_PROT_READ | RT_PROT_EXEC))  return "R-X";
    if (prot == (RT_PROT_WRITE | RT_PROT_EXEC)) return "W-X";
    if (prot == (RT_PROT_READ | RT_PROT_WRITE | RT_PROT_EXEC)) return "RWX";
    return "?";
}

/* ---------------------------------------------------------------- platform */

int rt_platform_apple_target(void)
{
#if defined(__APPLE__)
    return RT_APPLE_TARGET;
#else
    return RT_APPLE_TARGET_NONE;
#endif
}

const char *rt_platform_apple_target_name(void)
{
    switch (rt_platform_apple_target()) {
    case RT_APPLE_TARGET_MACOS:
        return "macos";
    case RT_APPLE_TARGET_IPHONE_DEVICE:
        return "ios-device";
    case RT_APPLE_TARGET_IPHONE_SIMULATOR:
        return "ios-simulator";
    case RT_APPLE_TARGET_UNKNOWN_APPLE:
        return "apple-unknown";
    case RT_APPLE_TARGET_NONE:
    default:
        return "none";
    }
}

int rt_platform_random_bytes(void *buffer, size_t length, int *err_out)
{
    const rt_platform_t *platform = rt_platform_current();
    if (platform == NULL || platform->random_bytes == NULL) {
        if (err_out != NULL) {
            *err_out = ENOTSUP;
        }
        return -1;
    }
    return platform->random_bytes(buffer, length, err_out);
}

int rt_platform_unique_shm_name(char *out, size_t capacity, const char *prefix, int *err_out)
{
    static const unsigned char zero[8] = { 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u };
    unsigned char random_bytes[8];
    int written;

    if (out == NULL || prefix == NULL || capacity == 0u) {
        if (err_out != NULL) {
            *err_out = EINVAL;
        }
        return -1;
    }
    if (rt_platform_random_bytes(random_bytes, sizeof(random_bytes), err_out) != 0) {
        out[0] = '\0';
        return -1;   /* no unpredictable name -> the caller must fail, never guess */
    }
    if (memcmp(random_bytes, zero, sizeof(zero)) == 0) {
        /* A source that returns nothing but zeros is not a source. One in 2^64, but
         * silently accepting it would be exactly the kind of "looks fine" fallback
         * this project refuses to ship. */
        if (err_out != NULL) {
            *err_out = EIO;
        }
        out[0] = '\0';
        return -1;
    }
    written = snprintf(out, capacity, "%s_%ld_%02x%02x%02x%02x%02x%02x%02x%02x",
                       prefix, (long)getpid(),
                       random_bytes[0], random_bytes[1], random_bytes[2], random_bytes[3],
                       random_bytes[4], random_bytes[5], random_bytes[6], random_bytes[7]);
    if (written < 0 || (size_t)written >= capacity) {
        out[0] = '\0';
        if (err_out != NULL) {
            *err_out = ENAMETOOLONG;
        }
        return -1;
    }
    if (err_out != NULL) {
        *err_out = 0;
    }
    return 0;
}

const rt_platform_t *rt_platform_current(void)
{
#if defined(__APPLE__)
    return &rt_platform_darwin;
#elif defined(__linux__)
    return &rt_platform_linux;
#else
    return &rt_platform_linux;
#endif
}

const char *rt_platform_name(void)
{
    const rt_platform_t *platform = rt_platform_current();
    return (platform != NULL && platform->name != NULL) ? platform->name : "unknown";
}

int rt_platform_page_size(void)
{
    const rt_platform_t *platform = rt_platform_current();
    if (platform == NULL || platform->page_size == NULL) {
        return 4096;
    }
    return platform->page_size();
}

uint32_t rt_platform_capabilities(void)
{
    const rt_platform_t *platform = rt_platform_current();
    if (platform == NULL || platform->capabilities == NULL) {
        return 0u;
    }
    return platform->capabilities();
}

int rt_platform_is_darwin(void)
{
    return (rt_platform_current() == &rt_platform_darwin) ? 1 : 0;
}

int rt_platform_is_apple(void)
{
#if defined(__APPLE__)
    return 1;
#else
    return 0;
#endif
}

/* ------------------------------------------------------------------ memory */

void *rt_mem_reserve(size_t len, int *err_out)
{
    const rt_platform_t *platform = rt_platform_current();
    size_t page = (size_t)rt_platform_page_size();
    size_t rounded;

    if (len == 0) {
        if (err_out != NULL) {
            *err_out = EINVAL;
        }
        return NULL;
    }
    /* round up without overflowing */
    if (len > SIZE_MAX - page) {
        if (err_out != NULL) {
            *err_out = EOVERFLOW;
        }
        return NULL;
    }
    rounded = ((len + page - 1u) / page) * page;
    return platform->mem_map(rounded, RT_PROT_READ | RT_PROT_WRITE, err_out);
}

int rt_mem_protect(void *addr, size_t len, rt_prot_t prot, int *err_out)
{
    const rt_platform_t *platform = rt_platform_current();
    size_t page = (size_t)rt_platform_page_size();
    size_t rounded;

    if (addr == NULL || len == 0) {
        if (err_out != NULL) {
            *err_out = EINVAL;
        }
        return -1;
    }
    /* The same overflow guard rt_mem_reserve() already had: without it, len + page - 1
     * wraps and the rounded length becomes tiny, so the call would protect far fewer bytes
     * than it was asked to while reporting success — a silent protection shortfall on the
     * caller's assumption that the whole region changed. */
    if (len > SIZE_MAX - page) {
        if (err_out != NULL) {
            *err_out = EOVERFLOW;
        }
        return -1;
    }
    rounded = ((len + page - 1u) / page) * page;
    return platform->mem_protect(addr, rounded, prot, err_out);
}

int rt_mem_release(void *addr, size_t len, int *err_out)
{
    const rt_platform_t *platform = rt_platform_current();
    if (addr == NULL) {
        if (err_out != NULL) {
            *err_out = EINVAL;
        }
        return -1;
    }
    return platform->mem_unmap(addr, len, err_out);
}

int rt_mem_read_probe(const void *addr, size_t len)
{
    const unsigned char *bytes = (const unsigned char *)addr;
    size_t page = (size_t)rt_platform_page_size();
    size_t offset;
    volatile unsigned char sink = 0;

    if (addr == NULL || len == 0) {
        return -1;
    }
    for (offset = 0; offset < len; offset += page) {
        sink = (unsigned char)(sink ^ bytes[offset]);
    }
    /* touch the last byte as well: a region that is only partly mapped must fail */
    sink = (unsigned char)(sink ^ bytes[len - 1u]);
    (void)sink; /* the value is irrelevant: reaching this line is the result */
    return 0;
}

int rt_mem_fault_probe(void *addr, int write, void **fault_addr_out, int *err_out)
{
    rt_fault_guard_t guard;
    int rc;

    if (fault_addr_out != NULL) {
        *fault_addr_out = NULL;
    }
    rc = rt_fault_guard_begin(&guard, err_out);
    if (rc != 0) {
        return -2;
    }
    guard.armed = 1;
    if (RT_FAULT_GUARD_TRY(&guard)) {
        if (write != 0) {
            *(volatile unsigned char *)addr = 0x5Au;
        } else {
            volatile unsigned char sink = *(volatile unsigned char *)addr;
            (void)sink;
        }
        (void)rt_fault_guard_end(&guard);
        return 0;
    }
    if (fault_addr_out != NULL) {
        *fault_addr_out = rt_fault_guard_addr(&guard);
    }
    (void)rt_fault_guard_end(&guard);
    return -1;
}

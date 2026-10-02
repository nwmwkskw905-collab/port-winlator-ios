/*
 * runtime_loader.c — the experimental in-process module loader.
 *
 * PHASE_02_RECONSTRUCTED_POC (see runtime_platform.h).
 *
 * Security posture: validation is pure and happens before any allocation or
 * execution; every field is range-checked against the image size; a rejected module
 * never reaches executable memory.
 */
#include "runtime_loader.h"
#include "runtime_jit.h"
#include "runtime_memory.h"
#include "runtime_signals.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

const char *rt_loader_error_name(rt_loader_error_t err)
{
    switch (err) {
    case RT_LOADER_OK:             return "OK";
    case RT_LOADER_ERR_TOO_SMALL:  return "TOO_SMALL";
    case RT_LOADER_ERR_MAGIC:      return "BAD_MAGIC";
    case RT_LOADER_ERR_VERSION:    return "BAD_VERSION";
    case RT_LOADER_ERR_FLAGS:      return "BAD_FLAGS";
    case RT_LOADER_ERR_RANGE:      return "OUT_OF_RANGE";
    case RT_LOADER_ERR_CODE_SIZE:  return "BAD_CODE_SIZE";
    case RT_LOADER_ERR_ENTRY:      return "BAD_ENTRY";
    }
    return "UNKNOWN";
}

rt_loader_error_t rt_loader_validate(const uint8_t *image, size_t size,
                                     rt_module_header_t *header_out)
{
    rt_module_header_t header;

    if (image == NULL || size < (size_t)RT_MODULE_HEADER_SIZE) {
        return RT_LOADER_ERR_TOO_SMALL;
    }
    memcpy(&header, image, sizeof(header));

    if (header.magic != RT_MODULE_MAGIC) {
        return RT_LOADER_ERR_MAGIC;
    }
    if (header.version != RT_MODULE_VERSION) {
        return RT_LOADER_ERR_VERSION;
    }
    if (header.flags != 0u) {
        return RT_LOADER_ERR_FLAGS;
    }
    if (header.code_len == 0u || header.code_len > RT_MODULE_MAX_CODE) {
        return RT_LOADER_ERR_CODE_SIZE;
    }
    /* Overflow-safe range check: code_off + code_len must fit inside the image.
     * (uint64 arithmetic keeps a hostile pair from wrapping past the test.) */
    if ((uint64_t)header.code_off + (uint64_t)header.code_len > (uint64_t)size) {
        return RT_LOADER_ERR_RANGE;
    }
    if (header.code_off < RT_MODULE_HEADER_SIZE) {
        return RT_LOADER_ERR_RANGE; /* the code must follow the header */
    }
    if (header.entry_off >= header.code_len) {
        return RT_LOADER_ERR_ENTRY;
    }
    if (header_out != NULL) {
        *header_out = header;
    }
    return RT_LOADER_OK;
}

size_t rt_loader_build_return_image(uint8_t *out, size_t cap, uint32_t imm)
{
    rt_module_header_t header;
    size_t code_len = 0u;

    if (out == NULL || cap < (size_t)RT_MODULE_MAX_IMAGE) {
        return 0u;
    }
    if (rt_jit_emit_return_imm(out + RT_MODULE_HEADER_SIZE,
                               cap - (size_t)RT_MODULE_HEADER_SIZE, imm, &code_len) != 0) {
        return 0u;
    }
    header.magic = RT_MODULE_MAGIC;
    header.version = RT_MODULE_VERSION;
    header.code_off = RT_MODULE_HEADER_SIZE;
    header.code_len = (uint32_t)code_len;
    header.entry_off = 0u;
    header.flags = 0u;
    memcpy(out, &header, sizeof(header));
    return (size_t)RT_MODULE_HEADER_SIZE + code_len;
}

rt_status_t rt_loader_run(const uint8_t *image, size_t size, uint32_t *value_out,
                          rt_loader_error_t *loader_err_out, void **fault_addr_out)
{
    rt_module_header_t header;
    rt_loader_error_t err;
    void *arena = NULL;
    void *target = NULL;
    void *fault = NULL;
    uint32_t value = 0u;
    int io_err = 0;
    int rc;
    rt_jit_fn_t fn;

    if (fault_addr_out != NULL) {
        *fault_addr_out = NULL;
    }
    if (loader_err_out != NULL) {
        *loader_err_out = RT_LOADER_OK;
    }

    err = rt_loader_validate(image, size, &header);
    if (err != RT_LOADER_OK) {
        if (loader_err_out != NULL) {
            *loader_err_out = err;
        }
        return RT_FAIL;
    }

    arena = rt_jit_alloc(header.code_len, &io_err, NULL);
    if (arena == NULL) {
        return RT_FAIL;
    }
    memcpy(arena, image + header.code_off, header.code_len);
    if (rt_jit_invalidate(arena, header.code_len) != 0) {
        (void)rt_jit_free(arena, header.code_len);
        return RT_FAIL;
    }
    if (rt_mem_protect(arena, header.code_len, RT_PROT_READ | RT_PROT_EXEC, &io_err) != 0) {
        /* Writable but not executable: report it as blocked, not as a failure of the
         * module, because the module itself is valid. */
        (void)rt_jit_free(arena, header.code_len);
        return RT_BLOCKED;
    }

    target = (void *)((uint8_t *)arena + header.entry_off);
    memcpy(&fn, &target, sizeof(fn));
    rc = rt_signal_call_guarded(fn, &value, &fault, &io_err);
    (void)rt_jit_free(arena, header.code_len);

    if (rc != 0) {
        if (fault_addr_out != NULL) {
            *fault_addr_out = fault;
        }
        return RT_BLOCKED;
    }
    if (value_out != NULL) {
        *value_out = value;
    }
    return RT_PASS;
}

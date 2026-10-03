/*
 * runtime_loader.h — the experimental in-process module loader.
 *
 * PHASE_02_RECONSTRUCTED_POC (see runtime_platform.h).
 *
 * This is NOT a PE loader and has nothing to do with Wine. It is the minimal
 * experiment the specification asks for: bytes -> validation -> executable memory
 * -> relocation-free entry -> execution, with hostile input rejected safely.
 *
 * Module layout (all fields little-endian, no padding):
 *
 *   offset  size  field
 *   0       4     magic        'RTM1' (0x52544D31)
 *   4       4     version      must be 1
 *   8       4     code_off     offset of the code blob
 *   12      4     code_len     length of the code blob (<= RT_MODULE_MAX_CODE)
 *   16      4     entry_off    offset of the entry point inside the code blob
 *   20      4     flags        must be 0
 *   24      ...   code blob
 */
#ifndef RUNTIME_LOADER_H
#define RUNTIME_LOADER_H

#include <stddef.h>
#include <stdint.h>

#include "runtime_platform.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RT_MODULE_MAGIC 0x52544D31u  /* 'R','T','M','1' */
#define RT_MODULE_VERSION 1u
#define RT_MODULE_HEADER_SIZE 24u
#define RT_MODULE_MAX_CODE 4096u
#define RT_MODULE_MAX_IMAGE (RT_MODULE_HEADER_SIZE + RT_MODULE_MAX_CODE)

typedef struct rt_module_header {
    uint32_t magic;
    uint32_t version;
    uint32_t code_off;
    uint32_t code_len;
    uint32_t entry_off;
    uint32_t flags;
} rt_module_header_t;

/* Reason for a rejection; RT_LOADER_OK == 0. */
typedef enum {
    RT_LOADER_OK = 0,
    RT_LOADER_ERR_TOO_SMALL,
    RT_LOADER_ERR_MAGIC,
    RT_LOADER_ERR_VERSION,
    RT_LOADER_ERR_FLAGS,
    RT_LOADER_ERR_RANGE,      /* code_off/code_len outside the image */
    RT_LOADER_ERR_CODE_SIZE,  /* code_len == 0 or > RT_MODULE_MAX_CODE */
    RT_LOADER_ERR_ENTRY,      /* entry_off >= code_len */
    RT_LOADER_ERR_JIT_UNAVAILABLE, /* the module is VALID and the platform refused the
                                    * executable arena (errno reported separately): the
                                    * image was not rejected — it cannot be executed here */
    RT_LOADER_ERR_EXEC_FAULT, /* the entry point ran and faulted (si_addr reported) */
    RT_LOADER_ERR_INTERNAL    /* our own step failed after a valid image was accepted */
} rt_loader_error_t;

const char *rt_loader_error_name(rt_loader_error_t err);

/* Pure validation, no allocation, no execution. */
rt_loader_error_t rt_loader_validate(const uint8_t *image, size_t size,
                                     rt_module_header_t *header_out);

/* Full load-and-run: validates, copies into executable memory, flushes the icache,
 * calls the entry point (guarded, so a fault is reported instead of crashing the
 * process) and returns the value produced by the generated code.
 *
 * Returns:
 *    RT_PASS  code executed, *value_out holds the result;
 *    RT_FAIL  the image was rejected (*loader_err_out says why);
 *    RT_BLOCKED a fault was caught while executing (the process may not execute
 *             generated memory); *fault_addr_out holds si_addr;
 *    RT_UNSUPPORTED this ISA has no emitter and no loader path. */
rt_status_t rt_loader_run(const uint8_t *image, size_t size, uint32_t *value_out,
                          rt_loader_error_t *loader_err_out, void **fault_addr_out);

/* Same call, also reporting the errno of the failing step and whether the MAP_JIT path was
 * attempted. Physical run #1 (iPhone 13, 2026-10-02) recorded
 *     loader.run_valid_module = FAIL  "rejected: OK"
 * for a structurally valid module: the arena allocation had failed, the reason was never
 * written (so it stayed RT_LOADER_OK and printed as "OK") and the errno was lost.
 *
 * Contract, enforced by the unit tests:
 *   - every non-RT_PASS return carries a reason that is NOT RT_LOADER_OK;
 *   - RT_LOADER_ERR_JIT_UNAVAILABLE means "valid module, no executable memory here" and
 *     comes back as RT_BLOCKED (never as RT_FAIL): the image was accepted, the capability
 *     was not. RT_FAIL is reserved for a rejected image or a defect of this code;
 *   - *os_err_out (when non-NULL) holds the errno of the failing step, or 0 when the step
 *     was not a syscall. */
rt_status_t rt_loader_run_ex(const uint8_t *image, size_t size, uint32_t *value_out,
                             rt_loader_error_t *loader_err_out, void **fault_addr_out,
                             int *os_err_out, int *map_jit_attempted_out);

/* Builds a well-formed image containing the ISA's `return imm` payload.
 * Returns the image length, or 0 when it does not fit / the ISA is unsupported. */
size_t rt_loader_build_return_image(uint8_t *out, size_t cap, uint32_t imm);

#ifdef __cplusplus
}
#endif

#endif /* RUNTIME_LOADER_H */

/*
 * runtime_filesystem.h — POSIX filesystem semantics the future runtime depends on.
 *
 * PHASE_02_RECONSTRUCTED_POC (see runtime_platform.h).
 *
 * The root directory is always supplied by the caller (the harness uses a temporary
 * directory). No Android-specific assumption is baked in: iOS sandbox paths are
 * whatever the caller passes, and results obtained on a Linux host are host results.
 */
#ifndef RUNTIME_FILESYSTEM_H
#define RUNTIME_FILESYSTEM_H

#include <stddef.h>
#include <stdint.h>

#include "runtime_platform.h"

#ifdef __cplusplus
extern "C" {
#endif

/* create -> write -> read back -> rename -> read again -> unlink, all inside root. */
int rt_fs_roundtrip(const char *root, int *err_out);

/* Nested directories up to ~1024 characters total; *path_len_out reports the
 * deepest path length that was actually created. */
int rt_fs_deep_paths(const char *root, size_t depth, size_t *path_len_out, int *err_out);

/* symlink + chmod + stat follow/not-follow semantics. */
int rt_fs_links_and_modes(const char *root, int *err_out);

/* mkstemp + unlink while open + close (the pattern used for runtime temp files). */
int rt_fs_temp_file(char *path_out, size_t path_cap, int *err_out);

/* Reported by statvfs on the filesystem holding root; the numbers are host facts. */
int rt_fs_capacity(const char *root, uint64_t *free_bytes_out, int *err_out);

#ifdef __cplusplus
}
#endif

#endif /* RUNTIME_FILESYSTEM_H */

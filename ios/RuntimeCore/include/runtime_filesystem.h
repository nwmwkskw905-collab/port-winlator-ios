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

/* Nested directories, one component per level. *path_len_out reports the deepest
 * path length that was actually created. The caller decides the depth: there is no
 * portable constant. Use rt_fs_limits_query() first — the reachable depth is a
 * function of the platform limit (PATH_MAX: 4096 on Linux, 1024 on Darwin), of the
 * length of root, and of this implementation's own path buffer (path_cap).
 *
 * On failure the function removes whatever it created before returning, so a failed
 * probe never leaves a partial tree behind (a leftover tree would make the next
 * probe fail with EEXIST instead of the real limit). */
int rt_fs_deep_paths(const char *root, size_t depth, size_t *path_len_out, int *err_out);

/* Which step of the deep-path probe failed. Physical run #1 (iPhone 13, 2026-10-02)
 * reported "unexpected errno=0 (Undefined error: 0) at depth=103": the failing call's
 * errno had been thrown away by the error path, so the record could not say WHAT failed
 * or WHY. The stage removes that blindness: the probe reports the step and the errno of
 * the failing call, captured immediately after it. */
typedef enum {
    RT_FS_STAGE_NONE = 0,     /* success */
    RT_FS_STAGE_ROOT,         /* root rejected: NULL, empty depth, or longer than the buffer */
    RT_FS_STAGE_SEGMENT,      /* a level name could not be formatted (never expected) */
    RT_FS_STAGE_PATH_CAP,     /* this implementation's own path buffer is the limit */
    RT_FS_STAGE_MKDIR,        /* mkdir(2) refused */
    RT_FS_STAGE_LEAF_JOIN,    /* composing <path>/leaf.bin overflowed the buffer */
    RT_FS_STAGE_LEAF_WRITE,   /* open(2)/write(2)/close(2) of leaf.bin failed */
    RT_FS_STAGE_LEAF_READ,    /* read-back failed */
    RT_FS_STAGE_LEAF_COMPARE, /* read-back succeeded and the content differed (defect) */
    RT_FS_STAGE_LEAF_UNLINK,  /* unlink(2) failed */
    RT_FS_STAGE_ERRNO_LOST    /* invariant breach: a failure returned errno 0 (defect) */
} rt_fs_stage_t;

const char *rt_fs_stage_name(rt_fs_stage_t stage);

/* Same probe, reporting the failing stage. On failure *err_out is always non-zero: an
 * error path that cannot name its errno sets EIO and RT_FS_STAGE_ERRNO_LOST rather than
 * reporting a zero. */
int rt_fs_deep_paths_ex(const char *root, size_t depth, size_t *path_len_out, int *err_out,
                        rt_fs_stage_t *stage_out);

/* The three limits that decide how deep a path may go, so callers can measure
 * capacity instead of assuming a depth that only holds on one platform. */
typedef struct rt_fs_limits {
    size_t path_max;        /* effective _PC_PATH_MAX for the filesystem holding root */
    size_t name_max;        /* effective _PC_NAME_MAX */
    size_t path_cap;        /* this implementation's own path buffer (RT_FS_PATH_CAP) */
    int    path_max_from_pathconf; /* 1 = measured, 0 = compile-time PATH_MAX fallback */
} rt_fs_limits_t;

int rt_fs_limits_query(const char *root, rt_fs_limits_t *out, int *err_out);

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

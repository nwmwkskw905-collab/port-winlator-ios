/*
 * runtime_filesystem.c — POSIX filesystem semantics used by the future runtime.
 *
 * PHASE_02_RECONSTRUCTED_POC (see runtime_platform.h).
 */
#include "runtime_filesystem.h"

#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>   /* PATH_MAX, NAME_MAX: the documented fallback */
#include <sys/stat.h>
/* statvfs is POSIX and present on the Linux and macOS SDKs. Some Apple SDKs ship
 * statfs (sys/mount.h) without statvfs; __has_include keeps the same code path where
 * the header exists and provides the Darwin fallback where it does not. */
#if __has_include(<sys/statvfs.h>)
#include <sys/statvfs.h>
#define RT_FS_HAVE_STATVFS 1
#elif defined(__APPLE__)
#include <sys/mount.h>
#include <sys/param.h>
#define RT_FS_HAVE_STATFS 1
#else
#define RT_FS_HAVE_STATVFS 1
#endif
#include <sys/types.h>
#include <unistd.h>

#define RT_FS_PATH_CAP 2048u

static const unsigned char rt_fs_pattern[6] = { 'R', 'T', '0', '2', 0x00u, 0xFFu };

static int rt_fs_join(char *out, size_t cap, const char *root, const char *leaf)
{
    int written = snprintf(out, cap, "%s/%s", root, leaf);
    if (written < 0 || (size_t)written >= cap) {
        return -1;
    }
    return 0;
}

/* Writes the pattern to `path`. The failure reason travels through err_out and is
 * captured *immediately* after the failing call: nothing else runs between the call and
 * the capture, so no intermediate call can overwrite or clear errno. This is the API the
 * deep-path probe needs — physical run #1 reported "errno=0 (Undefined error: 0)" because
 * the caller read its own not-yet-written out-parameter instead of the failing call's
 * errno (see rt_fs_deep_paths_ex). err_out is always non-zero on failure: a short write is
 * reported as EIO rather than as "no error". */
static int rt_fs_write_pattern(const char *path, int *err_out)
{
    int fd = open(path, O_CREAT | O_TRUNC | O_WRONLY, S_IRUSR | S_IWUSR);
    ssize_t written;
    if (fd < 0) {
        if (err_out != NULL) {
            *err_out = errno;
        }
        return -1;
    }
    written = write(fd, rt_fs_pattern, sizeof(rt_fs_pattern));
    if (written != (ssize_t)sizeof(rt_fs_pattern)) {
        int saved = (written < 0) ? errno : EIO;
        (void)close(fd);
        if (err_out != NULL) {
            *err_out = saved;
        }
        return -1;
    }
    if (close(fd) != 0) {
        if (err_out != NULL) {
            *err_out = errno;
        }
        return -1;
    }
    if (err_out != NULL) {
        *err_out = 0;
    }
    return 0;
}

static int rt_fs_read_pattern(const char *path, int *err_out)
{
    unsigned char buffer[sizeof(rt_fs_pattern)];
    int fd = open(path, O_RDONLY);
    ssize_t got;
    int equal;

    if (fd < 0) {
        if (err_out != NULL) {
            *err_out = errno;
        }
        return -1;
    }
    got = read(fd, buffer, sizeof(buffer));
    if (got != (ssize_t)sizeof(buffer)) {
        int saved = (got < 0) ? errno : EIO;   /* captured before close() touches anything */
        (void)close(fd);
        if (err_out != NULL) {
            *err_out = saved;
        }
        return -1;
    }
    (void)close(fd);
    equal = (memcmp(buffer, rt_fs_pattern, sizeof(buffer)) == 0) ? 1 : 0;
    if (equal == 0 && err_out != NULL) {
        *err_out = EILSEQ;
    }
    return equal;
}

int rt_fs_roundtrip(const char *root, int *err_out)
{
    char path[RT_FS_PATH_CAP];
    char renamed[RT_FS_PATH_CAP];
    struct stat info;
    int rc = -1;

    if (root == NULL) {
        if (err_out != NULL) {
            *err_out = EINVAL;
        }
        return -1;
    }
    if (rt_fs_join(path, sizeof(path), root, "rt_roundtrip.bin") != 0 ||
        rt_fs_join(renamed, sizeof(renamed), root, "rt_roundtrip.renamed") != 0) {
        if (err_out != NULL) {
            *err_out = ENAMETOOLONG;
        }
        return -1;
    }

    if (rt_fs_write_pattern(path, err_out) != 0) {
        return -1;
    }
    if (rt_fs_read_pattern(path, err_out) != 1) {
        return -1;
    }
    if (rename(path, renamed) != 0) {
        if (err_out != NULL) {
            *err_out = errno;
        }
        return -1;
    }
    if (stat(path, &info) == 0) {
        if (err_out != NULL) {
            *err_out = EEXIST; /* the old name must be gone after rename() */
        }
        return -1;
    }
    if (rt_fs_read_pattern(renamed, err_out) != 1) {
        return -1;
    }
    if (unlink(renamed) != 0) {
        if (err_out != NULL) {
            *err_out = errno;
        }
        return -1;
    }
    if (access(renamed, F_OK) == 0) {
        if (err_out != NULL) {
            *err_out = EEXIST;
        }
        return -1;
    }
    rc = 0;
    if (err_out != NULL) {
        *err_out = 0;
    }
    return rc;
}

int rt_fs_limits_query(const char *root, rt_fs_limits_t *out, int *err_out)
{
    long value;

    if (root == NULL || out == NULL) {
        if (err_out != NULL) {
            *err_out = EINVAL;
        }
        return -1;
    }
    out->path_max = (size_t)PATH_MAX;
    out->name_max = (size_t)NAME_MAX;
    out->path_cap = (size_t)RT_FS_PATH_CAP;
    out->path_max_from_pathconf = 0;
    if (err_out != NULL) {
        *err_out = 0;
    }

    errno = 0;
    value = pathconf(root, _PC_PATH_MAX);
    if (value > 0) {
        out->path_max = (size_t)value;
        out->path_max_from_pathconf = 1;
    }
    errno = 0;
    value = pathconf(root, _PC_NAME_MAX);
    if (value > 0) {
        out->name_max = (size_t)value;
    }
    /* A pathconf that fails is not fatal here: the compile-time PATH_MAX/NAME_MAX
     * numbers from <limits.h> are a documented, portable answer. Which one was used
     * is reported through path_max_from_pathconf so the caller can say so. */
    return 0;
}

static void rt_fs_remove_chain(char *path, size_t length, size_t stop_length)
{
    /* Remove directories from the deepest one back to (not including) stop_length.
     * Best effort by design: called on the success path AND on the failure path so a
     * failed probe cannot leave a tree that makes the next probe fail with EEXIST. */
    while (length > stop_length) {
        if (rmdir(path) != 0) {
            return;
        }
        while (length > 0u && path[length] != '/') {
            length--;
        }
        if (length <= stop_length) {
            return;
        }
        path[length] = '\0';
    }
}

const char *rt_fs_stage_name(rt_fs_stage_t stage)
{
    switch (stage) {
    case RT_FS_STAGE_NONE:         return "none";
    case RT_FS_STAGE_ROOT:         return "root";
    case RT_FS_STAGE_SEGMENT:      return "segment-name";
    case RT_FS_STAGE_PATH_CAP:     return "probe-path-buffer";
    case RT_FS_STAGE_MKDIR:        return "mkdir";
    case RT_FS_STAGE_LEAF_JOIN:    return "leaf-path-join";
    case RT_FS_STAGE_LEAF_WRITE:   return "leaf-write";
    case RT_FS_STAGE_LEAF_READ:    return "leaf-read";
    case RT_FS_STAGE_LEAF_COMPARE: return "leaf-compare";
    case RT_FS_STAGE_LEAF_UNLINK:  return "leaf-unlink";
    case RT_FS_STAGE_ERRNO_LOST:   return "errno-lost";
    }
    return "unknown";
}

int rt_fs_deep_paths_ex(const char *root, size_t depth, size_t *path_len_out, int *err_out,
                        rt_fs_stage_t *stage_out)
{
    char path[RT_FS_PATH_CAP];
    size_t root_length;
    size_t length;
    size_t level;
    int rc = 0;
    int failure = 0;
    rt_fs_stage_t stage = RT_FS_STAGE_NONE;

    if (stage_out != NULL) {
        *stage_out = RT_FS_STAGE_NONE;
    }
    if (root == NULL || depth == 0u) {
        if (stage_out != NULL) {
            *stage_out = RT_FS_STAGE_ROOT;
        }
        if (err_out != NULL) {
            *err_out = EINVAL;
        }
        return -1;
    }
    root_length = strlen(root);
    if (root_length + 2u >= sizeof(path)) {
        if (stage_out != NULL) {
            *stage_out = RT_FS_STAGE_ROOT;
        }
        if (err_out != NULL) {
            *err_out = ENAMETOOLONG;
        }
        return -1;
    }
    memcpy(path, root, root_length);
    path[root_length] = '\0';
    length = root_length;

    for (level = 0u; level < depth; level++) {
        char segment[16];
        int written = snprintf(segment, sizeof(segment), "d%07u", (unsigned)level);
        size_t candidate;
        if (written < 0 || (size_t)written >= sizeof(segment)) {
            stage = RT_FS_STAGE_SEGMENT;
            failure = EINVAL;
            rc = -1;
            break;
        }
        candidate = length + 1u + (size_t)written;
        if (candidate + 1u >= sizeof(path)) {
            stage = RT_FS_STAGE_PATH_CAP;   /* this implementation's own buffer is the limit */
            failure = ENAMETOOLONG;
            rc = -1;
            break;
        }
        path[length] = '/';
        memcpy(path + length + 1u, segment, (size_t)written + 1u);
        if (mkdir(path, S_IRWXU) != 0) {
            /* Do not commit the segment that was not created: the teardown below
             * must only walk directories that exist. */
            int saved = errno;              /* captured immediately, before anything else */
            path[length] = '\0';
            stage = RT_FS_STAGE_MKDIR;
            failure = saved;
            rc = -1;
            break;
        }
        length = candidate;
    }

    if (rc == 0) {
        char leaf[RT_FS_PATH_CAP];
        int write_err = 0;
        if (rt_fs_join(leaf, sizeof(leaf), path, "leaf.bin") != 0) {
            stage = RT_FS_STAGE_LEAF_JOIN;
            failure = ENAMETOOLONG;
            rc = -1;
        } else if (rt_fs_write_pattern(leaf, &write_err) != 0) {
            /* write_err comes from the failing call itself. The out-parameter of THIS
             * function is not read here: it has not been written yet, and reading it is
             * what produced "errno=0 (Undefined error: 0)" on the iPhone 13
             * (IPHONE13_PHYSICAL_RUN_01). */
            stage = RT_FS_STAGE_LEAF_WRITE;
            failure = (write_err != 0) ? write_err : EIO;
            rc = -1;
        } else if (rt_fs_read_pattern(leaf, &failure) != 1) {
            stage = (failure == EILSEQ) ? RT_FS_STAGE_LEAF_COMPARE : RT_FS_STAGE_LEAF_READ;
            if (failure == 0) {
                failure = EIO;
            }
            rc = -1;
        } else if (unlink(leaf) != 0) {
            stage = RT_FS_STAGE_LEAF_UNLINK;
            failure = errno;
            rc = -1;
        } else if (path_len_out != NULL) {
            *path_len_out = length;
        }
    }

    /* Teardown, deepest first — on both paths. */
    rt_fs_remove_chain(path, length, root_length);

    if (rc != 0) {
        if (failure == 0) {
            /* Invariant: a failure always carries a non-zero errno. If a future edit breaks
             * that, the record says so instead of printing "errno=0" as if it were a
             * platform answer. RT_FS_STAGE_ERRNO_LOST is never reached by the code above. */
            stage = RT_FS_STAGE_ERRNO_LOST;
            failure = EIO;
        }
        if (stage_out != NULL) {
            *stage_out = stage;
        }
        if (err_out != NULL) {
            *err_out = failure;
        }
        return -1;
    }
    if (err_out != NULL) {
        *err_out = 0;
    }
    return 0;
}

int rt_fs_deep_paths(const char *root, size_t depth, size_t *path_len_out, int *err_out)
{
    return rt_fs_deep_paths_ex(root, depth, path_len_out, err_out, NULL);
}

int rt_fs_links_and_modes(const char *root, int *err_out)
{
    char target[RT_FS_PATH_CAP];
    char link[RT_FS_PATH_CAP];
    struct stat target_info;
    struct stat link_info;
    mode_t mode;
    int rc = -1;

    if (rt_fs_join(target, sizeof(target), root, "rt_link_target.bin") != 0 ||
        rt_fs_join(link, sizeof(link), root, "rt_link_view.bin") != 0) {
        if (err_out != NULL) {
            *err_out = ENAMETOOLONG;
        }
        return -1;
    }
    if (rt_fs_write_pattern(target, err_out) != 0) {
        /* rt_fs_write_pattern already wrote the errno of the failing call. Reading `errno`
         * again here — after that function may have run further successful calls such as
         * close() — replaced a captured errno with an unrelated one, exactly the class of
         * mistake that produced "errno=0 (Undefined error: 0)" in IPHONE13_PHYSICAL_RUN_01. */
        return -1;
    }
    (void)unlink(link);
    if (symlink(target, link) != 0) {
        if (err_out != NULL) {
            *err_out = errno;
        }
        goto cleanup_target;
    }
    if (lstat(link, &link_info) != 0) {
        if (err_out != NULL) {
            *err_out = errno;
        }
        goto cleanup_both;
    }
    if (!S_ISLNK(link_info.st_mode)) {
        /* lstat() succeeded: this is not a syscall failure, so there is no errno to report.
         * Reading errno here would have published whatever the previous call left behind —
         * including 0, which this project never presents as a platform answer. */
        if (err_out != NULL) {
            *err_out = EINVAL;
        }
        goto cleanup_both;
    }
    if (stat(link, &target_info) != 0) {
        if (err_out != NULL) {
            *err_out = errno;
        }
        goto cleanup_both;
    }
    if (!S_ISREG(target_info.st_mode)) {
        if (err_out != NULL) {
            *err_out = EINVAL;
        }
        goto cleanup_both;
    }
    if (chmod(target, S_IRUSR | S_IWUSR | S_IRGRP) != 0) {
        if (err_out != NULL) {
            *err_out = errno;
        }
        goto cleanup_both;
    }
    if (stat(target, &target_info) != 0) {
        if (err_out != NULL) {
            *err_out = errno;
        }
        goto cleanup_both;
    }
    mode = target_info.st_mode & (mode_t)0777;
    if (mode != (mode_t)0640) {
        if (err_out != NULL) {
            *err_out = EINVAL;
        }
        goto cleanup_both;
    }
    if (rt_fs_read_pattern(link, err_out) != 1) {
        goto cleanup_both;
    }
    rc = 0;
    if (err_out != NULL) {
        *err_out = 0;
    }

cleanup_both:
    (void)unlink(link);
cleanup_target:
    (void)unlink(target);
    return rc;
}

int rt_fs_temp_file(char *path_out, size_t path_cap, int *err_out)
{
    const char *base = getenv("TMPDIR");
    char template_path[RT_FS_PATH_CAP];
    int written;
    int fd;

    if (path_out == NULL || path_cap == 0u) {
        if (err_out != NULL) {
            *err_out = EINVAL;
        }
        return -1;
    }
    if (base == NULL || base[0] == '\0') {
        base = "/tmp";
    }
    written = snprintf(template_path, sizeof(template_path), "%s/rt_tmp_XXXXXX", base);
    if (written < 0 || (size_t)written >= sizeof(template_path)) {
        if (err_out != NULL) {
            *err_out = ENAMETOOLONG;
        }
        return -1;
    }
    fd = mkstemp(template_path);
    if (fd < 0) {
        if (err_out != NULL) {
            *err_out = errno;
        }
        return -1;
    }
    /* unlink while open: the runtime keeps the descriptor, not the name */
    if (unlink(template_path) != 0) {
        if (err_out != NULL) {
            *err_out = errno;
        }
        (void)close(fd);
        return -1;
    }
    {
        /* `wrote`, not `written`: this function already has an `int written` for the snprintf
         * above, and shadowing it is exactly the kind of hidden warning this project refuses
         * to ship (-Wshadow is on in both builds). */
        ssize_t wrote = write(fd, rt_fs_pattern, sizeof(rt_fs_pattern));
        if (wrote != (ssize_t)sizeof(rt_fs_pattern)) {
            /* A short write is not an errno-producing failure: reporting `errno` unchanged
             * would publish a stale value, possibly 0. Same rule as rt_fs_write_pattern. */
            if (err_out != NULL) {
                *err_out = (wrote < 0) ? errno : EIO;
            }
            (void)close(fd);
            return -1;
        }
    }
    if (close(fd) != 0) {
        if (err_out != NULL) {
            *err_out = errno;
        }
        return -1;
    }
    {
        int copy = snprintf(path_out, path_cap, "%s", template_path);
        if (copy < 0 || (size_t)copy >= path_cap) {
            if (err_out != NULL) {
                *err_out = ENAMETOOLONG;
            }
            return -1;
        }
    }
    if (err_out != NULL) {
        *err_out = 0;
    }
    return 0;
}

int rt_fs_capacity(const char *root, uint64_t *free_bytes_out, int *err_out)
{
    if (root == NULL) {
        if (err_out != NULL) {
            *err_out = EINVAL;
        }
        return -1;
    }
#if defined(RT_FS_HAVE_STATVFS)
    {
        struct statvfs vfs;
        if (statvfs(root, &vfs) != 0) {
            if (err_out != NULL) {
                *err_out = errno;
            }
            return -1;
        }
        if (free_bytes_out != NULL) {
            *free_bytes_out = (uint64_t)vfs.f_bavail * (uint64_t)vfs.f_frsize;
        }
    }
#elif defined(RT_FS_HAVE_STATFS)
    {
        /* Darwin fallback for SDKs without <sys/statvfs.h>. statfs(2) is the older
         * BSD call and is present on every Darwin platform, iOS included. */
        struct statfs fs;
        if (statfs(root, &fs) != 0) {
            if (err_out != NULL) {
                *err_out = errno;
            }
            return -1;
        }
        if (free_bytes_out != NULL) {
            *free_bytes_out = (uint64_t)fs.f_bavail * (uint64_t)fs.f_bsize;
        }
    }
#endif
    if (err_out != NULL) {
        *err_out = 0;
    }
    return 0;
}

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
#include <sys/stat.h>
#include <sys/statvfs.h>
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

static int rt_fs_write_pattern(const char *path)
{
    int fd = open(path, O_CREAT | O_TRUNC | O_WRONLY, S_IRUSR | S_IWUSR);
    ssize_t written;
    if (fd < 0) {
        return -1;
    }
    written = write(fd, rt_fs_pattern, sizeof(rt_fs_pattern));
    if (written != (ssize_t)sizeof(rt_fs_pattern)) {
        int saved = errno;
        (void)close(fd);
        errno = saved;
        return -1;
    }
    if (close(fd) != 0) {
        return -1;
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
    (void)close(fd);
    if (got != (ssize_t)sizeof(buffer)) {
        if (err_out != NULL) {
            *err_out = (got < 0) ? errno : EIO;
        }
        return -1;
    }
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

    if (rt_fs_write_pattern(path) != 0) {
        if (err_out != NULL) {
            *err_out = errno;
        }
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

int rt_fs_deep_paths(const char *root, size_t depth, size_t *path_len_out, int *err_out)
{
    char path[RT_FS_PATH_CAP];
    size_t level;
    size_t length;

    if (root == NULL || depth == 0u) {
        if (err_out != NULL) {
            *err_out = EINVAL;
        }
        return -1;
    }
    length = strlen(root);
    if (length + 2u >= sizeof(path)) {
        if (err_out != NULL) {
            *err_out = ENAMETOOLONG;
        }
        return -1;
    }
    memcpy(path, root, length);
    path[length] = '\0';

    for (level = 0u; level < depth; level++) {
        char segment[16];
        int written = snprintf(segment, sizeof(segment), "d%07u", (unsigned)level);
        if (written < 0 || (size_t)written >= sizeof(segment)) {
            if (err_out != NULL) {
                *err_out = EINVAL;
            }
            return -1;
        }
        if (length + 1u + (size_t)written + 1u >= sizeof(path)) {
            if (err_out != NULL) {
                *err_out = ENAMETOOLONG;
            }
            return -1;
        }
        path[length] = '/';
        memcpy(path + length + 1u, segment, (size_t)written + 1u);
        length += 1u + (size_t)written;
        if (mkdir(path, S_IRWXU) != 0) {
            if (err_out != NULL) {
                *err_out = errno;
            }
            return -1;
        }
    }

    if (path_len_out != NULL) {
        *path_len_out = length;
    }

    {
        char leaf[RT_FS_PATH_CAP];
        if (rt_fs_join(leaf, sizeof(leaf), path, "leaf.bin") != 0) {
            if (err_out != NULL) {
                *err_out = ENAMETOOLONG;
            }
            return -1;
        }
        if (rt_fs_write_pattern(leaf) != 0 || rt_fs_read_pattern(leaf, err_out) != 1) {
            return -1;
        }
        if (unlink(leaf) != 0) {
            if (err_out != NULL) {
                *err_out = errno;
            }
            return -1;
        }
    }

    /* Teardown, deepest first. */
    while (length > strlen(root)) {
        if (rmdir(path) != 0) {
            if (err_out != NULL) {
                *err_out = errno;
            }
            return -1;
        }
        while (length > 0u && path[length] != '/') {
            length--;
        }
        path[length] = '\0';
    }

    if (err_out != NULL) {
        *err_out = 0;
    }
    return 0;
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
    if (rt_fs_write_pattern(target) != 0) {
        if (err_out != NULL) {
            *err_out = errno;
        }
        return -1;
    }
    (void)unlink(link);
    if (symlink(target, link) != 0) {
        if (err_out != NULL) {
            *err_out = errno;
        }
        goto cleanup_target;
    }
    if (lstat(link, &link_info) != 0 || !S_ISLNK(link_info.st_mode)) {
        if (err_out != NULL) {
            *err_out = (errno != 0) ? errno : EINVAL;
        }
        goto cleanup_both;
    }
    if (stat(link, &target_info) != 0 || !S_ISREG(target_info.st_mode)) {
        if (err_out != NULL) {
            *err_out = errno;
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
    if (write(fd, rt_fs_pattern, sizeof(rt_fs_pattern)) != (ssize_t)sizeof(rt_fs_pattern)) {
        if (err_out != NULL) {
            *err_out = errno;
        }
        (void)close(fd);
        return -1;
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
    struct statvfs vfs;

    if (root == NULL || statvfs(root, &vfs) != 0) {
        if (err_out != NULL) {
            *err_out = (root == NULL) ? EINVAL : errno;
        }
        return -1;
    }
    if (free_bytes_out != NULL) {
        *free_bytes_out = (uint64_t)vfs.f_bavail * (uint64_t)vfs.f_frsize;
    }
    if (err_out != NULL) {
        *err_out = 0;
    }
    return 0;
}

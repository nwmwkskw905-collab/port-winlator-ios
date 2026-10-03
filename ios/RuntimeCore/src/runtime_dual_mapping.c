/*
 * runtime_dual_mapping.c — the RW/RX dual-mapping experiment.
 *
 * PHASE_02_RECONSTRUCTED_POC (see runtime_platform.h).
 *
 * Experiment: map the same physical memory twice — once writable, once executable
 * and readable. On Linux this works with a POSIX shared-memory object; on macOS it
 * usually works as well; on iOS the executable view is expected to be refused
 * unless the process is allowed to create executable mappings at all. The code
 * does not assume the outcome: it reports the real errno so the report can say
 * "refused: EPERM/EACCES/..." instead of guessing.
 *
 * This is a *capability experiment*, never an architectural requirement (the port's
 * W^X preference stands: a single view flipped between RW and R-X is preferred when
 * the platform accepts it).
 *
 * Object naming: the name used to be "/rt_dual_<pid>_<counter>" — predictable, and
 * therefore pre-creatable by another process even with O_EXCL. It now comes from
 * rt_platform_unique_shm_name(), which pulls 64 bits from the platform CSPRNG
 * (arc4random_buf on Apple, getrandom on Linux) and FAILS with the real errno if no
 * acceptable source exists, instead of falling back to a guess.
 */
#include "runtime_memory.h"

#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static size_t rt_dual_round_up(size_t len)
{
    size_t page = (size_t)rt_platform_page_size();
    if (len == 0u) {
        len = 1u;
    }
    return ((len + page - 1u) / page) * page;
}

static int rt_dual_map_two_views(int fd, size_t len, rt_dual_map_t *out)
{
    void *rw;
    void *rx;

    if (ftruncate(fd, (off_t)len) != 0) {
        out->err = errno;
        out->stage = RT_DUAL_STAGE_FTRUNCATE;
        return -1;
    }

    rw = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (rw == MAP_FAILED) {
        out->err = errno;
        out->stage = RT_DUAL_STAGE_MAP_RW;
        return -1;
    }

    rx = mmap(NULL, len, PROT_READ | PROT_EXEC, MAP_SHARED, fd, 0);
    if (rx == MAP_FAILED) {
        out->err = errno; /* the interesting case on hardened platforms */
        out->stage = RT_DUAL_STAGE_MAP_RX;
        (void)munmap(rw, len);
        return -1;
    }

    out->rw = rw;
    out->rx = rx;
    out->supported = 1;
    return 0;
}

int rt_dual_map_create(size_t len, rt_dual_map_t *out)
{
    char name[64];
    int fd;
    int rc;

    if (out == NULL) {
        return -1;
    }
    memset(out, 0, sizeof(*out));
    out->len = rt_dual_round_up(len);

    if (rt_platform_unique_shm_name(name, sizeof(name), "/rt_dual", &out->err) != 0) {
        out->stage = RT_DUAL_STAGE_NAME;
        return -1;   /* no unpredictable name -> no experiment (never a fixed name) */
    }

    fd = shm_open(name, O_CREAT | O_EXCL | O_RDWR, S_IRUSR | S_IWUSR);
    if (fd < 0) {
        out->err = errno;               /* captured immediately */
        out->stage = RT_DUAL_STAGE_SHM_OPEN;
        return -1;
    }
    /* Unlink immediately: the mapping keeps the object alive, and a crash cannot
     * leave a name behind. */
    (void)shm_unlink(name);

    rc = rt_dual_map_two_views(fd, out->len, out);
    (void)close(fd);
    return rc;
}

int rt_dual_map_create_file_backed(const char *dir, size_t len, rt_dual_map_t *out)
{
    char name[64];
    char path[256];
    int written;
    int fd;
    int rc;

    if (out == NULL) {
        return -1;
    }
    memset(out, 0, sizeof(*out));
    out->len = rt_dual_round_up(len);

    if (dir == NULL || dir[0] == '\0') {
        /* No writable directory was supplied: refuse to guess one. */
        out->err = EINVAL;
        out->stage = RT_DUAL_STAGE_NAME;
        return -1;
    }
    if (rt_platform_unique_shm_name(name, sizeof(name), "rt_dual_file", &out->err) != 0) {
        out->stage = RT_DUAL_STAGE_NAME;
        return -1;
    }
    written = snprintf(path, sizeof(path), "%s/%s", dir, name);
    if (written < 0 || (size_t)written >= sizeof(path)) {
        out->err = ENAMETOOLONG;
        out->stage = RT_DUAL_STAGE_NAME;
        return -1;
    }

    fd = open(path, O_CREAT | O_EXCL | O_RDWR, S_IRUSR | S_IWUSR);
    if (fd < 0) {
        out->err = errno;               /* captured immediately */
        out->stage = RT_DUAL_STAGE_FILE_OPEN;
        return -1;
    }
    /* Unlink immediately: the live mappings keep the object alive, and neither a crash nor a
     * refusal below can leave a stray file in the app container. */
    (void)unlink(path);

    rc = rt_dual_map_two_views(fd, out->len, out);
    (void)close(fd);
    return rc;
}

const char *rt_dual_stage_name(rt_dual_stage_t stage)
{
    switch (stage) {
    case RT_DUAL_STAGE_NONE:       return "none";
    case RT_DUAL_STAGE_NAME:       return "unique-name";
    case RT_DUAL_STAGE_SHM_OPEN:   return "shm_open";
    case RT_DUAL_STAGE_FILE_OPEN:  return "open(file-backed object)";
    case RT_DUAL_STAGE_FTRUNCATE:  return "ftruncate";
    case RT_DUAL_STAGE_MAP_RW:     return "mmap(read/write view)";
    case RT_DUAL_STAGE_MAP_RX:     return "mmap(executable view)";
    case RT_DUAL_STAGE_ALIAS_CHECK:return "alias-check";
    }
    return "unknown";
}

int rt_dual_map_destroy(rt_dual_map_t *map)
{
    int rc = 0;
    if (map == NULL) {
        return -1;
    }
    if (map->rw != NULL) {
        if (munmap(map->rw, map->len) != 0) {
            rc = -1;
        }
        map->rw = NULL;
    }
    if (map->rx != NULL) {
        if (munmap(map->rx, map->len) != 0) {
            rc = -1;
        }
        map->rx = NULL;
    }
    map->supported = 0;
    return rc;
}

int rt_dual_map_views_aliased(rt_dual_map_t *map)
{
    static const unsigned char pattern[8] = { 0xA5u, 0x5Au, 0xC3u, 0x3Cu, 0x0Fu, 0xF0u, 0x11u, 0x22u };
    unsigned char observed[8];
    size_t i;

    if (map == NULL || map->supported == 0 || map->rw == NULL || map->rx == NULL) {
        return -1;
    }
    memcpy(map->rw, pattern, sizeof(pattern));
    memcpy(observed, map->rx, sizeof(observed));
    for (i = 0; i < sizeof(pattern); i++) {
        if (observed[i] != pattern[i]) {
            map->err = EILSEQ;
            map->stage = RT_DUAL_STAGE_ALIAS_CHECK;
            return 0;
        }
    }
    return 1;
}

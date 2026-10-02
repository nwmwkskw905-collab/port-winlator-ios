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

#if defined(__APPLE__)
#include <sys/random.h>
#endif

static unsigned rt_dual_map_counter = 0u;

static size_t rt_dual_round_up(size_t len)
{
    size_t page = (size_t)rt_platform_page_size();
    if (len == 0u) {
        len = 1u;
    }
    return ((len + page - 1u) / page) * page;
}

int rt_dual_map_create(size_t len, rt_dual_map_t *out)
{
    char name[64];
    size_t written;
    int fd;
    void *rw;
    void *rx;

    if (out == NULL) {
        return -1;
    }
    memset(out, 0, sizeof(*out));
    out->len = rt_dual_round_up(len);

    written = (size_t)snprintf(name, sizeof(name), "/rt_dual_%ld_%u",
                               (long)getpid(), rt_dual_map_counter);
    if (written >= sizeof(name)) {
        out->err = ENAMETOOLONG;
        return -1;
    }
    rt_dual_map_counter++;

    fd = shm_open(name, O_CREAT | O_EXCL | O_RDWR, S_IRUSR | S_IWUSR);
    if (fd < 0) {
        out->err = errno;
        return -1;
    }
    /* Unlink immediately: the mapping keeps the object alive, and a crash cannot
     * leave a name behind. */
    (void)shm_unlink(name);

    if (ftruncate(fd, (off_t)out->len) != 0) {
        out->err = errno;
        (void)close(fd);
        return -1;
    }

    rw = mmap(NULL, out->len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (rw == MAP_FAILED) {
        out->err = errno;
        (void)close(fd);
        return -1;
    }

    rx = mmap(NULL, out->len, PROT_READ | PROT_EXEC, MAP_SHARED, fd, 0);
    if (rx == MAP_FAILED) {
        out->err = errno; /* the interesting case on hardened platforms */
        (void)munmap(rw, out->len);
        (void)close(fd);
        return -1;
    }

    (void)close(fd);
    out->rw = rw;
    out->rx = rx;
    out->supported = 1;
    return 0;
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
            return 0;
        }
    }
    return 1;
}

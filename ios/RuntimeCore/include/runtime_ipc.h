/*
 * runtime_ipc.h — UNIX-domain IPC, file-descriptor passing and event multiplexing.
 *
 * PHASE_02_RECONSTRUCTED_POC (see runtime_platform.h).
 *
 * Multiplexing is reported per platform and never faked: Linux answers with epoll,
 * Darwin/iOS with kqueue/kevent. A Darwin build has no epoll and says so instead of
 * emulating one to make a test line green.
 */
#ifndef RUNTIME_IPC_H
#define RUNTIME_IPC_H

#include <stddef.h>
#include <stdint.h>

#include "runtime_platform.h"

#ifdef __cplusplus
extern "C" {
#endif

/* socketpair(AF_UNIX, SOCK_STREAM) round trip of a known byte pattern. */
int rt_ipc_socketpair(int *err_out);

/* AF_UNIX stream server/client inside `sockdir`; returns 0 when the byte travels. */
int rt_ipc_unix_stream(const char *sockdir, int *err_out);

/* Sends one end of a pipe over a UNIX socket; the receiver writes through it. */
int rt_ipc_scm_rights(const char *sockdir, int *err_out);

/* pipe() + write/read of a known pattern. */
int rt_ipc_pipe(int *err_out);

/* POSIX shared memory object: create, map, write, read through a second mapping. */
int rt_ipc_shm(int *err_out);

/* Which syscall of the POSIX shared-memory probe failed. Physical run #1 (iPhone 13,
 * 2026-10-02) reported `ipc.posix_shm = FAIL` with "errno=1" and nothing else: the
 * syscall that returned EPERM could not be identified from the record, and a single
 * errno cannot distinguish "the sandbox refuses shm_open" from "it refuses the second
 * mmap" — different findings. The stage is reported for every outcome. */
typedef enum {
    RT_IPC_STAGE_NONE = 0,   /* success */
    RT_IPC_STAGE_NAME,       /* composing the unique object name failed */
    RT_IPC_STAGE_SHM_OPEN,   /* shm_open(3) */
    RT_IPC_STAGE_FTRUNCATE,  /* ftruncate(2) */
    RT_IPC_STAGE_MAP_FIRST,  /* first mmap(2) (read/write view) */
    RT_IPC_STAGE_MAP_SECOND, /* second mmap(2) (read-only view of the same object) */
    RT_IPC_STAGE_COMPARE,    /* both mappings succeeded and the contents disagreed */
    RT_IPC_STAGE_UNLINK      /* the probe succeeded and the object could not be removed:
                              * the capability was proven, the cleanup was not */
} rt_ipc_stage_t;

const char *rt_ipc_stage_name(rt_ipc_stage_t stage);

/* Same probe, naming the failing stage. *err_out is the errno of the failing call,
 * captured immediately after it, and is never 0 when the call returns -1.
 *
 * One deliberate exception, documented because it is the only place where the return value
 * and the errno can disagree: when the probe itself succeeds and only the final
 * shm_unlink(3) fails, the return is 0 (the capability WAS proven), *err_out carries the
 * unlink's errno and *stage_out is RT_IPC_STAGE_UNLINK — a cleanup fact reported as a
 * cleanup fact, never silently dropped and never confused with the capability verdict. */
int rt_ipc_shm_ex(int *err_out, rt_ipc_stage_t *stage_out);

/* Records the usable size of sockaddr_un.sun_path (not counting the NUL). */
int rt_ipc_sun_path_limit(size_t *limit_out, int *err_out);

/* Waits for readability on a socket with a bounded timeout.
 * Uses kqueue when available, otherwise epoll. *mechanism_out names what was used. */
int rt_ipc_mux(int *err_out, const char **mechanism_out);

#ifdef __cplusplus
}
#endif

#endif /* RUNTIME_IPC_H */

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

/* Records the usable size of sockaddr_un.sun_path (not counting the NUL). */
int rt_ipc_sun_path_limit(size_t *limit_out, int *err_out);

/* Waits for readability on a socket with a bounded timeout.
 * Uses kqueue when available, otherwise epoll. *mechanism_out names what was used. */
int rt_ipc_mux(int *err_out, const char **mechanism_out);

#ifdef __cplusplus
}
#endif

#endif /* RUNTIME_IPC_H */

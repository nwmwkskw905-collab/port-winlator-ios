/*
 * runtime_ipc.c — UNIX sockets, fd passing, shared memory and event multiplexing.
 *
 * PHASE_02_RECONSTRUCTED_POC (see runtime_platform.h).
 *
 * Multiplexing is platform-native and never emulated: kqueue on Darwin, epoll on
 * Linux. A Darwin build reports UNSUPPORTED for epoll instead of faking it.
 */
#include "runtime_ipc.h"

#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <sys/event.h>
#include <sys/time.h>
#else
#include <sys/epoll.h>
#endif

static const unsigned char rt_ipc_pattern[8] = { 0x11u, 0x22u, 0x33u, 0x44u, 0x55u, 0x66u, 0x77u, 0x88u };

static int rt_ipc_roundtrip_pair(int write_fd, int read_fd, int *err_out)
{
    unsigned char buffer[sizeof(rt_ipc_pattern)];
    ssize_t wrote = write(write_fd, rt_ipc_pattern, sizeof(rt_ipc_pattern));
    ssize_t got;
    int rc;

    if (wrote != (ssize_t)sizeof(rt_ipc_pattern)) {
        if (err_out != NULL) {
            *err_out = (wrote < 0) ? errno : EIO;
        }
        return -1;
    }
    got = read(read_fd, buffer, sizeof(buffer));
    if (got != (ssize_t)sizeof(buffer)) {
        if (err_out != NULL) {
            *err_out = (got < 0) ? errno : EIO;
        }
        return -1;
    }
    rc = (memcmp(buffer, rt_ipc_pattern, sizeof(buffer)) == 0) ? 0 : -1;
    if (rc != 0 && err_out != NULL) {
        *err_out = EILSEQ;
    }
    return rc;
}

int rt_ipc_socketpair(int *err_out)
{
    int fds[2];
    int rc;

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) {
        if (err_out != NULL) {
            *err_out = errno;
        }
        return -1;
    }
    rc = rt_ipc_roundtrip_pair(fds[0], fds[1], err_out);
    (void)close(fds[0]);
    (void)close(fds[1]);
    if (rc == 0 && err_out != NULL) {
        *err_out = 0;
    }
    return rc;
}

static int rt_ipc_build_sockaddr(const char *sockdir, const char *name,
                                 struct sockaddr_un *addr, socklen_t *len_out, int *err_out)
{
    int written;

    memset(addr, 0, sizeof(*addr));
    addr->sun_family = (sa_family_t)AF_UNIX;
    written = snprintf(addr->sun_path, sizeof(addr->sun_path), "%s/%s", sockdir, name);
    if (written < 0 || (size_t)written >= sizeof(addr->sun_path)) {
        if (err_out != NULL) {
            *err_out = ENAMETOOLONG;
        }
        return -1;
    }
    *len_out = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + (size_t)written + 1u);
    return 0;
}

int rt_ipc_unix_stream(const char *sockdir, int *err_out)
{
    struct sockaddr_un addr;
    socklen_t addr_len = 0;
    int listener = -1;
    int client = -1;
    int accepted = -1;
    int rc = -1;

    if (sockdir == NULL) {
        if (err_out != NULL) {
            *err_out = EINVAL;
        }
        return -1;
    }
    if (rt_ipc_build_sockaddr(sockdir, "rt_ipc.sock", &addr, &addr_len, err_out) != 0) {
        return -1;
    }
    (void)unlink(addr.sun_path);

    listener = socket(AF_UNIX, SOCK_STREAM, 0);
    if (listener < 0) {
        if (err_out != NULL) {
            *err_out = errno;
        }
        return -1;
    }
    if (bind(listener, (struct sockaddr *)&addr, addr_len) != 0) {
        if (err_out != NULL) {
            *err_out = errno;
        }
        goto done;
    }
    if (listen(listener, 1) != 0) {
        if (err_out != NULL) {
            *err_out = errno;
        }
        goto done;
    }
    client = socket(AF_UNIX, SOCK_STREAM, 0);
    if (client < 0) {
        if (err_out != NULL) {
            *err_out = errno;
        }
        goto done;
    }
    if (connect(client, (struct sockaddr *)&addr, addr_len) != 0) {
        if (err_out != NULL) {
            *err_out = errno;
        }
        goto done;
    }
    accepted = accept(listener, NULL, NULL);
    if (accepted < 0) {
        if (err_out != NULL) {
            *err_out = errno;
        }
        goto done;
    }
    rc = rt_ipc_roundtrip_pair(client, accepted, err_out);
    if (rc == 0 && err_out != NULL) {
        *err_out = 0;
    }

done:
    if (accepted >= 0) {
        (void)close(accepted);
    }
    if (client >= 0) {
        (void)close(client);
    }
    if (listener >= 0) {
        (void)close(listener);
    }
    (void)unlink(addr.sun_path);
    return rc;
}

int rt_ipc_scm_rights(const char *sockdir, int *err_out)
{
    int pair[2] = { -1, -1 };
    int pipe_fds[2] = { -1, -1 };
    struct msghdr msg;
    struct iovec iov;
    unsigned char control[CMSG_SPACE(sizeof(int))];
    struct cmsghdr *cmsg;
    unsigned char token = 0x5Au;
    unsigned char received_token = 0u;
    unsigned char buffer[64];
    int rc = -1;
    int received_fd = -1;

    (void)sockdir; /* the pair is connected directly; the directory is not needed */

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, pair) != 0) {
        if (err_out != NULL) {
            *err_out = errno;
        }
        return -1;
    }
    if (pipe(pipe_fds) != 0) {
        if (err_out != NULL) {
            *err_out = errno;
        }
        goto done;
    }

    memset(&msg, 0, sizeof(msg));
    iov.iov_base = &token;
    iov.iov_len = sizeof(token);
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = control;
    msg.msg_controllen = sizeof(control);
    cmsg = CMSG_FIRSTHDR(&msg);
    if (cmsg == NULL) {
        if (err_out != NULL) {
            *err_out = EINVAL;
        }
        goto done;
    }
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(int));
    memcpy(CMSG_DATA(cmsg), &pipe_fds[0], sizeof(int));
    msg.msg_controllen = CMSG_SPACE(sizeof(int));

    if (sendmsg(pair[0], &msg, 0) < 0) {
        if (err_out != NULL) {
            *err_out = errno;
        }
        goto done;
    }

    memset(&msg, 0, sizeof(msg));
    memset(buffer, 0, sizeof(buffer));
    memset(control, 0, sizeof(control));
    iov.iov_base = buffer;
    iov.iov_len = sizeof(buffer);
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = control;
    msg.msg_controllen = sizeof(control);

    if (recvmsg(pair[1], &msg, 0) < 0) {
        if (err_out != NULL) {
            *err_out = errno;
        }
        goto done;
    }
    received_token = buffer[0];
    for (cmsg = CMSG_FIRSTHDR(&msg); cmsg != NULL; cmsg = CMSG_NXTHDR(&msg, cmsg)) {
        if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS) {
            memcpy(&received_fd, CMSG_DATA(cmsg), sizeof(int));
        }
    }
    if (received_fd < 0) {
        if (err_out != NULL) {
            *err_out = ENOENT;
        }
        goto done;
    }
    if (received_token != token) {
        if (err_out != NULL) {
            *err_out = EILSEQ;
        }
        goto done;
    }

    /* Read through the received descriptor: this proves the fd itself travelled. */
    if (write(pipe_fds[1], rt_ipc_pattern, sizeof(rt_ipc_pattern)) != (ssize_t)sizeof(rt_ipc_pattern)) {
        if (err_out != NULL) {
            *err_out = errno;
        }
        goto done;
    }
    if (read(received_fd, buffer, sizeof(rt_ipc_pattern)) != (ssize_t)sizeof(rt_ipc_pattern)) {
        if (err_out != NULL) {
            *err_out = errno;
        }
        goto done;
    }
    if (memcmp(buffer, rt_ipc_pattern, sizeof(rt_ipc_pattern)) != 0) {
        if (err_out != NULL) {
            *err_out = EILSEQ;
        }
        goto done;
    }
    rc = 0;
    if (err_out != NULL) {
        *err_out = 0;
    }

done:
    if (received_fd >= 0) {
        (void)close(received_fd);
    }
    if (pipe_fds[0] >= 0) {
        (void)close(pipe_fds[0]);
    }
    if (pipe_fds[1] >= 0) {
        (void)close(pipe_fds[1]);
    }
    if (pair[0] >= 0) {
        (void)close(pair[0]);
    }
    if (pair[1] >= 0) {
        (void)close(pair[1]);
    }
    return rc;
}

int rt_ipc_pipe(int *err_out)
{
    int fds[2];
    int rc;

    if (pipe(fds) != 0) {
        if (err_out != NULL) {
            *err_out = errno;
        }
        return -1;
    }
    rc = rt_ipc_roundtrip_pair(fds[1], fds[0], err_out);
    (void)close(fds[0]);
    (void)close(fds[1]);
    if (rc == 0 && err_out != NULL) {
        *err_out = 0;
    }
    return rc;
}

int rt_ipc_shm(int *err_out)
{
    char name[64];
    int fd;
    void *view_one;
    void *view_two;
    unsigned char observed[sizeof(rt_ipc_pattern)];
    size_t length = 4096u;
    int written;
    int rc = -1;

    written = snprintf(name, sizeof(name), "/rt_shm_%ld", (long)getpid());
    if (written < 0 || (size_t)written >= sizeof(name)) {
        if (err_out != NULL) {
            *err_out = ENAMETOOLONG;
        }
        return -1;
    }
    (void)shm_unlink(name);
    fd = shm_open(name, O_CREAT | O_EXCL | O_RDWR, S_IRUSR | S_IWUSR);
    if (fd < 0) {
        if (err_out != NULL) {
            *err_out = errno;
        }
        return -1;
    }
    if (ftruncate(fd, (off_t)length) != 0) {
        if (err_out != NULL) {
            *err_out = errno;
        }
        (void)close(fd);
        (void)shm_unlink(name);
        return -1;
    }
    view_one = mmap(NULL, length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (view_one == MAP_FAILED) {
        if (err_out != NULL) {
            *err_out = errno;
        }
        (void)close(fd);
        (void)shm_unlink(name);
        return -1;
    }
    view_two = mmap(NULL, length, PROT_READ, MAP_SHARED, fd, 0);
    if (view_two == MAP_FAILED) {
        if (err_out != NULL) {
            *err_out = errno;
        }
        (void)munmap(view_one, length);
        (void)close(fd);
        (void)shm_unlink(name);
        return -1;
    }

    memcpy(view_one, rt_ipc_pattern, sizeof(rt_ipc_pattern));
    memcpy(observed, view_two, sizeof(observed));
    if (memcmp(observed, rt_ipc_pattern, sizeof(rt_ipc_pattern)) == 0) {
        rc = 0;
        if (err_out != NULL) {
            *err_out = 0;
        }
    } else if (err_out != NULL) {
        *err_out = EILSEQ;
    }

    (void)munmap(view_two, length);
    (void)munmap(view_one, length);
    (void)close(fd);
    (void)shm_unlink(name);
    return rc;
}

int rt_ipc_sun_path_limit(size_t *limit_out, int *err_out)
{
    if (limit_out != NULL) {
        *limit_out = sizeof(((struct sockaddr_un *)0)->sun_path) - 1u;
    }
    if (err_out != NULL) {
        *err_out = 0;
    }
    return 0;
}

int rt_ipc_mux(int *err_out, const char **mechanism_out)
{
    int fds[2] = { -1, -1 };
    int rc = -1;

    if (pipe(fds) != 0) {
        if (err_out != NULL) {
            *err_out = errno;
        }
        return -1;
    }
    if (rt_ipc_roundtrip_pair(fds[1], fds[0], err_out) != 0) {
        goto done;
    }

#if defined(__APPLE__)
    {
        struct kevent change;
        struct kevent event;
        struct timespec timeout;
        int queue = kqueue();
        unsigned char byte = 0x01u;

        if (mechanism_out != NULL) {
            *mechanism_out = "kqueue/kevent";
        }
        if (queue < 0) {
            if (err_out != NULL) {
                *err_out = errno;
            }
            goto done;
        }
        EV_SET(&change, (uintptr_t)fds[0], EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, NULL);
        if (kevent(queue, &change, 1, NULL, 0, NULL) != 0) {
            if (err_out != NULL) {
                *err_out = errno;
            }
            (void)close(queue);
            goto done;
        }
        if (write(fds[1], &byte, 1u) != 1) {
            if (err_out != NULL) {
                *err_out = errno;
            }
            (void)close(queue);
            goto done;
        }
        timeout.tv_sec = 1;
        timeout.tv_nsec = 0;
        if (kevent(queue, NULL, 0, &event, 1, &timeout) == 1) {
            rc = 0;
            if (err_out != NULL) {
                *err_out = 0;
            }
        } else if (err_out != NULL) {
            *err_out = ETIMEDOUT;
        }
        (void)close(queue);
    }
#else
    {
        struct epoll_event event;
        struct epoll_event observed;
        unsigned char byte = 0x01u;
        int epoll_fd = epoll_create1(0);

        if (mechanism_out != NULL) {
            *mechanism_out = "epoll";
        }
        if (epoll_fd < 0) {
            if (err_out != NULL) {
                *err_out = errno;
            }
            goto done;
        }
        memset(&event, 0, sizeof(event));
        event.events = EPOLLIN;
        event.data.fd = fds[0];
        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fds[0], &event) != 0) {
            if (err_out != NULL) {
                *err_out = errno;
            }
            (void)close(epoll_fd);
            goto done;
        }
        if (write(fds[1], &byte, 1u) != 1) {
            if (err_out != NULL) {
                *err_out = errno;
            }
            (void)close(epoll_fd);
            goto done;
        }
        memset(&observed, 0, sizeof(observed));
        if (epoll_wait(epoll_fd, &observed, 1, 1000) == 1) {
            rc = 0;
            if (err_out != NULL) {
                *err_out = 0;
            }
        } else if (err_out != NULL) {
            *err_out = ETIMEDOUT;
        }
        (void)close(epoll_fd);
    }
#endif

done:
    if (fds[0] >= 0) {
        (void)close(fds[0]);
    }
    if (fds[1] >= 0) {
        (void)close(fds[1]);
    }
    return rc;
}

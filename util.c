#define _GNU_SOURCE
#include "util.h"
#include <poll.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

void config_socket(int fd, int timeout)
{
    struct timeval t;
    t.tv_sec = timeout;
    t.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &t, sizeof(t));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &t, sizeof(t));
}

void *emalloc(size_t sz)
{
    void *r = malloc(sz);
    if (r == NULL) die("malloc:");
    return r;
}

void die(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);

    if (fmt[0] && fmt[strlen(fmt) - 1] == ':') {
        fputc(' ', stderr);
        perror(NULL);
    } else {
        fputc('\n', stderr);
    }

    exit(1);
}

void tdie(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);

    if (fmt[0] && fmt[strlen(fmt) - 1] == ':') {
        fputc(' ', stderr);
        perror(NULL);
    } else {
        fputc('\n', stderr);
    }

    pthread_exit(NULL);
}

int bridge_fd(int fd1, int fd2)
{
    char buf[4096];
    ssize_t rn1, rn2, wn1, wn2, lrn1, lrn2;

    lrn1 = lrn2 = 0;

    struct pollfd fds[2];

    fds[0].fd     = fd1;
    fds[0].events = POLLIN;
    fds[1].fd     = fd2;
    fds[1].events = POLLIN;

    while (1) {
        rn1 = rn2 = fds[0].revents = fds[1].revents = 0;

        int e = poll(fds, 2, 2000);
        if (e == -1) return 1;

        if ((fds[0].revents | fds[1].revents) & POLLERR) {
            if ((fds[0].revents | fds[1].revents) & POLLHUP)
                return 0;
            return 1;
        }

        if (fds[0].revents & POLLIN) {
            rn1 = read(fd1, buf, sizeof(buf));
            if (rn1 == -1) return 1;
            wn2 = write(fd2, buf, rn1);
            if (wn2 != rn1) return 1;
        }

        if (fds[1].revents & POLLIN) {
            rn2 = read(fd2, buf, sizeof(buf));
            if (rn2 == -1) return 1;
            wn1 = write(fd1, buf, rn2);
            if (wn1 != rn2) return 1;
        }

        if ((rn1 | lrn1 | rn2 | lrn2) == 0) return 0;

        lrn1 = rn1;
        lrn2 = rn2;
    }
}

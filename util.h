#pragma once

#include <stddef.h>

int bridge_fd(int fd1, int fd2);
void set_sock_timeout(int fd, int seconds);
void die(const char *fmt, ...);
void *emalloc(size_t sz);
void tdie(const char *fmt, ...);

#pragma once

#include <stddef.h>

int bridge_fd(int fd1, int fd2);
void config_socket(int fd, int timeout);
void die(const char *fmt, ...);
void *emalloc(size_t sz);
void tdie(const char *fmt, ...);

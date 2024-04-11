#pragma once

#include <stddef.h>

void die(const char *fmt, ...);
void tdie(const char *fmt, ...);
void *emalloc(size_t sz);
int bridge_fd(int fd1, int fd2);


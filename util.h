#pragma once

void die(const char *fmt, ...);
void tdie(const char *fmt, ...);
int bridge_fd(int fd1, int fd2);


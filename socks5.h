#pragma once

#include "proxy.h"

#define SOCKS5_INVALID_AUTH  0xff
#define SOCKS5_NO_AUTH       0
#define SOCKS5_USERPASS_AUTH 2

int socks5_auth(proxy_info *proxy, int fd);
int socks5_chain(proxy_info *proxy, int fd);
int socks5_handler(proxy_info *proxy, int cfd, int pfd);

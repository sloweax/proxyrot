#pragma once

#include "proxy.h"

#define SOCKS5_INVALID_AUTH  0xff
#define SOCKS5_NO_AUTH       0
#define SOCKS5_USERPASS_AUTH 2

void socks5_handler(proxy_info *proxy, int cfd, int pfd);

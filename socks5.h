#pragma once

#include "proxy.h"

int socks5_auth(proxy_info *proxy, int fd);
void socks5_handler(proxy_info *proxy, int cfd, int pfd);
int socks5_userpass_auth(proxy_info *proxy, int fd);

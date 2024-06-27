#include "socks5.h"
#include "util.h"
#include <unistd.h>
#include <stdio.h>
#include <string.h>

static int socks5_userpass_auth(proxy_info *proxy, int fd);

int socks5_auth(proxy_info *proxy, int fd)
{
    // ver + nmethods + methods
    unsigned char buf[4];
    buf[0] = 5;
    buf[1] = proxy->user ? 2 : 1;

    if (proxy->user) {
        buf[2] = 2;
        buf[3] = 0;
    } else {
        buf[2] = 0;
    }

    size_t buflen = proxy->user ? 4 : 3;
    if (write(fd, buf, buflen) != (ssize_t)buflen) return -1;

    if (read(fd, buf, 2) != 2) return -1;

    if (buf[0] != 5) return -1;

    switch (buf[1]) {
    case 0:
        return 0;
    case 2:
        return socks5_userpass_auth(proxy, fd);
    default:
        return -1;
    }
}

static int socks5_userpass_auth(proxy_info *proxy, int fd)
{
    // ver + ulen + max uname + plen + max passwd
    unsigned char buf[1 + 1 + 255 + 1 + 255];
    unsigned char *tmp = buf;

    size_t ulen = proxy->user ? strlen(proxy->user) : 0;
    size_t plen = proxy->pass ? strlen(proxy->pass) : 0;

    *tmp++ = 1;
    *tmp++ = ulen & 0xff;
    if (proxy->user)
        memcpy(tmp, proxy->user, ulen);
    tmp+=ulen;
    *tmp++ = plen & 0xff;
    if (proxy->pass)
        memcpy(tmp, proxy->pass, plen);
    tmp+=plen;

    if (write(fd, buf, tmp - buf) != tmp - buf) return -1;

    if (read(fd, buf, 2) != 2) return -1;

    if (buf[0] != 1) return -1;

    if (buf[1] != 0) return -1;

    return 0;
}

int socks5_handler(proxy_info *proxy, int cfd, int pfd)
{
    (void)proxy;
    // TODO convert socks5h to socks5 if needed

    if (bridge_fd(cfd, pfd) != 0) {
        fprintf(stderr, "connection failed\n");
        return -1;
    }

    return 0;
}

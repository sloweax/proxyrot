#include "proxy.h"
#include "socks5.h"
#include "util.h"
#include <ctype.h>
#include <netdb.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int parse_identifier(const char *str, char **field, char **endptr);
static size_t get_identifier_len(const char *str, char **startptr);

int connect_proxy(const proxy_info *proxy, int timeout)
{
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_PASSIVE;

    if (getaddrinfo(proxy->host, proxy->port, &hints, &res) != 0)
        return -1;

    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd == -1) goto free_err;

    config_socket(fd, timeout);

    if (connect(fd, res->ai_addr, res->ai_addrlen) != 0) goto close_err;

    freeaddrinfo(res);
    return fd;

close_err:
    close(fd);
free_err:
    freeaddrinfo(res);
    return -1;
}

int parse_proxy_info(const char *line, proxy_info *p)
{
    memset(p, 0, sizeof(*p));
    char *tmp;

    int r = parse_identifier(line, &p->proto, &tmp);
    if (r != 0 || p->proto == NULL) goto err;
    if (!is_supported_proto(p->proto)) goto err;

    r = parse_identifier(tmp, &p->host, &tmp);
    if (r != 0 || p->host == NULL) goto err;

    r = parse_identifier(tmp, &p->port, &tmp);
    if (r != 0 || p->port == NULL) goto err;

    if (strncmp(p->proto, "socks5", 6) == 0) {
        r = parse_identifier(tmp, &p->user, &tmp);
        if (r != 0) goto err;

        r = parse_identifier(tmp, &p->pass, &tmp);
        if (r != 0) goto err;
    }

    return 0;

err:
    free_proxy_info(p);
    return -1;
}

void free_proxy_info(proxy_info *p)
{
    if (p->host)  free(p->host);
    if (p->port)  free(p->port);
    if (p->user)  free(p->user);
    if (p->pass)  free(p->pass);
    if (p->proto) free(p->proto);
    p->host = p->port = p->user = p->pass = p->proto = NULL;
}

static int parse_identifier(const char *str, char **field, char **endptr)
{
    char *tmp;
    size_t idlen = get_identifier_len(str, &tmp);
    if (idlen == 0) return 0;
    char *id = strndup(tmp, idlen);
    if (id == NULL) return -1;
    *field = id;
    if (endptr) *endptr = tmp + idlen;
    return 0;
}

static size_t get_identifier_len(const char *str, char **startptr)
{
    size_t r = 0;
    while (isspace(*str)) str++;
    if (startptr) *startptr = (char*)str;
    while (*str && !isspace(*str++)) r++;
    return r;
}

int is_supported_proto(const char *proto)
{
    if (strcmp(proto, "socks5")  == 0) return 1;
    if (strcmp(proto, "socks5h") == 0) return 1;
    return 0;
}

int proxy_auth(proxy_info *proxy, int pfd)
{
    return socks5_auth(proxy, pfd);
}

int proxy_handler(proxy_info *proxy, int cfd, int pfd)
{
    return socks5_handler(proxy, cfd, pfd);
}

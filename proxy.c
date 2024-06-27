#include "proxy.h"
#include "socks5.h"
#include "util.h"
#include <ctype.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int is_str_number(const char *str);
static int parse_identifier(const char *str, char **field, char **endptr);
static size_t get_identifier_len(const char *str, char **startptr);

static int is_str_number(const char *str) {
    while (*str) {
        if (!isdigit(*str++)) return 0;
    }
    return 1;
}

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
    char *tmp = (char*)line;
    proxy_info *current_proxy = p;

    for (;;) {
        int r = parse_identifier(tmp, &current_proxy->proto, &tmp);
        if (r != 0 || current_proxy->proto == NULL) goto err;
        if (!is_supported_proto(current_proxy->proto)) goto err;

        r = parse_identifier(tmp, &current_proxy->host, &tmp);
        if (r != 0 || current_proxy->host == NULL) goto err;

        r = parse_identifier(tmp, &current_proxy->port, &tmp);
        if (r != 0 || current_proxy->port == NULL) goto err;
        if (!is_str_number(current_proxy->port)) goto err;

        if (strncmp(current_proxy->proto, "socks5", 6) == 0) {
            if (get_identifier_len(tmp, &tmp) != 0 && *tmp == '|')
                goto next_chain_proxy;
            r = parse_identifier(tmp, &current_proxy->user, &tmp);
            if (r != 0) goto err;

            if (get_identifier_len(tmp, &tmp) != 0 && *tmp == '|')
                goto next_chain_proxy;
            r = parse_identifier(tmp, &current_proxy->pass, &tmp);
            if (r != 0) goto err;
        }

        if (get_identifier_len(tmp, &tmp) == 0 || *tmp == '#')
            return 0;

next_chain_proxy:

        if (*tmp == '|') {
            current_proxy->chain = calloc(1, sizeof(*current_proxy));
            if (current_proxy->chain == NULL) goto err;
            current_proxy = current_proxy->chain;
            tmp++;
            continue;
        }

        goto err;
    }

err:
    free_proxy_info(p);
    return -1;
}

void free_proxy_info(proxy_info *p)
{
    if (p->chain) {
        free_proxy_info(p->chain);
        free(p->chain);
    }
    if (p->host)  free(p->host);
    if (p->port)  free(p->port);
    if (p->user)  free(p->user);
    if (p->pass)  free(p->pass);
    if (p->proto) free(p->proto);
    p->chain = NULL;
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

int proxy_chain(proxy_info *proxy, int pfd)
{
    return socks5_chain(proxy, pfd);
}

int proxy_auth(proxy_info *proxy, int pfd)
{
    return socks5_auth(proxy, pfd);
}

int proxy_handler(proxy_info *proxy, int cfd, int pfd)
{
    return socks5_handler(proxy, cfd, pfd);
}

void print_proxy(proxy_info *proxy, FILE *f)
{
    printf("%s %s:%s", proxy->proto, proxy->host, proxy->port);
    if (proxy->chain) {
        printf(" | ");
        print_proxy(proxy->chain, f);
    } else {
        puts("");
    }
}

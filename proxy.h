#pragma once

#include <stdio.h>

typedef struct proxy_info {
    char *proto;
    char *host;
    char *port;
    char *user;
    char *pass;
    struct proxy_info *chain;
    struct proxy_info *next;
} proxy_info;

void print_proxy(proxy_info *proxy, FILE *f);
int connect_proxy(const proxy_info *proxy, int timeout);
int is_supported_proto(const char *proto);
int parse_proxy_info(const char *line, proxy_info *p);
void free_proxy_info(proxy_info *p);
int proxy_auth(proxy_info *proxy, int pfd);
int proxy_chain(proxy_info *proxy, int pfd);
int proxy_handler(proxy_info *proxy, int cfd, int pfd);

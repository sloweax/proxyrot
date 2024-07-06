#define _GNU_SOURCE
#include "proxy.h"
#include "socks5.h"
#include "util.h"
#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <netdb.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define VERSION "0.1.0"
#define PORT "1080"
#define ADDR "127.0.0.1"
#define WORKERS 8
#define TIMEOUT 10

#define FLAG_NO_AUTH       (1 << 0)
#define FLAG_USERPASS_AUTH (1 << 1)

char *server_pass;
char *server_user;
bool retry = false;
int timeout;
int nworkers;
int run;
int serverfd;
int server_flags;
proxy_info *current_proxy;
proxy_info *proxies;
proxy_info *proxies_tail;
pthread_mutex_t proxies_lock;
pthread_t *threads;

static int create_server(const char *host, const char *port, int backlog);
static proxy_info *get_next_proxy(void);
static void load_proxy_file(const char *path);
static void cleanup(void);
static void int_handler(int sig);
static void usage(int argc, char **argv);
static void *work(void *arg);

int main(int argc, char **argv)
{
    setlinebuf(stdout);
    setlinebuf(stderr);

    if (pthread_mutex_init(&proxies_lock, NULL) != 0)
        die("pthread_mutex_init:");

    proxies_tail = proxies;

    char *addr = ADDR, *port = PORT;
    int opt;
    nworkers = WORKERS;
    timeout = TIMEOUT;

    static struct option long_options[] = {
        {"help"    , no_argument      , NULL, 'h'},
        {"version" , no_argument      , NULL, 'v'},
        {"no-auth" , no_argument      , NULL, 'n'},
        {"retry"   , no_argument      , NULL, 'r'},
        {"addr"    , required_argument, NULL, 'a'},
        {"port"    , required_argument, NULL, 'p'},
        {"proxies" , required_argument, NULL, 'P'},
        {"userpass", required_argument, NULL, 'u'},
        {"workers" , required_argument, NULL, 'w'},
        {"timeout" , required_argument, NULL, 't'},
        {NULL      , 0                , NULL, 0}
    };

    while((opt = getopt_long(argc, argv, ":hvnra:p:u:w:P:t:", long_options, NULL)) != -1) {
        switch(opt) {
        case 'u':
            {
                server_flags |= FLAG_USERPASS_AUTH;
                char *tmp = strchr(optarg, ':');
                server_user = strndup(optarg, tmp == NULL ? strlen(optarg) : (size_t)(tmp - optarg));
                if (server_user == NULL) die("strndup:");
                if (tmp) {
                    server_pass = strdup(++tmp);
                    if (server_pass == NULL) die("strdup:");
                }
            }
            break;
        case 'P':
            load_proxy_file(optarg);
            break;
        case 'v':
            printf("%s %s\n", argv[0], VERSION);
            return 0;
        break;
        case 'w':
            nworkers = atoi(optarg);
            if (nworkers <= 0)
                die("%s %s is invalid", argv[optind-2], optarg, argv[0]);
            break;
        case 't':
            timeout = atoi(optarg);
            if (timeout <= 0)
                die("%s %s is invalid", argv[optind-2], optarg, argv[0]);
            break;
        case 'n': server_flags |= FLAG_NO_AUTH; break;
        case 'a': addr = optarg; break;
        case 'p': port = optarg; break;
        case 'r': retry = true; break;
        case 'h':
            usage(argc, argv);
            return 0;
        case '?':
            die("unknown option %s\n%s -h for help", argv[optind-1], argv[0]);
        }
    }

    threads = emalloc(sizeof(pthread_t[nworkers]));

    if (proxies == NULL)
        die("missing proxies");

    if (!(server_flags & (FLAG_NO_AUTH | FLAG_USERPASS_AUTH)))
        die("no auth method provided, exiting\n%s -h for help", argv[0]);

    if (server_flags & FLAG_NO_AUTH)
        puts("accepting no auth");

    if (server_flags & FLAG_USERPASS_AUTH)
        puts("accepting userpass auth");

    printf("listening on %s:%s\n", addr, port);

    serverfd = create_server(addr, port, nworkers);
    if (serverfd == -1) die("create_server:");

    if (signal(SIGINT, int_handler) != 0)
        die("signal:");

    if (signal(SIGPIPE, SIG_IGN) != 0)
        die("signal:");

    proxies_tail->next = proxies;
    current_proxy = proxies;
    run = 1;

    for (int i = 0; i < nworkers; i++) {
        printf("starting worker %d\n", i);
        if (pthread_create(&threads[i], NULL, &work, &serverfd) != 0)
            die("pthread_create:");
    }

    for (int i = 0; i < nworkers; i++) {
        if (pthread_join(threads[i], NULL) != 0)
            die("pthread_join:");
        printf("stopping worker %d\n", i);
    }

    cleanup();

    return 0;
}

static int create_server(const char *host, const char *port, int backlog)
{
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_PASSIVE;

    if (getaddrinfo(host, port, &hints, &res) != 0)
        return -1;

    int sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sockfd == -1)
        goto error;

    int optval = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) != 0)
        goto error_close;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval)) != 0)
        goto error_close;

    if (bind(sockfd, res->ai_addr, res->ai_addrlen) != 0)
        goto error_close;

    if (res->ai_socktype != SOCK_DGRAM && listen(sockfd, backlog) != 0)
        goto error_close;

    freeaddrinfo(res);
    return sockfd;

error_close:
    close(sockfd);
error:
    freeaddrinfo(res);
    return -1;
}

static void usage(int argc, char **argv)
{
    (void)argc;
    printf(
        "usage: %s [OPTION...]\n"
        "OPTION:\n"
        "     -h,--help                      shows usage and exits\n"
        "     -v,--version                   shows version and exits\n"
        "     -P,--proxies FILE              add proxies from FILE\n"
        "     -n,--no-auth                   allow no auth authentication\n"
        "     -u,--userpass USER:PASS        use USER:PASS as authentication\n"
        "     -p,--port PORT                 listen on PORT ("PORT" by default)\n"
        "     -a,--addr ADDR                 bind on ADDR ("ADDR" by default)\n"
        "     -w,--workers WORKERS           number of WORKERS (%d by default)\n"
        "     -t,--timeout SECONDS           set connection timeout (%d by default)\n"
        "     -r,--retry                     if proxy connection fail, try another\n"
    , argv[0], WORKERS, TIMEOUT);
}

static void cleanup(void)
{
    if (server_pass) free(server_pass);
    if (server_user) free(server_user);
    if (proxies_tail) proxies_tail->next = NULL;
    if (threads) free(threads);
    pthread_mutex_destroy(&proxies_lock);
    for (proxy_info *tmp = proxies, *next; tmp && (next = tmp->next, 1); tmp = next) {
        free_proxy_info(tmp);
        free(tmp);
    }
    close(serverfd);
}

static void load_proxy_file(const char *path)
{
    char *line = NULL;
    char *tmp;
    size_t len;
    ssize_t read;
    FILE *f = fopen(path, "r");
    proxy_info *p;
    if (f == NULL) die("fopen:");

    while ((read = getline(&line, &len, f)) != -1) {
        if (read == 0) continue;
        if (line[read - 1] == '\n') {
            line[read - 1] = 0;
            read--;
        }
        if (read == 0) continue;
        tmp = line;
        while(isspace(*tmp)) tmp++;
        if (*tmp == '#' || *tmp == '\0') continue;

        p = emalloc(sizeof(*p));

        if (parse_proxy_info(tmp, p) != 0)
            die("could not parse proxy `%s`", line);

        if (proxies == NULL) {
            proxies = proxies_tail = p;
        } else {
            proxies_tail->next = p;
            proxies_tail = p;
        }
    }

    if (errno)
        die("getline:");

    if (line)
        free(line);

    fclose(f);
}

static proxy_info *get_next_proxy(void)
{
    if (pthread_mutex_lock(&proxies_lock) != 0)
        tdie("pthread_mutex_lock:");

    proxy_info *proxy = current_proxy;
    current_proxy = current_proxy->next;

    if (pthread_mutex_unlock(&proxies_lock) != 0)
        tdie("pthread_mutex_unlock:");

    return proxy;
}

static int userpass_auth(int fd)
{
    unsigned char buf[1 + 1 + 255 + 1 + 255];

    if (read(fd, buf, sizeof(buf)) < 3) return -1;

    if (buf[0] != 1) return -1;

    unsigned char *tmp = &buf[1];
    if (strncmp((char*)(tmp+1), server_user, *tmp) != 0) goto auth_err;

    tmp += *tmp + 1;
    if (server_pass && strncmp((char*)(tmp+1), server_pass, *tmp) != 0) goto auth_err;

    buf[1] = 0;
    if (write(fd, buf, 2) != 2) return -1;

    return 0;

auth_err:
    buf[1] = -1;
    write(fd, buf, 2);
    return -1;
}

static int auth(int fd)
{
    unsigned char buf[1 + 1 + 255];

    if (read(fd, buf, sizeof(buf)) < 3) return -1;

    if (buf[0] != 5) return -1;

    if (server_flags & FLAG_NO_AUTH && memchr(&buf[2], 0, buf[1])) {
        buf[1] = SOCKS5_NO_AUTH;
        if (write(fd, buf, 2) != 2)
            return -1;
        return 0;
    }

    if (server_flags & FLAG_USERPASS_AUTH && memchr(&buf[2], 2, buf[1])) {
        buf[1] = SOCKS5_USERPASS_AUTH;
        if (write(fd, buf, 2) != 2)
            return -1;
        return userpass_auth(fd);
    }

    buf[1] = SOCKS5_INVALID_AUTH;
    write(fd, buf, 2);
    return -1;
}

static int handler(proxy_info *proxy, int cfd, int pfd)
{
    proxy_info *cur = proxy;
    for (;;) {
        if (proxy_auth(cur, pfd) != 0) {
            fprintf(stderr, "auth negotiation with proxy %s %s:%s failed\n", cur->proto, cur->host, cur->port);
            return -2;
        }

        if (cur->chain == NULL) break;

        if (proxy_chain(cur->chain, pfd) != 0) {
            fprintf(stderr, "could not chain with proxy %s %s:%s\n", cur->chain->proto, cur->chain->host, cur->chain->port);
            return -2;
        }

        cur = cur->chain;
    }

    // After succesfull connection, remove timeout
    config_socket(cfd, 0);
    config_socket(pfd, 0);

    return proxy_handler(proxy, cfd, pfd);
}

static void *work(void *arg)
{
    int fd = ((int*)arg)[0];

    struct sockaddr_storage cli;

    while (run) {
        socklen_t addrlen = sizeof(cli);
        int cfd = accept(fd, (struct sockaddr*)&cli, &addrlen);

        if (!run) {
            if (cfd != -1)
                close(cfd);
            return NULL;
        }

        if (cfd == -1)
            tdie("accept:");

        config_socket(cfd, timeout);

        char clihost[INET6_ADDRSTRLEN];
        clihost[0] = 0;

        switch (cli.ss_family) {
        case AF_INET:
            inet_ntop(cli.ss_family, &((struct sockaddr_in *)&cli)->sin_addr, clihost, sizeof(clihost));
            break;
        case AF_INET6:
            inet_ntop(cli.ss_family, &((struct sockaddr_in6 *)&cli)->sin6_addr, clihost, sizeof(clihost));
            break;
        }

        if (auth(cfd) != 0) {
            fprintf(stderr, "auth negotiation failed\n");
            close(cfd);
            continue;
        }

        for (;;) {
            char proxy_str[4096];
            proxy_info *proxy = get_next_proxy();
            sprint_proxy(proxy, proxy_str, sizeof(proxy_str));
            printf("connection from %s through proxy %s\n", clihost, proxy_str);

            int pfd = connect_proxy(proxy, timeout);
            if (pfd == -1) {
                fprintf(stderr, "could not connect to proxy %s\n", proxy_str);
                if (retry) continue;
                close(cfd);
                break;
            }

            if (handler(proxy, cfd, pfd) == -2 && retry) {
                close(pfd);
                continue;
            }

            close(pfd);
            close(cfd);
            break;
        }
    }

    return NULL;
}

static void int_handler(int sig)
{
    (void)sig;
    run = 0;
    if (shutdown(serverfd, SHUT_RD) != 0)
        die("shutdown:");
}

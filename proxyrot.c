#include "util.h"
#include <arpa/inet.h>
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct proxy_info {
    char *proto;
    char *host;
    char *port;
    char *user;
    char *pass;
    struct proxy_info *next;
} proxy_info;

#define VERSION "0.1.0"
#define PORT "1080"
#define ADDR "0.0.0.0"
#define WORKERS 8

#define FLAG_NO_AUTH       (1 << 0)
#define FLAG_USERPASS_AUTH (1 << 1)

char *server_pass;
char *server_user;
int nworkers;
int run;
int serverfd;
int server_flags;
proxy_info *current_proxy;
proxy_info *proxies;
proxy_info *proxies_tail;
pthread_mutex_t proxies_lock;
pthread_t *threads;

static int bridge_fd(int fd1, int fd2);
static int connect_proxy(const proxy_info *proxy);
static int create_server(const char *host, const char *port, int backlog);
static int is_supported_proto(const char *proto);
static int parse_identifier(const char *str, char **field, char **endptr);
static int parse_proxy_info(const char *line, proxy_info *p);
static proxy_info *get_next_proxy(void);
static size_t get_identifier_len(const char *str, char **startptr);
static void load_proxy_file(const char *path);
static void cleanup(void);
static void free_proxy_info(proxy_info *p);
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

    static struct option long_options[] = {
        {"help"    , no_argument      , NULL, 'h'},
        {"version" , no_argument      , NULL, 'v'},
        {"no-auth" , no_argument      , NULL, 'n'},
        {"addr"    , required_argument, NULL, 'a'},
        {"port"    , required_argument, NULL, 'p'},
        {"proxies" , required_argument, NULL, 'P'},
        {"userpass", required_argument, NULL, 'u'},
        {"workers" , required_argument, NULL, 'w'},
        {NULL      , 0                , NULL, 0}
    };

    while((opt = getopt_long(argc, argv, ":hvna:p:u:w:P:", long_options, NULL)) != -1) {
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
        case 'n': server_flags |= FLAG_NO_AUTH; break;
        case 'a': addr = optarg; break;
        case 'p': port = optarg; break;
        case 'h':
            usage(argc, argv);
            return 0;
        case '?':
            die("unknown option %s\n%s -h for help", argv[optind-1], argv[0]);
        }
    }

    threads = malloc(sizeof(pthread_t[nworkers]));
    if (threads == NULL) die("malloc:");

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

static size_t get_identifier_len(const char *str, char **startptr)
{
    size_t r = 0;
    while (isspace(*str)) str++;
    if (startptr) *startptr = (char*)str;
    while (*str && !isspace(*str++)) r++;
    return r;
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

static void free_proxy_info(proxy_info *p)
{
    if (p->host)  free(p->host);
    if (p->port)  free(p->port);
    if (p->user)  free(p->user);
    if (p->pass)  free(p->pass);
    if (p->proto) free(p->proto);
    p->host = p->port = p->user = p->pass = p->proto = NULL;
}

static int is_supported_proto(const char *proto)
{
    if (strcmp(proto, "socks5")  == 0) return 1;
    if (strcmp(proto, "socks5h") == 0) return 1;
    return 0;
}

static int parse_proxy_info(const char *line, proxy_info *p)
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

    if (bind(sockfd, res->ai_addr, res->ai_addrlen) != 0) {
        close(sockfd);
        sockfd = -1;
        goto error;
    }

    if (res->ai_socktype != SOCK_DGRAM && listen(sockfd, backlog) != 0) {
        close(sockfd);
        sockfd = -1;
        goto error;
    }

error:

    freeaddrinfo(res);
    return sockfd;
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
        "     -n,--no-auth                   allow NO AUTH\n"
        "     -u,--userpass USER:PASS        add USER:PASS\n"
        "     -p,--port PORT                 listen on PORT ("PORT" by default)\n"
        "     -a,--addr ADDR                 bind on ADDR ("ADDR" by default)\n"
        "     -w,--workers WORKERS           number of WORKERS (%d by default)\n"
    , argv[0], WORKERS);
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
        if (line[read - 1] == '\n') {
            line[read - 1] = 0;
            read--;
        }
        if (read == 0) continue;
        tmp = line;
        while(isspace(*tmp)) tmp++;
        if (*tmp == '#' || *tmp == '\0') continue;

        p = malloc(sizeof(*p));
        if (p == NULL) die("malloc:");

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

static int socks5_auth(proxy_info *proxy, int fd)
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

static void socks5_handler(proxy_info *proxy, int cfd, int pfd)
{
    int r = socks5_auth(proxy, pfd);
    if (r != 0) {
        fprintf(stderr, "auth negotiation with %s %s:%s failed\n", proxy->proto, proxy->host, proxy->port);
        return;
    }

    // TODO convert socks5 to socks5h if needed

    if (bridge_fd(cfd, pfd) != 0) {
        fprintf(stderr, "connection failed\n");
    }
}

static void handler(proxy_info *proxy, int cfd, int pfd)
{
    return socks5_handler(proxy, cfd, pfd);
}

static int userpass_auth(int fd)
{
    unsigned char buf[1 + 1 + 255 + 1 + 255];

    if (read(fd, buf, sizeof(buf)) < 3) return -1;

    if (buf[0] != 1) return -1;

    unsigned char *tmp = &buf[1];
    if (strncmp((char*)(tmp+1), server_user, *tmp) != 0) goto auth_err;

    tmp += *tmp + 1;
    if (strncmp((char*)(tmp+1), server_pass, *tmp) != 0) goto auth_err;

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
        buf[1] = 0;
        if (write(fd, buf, 2) != 2)
            return -1;
        return 0;
    }

    if (server_flags & FLAG_USERPASS_AUTH && memchr(&buf[2], 2, buf[1])) {
        buf[1] = 2;
        if (write(fd, buf, 2) != 2)
            return -1;
        return userpass_auth(fd);
    }

    buf[1] = 0xff;
    write(fd, buf, 2);
    return -1;
}

static int connect_proxy(const proxy_info *proxy)
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

    if (connect(fd, res->ai_addr, res->ai_addrlen) != 0) goto close_err;

    freeaddrinfo(res);
    return fd;

close_err:
    close(fd);
free_err:
    freeaddrinfo(res);
    return -1;
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

        printf("connection from %s\n", clihost);

        if (auth(cfd) != 0) {
            fprintf(stderr, "auth negotiation failed\n");
            close(cfd);
            continue;
        }

        proxy_info *proxy = get_next_proxy();

        int pfd = connect_proxy(proxy);
        if (pfd == -1) {
            fprintf(stderr, "could not connect to proxy %s %s:%s\n", proxy->proto, proxy->host, proxy->port);
            close(cfd);
            continue;
        }

        handler(proxy, cfd, pfd);

        close(pfd);
        close(cfd);
    }

    return NULL;
}

static int bridge_fd(int fd1, int fd2)
{
    char buf[4096];
    ssize_t rn1, rn2, wn1, wn2, lrn1, lrn2;

    lrn1 = lrn2 = 0;

    struct pollfd fds[2];

    fds[0].fd     = fd1;
    fds[0].events = POLLIN;
    fds[1].fd     = fd2;
    fds[1].events = POLLIN;

    while (1) {
        rn1 = rn2 = fds[0].revents = fds[1].revents = 0;

        int e = poll(fds, 2, 2000);
        if (e == -1) return 1;

        if ((fds[0].revents | fds[1].revents) & (POLLHUP | POLLERR | POLLNVAL))
            return 1;

        if (fds[0].revents & POLLIN) {
            rn1 = read(fd1, buf, sizeof(buf));
            if (rn1 == -1) return 1;
            wn2 = write(fd2, buf, rn1);
            if (wn2 != rn1) return 1;
        }

        if (fds[1].revents & POLLIN) {
            rn2 = read(fd2, buf, sizeof(buf));
            if (rn2 == -1) return 1;
            wn1 = write(fd1, buf, rn2);
            if (wn1 != rn2) return 1;
        }

        if ((rn1 | lrn1 | rn2 | lrn2) == 0) return 0;

        lrn1 = rn1;
        lrn2 = rn2;
    }
}

static void int_handler(int sig)
{
    (void)sig;
    run = 0;
    if (shutdown(serverfd, SHUT_RD) != 0)
        die("shutdown:");
}

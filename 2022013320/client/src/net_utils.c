#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200112L
#endif

#include <netdb.h>

#include "net_utils.h"

// 发送全部数据，确保完整发送
int net_send_all(int sockfd, const void *buf, size_t len) {
    size_t sent = 0;
    const char *p = buf;
    while (sent < len) {
        ssize_t n = send(sockfd, p + sent, len - sent, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) {
            return -1;
        }
        sent += (size_t)n;
    }
    return 0;
}

// 连接到指定主机和端口，返回套接字
int net_connect_tcp(const char *host, uint16_t port) {
    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%u", port);

    struct addrinfo hints = {0}, *res = NULL, *rp = NULL;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family = AF_UNSPEC;

    if (getaddrinfo(host, port_str, &hints, &res) != 0) {
        return -1;
    }
    int sockfd = -1;
    for (rp = res; rp != NULL; rp = rp->ai_next) {
        sockfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sockfd < 0) continue;
        if (connect(sockfd, rp->ai_addr, rp->ai_addrlen) == 0) {
            break;
        }
        close(sockfd);
        sockfd = -1;
    }
    freeaddrinfo(res);
    return sockfd;
}

// 在指定 IP 和端口上监听 TCP 连接，返回监听套接字
int net_listen_tcp(const char *bind_ip, uint16_t port, int backlog) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) return -1;

    int opt = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = bind_ip ? inet_addr(bind_ip) : htonl(INADDR_ANY);

    if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(sockfd);
        return -1;
    }
    if (listen(sockfd, backlog) < 0) {
        close(sockfd);
        return -1;
    }
    return sockfd;
}

// 获取套接字对应的本地 IP 地址
int net_get_local_ip(int sockfd, char *ip_buf, size_t buf_sz) {
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    if (getsockname(sockfd, (struct sockaddr *)&addr, &len) < 0) {
        return -1;
    }
    if (!inet_ntop(AF_INET, &addr.sin_addr, ip_buf, buf_sz)) {
        return -1;
    }
    return 0;
}
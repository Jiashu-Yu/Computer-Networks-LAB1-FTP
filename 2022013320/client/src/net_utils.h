#ifndef NET_UTILS_H
#define NET_UTILS_H

#include "client.h"

/* 阻塞发送全部数据 */
int net_send_all(int sockfd, const void *buf, size_t len);

/* 简易 TCP 连接封装 */
int net_connect_tcp(const char *host, uint16_t port);

/* 创建监听 socket 并绑定到指定端口 */
int net_listen_tcp(const char *bind_ip, uint16_t port, int backlog);

/* 获取本地 IP（用于 PORT），简单版本可调用 getsockname */
int net_get_local_ip(int sockfd, char *ip_buf, size_t buf_sz);

#endif /* NET_UTILS_H */
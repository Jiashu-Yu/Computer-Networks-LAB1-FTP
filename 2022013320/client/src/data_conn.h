#ifndef DATA_CONN_H
#define DATA_CONN_H

#include "client.h"

/* 主动模式：建立监听并发送 PORT 命令，返回等待的监听 socket */
int data_prepare_port(ftp_client_ctx *ctx, int *listen_sock);

/* 被动模式：发送 PASV 命令并建立数据 socket */
int data_prepare_pasv(ftp_client_ctx *ctx);

/* 解析 PASV 响应，提取 IP 和端口 */
int parse_pasv_response(const char *resp, char *ip_buf, size_t ip_buf_sz, uint16_t *port_out);

/* 对于 PORT 模式，在发送 RETR/STOR 后调用 accept */
int data_accept_port_client(int listen_sock);

/* 关闭数据连接 */
void data_close(int *sockfd);

#endif /* DATA_CONN_H */
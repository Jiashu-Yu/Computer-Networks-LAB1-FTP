#ifndef CONTROL_H
#define CONTROL_H

#include "client.h"

/* 建立控制连接并读取欢迎语 */
int ctrl_connect(ftp_client_ctx *ctx);

/* 关闭控制连接 */
void ctrl_close(ftp_client_ctx *ctx);

/* 发送命令（自动加 \r\n） */
int ctrl_send_cmd(int sockfd, const char *verb, const char *arg);

/* 读取一行响应，返回是否成功 */
int ctrl_read_line(int sockfd, char *line, size_t maxlen);

/* 读取完整响应（处理多行），返回最终的 3 位响应码 */
int ctrl_read_response(int sockfd, char *last_line, size_t maxlen);

#endif /* CONTROL_H */
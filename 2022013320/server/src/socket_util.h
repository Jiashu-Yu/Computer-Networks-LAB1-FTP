#ifndef SOCKET_UTIL_H
#define SOCKET_UTIL_H

#include "config.h"

// 向连接 socket 发送一段文本（不附加 CRLF）
void write_message(int connfd, const char *message); 

// 从监听 socket 接受一个新的控制连接
int get_connection_fd();

// 获取服务器监听地址（供 PASV 回复使用）
int get_server_ip(const ftp_session *session, struct in_addr *out);

// 建立数据连接（若 PORT 模式则主动连接，PASV 模式则接受连接）
int open_data_connection(ftp_session *session);

// 关闭并清理当前数据连接
int close_data_connection(ftp_session *session);

# endif // SOCKET_UTIL_H
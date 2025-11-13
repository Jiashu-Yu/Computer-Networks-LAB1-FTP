#ifndef SESSION_H
#define SESSION_H

#include "config.h"

// 包装 write_message，向客户端发送标准 FTP 响应码和文本
void send_response(ftp_session *session, int code, const char *fmt, ...);

// 线程入口：处理单个客户端控制流
void *handle_client(void *arg);

#endif // SESSION_H
#ifndef DATA_CHANNEL_H
#define DATA_CHANNEL_H

#include "config.h"

// 处理 PORT 命令：记录客户端提供的数据通道地址并更新会话状态
int handle_PORT(ftp_session *session, const char *param);

// 处理 PASV 命令：启动被动监听 socket 并向客户端回复端口信息
int handle_PASV(ftp_session *session, const char *param);

#endif // DATA_CHANNEL_H
#ifndef FILE_TRANS_H
#define FILE_TRANS_H

#include "config.h"

// 处理 RETR 命令：将服务器文件发送给客户端
int handle_RETR(ftp_session *session, const char *param);

// 处理 STOR 命令：接收客户端上传并保存为服务器文件
int handle_STOR(ftp_session *session, const char *param);

#endif // FILE_TRANS_H
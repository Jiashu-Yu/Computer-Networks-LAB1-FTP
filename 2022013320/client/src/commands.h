#ifndef COMMANDS_H
#define COMMANDS_H

#include "client.h"

/* 控制连接上的基本命令 */
/* USER <name>：发送用户名。 */
int cmd_user(ftp_client_ctx *ctx, const char *user);
/* PASS <password>：发送密码。 */
int cmd_pass(ftp_client_ctx *ctx, const char *pass);
/* SYST：查询服务器类型。 */
int cmd_syst(ftp_client_ctx *ctx);
/* TYPE <mode>：设置传输类型（如 I/Binary）。 */
int cmd_type(ftp_client_ctx *ctx, const char *arg);
/* PWD：打印当前服务器工作目录。 */
int cmd_pwd(ftp_client_ctx *ctx);
/* CWD <path>：切换服务器工作目录。 */
int cmd_cwd(ftp_client_ctx *ctx, const char *path);
/* MKD <path>：创建服务器目录。 */
int cmd_mkd(ftp_client_ctx *ctx, const char *path);
/* RMD <path>：删除服务器目录。 */
int cmd_rmd(ftp_client_ctx *ctx, const char *path);
/* LIST [path]：列出目录内容。 */
int cmd_list(ftp_client_ctx *ctx, const char *path);
/* RETR <remote> [local]：下载文件，resume 表示是否断点续传。 */
int cmd_retr(ftp_client_ctx *ctx, const char *remote, const char *local, bool resume);
/* STOR <local> [remote]：上传文件，resume 表示是否断点续传。 */
int cmd_stor(ftp_client_ctx *ctx, const char *local, const char *remote, bool resume);
/* QUIT：退出会话并关闭控制连接。 */
int cmd_quit(ftp_client_ctx *ctx);

#endif /* COMMANDS_H */
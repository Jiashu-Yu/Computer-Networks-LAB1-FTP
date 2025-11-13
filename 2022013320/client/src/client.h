#ifndef CLIENT_H
#define CLIENT_H

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>

#define CLIENT_DEFAULT_IP   "127.0.0.1"
#define CLIENT_DEFAULT_PORT 21

#define CTRL_BUF_SIZE   1024
#define DATA_BUF_SIZE   8192
#define PATH_BUF_SIZE   512

typedef enum {
    FTP_MODE_PASV = 0,
    FTP_MODE_PORT = 1
} ftp_data_mode;

/* 客户端运行时上下文：
 *  - ctrl_sock：控制连接套接字。
 *  - data_mode：数据通道模式（PASV/PORT）。
 *  - server_ip/server_port：服务器控制通道地址。
 *  - local_ip：PORT 模式下通告给服务器的本地 IP。
 *  - port_listen_sock：手工 PORT 监听 FD。
 *  - port_manual_active：手工 PORT 是否处于待用状态。
 *  - pasv_ip/pasv_port：最近一次 PASV 返回的数据通道地址。
 *  - pasv_ready：是否已有可用的 PASV 地址。
 */
typedef struct {
    int ctrl_sock;                  /* 控制连接 socket */
    ftp_data_mode data_mode;        /* 当前数据连接模式 */
    char server_ip[64];             /* 服务器 IP */
    uint16_t server_port;           /* 控制端口 */
    char local_ip[64];              /* 本地用于 PORT 的 IP */

    int  port_listen_sock;          /* 手工 PORT 命令的监听 socket */
    bool port_manual_active;        /* 是否待用的手工 PORT */

    char     pasv_ip[64];           /* 上一次 PASV 返回的 IP */
    uint16_t pasv_port;             /* 上一次 PASV 返回的端口 */
    bool     pasv_ready;            /* 是否有可用的 PASV 地址 */
} ftp_client_ctx;

#endif /* CLIENT_H */
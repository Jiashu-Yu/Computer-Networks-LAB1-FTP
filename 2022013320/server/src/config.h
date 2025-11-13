#ifndef CONFIG_H
#define CONFIG_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/select.h> 
#include <fcntl.h> 
#include <netinet/in.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <ctype.h>
#include <string.h>
#include <pthread.h>
#include <linux/limits.h>
#include <getopt.h>
#include <stdbool.h>

#define DEFAULT_PORT 21            // 默认监听端口
#define DEFAULT_ROOT "/tmp"        // 默认根目录
#define MAX_PENDING_CONNECTIONS 16 // listen 队列长度
#define FTP_CMD_MAX_LEN 1024       // 控制命令最大长度

typedef enum {
    FTP_DATA_MODE_NONE = 0,        // 尚未建立数据通道
    FTP_DATA_MODE_PORT,            // PORT 主动模式
    FTP_DATA_MODE_PASV             // PASV 被动模式
} ftp_data_mode;

// 维护单个 FTP 会话状态
typedef struct ftp_session {
    int ctrl_fd;                    // 控制连接 FD
    int data_fd;                    // 已建立的数据连接 FD（RETR/STOR 使用）
    int pasv_listenfd;              // PASV 模式下的监听 FD
    struct sockaddr_in port_addr;   // PORT 模式下客户端绘制的数据连接地址
    ftp_data_mode data_mode;        // 当前数据连接模式
    bool logged_in;                 // 用户是否已通过认证
    char username[64];              // 登录用户名（匿名模式也存储）
    char cwd[PATH_MAX];             // 当前会话的工作目录（相对于 root）
    char root[PATH_MAX];            // 会话对应的根目录绝对路径
    off_t restart_pos;              // REST 命令指定的断点位置
} ftp_session;

// 保存服务器全局配置（监听端口/根目录）
typedef struct server_config {
    uint16_t listen_port;
    char root_dir[PATH_MAX];
} server_config;

extern int server_listenfd;                // 全局监听 FD，供信号清理使用

void cleanup_session(ftp_session *session);

#endif // CONFIG_H
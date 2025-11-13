#include <ctype.h>

#include "control.h"
#include "net_utils.h"

// 建立控制连接并读取服务器欢迎消息
int ctrl_connect(ftp_client_ctx *ctx) {
    if (!ctx) {
        return -1;
    }
    ctx->ctrl_sock = net_connect_tcp(ctx->server_ip, ctx->server_port);
    if (ctx->ctrl_sock < 0) {
        fprintf(stderr, "Failed to connect control socket\n");
        return -1;
    }

    char line[CTRL_BUF_SIZE];
    int code = ctrl_read_response(ctx->ctrl_sock, line, sizeof(line));
    if (code < 0) {
        fprintf(stderr, "Failed to read greeting from server\n");
        close(ctx->ctrl_sock);
        ctx->ctrl_sock = -1;
        return -1;
    }
    printf("%s\n", line);
    if (code != 220) {
        fprintf(stderr, "Unexpected greeting: %s\n", line);
        close(ctx->ctrl_sock);
        ctx->ctrl_sock = -1;
        return -1;
    }

    ctx->local_ip[0] = '\0';
    if (net_get_local_ip(ctx->ctrl_sock, ctx->local_ip, sizeof(ctx->local_ip)) < 0) {
        fprintf(stderr, "Warning: unable to determine local IP, PORT mode may fail.\n");
    }

    return 0;
}

// 关闭控制连接
void ctrl_close(ftp_client_ctx *ctx) {
    if (ctx && ctx->ctrl_sock >= 0) {
        close(ctx->ctrl_sock);
        ctx->ctrl_sock = -1;
    }
}

// 发送控制命令
int ctrl_send_cmd(int sockfd, const char *verb, const char *arg) {
    char buf[CTRL_BUF_SIZE];
    if (arg && *arg) {
        snprintf(buf, sizeof(buf), "%s %s\r\n", verb, arg);
    } else {
        snprintf(buf, sizeof(buf), "%s\r\n", verb);
    }
    return net_send_all(sockfd, buf, strlen(buf));
}

// 读取一行控制响应
int ctrl_read_line(int sockfd, char *line, size_t maxlen) {
    size_t idx = 0;
    while (idx + 1 < maxlen) {
        char c;
        ssize_t n = recv(sockfd, &c, 1, 0);
        if (n <= 0) {
            return -1;
        }
        if (c == '\n') {
            break;
        }
        if (c != '\r') {
            line[idx++] = c;
        }
    }
    line[idx] = '\0';
    return 0;
}

// 读取完整的控制响应，处理多行响应
int ctrl_read_response(int sockfd, char *last_line, size_t maxlen) {
    char line[CTRL_BUF_SIZE];
    int code = 0;
    bool multiline = false;

    while (1) {
        if (ctrl_read_line(sockfd, line, sizeof(line)) < 0) {
            return -1;
        }
        if (strlen(line) >= 4 && isdigit((unsigned char)line[0]) &&
            isdigit((unsigned char)line[1]) && isdigit((unsigned char)line[2])) {
            code = (line[0] - '0') * 100 + (line[1] - '0') * 10 + (line[2] - '0');
            if (line[3] == '-') {
                multiline = true;
            } else if (line[3] == ' ') {
                multiline = false;
                strncpy(last_line, line, maxlen);
                last_line[maxlen - 1] = '\0';
                return code;
            }
        }
        if (!multiline) {
            strncpy(last_line, line, maxlen);
            last_line[maxlen - 1] = '\0';
            return code;
        }
    }
}
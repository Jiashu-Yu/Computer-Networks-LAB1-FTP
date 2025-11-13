#include "data_conn.h"
#include "control.h"
#include "net_utils.h"

// 解析 PASV 响应中的地址
int parse_pasv_response(const char *resp, char *ip_buf, size_t ip_buf_sz, uint16_t *port_out) {
    const char *p = strchr(resp, '(');
    if (!p) {
        return -1;
    }
    int nums[6];
    if (sscanf(p + 1, "%d,%d,%d,%d,%d,%d",
               &nums[0], &nums[1], &nums[2], &nums[3], &nums[4], &nums[5]) != 6) {
        return -1;
    }
    for (int i = 0; i < 6; ++i) {
        if (nums[i] < 0 || nums[i] > 255) {
            return -1;
        }
    }
    snprintf(ip_buf, ip_buf_sz, "%d.%d.%d.%d", nums[0], nums[1], nums[2], nums[3]);
    *port_out = (uint16_t)((nums[4] << 8) | nums[5]);
    return 0;
}

// 准备主动模式的数据连接，返回监听套接字
int data_prepare_port(ftp_client_ctx *ctx, int *listen_sock) {
    if (!ctx || !listen_sock) {
        return -1;
    }
    *listen_sock = -1;

    ctx->pasv_ready = false;
    ctx->pasv_ip[0] = '\0';
    ctx->pasv_port  = 0;

    if (ctx->port_manual_active && ctx->port_listen_sock >= 0) {
        *listen_sock = ctx->port_listen_sock;
        ctx->port_listen_sock = -1;
        ctx->port_manual_active = false;
        return 0;
    }

    if (ctx->local_ip[0] == '\0') {
        if (net_get_local_ip(ctx->ctrl_sock, ctx->local_ip, sizeof(ctx->local_ip)) < 0) {
            fprintf(stderr, "Failed to determine local IP for PORT mode.\n");
            return -1;
        }
    }

    unsigned ip_parts[4];
    if (sscanf(ctx->local_ip, "%u.%u.%u.%u",
               &ip_parts[0], &ip_parts[1], &ip_parts[2], &ip_parts[3]) != 4) {
        fprintf(stderr, "Invalid local IP for PORT mode: %s\n", ctx->local_ip);
        return -1;
    }

    int sock = net_listen_tcp(ctx->local_ip[0] ? ctx->local_ip : NULL, 0, 1);
    if (sock < 0) {
        fprintf(stderr, "Unable to open local listening socket for PORT mode.\n");
        return -1;
    }

    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);
    if (getsockname(sock, (struct sockaddr *)&addr, &addrlen) < 0) {
        perror("getsockname");
        close(sock);
        return -1;
    }
    uint16_t chosen_port = ntohs(addr.sin_port);

    char arg[64];
    snprintf(arg, sizeof(arg), "%u,%u,%u,%u,%u,%u",
             ip_parts[0], ip_parts[1], ip_parts[2], ip_parts[3],
             (chosen_port >> 8) & 0xFF, chosen_port & 0xFF);

    if (ctrl_send_cmd(ctx->ctrl_sock, "PORT", arg) < 0) {
        close(sock);
        return -1;
    }

    char resp[CTRL_BUF_SIZE];
    int code = ctrl_read_response(ctx->ctrl_sock, resp, sizeof(resp));
    if (code < 0) {
        close(sock);
        return -1;
    }
    printf("%s\n", resp);
    if (code != 200) {
        close(sock);
        return -1;
    }

    *listen_sock = sock;
    return 0;
}

// 准备被动模式的数据连接，返回数据连接套接字
int data_prepare_pasv(ftp_client_ctx *ctx) {
    if (!ctx) {
        return -1;
    }

    char ip[64];
    uint16_t port = 0;

    if (ctx->pasv_ready) {
        snprintf(ip, sizeof(ip), "%s", ctx->pasv_ip);
        port = ctx->pasv_port;
    } else {
        char resp[CTRL_BUF_SIZE];
        if (ctrl_send_cmd(ctx->ctrl_sock, "PASV", NULL) < 0) {
            return -1;
        }

        int code = ctrl_read_response(ctx->ctrl_sock, resp, sizeof(resp));
        if (code < 0) {
            return -1;
        }
        printf("%s\n", resp);
        if (code != 227) {
            fprintf(stderr, "PASV failed: %s\n", resp);
            return -1;
        }

        if (parse_pasv_response(resp, ctx->pasv_ip, sizeof(ctx->pasv_ip), &ctx->pasv_port) < 0) {
            fprintf(stderr, "Invalid PASV response format.\n");
            ctx->pasv_ip[0] = '\0';
            ctx->pasv_port  = 0;
            ctx->pasv_ready = false;
            return -1;
        }

        ctx->pasv_ready = true;
        snprintf(ip, sizeof(ip), "%s", ctx->pasv_ip);
        port = ctx->pasv_port;
    }

    int data_sock = net_connect_tcp(ip, port);
    if (data_sock < 0) {
        fprintf(stderr, "Failed to connect passive data socket %s:%u\n", ip, port);
        ctx->pasv_ready = false;
        ctx->pasv_ip[0] = '\0';
        ctx->pasv_port  = 0;
        return -1;
    }

    ctx->pasv_ready = false;
    ctx->pasv_ip[0] = '\0';
    ctx->pasv_port  = 0;
    return data_sock;
}

// 接受主动模式的数据连接请求，返回数据连接套接字
int data_accept_port_client(int listen_sock) {
    if (listen_sock < 0) {
        return -1;
    }

    int data_fd;
    while ((data_fd = accept(listen_sock, NULL, NULL)) < 0) {
        if (errno == EINTR) {
            continue;
        }
        perror("accept");
        return -1;
    }
    return data_fd;
}

void data_close(int *sockfd) {
    if (sockfd && *sockfd >= 0) {
        close(*sockfd);
        *sockfd = -1;
    }
}
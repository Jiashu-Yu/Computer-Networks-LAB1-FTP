#include <sys/stat.h>

#include "commands.h"
#include "control.h"
#include "data_conn.h"
#include "net_utils.h"

// 读取最终响应
static int read_final_response(ftp_client_ctx *ctx) {
    char resp[CTRL_BUF_SIZE];
    int code = ctrl_read_response(ctx->ctrl_sock, resp, sizeof(resp));
    if (code < 0) {
        fprintf(stderr, "Failed to read final response\n");
        return -1;
    }
    printf("%s\n", resp);
    return code;
}

// 发送 REST 命令
static int send_rest_command(ftp_client_ctx *ctx, off_t offset) {
    char arg[64];
    snprintf(arg, sizeof(arg), "%lld", (long long)offset);
    if (ctrl_send_cmd(ctx->ctrl_sock, "REST", arg) < 0) {
        return -1;
    }
    char resp[CTRL_BUF_SIZE];
    int code = ctrl_read_response(ctx->ctrl_sock, resp, sizeof(resp));
    if (code < 0) {
        return -1;
    }
    printf("%s\n", resp);
    if (code != 350) {
        fprintf(stderr, "Server rejected REST command.\n");
        return -1;
    }
    return 0;
}

// 统一清理数据连接并根据最终响应返回结果
static int finalize_transfer(ftp_client_ctx *ctx,
                             int *data_sock,
                             int *listen_sock,
                             bool preliminary_ok,
                             int operation_status) {
    data_close(data_sock);
    data_close(listen_sock);

    if (preliminary_ok) {
        int final_code = read_final_response(ctx);
        if (final_code < 0 || final_code >= 300) {
            return -1;
        }
        if (operation_status != -1) {
            return 0;
        }
    }
    return operation_status;
}

// USER命令
int cmd_user(ftp_client_ctx *ctx, const char *user) {
    if (!ctx || !user || !*user) {
        fprintf(stderr, "USER requires a username\n");
        return -1;
    }
    char resp[CTRL_BUF_SIZE];

    if (ctrl_send_cmd(ctx->ctrl_sock, "USER", user) < 0) {
        return -1;
    }
    int code = ctrl_read_response(ctx->ctrl_sock, resp, sizeof(resp));
    if (code < 0) {
        return -1;
    }
    printf("%s\n", resp);
    return code;
}

// PASS命令
int cmd_pass(ftp_client_ctx *ctx, const char *pass) {
    if (!ctx) {
        return -1;
    }
    char resp[CTRL_BUF_SIZE];
    const char *pwd = pass ? pass : "";

    if (ctrl_send_cmd(ctx->ctrl_sock, "PASS", pwd) < 0) {
        return -1;
    }
    int code = ctrl_read_response(ctx->ctrl_sock, resp, sizeof(resp));
    if (code < 0) {
        return -1;
    }
    printf("%s\n", resp);
    return code;
}

// SYST命令
int cmd_syst(ftp_client_ctx *ctx) {
    if (!ctx) return -1;
    char resp[CTRL_BUF_SIZE];

    if (ctrl_send_cmd(ctx->ctrl_sock, "SYST", NULL) < 0) {
        return -1;
    }
    int code = ctrl_read_response(ctx->ctrl_sock, resp, sizeof(resp));
    if (code < 0) {
        return -1;
    }
    printf("%s\n", resp);
    return (code == 215) ? 0 : -1;
}

// TYPE命令
int cmd_type(ftp_client_ctx *ctx, const char *arg) {
    if (!ctx) return -1;
    const char *mode = (arg && *arg) ? arg : "I";
    char resp[CTRL_BUF_SIZE];

    if (ctrl_send_cmd(ctx->ctrl_sock, "TYPE", mode) < 0) {
        return -1;
    }
    int code = ctrl_read_response(ctx->ctrl_sock, resp, sizeof(resp));
    if (code < 0) {
        return -1;
    }
    printf("%s\n", resp);
    return (code == 200) ? 0 : -1;
}

// PWD命令
int cmd_pwd(ftp_client_ctx *ctx) {
    if (!ctx) return -1;
    char resp[CTRL_BUF_SIZE];

    if (ctrl_send_cmd(ctx->ctrl_sock, "PWD", NULL) < 0) {
        return -1;
    }
    int code = ctrl_read_response(ctx->ctrl_sock, resp, sizeof(resp));
    if (code < 0) {
        return -1;
    }
    printf("%s\n", resp);
    return (code >= 200 && code < 300) ? 0 : -1;
}

// CWD命令
int cmd_cwd(ftp_client_ctx *ctx, const char *path) {
    if (!ctx || !path || !*path) {
        fprintf(stderr, "CWD requires a path\n");
        return -1;
    }
    char resp[CTRL_BUF_SIZE];

    if (ctrl_send_cmd(ctx->ctrl_sock, "CWD", path) < 0) {
        return -1;
    }
    int code = ctrl_read_response(ctx->ctrl_sock, resp, sizeof(resp));
    if (code < 0) {
        return -1;
    }
    printf("%s\n", resp);
    return (code >= 200 && code < 300) ? 0 : -1;
}

// MKD命令
int cmd_mkd(ftp_client_ctx *ctx, const char *path) {
    if (!ctx || !path || !*path) {
        fprintf(stderr, "MKD requires a directory name\n");
        return -1;
    }
    char resp[CTRL_BUF_SIZE];

    if (ctrl_send_cmd(ctx->ctrl_sock, "MKD", path) < 0) {
        return -1;
    }
    int code = ctrl_read_response(ctx->ctrl_sock, resp, sizeof(resp));
    if (code < 0) {
        return -1;
    }
    printf("%s\n", resp);
    return (code >= 200 && code < 300) ? 0 : -1;
}

// RMD命令
int cmd_rmd(ftp_client_ctx *ctx, const char *path) {
    if (!ctx || !path || !*path) {
        fprintf(stderr, "RMD requires a directory name\n");
        return -1;
    }
    char resp[CTRL_BUF_SIZE];

    if (ctrl_send_cmd(ctx->ctrl_sock, "RMD", path) < 0) {
        return -1;
    }
    int code = ctrl_read_response(ctx->ctrl_sock, resp, sizeof(resp));
    if (code < 0) {
        return -1;
    }
    printf("%s\n", resp);
    return (code >= 200 && code < 300) ? 0 : -1;
}

// LIST命令
int cmd_list(ftp_client_ctx *ctx, const char *path) {
    if (!ctx) return -1;

    int data_sock = -1;
    int listen_sock = -1;
    bool preliminary_ok = false;
    bool flow_ok = true;
    int operation_status = -1;

    if (ctx->data_mode == FTP_MODE_PASV) {
        data_sock = data_prepare_pasv(ctx);
        if (data_sock < 0) {
            flow_ok = false;
        }
    } else {
        if (data_prepare_port(ctx, &listen_sock) < 0) {
            flow_ok = false;
        }
    }

    if (flow_ok) {
        if (ctrl_send_cmd(ctx->ctrl_sock, "LIST", (path && *path) ? path : NULL) < 0) {
            flow_ok = false;
        }
    }

    char resp[CTRL_BUF_SIZE];
    if (flow_ok) {
        int code = ctrl_read_response(ctx->ctrl_sock, resp, sizeof(resp));
        if (code < 0) {
            flow_ok = false;
        } else {
            printf("%s\n", resp);
            if (code == 150 || code == 125) {
                preliminary_ok = true;
            } else {
                flow_ok = false;
            }
        }
    }

    if (flow_ok && ctx->data_mode == FTP_MODE_PORT) {
        data_sock = data_accept_port_client(listen_sock);
        data_close(&listen_sock);
        if (data_sock < 0) {
            flow_ok = false;
        }
    }

    if (flow_ok) {
        char buf[DATA_BUF_SIZE];
        ssize_t n;
        while ((n = recv(data_sock, buf, sizeof(buf), 0)) > 0) {
            size_t written = fwrite(buf, 1, (size_t)n, stdout);
            if (written < (size_t)n) {
                fprintf(stderr, "Failed to write LIST output to stdout.\n");
                flow_ok = false;
                break;
            }
        }
        if (flow_ok && n < 0) {
            perror("recv");
            flow_ok = false;
        }
    }

    if (flow_ok) {
        operation_status = 0;
    }

    int result = finalize_transfer(ctx, &data_sock, &listen_sock, preliminary_ok, operation_status);
    fflush(stdout);
    return result;
}

// RETR命令
int cmd_retr(ftp_client_ctx *ctx, const char *remote, const char *local, bool resume) {
    if (!ctx || !remote || !*remote || !local || !*local) {
        fprintf(stderr, "Usage: retr <remote> [local] [-r|--resume]\n");
        return -1;
    }

    int data_sock   = -1;
    int listen_sock = -1;
    int file_fd     = -1;
    bool preliminary_ok   = false;
    bool flow_ok          = true;
    int  operation_status = -1;
    off_t resume_offset   = 0;

    if (resume) {
        struct stat st;
        if (stat(local, &st) < 0) {
            fprintf(stderr, "Local file '%s' not found, resume disabled.\n", local);
            resume = false;
        } else if (st.st_size == 0) {
            resume = false;
        } else {
            resume_offset = st.st_size;
            printf("Resuming download from offset %lld bytes.\n", (long long)resume_offset);
        }
    }

    if (ctx->data_mode == FTP_MODE_PASV) {
        data_sock = data_prepare_pasv(ctx);
        if (data_sock < 0) {
            flow_ok = false;
        }
    } else {
        if (data_prepare_port(ctx, &listen_sock) < 0) {
            flow_ok = false;
        }
    }

    if (flow_ok && resume_offset > 0) {
        if (send_rest_command(ctx, resume_offset) < 0) {
            flow_ok = false;
        }
    }

    if (flow_ok) {
        if (ctrl_send_cmd(ctx->ctrl_sock, "RETR", remote) < 0) {
            flow_ok = false;
        }
    }

    char resp[CTRL_BUF_SIZE];
    if (flow_ok) {
        int code = ctrl_read_response(ctx->ctrl_sock, resp, sizeof(resp));
        if (code < 0) {
            flow_ok = false;
        } else {
            printf("%s\n", resp);
            if (code == 150 || code == 125) {
                preliminary_ok = true;
            } else {
                flow_ok = false;
            }
        }
    }

    if (flow_ok && ctx->data_mode == FTP_MODE_PORT) {
        data_sock = data_accept_port_client(listen_sock);
        data_close(&listen_sock);
        if (data_sock < 0) {
            flow_ok = false;
        }
    }

    if (flow_ok) {
        int flags = O_WRONLY | O_CREAT;
        if (resume_offset == 0) {
            flags |= O_TRUNC;
        }
        file_fd = open(local, flags, 0666);
        if (file_fd < 0) {
            perror("open");
            flow_ok = false;
        } else if (resume_offset > 0) {
            if (lseek(file_fd, resume_offset, SEEK_SET) < 0) {
                perror("lseek");
                flow_ok = false;
            }
        }
    }

    if (flow_ok) {
        char buf[DATA_BUF_SIZE];
        ssize_t n;
        while (flow_ok && (n = recv(data_sock, buf, sizeof(buf), 0)) > 0) {
            ssize_t offset = 0;
            while (offset < n) {
                ssize_t w = write(file_fd, buf + offset, (size_t)(n - offset));
                if (w < 0) {
                    if (errno == EINTR) {
                        continue;
                    }
                    perror("write");
                    flow_ok = false;
                    break;
                }
                if (w == 0) {
                    fprintf(stderr, "write returned 0, disk full?\n");
                    flow_ok = false;
                    break;
                }
                offset += w;
            }
        }
        if (flow_ok && n < 0) {
            perror("recv");
            flow_ok = false;
        }
    }

    if (flow_ok) {
        operation_status = 0;
    }

    if (file_fd >= 0) {
        close(file_fd);
        file_fd = -1;
    }

    return finalize_transfer(ctx, &data_sock, &listen_sock, preliminary_ok, operation_status);
}

// STOR命令
int cmd_stor(ftp_client_ctx *ctx, const char *local, const char *remote, bool resume) {
    if (!ctx || !local || !*local || !remote || !*remote) {
        fprintf(stderr, "STOR requires local and remote filename\n");
        return -1;
    }

    if (resume) {
        fprintf(stderr, "Resume for STOR is not supported; uploading from beginning.\n");
        resume = false;
    }

    int data_sock = -1;
    int listen_sock = -1;
    int file_fd = -1;
    bool preliminary_ok = false;
    bool flow_ok = true;
    int operation_status = -1;

    file_fd = open(local, O_RDONLY);
    if (file_fd < 0) {
        perror("open");
        return -1;
    }

    if (ctx->data_mode == FTP_MODE_PASV) {
        data_sock = data_prepare_pasv(ctx);
        if (data_sock < 0) {
            flow_ok = false;
        }
    } else {
        if (data_prepare_port(ctx, &listen_sock) < 0) {
            flow_ok = false;
        }
    }

    if (flow_ok) {
        if (ctrl_send_cmd(ctx->ctrl_sock, "STOR", remote) < 0) {
            flow_ok = false;
        }
    }

    char resp[CTRL_BUF_SIZE];
    if (flow_ok) {
        int code = ctrl_read_response(ctx->ctrl_sock, resp, sizeof(resp));
        if (code < 0) {
            flow_ok = false;
        } else {
            printf("%s\n", resp);
            if (code == 150 || code == 125) {
                preliminary_ok = true;
            } else {
                flow_ok = false;
            }
        }
    }

    if (flow_ok && ctx->data_mode == FTP_MODE_PORT) {
        data_sock = data_accept_port_client(listen_sock);
        data_close(&listen_sock);
        if (data_sock < 0) {
            flow_ok = false;
        }
    }

    if (flow_ok) {
        char buf[DATA_BUF_SIZE];
        while (flow_ok) {
            ssize_t n = read(file_fd, buf, sizeof(buf));
            if (n > 0) {
                if (net_send_all(data_sock, buf, (size_t)n) < 0) {
                    perror("send");
                    flow_ok = false;
                }
            } else if (n == 0) {
                break;
            } else {
                if (errno == EINTR) {
                    continue;
                }
                perror("read");
                flow_ok = false;
            }
        }
    }

    if (flow_ok) {
        operation_status = 0;
    }

    if (file_fd >= 0) {
        close(file_fd);
        file_fd = -1;
    }

    return finalize_transfer(ctx, &data_sock, &listen_sock, preliminary_ok, operation_status);
}

// QUIT命令
int cmd_quit(ftp_client_ctx *ctx) {
    if (!ctx) return -1;
    char resp[CTRL_BUF_SIZE];

    if (ctrl_send_cmd(ctx->ctrl_sock, "QUIT", NULL) < 0) {
        return -1;
    }
    int code = ctrl_read_response(ctx->ctrl_sock, resp, sizeof(resp));
    if (code < 0) {
        return -1;
    }
    printf("%s\n", resp);
    ctrl_close(ctx);
    return (code == 221) ? 0 : -1;
}
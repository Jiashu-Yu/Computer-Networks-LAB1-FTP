#include <getopt.h>

#include "client.h"
#include "control.h"
#include "commands.h"
#include "net_utils.h"
#include "data_conn.h"

static bool is_resume_flag(const char *s) {
    return s && (strcmp(s, "-r") == 0 || strcmp(s, "--resume") == 0);
}

// 显示用法信息
static void usage(const char *prog) {
    fprintf(stderr,
            "Usage: %s [-ip addr] [-port n]\n"
            "       %s [--ip addr] [--port n]\n",
            prog, prog);
}

/* 解析命令行参数，设置客户端上下文 */
static void parse_args(int argc, char **argv, ftp_client_ctx *ctx) {
    // 初始化默认连接配置（IP、端口、模式等），确保未传参时仍可运行
    snprintf(ctx->server_ip, sizeof(ctx->server_ip), "%s", CLIENT_DEFAULT_IP);
    ctx->server_port        = CLIENT_DEFAULT_PORT;
    ctx->data_mode          = FTP_MODE_PASV;
    ctx->ctrl_sock          = -1;
    ctx->local_ip[0]        = '\0';
    ctx->port_listen_sock   = -1;
    ctx->port_manual_active = false;
    ctx->pasv_ip[0]         = '\0';
    ctx->pasv_port          = 0;
    ctx->pasv_ready         = false;

    static const struct option long_opts[] = {
        {"ip",   required_argument, NULL, 'i'},
        {"port", required_argument, NULL, 'p'},
        {"help", no_argument,       NULL, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    optind = 1;

    while ((opt = getopt_long_only(argc, argv, "i:p:h", long_opts, NULL)) != -1) {
        switch (opt) {
        case 'i':
            // 处理 -i/--ip 选项，将服务器 IP 保存到上下文
            snprintf(ctx->server_ip, sizeof(ctx->server_ip), "%s", optarg);
            break;
        case 'p': {
            // 解析端口字符串，校验范围，避免非法端口导致后续连接失败
            char *endp = NULL;
            unsigned long val = strtoul(optarg, &endp, 10);
            if (endp == optarg || *endp != '\0' || val == 0 || val > 65535) {
                fprintf(stderr, "Invalid port: %s\n", optarg);
                usage(argv[0]);
                exit(EXIT_FAILURE);
            }
            ctx->server_port = (uint16_t)val;
            break;
        }
        case 'h':
        default:
            // 遇到 -h 或未知选项时输出用法并退出
            usage(argv[0]);
            exit(EXIT_FAILURE);
        }
    }
}

// 处理PASV命令
static void handle_pasv_cmd(ftp_client_ctx *ctx) {
    char resp[CTRL_BUF_SIZE];

    // 切换到 PASV 前先关闭可能存在的 PORT 监听socket，避免资源泄漏
    if (ctx->port_listen_sock >= 0) {
        close(ctx->port_listen_sock);
        ctx->port_listen_sock = -1;
    }
    ctx->port_manual_active = false;

    if (ctrl_send_cmd(ctx->ctrl_sock, "PASV", NULL) < 0) {
        return;
    }
    int code = ctrl_read_response(ctx->ctrl_sock, resp, sizeof(resp));
    if (code < 0) {
        return;
    }
    printf("%s\n", resp);

    if (code == 227) {
        // 解析服务器返回的 PASV 信息，提取被动模式的数据连接地址与端口
        if (parse_pasv_response(resp, ctx->pasv_ip, sizeof(ctx->pasv_ip), &ctx->pasv_port) == 0) {
            ctx->pasv_ready = true;
        } else {
            // 非 227 回复表示 PASV 设置失败，清理状态
            ctx->pasv_ip[0] = '\0';
            ctx->pasv_port  = 0;
            ctx->pasv_ready = false;
            fprintf(stderr, "Failed to parse PASV response.\n");
        }
        ctx->data_mode = FTP_MODE_PASV;
    } else {
        ctx->pasv_ip[0] = '\0';
        ctx->pasv_port  = 0;
        ctx->pasv_ready = false;
    }
}

// 处理PORT命令
static void handle_port_cmd(ftp_client_ctx *ctx, const char *addr_port) {
    // 解析用户传入的 h1,h2,h3,h4,p1,p2 格式，转换为 IP 与端口
    if (!addr_port || !*addr_port) {
        fprintf(stderr, "Usage: PORT h1,h2,h3,h4,p1,p2\n");
        return;
    }

    int nums[6];
    if (sscanf(addr_port, "%d,%d,%d,%d,%d,%d",
               &nums[0], &nums[1], &nums[2], &nums[3], &nums[4], &nums[5]) != 6) {
        fprintf(stderr, "Invalid PORT parameter.\n");
        return;
    }
    for (int i = 0; i < 6; ++i) {
        if (nums[i] < 0 || nums[i] > 255) {
            fprintf(stderr, "PORT parameter out of range.\n");
            return;
        }
    }

    char ip[32];
    snprintf(ip, sizeof(ip), "%d.%d.%d.%d",
             nums[0], nums[1], nums[2], nums[3]);
    uint16_t port = (uint16_t)((nums[4] << 8) | nums[5]);

    if (ctx->port_listen_sock >= 0) {
        close(ctx->port_listen_sock);
        ctx->port_listen_sock = -1;
    }
    ctx->port_manual_active = false;

    ctx->pasv_ip[0] = '\0';
    ctx->pasv_port  = 0;
    ctx->pasv_ready = false;

    int sock = net_listen_tcp(ip, port, 1);
    if (sock < 0) {
        fprintf(stderr, "Failed to listen on %s:%u for PORT.\n", ip, port);
        return;
    }

    char resp[CTRL_BUF_SIZE];
    if (ctrl_send_cmd(ctx->ctrl_sock, "PORT", addr_port) < 0) {
        close(sock);
        return;
    }
    int code = ctrl_read_response(ctx->ctrl_sock, resp, sizeof(resp));
    if (code < 0) {
        close(sock);
        return;
    }
    printf("%s\n", resp);

    if (code == 200) {
        // 服务器确认 PORT 成功，记录监听状态并切换到主动模式
        snprintf(ctx->local_ip, sizeof(ctx->local_ip), "%s", ip);
        ctx->data_mode         = FTP_MODE_PORT;
        ctx->port_listen_sock  = sock;
        ctx->port_manual_active = true;
    } else {
        close(sock);
    }
}

// 读取用户命令并执行
static void read_action_loop(ftp_client_ctx *ctx) {
    char line[CTRL_BUF_SIZE];
    while (1) {
        // 从标准输入读取一行命令，作为交互式 REPL 的“读”步骤
        if (!fgets(line, sizeof(line), stdin)) {
            break;
        }
        char *newline = strchr(line, '\n');
        if (newline) *newline = '\0';
        if (*line == '\0') continue;

        char *cmd  = strtok(line, " ");
        char *arg1 = strtok(NULL, " ");
        char *arg2 = strtok(NULL, " ");
        char *arg3 = strtok(NULL, " ");

        if (!cmd) continue;

        if (strncmp(cmd, "help", sizeof "help") == 0) {
            printf("Commands:\n"
                   "  USER <username>\n"
                   "  PASS <password>\n"
                   "  SYST\n"
                   "  TYPE [A|I]\n"
                   "  PWD\n"
                   "  CWD <dir>\n"
                   "  MKD <dir>\n"
                   "  RMD <dir>\n"
                   "  LIST [path]\n"
                   "  RETR <remote> [local] [-r|--resume]\n"
                   "  STOR <local> [remote]\n"
                   "  PORT <h1,h2,h3,h4,p1,p2>\n"
                   "  PASV\n"
                   "  MODE [PASV|PORT]\n"
                   "  QUIT\n");
        } else if (strncmp(cmd, "QUIT", sizeof("QUIT")) == 0) {
            cmd_quit(ctx);
            break;
        } else if (strncmp(cmd, "USER", sizeof("USER")) == 0) {
            cmd_user(ctx, arg1);
        } else if (strncmp(cmd, "PASS", sizeof("PASS")) == 0) {
            cmd_pass(ctx, arg1);
        } else if (strncmp(cmd, "SYST", sizeof("SYST")) == 0) {
            cmd_syst(ctx);
        } else if (strncmp(cmd, "TYPE", sizeof("TYPE")) == 0) {
            cmd_type(ctx, arg1);
        } else if (strncmp(cmd, "PWD", sizeof("PWD")) == 0) {
            cmd_pwd(ctx);
        } else if (strncmp(cmd, "CWD", sizeof("CWD")) == 0) {
            if (!arg1) {
                fprintf(stderr, "Usage: CWD <path>\n");
                continue;
            }
            cmd_cwd(ctx, arg1);
        } else if (strncmp(cmd, "MKD", sizeof("MKD")) == 0) {
            if (!arg1) {
                fprintf(stderr, "Usage: MKD <dirname>\n");
                continue;
            }
            cmd_mkd(ctx, arg1);
        } else if (strncmp(cmd, "RMD", sizeof("RMD")) == 0) {
            if (!arg1) {
                fprintf(stderr, "Usage: RMD <dirname>\n");
                continue;
            }
            cmd_rmd(ctx, arg1);
        } else if (strncmp(cmd, "LIST", sizeof("LIST")) == 0) {
            cmd_list(ctx, arg1);
        } else if (strncmp(cmd, "RETR", sizeof("RETR")) == 0) {
            if (!arg1) {
                fprintf(stderr, "Usage: RETR <remote> [local] [-r|--resume]\n");
                continue;
            }
            if (arg3 && !is_resume_flag(arg3)) {
                fprintf(stderr, "Usage: RETR <remote> [local] [-r|--resume]\n");
                continue;
            }
            const char *remote = arg1;
            const char *local  = NULL;
            bool resume = false;

            if (arg2 && is_resume_flag(arg2)) {
                resume = true;
                local = remote;
            } else {
                local = (arg2 && !is_resume_flag(arg2)) ? arg2 : remote;
                if (arg3 && is_resume_flag(arg3)) {
                    resume = true;
                }
            }

            if (!local || !*local) {
                local = remote;
            }

            cmd_retr(ctx, remote, local, resume);
        } else if (strncmp(cmd, "STOR", sizeof("STOR")) == 0) {
            if (!arg1) {
                fprintf(stderr, "Usage: STOR <local> [remote]\n");
                continue;
            }
            if (arg3 && !is_resume_flag(arg3)) {
                fprintf(stderr, "Usage: STOR <local> [remote]\n");
                continue;
            }
            const char *local = arg1;
            const char *remote = NULL;
            bool resume = false;

            if (arg2 && is_resume_flag(arg2)) {
                resume = true;
                remote = local;
            } else {
                remote = (arg2 && !is_resume_flag(arg2)) ? arg2 : local;
                if (arg3 && is_resume_flag(arg3)) {
                    resume = true;
                }
            }

            if (!remote || !*remote) {
                remote = local;
            }

            cmd_stor(ctx, local, remote, resume);
        } else if (strncmp(cmd, "PASV", sizeof("PASV")) == 0) {
            handle_pasv_cmd(ctx);
        } else if (strncmp(cmd, "PORT", sizeof("PORT")) == 0) {
            handle_port_cmd(ctx, arg1);
        } else if (strncmp(cmd, "MODE", sizeof("MODE")) == 0) {
            if (!arg1) {
                printf("Current mode: %s\n",
                       ctx->data_mode == FTP_MODE_PASV ? "PASV" : "PORT");
                continue;
            }
            if (strncmp(arg1, "PASV", sizeof("PASV")) == 0) {
                ctx->data_mode = FTP_MODE_PASV;
            } else if (strncmp(arg1, "PORT", sizeof("PORT")) == 0) {
                ctx->data_mode = FTP_MODE_PORT;
            } else {
                fprintf(stderr, "Usage: MODE [PASV|PORT]\n");
            }
        } else {
            fprintf(stderr, "Unknown command: %s (try 'help')\n", cmd);
        }
    }

    if (ctx->port_listen_sock >= 0) {
        // 循环退出时确保清理监听套接字，避免资源泄漏
        close(ctx->port_listen_sock);
        ctx->port_listen_sock = -1;
    }
    ctx->port_manual_active = false;
    ctx->pasv_ready         = false;
    ctx->pasv_ip[0]         = '\0';
    ctx->pasv_port          = 0;
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    ftp_client_ctx ctx;
    parse_args(argc, argv, &ctx);

    if (ctrl_connect(&ctx) < 0) {
        fprintf(stderr, "Failed to connect to %s:%u\n",
                ctx.server_ip, ctx.server_port);
        return EXIT_FAILURE;
    }

    // 进入 REPL 循环，等待用户输入命令并与服务器交互
    read_action_loop(&ctx);

    // 退出前关闭控制连接，释放资源
    ctrl_close(&ctx);
    return EXIT_SUCCESS;
}
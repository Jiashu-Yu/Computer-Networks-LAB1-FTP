#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <sys/stat.h>

#include "config.h"
#include "socket_util.h"
#include "session.h"

int server_listenfd = -1;

// 检查端口号是否正确
uint16_t parse_port_or_exit(const char *str, const char *progname)
{
    char *endptr = NULL;
    long value;

    errno = 0;
    value = strtol(str, &endptr, 10);
    if (errno != 0) {
        fprintf(stderr, "%s: 解析端口号失败: %s\n", progname, strerror(errno));
        exit(EXIT_FAILURE);
    }
    if (endptr == str || *endptr != '\0') {
        fprintf(stderr, "%s: 端口号必须是纯数字: %s\n", progname, str);
        exit(EXIT_FAILURE);
    }
    if (value <= 0 || value > 65535) {
        fprintf(stderr, "%s: 端口号必须在 1-65535 范围内: %ld\n", progname, value);
        exit(EXIT_FAILURE);
    }
    if (value < 1024) {
        fprintf(stderr, "提示: 端口 %ld 需要 root 权限。\n", value);
    }
    return (uint16_t)value;
}

// 接收命令行传入参数,设置监听端口号和文件路径
void parse_arguments(int argc, char **argv, struct server_config *cfg)
{
    struct option long_opts[] = {
        {"port", required_argument, NULL, 'p'},
        {"root", required_argument, NULL, 'r'},
        {0, 0, 0, 0}
    };

    int opt;
    optind = 1; // 重置optind，防止环境变量影响

    while ((opt = getopt_long_only(argc, argv, "p:r:", long_opts, NULL)) != -1) {
        switch (opt) {
        case 'p':
            cfg->listen_port = parse_port_or_exit(optarg, argv[0]);
            break;
        case 'r':
            snprintf(cfg->root_dir, sizeof(cfg->root_dir), "%s", optarg);
            break;
        default:
            fprintf(stderr, "用法: %s [-port port_num] [-root /path/to/file/area]\n", argv[0]);
            exit(EXIT_FAILURE);
        }
    }
}

// 创建并启动FTP server的监听socket
int init_server(struct server_config cfg)
{
	struct sockaddr_in addr;

    //创建server的监听socket
	if ((server_listenfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1) {
		fprintf(stderr, "Error socket(): %s(%d)\n", strerror(errno), errno);
		return 1;
	}

    //设置ip和port
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(cfg.listen_port);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);	//监听"0.0.0.0"（任何来源）

    // 设置端口复用选项
    int opt = 1;
    setsockopt(server_listenfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    //将server ip和port与socket绑定
	if (bind(server_listenfd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
		fprintf(stderr, "Error bind(): %s(%d)\n", strerror(errno), errno);
		return 1;
	}

    //开始监听socket
	if (listen(server_listenfd, MAX_PENDING_CONNECTIONS) == -1) {
		fprintf(stderr, "Error listen(): %s(%d)\n", strerror(errno), errno);
		return 1;
	}

    return 0;
}

// 主函数
int main(int argc, char **argv)
{
    struct server_config cfg;
    // 设置默认端口号和根目录
    cfg.listen_port = DEFAULT_PORT;
    memset(cfg.root_dir, 0, sizeof(cfg.root_dir));
    snprintf(cfg.root_dir, sizeof(cfg.root_dir), "%s", DEFAULT_ROOT);

    parse_arguments(argc, argv, &cfg);

    char root_abs[PATH_MAX];
    if (cfg.root_dir[0] == '/') {
        snprintf(root_abs, sizeof(root_abs), "%s", cfg.root_dir);
    } else {
        char cwd[PATH_MAX];
        if (!getcwd(cwd, sizeof(cwd))) {
            fprintf(stderr, "无法获取当前工作目录: %s\n", strerror(errno));
            return EXIT_FAILURE;
        }
        if (snprintf(root_abs, sizeof(root_abs), "%s/%s", cwd, cfg.root_dir) >= (int)sizeof(root_abs)) {
            fprintf(stderr, "根目录路径过长\n");
            return EXIT_FAILURE;
        }
    }
    char resolved_root[PATH_MAX];
    if (!realpath(root_abs, resolved_root)) {
        fprintf(stderr, "根目录不存在或不可访问: %s (%s)\n", root_abs, strerror(errno));
        return EXIT_FAILURE;
    }

    struct stat root_st;
    if (stat(resolved_root, &root_st) < 0 || !S_ISDIR(root_st.st_mode)) {
        fprintf(stderr, "根目录不是一个有效的目录: %s\n", resolved_root);
        return EXIT_FAILURE;
    }
    snprintf(cfg.root_dir, sizeof(cfg.root_dir), "%s", resolved_root);

    // printf("Current Port is %d\n", cfg.listen_port);
    // printf("Current Root is %s\n", cfg.root_dir);

    if (init_server(cfg) == 1) {
        fprintf(stderr, "Failed to initialize server\n");
        return EXIT_FAILURE;
    }

    // printf("Server is listening on port %d\n", cfg.listen_port);

    // 主循环，等待并接受连接请求
    while (1) {
        int connfd = get_connection_fd();
        if (connfd < 0) {
            continue;
        }

        // 为每个新连接创建一个会话结构体
        ftp_session *session = (ftp_session *)malloc(sizeof(ftp_session));
        if (!session) {
            fprintf(stderr, "Failed to allocate memory for session\n");
            close(connfd);
            continue;
        }
        // 初始化session结构体
        memset(session, 0, sizeof(ftp_session));
        session->ctrl_fd = connfd;
        session->data_fd = -1;
        session->pasv_listenfd = -1;
        session->data_mode = FTP_DATA_MODE_NONE;
        session->logged_in = false;
        snprintf(session->cwd, sizeof(session->cwd), "/");
        snprintf(session->root, sizeof(session->root), "%s", cfg.root_dir);
        session->username[0] = '\0';
        session->restart_pos = 0;
        
        // 创建一个新线程处理该连接
        pthread_t thread_id;
        int rc = pthread_create(&thread_id, NULL, handle_client, session);
        if (rc != 0) {
            fprintf(stderr, "Failed to create thread: %s\n", strerror(rc));
            cleanup_session(session);
            continue;
        }
        pthread_detach(thread_id); // 分离线程，避免内存泄漏
    }

    close(server_listenfd);
    // printf("Server exited normally\n");
    return 0;
}
#include "config.h"
#include "session.h"
#include "socket_util.h"

/*
 * 处理FTP命令: PORT
 * session: FTP会话
 * param: 命令参数
 * return: 1表示成功，0表示失败
 */
int handle_PORT(ftp_session *session, const char *param)
{
    if (!param) {
        send_response(session, 501, "PORT command requires an argument.");
        return 1;
    }

    // 解析参数，格式为 h1,h2,h3,h4,p1,p2
    int h1, h2, h3, h4, p1, p2;
    if (sscanf(param, "%d,%d,%d,%d,%d,%d", &h1, &h2, &h3, &h4, &p1, &p2) != 6) {
        send_response(session, 501, "Invalid PORT command format.");
        return 1;
    }

    if (h1 < 0 || h1 > 255 || h2 < 0 || h2 > 255 ||
        h3 < 0 || h3 > 255 || h4 < 0 || h4 > 255 ||
        p1 < 0 || p1 > 255 || p2 < 0 || p2 > 255) {
        send_response(session, 501, "Invalid PORT command format.");
        return 1;
    }

    char ip_str[32];
    snprintf(ip_str, sizeof(ip_str), "%d.%d.%d.%d", h1, h2, h3, h4);
    uint16_t port = (p1 << 8) | p2; //  TCP port number is p1*256+p2

    // 如果存在旧的数据连接，先关掉
    if (session->data_fd != -1) {
        close(session->data_fd);
        session->data_fd = -1;
    }

    // 如果存在旧的PASV监听socket，先关掉
    if (session->pasv_listenfd != -1) {
        close(session->pasv_listenfd);
        session->pasv_listenfd = -1;
    }

    // 设置PORT模式下的客户端data socket地址
    memset(&session->port_addr, 0, sizeof(session->port_addr));
    session->port_addr.sin_family = AF_INET;
    session->port_addr.sin_port = htons(port);

    // 将字符串形式的IP地址转换为二进制并存储
    if (inet_pton(AF_INET, ip_str, &session->port_addr.sin_addr) <= 0) {
        send_response(session, 501, "Invalid IP address in PORT command.");
        return 1;
    }

    // 更新session状态
    session->data_mode = FTP_DATA_MODE_PORT;
    send_response(session, 200, "PORT command successful.");
    return 1;
}


/*
 * 处理PASV命令
 * session: FTP会话
 * param: 命令参数
 * return: 1表示成功，0表示失败
 */
int handle_PASV(ftp_session *session, const char *param)
{
    if (param) {
        send_response(session, 501, "PASV parameters are prohibited.");
        return 1;
    }

    // 如果存在旧的数据连接，先关掉
    if (session->data_fd != -1) {
        close(session->data_fd);
        session->data_fd = -1;
    }

    // 如果存在旧的PASV监听socket，先关掉
    if (session->pasv_listenfd != -1) {
        close(session->pasv_listenfd);
        session->pasv_listenfd = -1;
    }

    // 创建监听socket
    int listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd < 0) {
        send_response(session, 425, "Cannot open passive connection.");
        return 1;
    }

    // ctrl socket的本地地址
    struct sockaddr_in addr, ctrl_addr;
    socklen_t addrlen = sizeof(ctrl_addr);
    memset(&ctrl_addr, 0, sizeof(ctrl_addr));
    if (getsockname(session->ctrl_fd, (struct sockaddr *)&ctrl_addr, &addrlen) < 0) {
        ctrl_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    }

    // 尝试绑定在20000到65535范围内的随机端口，直到成功（避免多个端口冲突，但最多重试16次）
    bool bound = false;
    for (int i = 0; i < 16; i++) {
        uint16_t port = (uint16_t)(20000 + rand() % (65535 - 20000 + 1));
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr = ctrl_addr.sin_addr;
        addr.sin_port = htons(port);

        if (bind(listenfd, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
            bound = true;
            break;
        }
    }
    if (!bound || listen(listenfd, 1) < 0) {
        close(listenfd);
        send_response(session, 425, "Cannot open passive connection.");
        return 1;
    }

    // 获取监听socket的实际绑定地址（端口可能是系统分配的）
    addrlen = sizeof(addr);
    if (getsockname(listenfd, (struct sockaddr *)&addr, &addrlen) < 0) {
        close(listenfd);
        send_response(session, 425, "Cannot open passive connection.");
        return 1;
    }

    // 通过控制连接获取真实本地 IP
    struct in_addr server_ip;
    if (!get_server_ip(session, &server_ip)) {
        server_ip.s_addr = htonl(INADDR_LOOPBACK);   // 兜底使用 127.0.0.1
    }

    uint32_t ip = ntohl(server_ip.s_addr); // 服务器IP地址
    uint16_t port = ntohs(addr.sin_port); // 服务器端口号

    char response[128];
    // 格式按照："227 Entering Passive Mode (h1,h2,h3,h4,p1,p2)", where the server’s IP address is h1.h2.h3.h4 and the TCP port number is p1*256+p2.
    snprintf(response, sizeof(response),
             "%u,%u,%u,%u,%u,%u",
             (ip >> 24) & 0xFF,
             (ip >> 16) & 0xFF,
             (ip >> 8) & 0xFF,
             ip & 0xFF,
             (port >> 8) & 0xFF,
             port & 0xFF);

    session->pasv_listenfd = listenfd;
    session->data_mode = FTP_DATA_MODE_PASV;
    send_response(session, 227, "Entering Passive Mode (%s)", response);
    return 1;
}
#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "config.h"
#include "data_channel.h"
#include "socket_util.h"
#include "file_trans.h"
#include "path_utils.h"

static int handle_USER(ftp_session *session, const char *param);
static int handle_PASS(ftp_session *session, const char *param);
static int handle_SYST(ftp_session *session, const char *param);
static int handle_TYPE(ftp_session *session, const char *param);
static int handle_QUIT(ftp_session *session, const char *param);
static int handle_PWD(ftp_session *session, const char *param);
static int handle_CWD(ftp_session *session, const char *param);
static int handle_MKD(ftp_session *session, const char *param);
static int handle_RMD(ftp_session *session, const char *param);
static int handle_REST(ftp_session *session, const char *param);

typedef struct {
    const char *verb; // FTP command verb 字符串指针
    int (*handler)(ftp_session *, const char *); // 处理函数指针
} ftp_command_entry;


// 命令和处理函数映射表，用于将命令分发到对应的处理函数
const ftp_command_entry command_table[] = {
    {"USER", handle_USER},
    {"PASS", handle_PASS},
    {"SYST", handle_SYST},
    {"TYPE", handle_TYPE},
    {"QUIT", handle_QUIT},
    {"PORT", handle_PORT},
    {"PASV", handle_PASV},
    {"REST", handle_REST},
    {"RETR", handle_RETR}, 
    {"STOR", handle_STOR},
    {"PWD", handle_PWD},
    {"CWD", handle_CWD},
    {"MKD", handle_MKD},
    {"RMD", handle_RMD}
};


/*
 * 发送FTP响应
 * session: FTP会话
 * code: 响应代码
 * fmt: 格式化字符串
 * ...: 可变参数
 */
void send_response(ftp_session *session, int code, const char *fmt, ...)
{
    // 检查 session 和control socket是否有效
    if (!session || session->ctrl_fd == -1) {
        return;
    }

    // 格式化响应消息，可将多个参数格式化为字符串
    char msg_body[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg_body, sizeof(msg_body), fmt, ap);
    va_end(ap);

    // 完整的响应格式：<code> <message body>\r\n
    char message[600];
    snprintf(message, sizeof(message), "%d %s\r\n", code, msg_body);
    write_message(session->ctrl_fd, message);
}


/*
 * 从控制连接接收一行命令
 * fd: control socket的文件描述符
 * out: 输出缓冲区
 * outlen: 输出缓冲区大小
 * return: 成功返回接收到的字节数，连接关闭返回0，出错返回-1，行太长返回-2
 */
int recv_command_line(int fd, char *out, size_t outlen)
{
    size_t idx = 0;

    // 循环接收数据，直到接收到完整的一行命令（以\n结尾）
    while (idx < outlen - 1) {
        char c;
        // 每次从socket里接收一个字节，阻塞直到有数据可读，返回接收到的字节数
        ssize_t n = recv(fd, &c, 1, 0); 
        if (n == 0) {
            return 0; // 连接关闭
        }
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1; // 读取错误
        }

        if (c == '\r') {
            continue;
        }
        if (c == '\n') {
            out[idx] = '\0';
            return (int)idx;
        }
        out[idx] = c;
        idx++;
    }
    out[outlen - 1] = '\0';
    return -2; // 太长了，还没到换行符就超出缓冲区了
}

/*
 * 解析FTP命令
 * cmd_line: 输入的命令行
 * verb_out: 输出的命令动词
 * verb_sz: 命令动词缓冲区大小
 * param_out: 输出的命令参数
 * return: 是否解析成功
 */
bool parse_command(const char *cmd_line, char *verb_out, size_t verb_sz, const char **param_out)
{
    // 检查输入参数的有效性
    if (!cmd_line || !verb_out || !param_out) {
        return false;
    }

    // 跳过命令行开头的空格
    while (*cmd_line == ' ') {
        cmd_line++;
    }

    // 如果命令行为空，解析失败
    if (*cmd_line == '\0') {
        return false;
    }

    // 提取命令动词，直到遇到空格或字符串结束
    size_t verb_len = 0;
    while (cmd_line[verb_len] && !isspace((unsigned char)cmd_line[verb_len])) {
        verb_len++;
    }
    // 如果动词长度为0或超过缓冲区大小，解析失败
    if (verb_len == 0 || verb_len >= verb_sz) {
        return false;
    }
    // 将动词转换为大写并存入输出缓冲区
    for (size_t i = 0; i < verb_len; ++i) {
        verb_out[i] = (char)toupper((unsigned char)cmd_line[i]);
    }
    verb_out[verb_len] = '\0';

    // 提取参数，跳过动词后的空格
    const char *param = cmd_line + verb_len; // 指向动词后第一个字符
    while (*param == ' ') {
        param++;
    }
    if (*param == '\0') {
        *param_out = NULL;
    } else {
        *param_out = param; // 指向参数字符串
    }
    return true;
}


/*
 * 判断某个命令是否允许在未登录状态下执行
 * verb: 命令动词
 * return: 允许返回true，否则返回false
*/
bool is_command_allowed_without_login(const char *verb)
{
    static const char* allowed[] = {"USER", "PASS", "QUIT"};
    size_t count = sizeof(allowed) / sizeof(allowed[0]);
    for (size_t i = 0; i < count; i++) {
        if (strcmp(allowed[i], verb) == 0) {
            return true;
        }
    }
    return false;
}


/*
 * 分发命令到对应的处理函数
 * session: FTP会话
 * verb: 命令动词
 * param: 命令参数
 * return: 处理成功返回true，否则返回false
 */
bool dispatch_command(ftp_session *session, const char *verb, const char *param)
{
    size_t n = sizeof(command_table) / sizeof(command_table[0]);
    for (size_t i = 0; i < n; i++) {
        if (strcmp(command_table[i].verb, verb) == 0) {
            return command_table[i].handler(session, param) != 0;  // 调用对应的处理函数并返回结果
        }
    }
    return false;
}

// 清理FTP session
void cleanup_session(ftp_session *session)
{
    if (!session) {
        return;
    }

    if (session->ctrl_fd != -1) {
        close(session->ctrl_fd);
        session->ctrl_fd = -1;
    }
    if (session->data_fd != -1) {
        close(session->data_fd);
        session->data_fd = -1;
    }
    if (session->pasv_listenfd != -1) {
        close(session->pasv_listenfd);
        session->pasv_listenfd = -1;
    }
    free(session);
}


/*
 * 处理USER命令
 * session: FTP会话
 * param: 命令参数（用户名）
 * return: 处理结果，1表示已处理
 */
int handle_USER(ftp_session *session, const char *param)
{
    if (!param || param[0] == '\0') {
        send_response(session, 501, "Syntax error in parameters.");
        return 1;
    }
    if (strcmp(param, "anonymous") != 0) {
        send_response(session, 530, "Only anonymous user is allowed.");
        return 1;
    }

    snprintf(session->username, sizeof(session->username), "%s", "anonymous");
    send_response(session, 331, "User anonymous is accepted, send your e-mail address as password.");
    return 1;
}


/*
 * 处理PASS命令
 * session: FTP会话
 * param: 命令参数（密码）
 * return: 处理结果，1表示已处理
 */
int handle_PASS(ftp_session *session, const char *param)
{
    if (session->username[0] == '\0') {
        send_response(session, 503, "Previous request was not USER.");
        return 1;
    }

    if (!param) {
        param = "";
    }

    session->logged_in = true;
    send_response(session, 230, "Welcome to Jiashu-Yu's FTP server! Guest login ok, access restrictions apply.");
    return 1;
}


/*
 * 处理SYST命令
 * session: FTP会话
 * param: 命令参数
 * return: 处理结果，1表示已处理
 */
int handle_SYST(ftp_session *session, const char *param)
{
    (void)param; // 未使用参数,避免编译器警告
    send_response(session, 215, "UNIX Type: L8");
    return 1;
}


/*
 * 处理TYPE命令
 * session: FTP会话
 * param: 命令参数
 * return: 处理结果，1表示已处理
 */
int handle_TYPE(ftp_session *session, const char *param)
{
    // 仅支持TYPE I（二进制模式）
    if (!param || strcmp(param, "I") != 0) {
        send_response(session, 504, "Only TYPE I is supported.");
        return 1;
    }
    send_response(session, 200, "Type set to I.");
    return 1;
}


/*
 * 处理QUIT命令
 * session: FTP会话
 * param: 命令参数
 * return: 处理结果，1表示已处理
 */
int handle_QUIT(ftp_session *session, const char *param)
{
    (void)param;
    send_response(session, 221, "Thank you for using FTP server designed by Jiashu-Yu. Goodbye.");
    if (session->ctrl_fd != -1) {
        close(session->ctrl_fd);
        session->ctrl_fd = -1;
    }
    return 1;
}

/*
 * 处理PWD命令
 * session: FTP会话
 * param: 命令参数
 * return: 处理结果，1表示已处理
 */
int handle_PWD(ftp_session *session, const char *param)
{
    (void)param;
    const char *cwd = session->cwd[0] ? session->cwd : "/";
    send_response(session, 257, "\"%s\"", cwd);
    return 1;
}

/*
 * 处理CWD命令
 * session: FTP会话
 * param: 命令参数
 * return: 处理结果，1表示已处理
 */
int handle_CWD(ftp_session *session, const char *param)
{
    if (!param || param[0] == '\0') {
        send_response(session, 501, "Syntax error in parameters.");
        return 1;
    }

    char abs_path[PATH_MAX];
    if (!ftp_resolve_existing_directory(session, param, abs_path, sizeof(abs_path))) {
        send_response(session, 550, "Failed to change directory.");
        return 1;
    }

    char rel_path[PATH_MAX];
    if (!ftp_make_relative_path(session, abs_path, rel_path, sizeof(rel_path))) {
        send_response(session, 550, "Failed to change directory.");
        return 1;
    }

    snprintf(session->cwd, sizeof(session->cwd), "%s", rel_path);
    send_response(session, 250, "Directory successfully changed.");
    return 1;
}

/*
 * 处理MKD命令
 * session: FTP会话
 * param: 命令参数
 * return: 处理结果，1表示已处理
 */
int handle_MKD(ftp_session *session, const char *param)
{
    if (!param || param[0] == '\0') {
        send_response(session, 501, "Syntax error in parameters.");
        return 1;
    }

    char abs_path[PATH_MAX];
    if (!ftp_resolve_for_creation(session, param, abs_path, sizeof(abs_path), NULL, 0)) {
        send_response(session, 550, "Failed to create directory.");
        return 1;
    }

    if (mkdir(abs_path, 0755) < 0) {
        if (errno == EEXIST) {
            send_response(session, 550, "Directory already exists.");
            return 1;
        }
        else {
            send_response(session, 550, "Failed to create directory.");
        }
        return 1;
    }

    char rel_path[PATH_MAX];
    if (!ftp_make_relative_path(session, abs_path, rel_path, sizeof(rel_path))) {
        send_response(session, 550, "Failed to create directory.");
        return 1;
    }

    send_response(session, 257, "\"%s\" directory created.", rel_path);
    return 1;
}

/*
 * 处理RMD命令
 * session: FTP会话
 * param: 命令参数
 * return: 处理结果，1表示已处理
 */
int handle_RMD(ftp_session *session, const char *param)
{
    if (!param || param[0] == '\0') {
        send_response(session, 501, "Syntax error in parameters.");
        return 1;
    }

    char abs_path[PATH_MAX];
    if (!ftp_resolve_existing_directory(session, param, abs_path, sizeof(abs_path))) {
        send_response(session, 550, "Failed to remove directory.");
        return 1;
    }

    if (strcmp(abs_path, session->root) == 0) {
        send_response(session, 550, "Cannot remove root directory.");
        return 1;
    }

    if (rmdir(abs_path) < 0) {
        if (errno == ENOENT) {
            send_response(session, 550, "Directory does not exist.");
        }
        else {
            send_response(session, 550, "Failed to remove directory.");
        }
        return 1;
    }

    send_response(session, 250, "Directory successfully removed.");
    return 1;
}

/*
 * 处理REST命令
 * session: FTP会话
 * param: 命令参数
 * return: 处理结果，1表示已处理
 */
int handle_REST(ftp_session *session, const char *param)
{
    if (!param || param[0] == '\0') {
        send_response(session, 501, "Syntax error in parameters.");
        return 1;
    }

    char *endptr = NULL;
    errno = 0;
    long long value = strtoll(param, &endptr, 10);
    if (errno != 0 || endptr == param || *endptr != '\0' || value < 0) {
        send_response(session, 501, "Invalid restart position.");
        return 1;
    }

    session->restart_pos = (off_t)value;
    send_response(session, 350, "Restart position accepted (%lld).", value);
    return 1;
}

// 处理客户端会话
void *handle_client(void *arg)
{
    ftp_session *session = arg;
    char cmd_line[FTP_CMD_MAX_LEN];

    send_response(session, 220, "Anonymous FTP server ready.");

    while (1) {
        // 接收一行命令并存入cmd_line缓冲区
        int n = recv_command_line(session->ctrl_fd, cmd_line, sizeof(cmd_line));
        if (n == 0) {
            // printf("Client (fd=%d) disconnected\n", session->ctrl_fd);
            break;
        } else if (n < 0) {
            if (n == -2) {
                send_response(session, 500, "Line too long.");
            } 
            // else
            // {
            //     printf("Error reading from client (fd=%d): %s(%d)\n",
            //             session->ctrl_fd, strerror(errno), errno);
            // }
            break;
        }

        char verb[16];
        const char *param = NULL;
        // 解析命令行，提取动词和参数
        if (!parse_command(cmd_line, verb, sizeof(verb), &param)) {
            send_response(session, 500, "Syntax error, command unrecognized.");
            continue;
        }

        // 如果未登录且命令不允许未登录执行，发送530响应
        if (!session->logged_in && !is_command_allowed_without_login(verb)) {
            send_response(session, 530, "Please login with command USER and PASS.");
            continue;
        }

        // 分发命令到对应的处理函数，如果处理失败则发送502响应
        if (!dispatch_command(session, verb, param)) {
            send_response(session, 502, "Command not implemented.");
        }

        if (session->ctrl_fd == -1) {
            break;
        }
    }

    cleanup_session(session);
    return NULL;
}

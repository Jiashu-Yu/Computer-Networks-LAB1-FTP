#include <dirent.h>
#include <sys/stat.h>

#include "config.h"
#include "session.h"
#include "data_channel.h"
#include "socket_util.h"
#include "path_utils.h"

/*
 * 处理RETR命令
 * session: FTP会话
 * param: 命令参数（文件路径）
 * return: 1表示已处理
 */
int handle_RETR(ftp_session *session, const char *param)
{
    char abs_path[PATH_MAX];
    int fd = -1;
    int data_fd = -1;
    bool data_error = false;
    bool disk_error = false;
    off_t restart_offset = session->restart_pos;

    // 解析并验证路径
    if (!ftp_resolve_existing(session, param, abs_path, sizeof(abs_path))) {
        send_response(session, 550, "Failed to open file."); // 路径非法或越界
        session->restart_pos = 0; // 重置断点续传位置
        return 1;
    }

    fd = open(abs_path, O_RDONLY); // 只读方式打开文件
    if (fd < 0) {
        send_response(session, 550, "Failed to open file.");
        session->restart_pos = 0; // 重置断点续传位置
        return 1;
    }

    // 如果设置了断点续传位置，调整文件偏移
    if (restart_offset > 0) {
        struct stat st;
        if (fstat(fd, &st) < 0) {
            send_response(session, 451, "Cannot stat file.");
            close(fd);
            session->restart_pos = 0;
            return 1;
        }
        if (restart_offset > st.st_size) {
            send_response(session, 551, "Invalid restart position.");
            close(fd);
            session->restart_pos = 0;
            return 1;
        }
        if (lseek(fd, restart_offset, SEEK_SET) < 0) {
            send_response(session, 451, "Unable to seek to restart position.");
            close(fd);
            session->restart_pos = 0;
            return 1;
        }
    }

    // 建立数据通道
    data_fd = open_data_connection(session);
    if (data_fd < 0) {
        close(fd);
        session->restart_pos = 0;
        return 1;
    }

    send_response(session, 150, "Opening binary mode data connection.");

    char buf[8192];
    // 读取文件直到结束或出错，通过data socket发送给client
    while(1) {
        ssize_t n = read(fd, buf, sizeof(buf));
        // 读取到EOF就跳出
        if (n == 0) {
            break; 
        }
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            disk_error = true;
            break;
        }

        ssize_t sent = 0; // 发送了多少字节
        while (sent < n) {
            ssize_t w = write(data_fd, buf + sent, n - sent);
            if (w < 0) {
                if (errno == EINTR) {
                    continue;
                }
                data_error = true;
                break;
            }
            if (w == 0) {
                data_error = true;
                break;
            }
            sent += w;
        }
        if (data_error) {
            break;
        }
    }

    close(fd);
    fd = -1;
    close_data_connection(session);
    data_fd = -1;

    if (data_error) {
        send_response(session, 426, "Connection closed; transfer aborted.");
        session->restart_pos = 0;
        return 1;
    } else if (disk_error) {
        send_response(session, 451, "Requested action aborted. Server had trouble reading the file from disk.");
        session->restart_pos = 0;
        return 1;
    }

    send_response(session, 226, "Transfer complete.");
    session->restart_pos = 0;
    return 1;
}


/*
 * 处理STOR命令
 * session: FTP会话
 * param: 命令参数（文件路径）
 * return: 1表示已处理
 */
int handle_STOR(ftp_session *session, const char *param)
{
    session->restart_pos = 0;

    char abs_path[PATH_MAX];
    // 解析并规范化路径
    if (!ftp_resolve_for_creation(session, param, abs_path, sizeof(abs_path), NULL, 0)) {
        send_response(session, 550, "Failed to open file.");
        return 1;
    }

    // 创建或覆盖文件
    int fd = open(abs_path, O_WRONLY | O_CREAT | O_TRUNC, 0666); 
    if (fd < 0) {
        send_response(session, 550, "Failed to open file.");
        return 1;
    }

    // 打开数据连接
    int data_fd = open_data_connection(session);
    if (data_fd < 0) {
        close(fd);
        return 1;
    }

    send_response(session, 150, "Opening binary mode data connection.");

    char buf[8192];
    bool data_error = false;
    bool disk_error = false;

    while (1) {
        ssize_t n = read(data_fd, buf, sizeof(buf));
        if (n == 0) {
            break;
        }
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            data_error = true;
            break;
        }

        ssize_t written = 0;
        while (written < n) {
            ssize_t w = write(fd, buf + written, n - written);
            if (w < 0) {
                if (errno == EINTR) {
                    continue;
                }
                disk_error = true;
                break;
            }
            if (w == 0) {
                disk_error = true;
                break;
            }
            written += w;
        }
        if (disk_error) {
            break;
        }
    }

    close(fd);
    close_data_connection(session);

    if (data_error) {
        send_response(session, 426, "Connection closed; transfer aborted.");
        return 1;
    } else if (disk_error) {
        send_response(session, 451, "Requested action aborted. Local error in processing.");
        return 1;
    } 

    send_response(session, 226, "Transfer complete.");
    return 1;
}
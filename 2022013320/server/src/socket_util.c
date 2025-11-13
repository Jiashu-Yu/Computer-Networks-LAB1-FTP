#include "config.h"
#include "session.h"
#include "socket_util.h"

// 向连接socket发送信息
void write_message(int connfd, const char *message)
{
    int p = 0;
    int len = strlen(message);
    while (p < len) {
        int n = write(connfd, message + p, len - p);		//write函数不保证所有的数据写完，可能中途退出
        if (n < 0) {
            fprintf(stderr, "Error write(): %s(%d)\n", strerror(errno), errno);
            return;
        } else {
            p += n;
        }			
    }
}

// 等待并接受一个新的连接请求，返回连接socket的文件描述符
int get_connection_fd()
{
    int connfd;
    while(1){
        //等待client的连接 -- 阻塞函数
        if ((connfd = accept(server_listenfd, NULL, NULL)) == -1) {
            fprintf(stderr, "Error accept(): %s(%d)\n", strerror(errno), errno);
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        else{
            // 成功接受连接
            // printf("Accepted new connection, fd=%d\n", connfd);
            break;
        }    
    }
    
    return connfd;
}

// 获取服务器的IP地址
int get_server_ip(const ftp_session *session, struct in_addr *out)
{
    if (!session || session->ctrl_fd == -1 || !out) {
        return 0;
    }

    // 使用getsockname获取本地地址
    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);
    if (getsockname(session->ctrl_fd, (struct sockaddr *)&addr, &addrlen) < 0) {
        return 0;
    }

    *out = addr.sin_addr;
    return 1;
}


// 打开数据连接（PORT或PASV模式）
int open_data_connection(ftp_session *session) {
    if (!session) {
        return -1;
    }

    if (session->data_fd != -1) {
        close(session->data_fd);
        session->data_fd = -1;
    }

    if (session->data_mode == FTP_DATA_MODE_PORT) {
        // PORT模式，连接客户端提供的地址
        int data_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (data_fd < 0) {
            send_response(session, 425, "Cannot open data connection.");
            return -1;
        }

        if (connect(data_fd, (struct sockaddr *)&session->port_addr, sizeof(session->port_addr)) < 0) {
            close(data_fd);
            send_response(session, 425, "Cannot open data connection.");
            return -1;
        }

        session->data_fd = data_fd;
        return data_fd;
    } 
    else if (session->data_mode == FTP_DATA_MODE_PASV) {
        // PASV模式，接受客户端的连接
        struct sockaddr_in client_addr;
        socklen_t addrlen = sizeof(client_addr);
        int data_fd = accept(session->pasv_listenfd, (struct sockaddr *)&client_addr, &addrlen);
        if (data_fd < 0) {
            send_response(session, 425, "Cannot open data connection.");
            return -1;
        }

        if (session->pasv_listenfd != -1) {
            close(session->pasv_listenfd);
            session->pasv_listenfd = -1;
        }

        session->data_fd = data_fd;
        return data_fd;
    } 
    else {
        send_response(session, 425, "No data connection mode set.");
        return -1;
    }
}


// 关闭数据连接
int close_data_connection(ftp_session *session) {
    if (!session) {
        return -1;
    }

    if (session->data_fd != -1) {
        close(session->data_fd);
        session->data_fd = -1;
    }

    if (session->data_mode == FTP_DATA_MODE_PASV && session->pasv_listenfd != -1) {
        close(session->pasv_listenfd);
        session->pasv_listenfd = -1;
    }

    session->data_mode = FTP_DATA_MODE_NONE;
    return 0;
}
### FTP 整体流程概述（基于当前实现）

1. **服务器启动与监听**
   - `server.c` 中的 `main` 调用 `socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)` 创建监听 socket，并在 `init_server` 中将其绑定到 `0.0.0.0:<port>`（默认 21），随后执行 `listen` 等待控制连接。
   - 主循环使用 `accept`（封装在 `get_connection_fd`）阻塞等待客户端；每次成功后分配一个 `ftp_session` 并创建线程 `handle_client` 处理该控制连接。

2. **控制连接建立与登录**
   - 客户端使用 `connect` 连接服务器 `IP:port`（例如 `server_ip:21`）建立控制 socket。
   - 服务器线程立即发送 `220` 欢迎消息，之后循环读取命令：
     - 客户端先发 `USER anonymous` → 服务器验证后返回 `331`。
     - 随后发 `PASS <email>` → 服务器设置 `logged_in = true` 并返回 `230`。
   - 控制连接在整个会话周期保持打开状态，所有 FTP 命令和响应都通过这个 TCP 通道交换。

3. **数据通道准备**
   - **PORT 模式（主动模式）**
     - 客户端先在本地随机开一个监听 socket（通常绑定 `client_ip:client_port`，端口 > 1024）。
     - 客户端通过控制通道发送 `PORT h1,h2,h3,h4,p1,p2`，携带自己的 IP 和监听端口。
     - 服务器接收后关闭旧数据 socket，记录 `session->port_addr`，待会儿在 `open_data_connection` 调用 `connect` 主动连回客户端的 `client_ip:client_port`。
   - **PASV 模式（被动模式）**
     - 客户端发 `PASV`。
     - 服务器调用 `socket` 新建被动监听 socket，绑定到服务器控制连接的本地 IP（例如 `server_ip`）和随机高位端口（20000-65535），`listen` 后返回 `227 Entering Passive Mode (h1,h2,h3,h4,p1,p2)`。
     - 客户端解析响应，通过 `connect` 主动连接服务器提供的 `server_ip:pasv_port` 建立数据通道。

4. **文件/目录操作（数据传输阶段）**
   - 在正确的模式下，客户端下发 `RETR`、`STOR`、`LIST` 等命令：
     - 服务器收到命令后调用 `open_data_connection`：
       - PORT：服务器创建 socket 并连接到记录的 `client_ip:client_port`。
       - PASV：服务器在 `pasv_listenfd` 上 `accept` 客户端连接，得到数据 FD。
     - 服务器返回 `150 Opening binary mode data connection.`，开始通过数据 socket 读写：
       - `RETR`：服务器 `read` 磁盘文件，经 `write(data_fd, ...)` 发送给客户端。
       - `STOR`：服务器 `read(data_fd, ...)` 接收客户端上传，再 `write` 到本地文件。
       - `LIST`（客户端实现）：客户端在数据通道上发送目录列表结果。
     - 传输完成后关闭数据 socket（`close_data_connection`）并返回 `226 Transfer complete.`。异常时根据情况返回 `425/426/451` 等。

5. **会话结束**
   - 客户端发送 `QUIT`，服务器 `send_response(221, ...)` 并关闭控制 FD。
   - 线程退出，`cleanup_session` 回收资源；主线程继续监听新的控制连接。

### Socket 与 IP/端口关系总结

| 阶段 | 动作 | 发起方 | 使用的 IP / 端口 | 说明 |
|------|------|--------|------------------|------|
| 服务器启动 | `socket` → `bind` → `listen` | 服务器 | `0.0.0.0:<listen_port>`（默认 21） | 控制监听 socket（`server_listenfd`） |
| 控制连接 | `connect` | 客户端 | `server_ip:<listen_port>` | 建立控制通道（TCP） |
| PORT 模式 | 客户端先 `socket` + `bind` + `listen` | 客户端 | `client_ip:<port>` | 客户端提供给服务器的回连地址 |
| PORT 建立数据连接 | `connect` | 服务器 | 连接至 `client_ip:<port>` | `open_data_connection` 内执行 |
| PASV 模式 | 服务器 `socket` + `bind` + `listen` | 服务器 | `server_ip:<pasv_port>`（随机 20000-65535） | 并在 `227` 响应中告知客户端 |
| PASV 建立数据连接 | `connect` | 客户端 | 连接至 `server_ip:<pasv_port>` | 客户端主动发起 |
| 文件传输 | `read`/`write` | 双方 | 数据通道 socket | 结束后双方关闭数据通道 |
| 退出 | `close` 控制 socket | 服务器/客户端 | 控制通道 | `QUIT` 之后双方关闭 |

通过上述流程，控制通道始终固定在服务器监听端口上，而数据通道则由 PORT/PASV 协商确定，保证了 FTP 的双通道通信模型。
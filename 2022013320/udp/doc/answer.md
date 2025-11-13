# How to write a chat program (two clients chat with each other) with UDP?
假设两个对话的client分别叫clientA和clientB。
先在两个client上分别创建一个UDP socket，然后分别bind到portA和portB上。
- clientA本地的socket bind到了portA上，监听portA收到的信息，A要发送消息需要发送到IP_B:portB
- clientB本地的socket bind到了portB上，监听portB收到的信息，B要发送消息需要发送到IP_A:portA

双方在启动的时候给对方发一个"hello",这样一定有一方能先收到，收到后回复对方一个"hello"，
这样双方都能通过recvfrom()拿到对方的地址信息。
此时双方都掌握了对方的IP地址和端口号（假设端口号是提前约定好的）。
随后进入收发循环，两个client在用sendto()发送完消息后立即转到接收模式。
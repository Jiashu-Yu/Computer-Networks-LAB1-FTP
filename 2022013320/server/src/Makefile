# 目标程序名
TARGET := server

# 源文件列表
SRCS := server.c \
        session.c \
        socket_util.c \
        data_channel.c \
        file_trans.c \
        path_utils.c

# 对应的目标文件（自动替换 .c -> .o）
OBJS := $(SRCS:.c=.o)

# 公共编译选项
CFLAGS := -Wall -Wextra -std=c11 -pthread
LDFLAGS := -pthread

# 默认目标（直接执行 make）
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $@ $(LDFLAGS)

# 通用的 .c -> .o 规则
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

.PHONY: clean
clean:
	rm -f $(OBJS) $(TARGET)
TARGET := client
CFLAGS := -Wall -Wextra -std=c11
LDFLAGS :=

SRCS := client.c \
    commands.c \
    control.c \
    data_conn.c \
    net_utils.c

OBJS := $(SRCS:.c=.o)


$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $@ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

.PHONY: clean
clean:
	rm -f $(OBJS) $(TARGET)
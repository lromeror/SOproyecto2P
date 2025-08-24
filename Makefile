CC = gcc

LDFLAGS = -pthread -lncurses -lrt

SRCS = main.c belt_process.c order_generator.c ui_control_process.c

OBJS = $(SRCS:.c=.o)

TARGET = burger_machine

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS) $(LDFLAGS)


%.o: %.c shared_data.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean
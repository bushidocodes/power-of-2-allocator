CC      = gcc
CFLAGS  = -Wall -Wextra -std=c11 -O2
TARGET  = p2test
SRCS    = main.c p2malloc.c
OBJS    = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c p2malloc.h ps_list.h
	$(CC) $(CFLAGS) -c -o $@ $<

run: $(TARGET)
	./$(TARGET)

clean:
	-rm -f $(OBJS) $(TARGET) $(TARGET).exe

.PHONY: all run clean

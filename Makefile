CC     = gcc
CFLAGS = -Wall -Wextra -std=c11 -O2

# Link pthreads on POSIX; Windows uses native SRWLOCK (no extra lib needed)
ifeq ($(OS),Windows_NT)
    LDFLAGS =
else
    LDFLAGS = -lpthread
endif

TARGET      = p2test
TEST_TARGET = test_p2malloc

ALLOCATOR_SRCS = p2malloc.c
ALLOCATOR_OBJS = $(ALLOCATOR_SRCS:.c=.o)

all: $(TARGET)

$(TARGET): main.o $(ALLOCATOR_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(TEST_TARGET): test_p2malloc.o $(ALLOCATOR_OBJS) unity/unity.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

unity/unity.o: unity/unity.c unity/unity.h unity/unity_internals.h
	$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.c p2malloc.h ps_list.h
	$(CC) $(CFLAGS) -c -o $@ $<

test_p2malloc.o: test_p2malloc.c p2malloc.h unity/unity.h
	$(CC) $(CFLAGS) -Iunity -c -o $@ $<

run: $(TARGET)
	./$(TARGET)

test: $(TEST_TARGET)
	./$(TEST_TARGET)

clean:
	-rm -f $(ALLOCATOR_OBJS) main.o test_p2malloc.o unity/unity.o
	-rm -f $(TARGET) $(TARGET).exe $(TEST_TARGET) $(TEST_TARGET).exe

.PHONY: all run test clean

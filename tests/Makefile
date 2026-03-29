CC := gcc
CFLAGS := -g -Wall -Wextra -std=c11 -I../src -D_XOPEN_SOURCE=700

QUEUE_SRCS := test_queue.c ../src/utils/queue.c
QUEUE_OBJS := $(QUEUE_SRCS:.c=.o)
QUEUE_TARGET := queue_test

URI_TEMPLATE_SRCS := test_uri_template.c ../src/utils/uri_template.c
URI_TEMPLATE_OBJS := $(URI_TEMPLATE_SRCS:.c=.o)
URI_TEMPLATE_TARGET := uri_template_test

TARGETS := $(QUEUE_TARGET) $(URI_TEMPLATE_TARGET)
ALL_OBJS := $(QUEUE_OBJS) $(URI_TEMPLATE_OBJS)

all: $(TARGETS)

$(QUEUE_TARGET): $(QUEUE_OBJS)
	$(CC) $(CFLAGS) $^ -o $@

$(URI_TEMPLATE_TARGET): $(URI_TEMPLATE_OBJS)
	$(CC) $(CFLAGS) $^ -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

test: all
	./$(QUEUE_TARGET)
	./$(URI_TEMPLATE_TARGET)

clean:
	rm -f $(ALL_OBJS) $(TARGETS)

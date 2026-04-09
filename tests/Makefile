CC := gcc
CFLAGS := -g -Wall -Wextra -std=c11 -I../src -D_XOPEN_SOURCE=700

QUEUE_SRCS := test_queue.c ../src/utils/queue.c
QUEUE_OBJS := $(QUEUE_SRCS:.c=.o)
QUEUE_TARGET := queue_test

URI_TEMPLATE_SRCS := test_uri_template.c ../src/utils/uri_template.c
URI_TEMPLATE_OBJS := $(URI_TEMPLATE_SRCS:.c=.o)
URI_TEMPLATE_TARGET := uri_template_test

WAL_SRCS := test_wal_scan_recover.c \
	../src/wal/wal.c \
	../src/utils/crc.c \
	../src/utils/logging.c
WAL_OBJS := $(WAL_SRCS:.c=.o)
WAL_TARGET := wal_scan_recover_test

OPERATE_SEARCH_SRCS := test_operate_search.c \
	../src/utils/logging.c \
	../src/utils/os_specific.c \
	../src/utils/list.c \
	../src/utils/stack.c \
	../src/utils/queue.c \
	../src/utils/uri_template.c \
	../src/event/event.c \
	../src/wal/wal.c \
	../src/utils/hashtable_u64.c \
	../src/utils/crc.c \
	../src/utils/radix_tree.c \
	../src/ui/tty.c \
	../src/tree/tree_view.c \
	../src/tree/tree_overlay.c \
	../src/tree/tree_storage.c \
	../src/layout/mindmap_layout.c \
	../src/ui/ui.c \
	../src/command/command.c \
	../src/operate/operate.c \
	../src/connect/connect.c \
	../src/app/app.c
OPERATE_SEARCH_OBJS := $(OPERATE_SEARCH_SRCS:.c=.o)
OPERATE_SEARCH_TARGET := operate_search_test

TARGETS := $(QUEUE_TARGET) $(URI_TEMPLATE_TARGET) $(WAL_TARGET) $(OPERATE_SEARCH_TARGET)
ALL_OBJS := $(QUEUE_OBJS) $(URI_TEMPLATE_OBJS) $(WAL_OBJS) $(OPERATE_SEARCH_OBJS)

all: $(TARGETS)

$(QUEUE_TARGET): $(QUEUE_OBJS)
	$(CC) $(CFLAGS) $^ -o $@

$(URI_TEMPLATE_TARGET): $(URI_TEMPLATE_OBJS)
	$(CC) $(CFLAGS) $^ -o $@

$(WAL_TARGET): $(WAL_OBJS)
	$(CC) $(CFLAGS) $^ -o $@

$(OPERATE_SEARCH_TARGET): $(OPERATE_SEARCH_OBJS)
	$(CC) $(CFLAGS) $^ -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

test: all
	./$(QUEUE_TARGET)
	./$(URI_TEMPLATE_TARGET)
	./$(WAL_TARGET)
	./$(OPERATE_SEARCH_TARGET)

clean:
	rm -f $(ALL_OBJS) $(TARGETS)

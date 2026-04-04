CC      := gcc
CFLAGS  := -g3 -O0 -Wall -Wextra -std=c11 -D_XOPEN_SOURCE=700 -I src -MMD -MP \
           -Werror=incompatible-pointer-types

ifeq ($(shell uname -s),Darwin)
CFLAGS += -D_DARWIN_C_SOURCE
endif

# Core library sources
CORE_SRCS := \
	src/utils/logging.c \
	src/utils/os_specific.c \
	src/utils/list.c \
	src/utils/stack.c \
	src/utils/queue.c \
	src/utils/uri_template.c \
	src/event/event.c \
	src/wal/wal.c \
	src/utils/hashtable_u64.c \
	src/utils/crc.c \
	src/utils/radix_tree.c \
	src/ui/tty.c \
	src/tree/tree_view.c \
	src/tree/tree_overlay.c \
	src/tree/tree_storage.c \
	src/layout/mindmap_layout.c

# UI and application sources
APP_SRCS := \
	src/ui/ui.c \
	src/command/command.c \
	src/operate/operate.c \
	src/connect/connect.c \
	src/app/app.c \
	src/main.c

# Object files
CORE_OBJS := $(CORE_SRCS:.c=.o)
APP_OBJS := $(APP_SRCS:.c=.o)
DEPS := $(CORE_OBJS:.o=.d) $(APP_OBJS:.o=.d)

TARGET := bin/universe-mindmap

.PHONY: all clean run help install uninstall loc

all: $(TARGET)

$(TARGET): $(CORE_OBJS) $(APP_OBJS)
	mkdir -p bin
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)
	@echo "✓ compilation success: $(TARGET)"

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

run: $(TARGET)
	$(TARGET)

install: $(TARGET)
	mkdir -p /usr/local/bin
	cp $(TARGET) /usr/local/bin/universe-mindmap
	cp $(TARGET) /usr/local/bin/umm

uninstall:
	rm -f /usr/local/bin/universe-mindmap
	rm -f /usr/local/bin/umm

loc:
	@echo "Counting lines of C code in src/ and tests/ ..."
	@find src tests -type f \( -name '*.c' -o -name '*.h' \) -print0 | xargs -0 wc -l
	
clean:
	rm -f $(CORE_OBJS) $(APP_OBJS) $(TARGET)
	rm -f $(CORE_OBJS:.o=.d) $(APP_OBJS:.o=.d)

help:
	@echo "universe-mindmap - Build System"
	@echo ""
	@echo "Targets:"
	@echo "  make all      Build the application (default)"
	@echo "  make run      Build and run the application"
	@echo "  make clean    Remove build artifacts"
	@echo "  make help     Show this help"
	@echo "  make install  Install the application"
	@echo "  make uninstall Uninstall the application"
	@echo "  make loc      Count lines of code in C source/header files"

-include $(DEPS)
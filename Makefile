CC := clang
CFLAGS := -I. -Isrc -std=c17 -Wall -Wextra -Werror
DEBUG_FLAGS := -g
RELEASE_FLAGS := -O2
SANITIZE_FLAGS := -g -fsanitize=address,undefined

SRC := src/main.c \
       src/buffer/buffer.c \
       src/editor/editor.c \
       src/editor/edit_ops.c \
       src/fileio/fileio.c \
       src/input/input.c \
       src/terminal/terminal.c \
       src/terminal/signals.c \
       src/render/escape.c \
       src/render/status.c \
       src/render/render.c \
       src/viewport/geometry.c \
       src/viewport/viewport.c \
       src/app/app.c

HEADERS := $(wildcard src/**/*.h)

UNIT_SRC := tests/unit_test.c \
            src/buffer/buffer.c \
            src/editor/editor.c \
            src/editor/edit_ops.c \
            src/fileio/fileio.c \
            src/viewport/geometry.c \
            src/viewport/viewport.c

UNIT_BIN := tests/unit_tests
TARGET := oldvim

INPUT := text.rst

.PHONY: all clean run test test-unit test-sanitize debug release

all: $(TARGET)

$(TARGET): $(SRC) $(HEADERS)
	$(CC) $(CFLAGS) $(DEBUG_FLAGS) $(SRC) -o $@

debug: $(SRC) $(HEADERS)
	$(CC) $(CFLAGS) $(DEBUG_FLAGS) $(SRC) -o $(TARGET)

release: $(SRC) $(HEADERS)
	$(CC) $(CFLAGS) $(RELEASE_FLAGS) $(SRC) -o $(TARGET)

run: $(TARGET)
	./$(TARGET) $(INPUT)

$(UNIT_BIN): $(UNIT_SRC) $(HEADERS)
	$(CC) $(CFLAGS) $(DEBUG_FLAGS) $(UNIT_SRC) -o $@

test-unit: $(UNIT_BIN)
	./$(UNIT_BIN)

test: test-unit $(TARGET)
	python3 tests/test_editor.py

test-sanitize:
	$(CC) $(CFLAGS) $(SANITIZE_FLAGS) $(SRC) -o $(TARGET)
	$(CC) $(CFLAGS) $(SANITIZE_FLAGS) $(UNIT_SRC) -o $(UNIT_BIN)
	./$(UNIT_BIN)
	python3 tests/test_editor.py

clean:
	rm -f $(TARGET) $(UNIT_BIN)

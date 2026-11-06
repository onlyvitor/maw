CC := clang
CFLAGS := -I. -std=c17 -Wall -Wextra -Werror
DEBUG_FLAGS := -g
RELEASE_FLAGS := -O2
SANITIZE_FLAGS := -g -fsanitize=address,undefined

SRC := $(wildcard *.c) $(wildcard io/*/*.c) $(wildcard fileio/*.c) $(wildcard editor/*.c)
HEADERS := $(wildcard *.h) $(wildcard io/*/*.h) $(wildcard fileio/*.h) $(wildcard editor/*.h)
UNIT_SRC := tests/unit_test.c editor/editor.c fileio/fileio.c
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

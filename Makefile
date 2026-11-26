CC       = gcc
CFLAGS   = -O2 -Wall -Wextra -Wno-unused-result -Wno-format-truncation
LDFLAGS  =
LDLIBS   = -lm
TARGET   = jvma
SRC      = $(sort $(wildcard src/*.c))
HDRS     = $(wildcard src/*.h)
LIB_SRCS = $(filter-out src/main.c,$(SRC))
TEST_SRC = tests/test_unit.c
TEST_BIN = tests/test_unit
TEST_ASAN = tests/test_unit_asan
SAN_FLAGS = -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer

all: $(TARGET)

$(TARGET): $(SRC) $(HDRS)
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LDFLAGS) $(LDLIBS)

check: $(TEST_BIN)
	LC_ALL=C ./tests/run_tests.sh $(TEST_BIN)

asan: $(TEST_ASAN)
	LC_ALL=C ./tests/run_tests.sh $(TEST_ASAN)

$(TEST_BIN): $(TEST_SRC) $(SRC) $(HDRS)
	$(CC) $(CFLAGS) -o $@ $(TEST_SRC) $(LIB_SRCS) $(LDFLAGS) $(LDLIBS)

$(TEST_ASAN): $(TEST_SRC) $(SRC) $(HDRS)
	$(CC) $(CFLAGS) $(SAN_FLAGS) -o $@ $(TEST_SRC) $(LIB_SRCS) $(LDFLAGS) $(LDLIBS)

PREFIX ?= /usr/local

install: all
	install -D -m 755 $(TARGET) $(DESTDIR)$(PREFIX)/bin/jvma
	@echo "Installed to $(DESTDIR)$(PREFIX)/bin/jvma"

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/jvma

clean:
	rm -f $(TARGET) $(TEST_BIN) $(TEST_ASAN)

.PHONY: all check asan install uninstall clean

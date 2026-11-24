CC       = gcc
CFLAGS   = -O2 -Wall -Wextra -Wno-unused-result -Wno-format-truncation
LDFLAGS  =
LDLIBS   = -lm
TARGET   = jvma
SRC      = jvm_analyzer.c
TEST_SRC = tests/test_unit.c
TEST_BIN = tests/test_unit
TEST_ASAN = tests/test_unit_asan
SAN_FLAGS = -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LDFLAGS) $(LDLIBS)

check: $(TEST_BIN)
	LC_ALL=C ./tests/run_tests.sh $(TEST_BIN)

asan: $(TEST_ASAN)
	LC_ALL=C ./tests/run_tests.sh $(TEST_ASAN)

$(TEST_BIN): $(TEST_SRC) $(SRC)
	$(CC) $(CFLAGS) -o $@ $(TEST_SRC) $(LDFLAGS) $(LDLIBS)

$(TEST_ASAN): $(TEST_SRC) $(SRC)
	$(CC) $(CFLAGS) $(SAN_FLAGS) -o $@ $(TEST_SRC) $(LDFLAGS) $(LDLIBS)

install: all
	install -m 755 $(TARGET) /usr/local/bin/jvma
	@echo "Installed to /usr/local/bin/jvma"

clean:
	rm -f $(TARGET) $(TEST_BIN) $(TEST_ASAN)

.PHONY: all check asan install clean

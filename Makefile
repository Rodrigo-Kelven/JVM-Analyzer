CC      = gcc
CFLAGS  = -O2 -Wall -Wextra -Wno-unused-result -Wno-format-truncation
LDFLAGS = -lm
TARGET  = jvma
SRC     = jvm_analyzer.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

install: all
	install -m 755 $(TARGET) /usr/local/bin/jvma
	@echo "Installed to /usr/local/bin/jvma"

clean:
	rm -f $(TARGET)

.PHONY: all ncursesw install clean

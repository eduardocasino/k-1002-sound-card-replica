CC ?= gcc
CFLAGS ?= -Wall -Wextra -O2 -I.
LDFLAGS ?= -lyaml -lm
SRC := wavegen.c
BINDIR ?= ../bin
TARGET := $(BINDIR)/wavegen

.PHONY: all clean

all: $(TARGET)

$(BINDIR)/:
	mkdir -p $@

$(TARGET): $(SRC) | $(BINDIR)/
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f $(TARGET)

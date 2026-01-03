CC ?= gcc
AS = ca65
LD = ld65

CFLAGS ?= -Wall -Wextra -O2 -I.
LDFLAGS ?= -lasound -lm
BINDIR ?= ../bin
TARGET := $(BINDIR)/notint

.PHONY: all clean

all: $(TARGET)

$(TARGET): notint.c
	@echo "CC $@"
	@$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

# Generic rules

clean:
	rm -f $(TARGET)

CC ?= gcc
CFLAGS ?= -Wall -Wextra -O2 -I.
SRCS := notcmp.c objfile.c
OBJS := $(SRCS:.c=.o)
DEPS := objfile.h
BINDIR ?= ../bin
TARGET := $(BINDIR)/notcmp

.PHONY: all clean

all: $(TARGET)

$(BINDIR)/:
	mkdir -p $@

$(TARGET): $(OBJS) | $(BINDIR)/
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c $(DEPS)
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET)

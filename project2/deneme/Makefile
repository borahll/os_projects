CC := gcc
CFLAGS := -g -Wall
TARGETS := libtsl.a main

all: $(TARGETS)

TSL_SRC := tsl.c
TSL_OBJS := $(TSL_SRC:.c=.o)

libtsl.a: $(TSL_OBJS)
	ar rcs $@ $^

main: main.c libtsl.a
	$(CC) $(CFLAGS) -o $@ $< -L. -ltsl

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -rf core *.o $(TARGETS)


CC := gcc
CFLAGS := -g -Wall
TARGETS := libtsl.a app

all: $(TARGETS)

TSL_SRC := tsl.c
TSL_OBJS := $(TSL_SRC:.c=.o)

libtsl.a: $(TSL_OBJS)
	ar rcs $@ $^

app: app.c libtsl.a
	$(CC) $(CFLAGS) -o $@ $< -L. -ltsl

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -rf core *.o $(TARGETS)
	

CC	:= gcc
CFLAGS := -g -Wall

TARGETS :=  libtsl.a  main

all: $(TARGETS)

TSL_SRC :=  tsl.c
TSL_OBJS := $(TSL_SRC:.c=.o)

libtsl.a: $(TSL_OBJS)
	ar rcs $@ $(TSL_OBJS)

TSL_LIB :=  -L . -l tsl

tsl.o: tsl.c tsl.h
	gcc -c $(CFLAGS) -m32 -o $@ tsl.c

main.o: main.c  tsl.h
	gcc -c $(CFLAGS) -m32  -o $@ app.c

clean:
	rm -rf core  *.o $(TARGETS)
	


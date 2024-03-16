CC=gcc
CFLAGS=-Wall -g
LDFLAGS=-L. -ltsl -lpthread

app: app.o libtsl.so
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

libtsl.so: tsl.o
	$(CC) -shared -o $@ $^ -lpthread

tsl.o: tsl.c tsl.h
	$(CC) $(CFLAGS) -fPIC -c $<

clean:
	rm -f app libtsl.so *.o


CC	:= gcc
CFLAGS := -g -Wall

TARGETS :=  libmf.a  mfserver

# Make sure that 'all' is the first target
all: $(TARGETS)

MF_SRC :=  mf.c
MF_OBJS := $(MF_SRC:.c=.o)

libmf.a: $(MF_OBJS)
	ar rcs $@ $(MF_OBJS)

MF_LIB :=  -L. -lrt -lpthread -lmf

mf.o: mf.c mf.h
	gcc -c $(CFLAGS) -o $@ mf.c


mfserver: mfserver.c  mf.c mf.h
	gcc -c $(CFLAGS)  -o $@ mfserver.c

mfserver: mfserver.o libmf.a mf.o
	gcc $(CFLAGS) -o $@ mfserver.o $(MF_LIB)


clean:
	rm -rf core  *.o *.out *~ $(TARGETS)
	rm -rf  /dev/shm/sem.mf_global_management_sem_* /dev/shm/sharedmemoryname*
	rm -rf  /dev/shm/sem.mf_sem_msgqueue* /dev/shm/sem.semaphore*

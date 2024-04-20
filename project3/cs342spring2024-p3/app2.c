#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <semaphore.h>
#include "mf.h"

#define COUNT 5
#define NUM_QUEUES 3
char *semname1 = "/semaphore1";
char *semname2 = "/semaphore2";
sem_t *sem1, *sem2;
char *mqnames[NUM_QUEUES] = {"msgqueue1", "msgqueue2", "msgqueue3"};
int qids[NUM_QUEUES];

int main(int argc, char **argv)
{
    int ret, i, j;

    sem1 = sem_open(semname1, O_CREAT, 0666, 0); // init sem
    sem2 = sem_open(semname2, O_CREAT, 0666, 0); // init sem

    ret = fork();
    if (ret > 0) {
        // Parent process - P1
        char *bufptr = (char *) malloc (MAX_DATALEN);

        sem1 = sem_open(semname1, 0);
        sem2 = sem_open(semname2, 0);
        mf_connect();

        for (j = 0; j < NUM_QUEUES; j++) {
            mf_create(mqnames[j], 16); // create mq; 16 KB each
            qids[j] = mf_open(mqnames[j]);
        }

        sem_post(sem1);

        for (j = 0; j < NUM_QUEUES; j++) {
            for (i = 0; i < COUNT; ++i) {
                sprintf(bufptr, "%s-%d-%d", mqnames[j], j, i);
                mf_send(qids[j], (void *)bufptr, strlen(bufptr)+1);
            }
        }

        for (j = 0; j < NUM_QUEUES; j++) {
            mf_close(qids[j]);
        }
        mf_disconnect();
        sem_wait(sem2);
        for (j = 0; j < NUM_QUEUES; j++) {
            mf_remove(mqnames[j]); // remove mqs
        }
    }
    else if (ret == 0) {
        // Child process - P2
        char *bufptr = (char *) malloc (MAX_DATALEN);

        sem1 = sem_open(semname1, 0);
        sem2 = sem_open(semname2, 0);
        sem_wait(sem1);

        mf_connect();

        for (j = 0; j < NUM_QUEUES; j++) {
            qids[j] = mf_open(mqnames[j]);
        }

        for (j = 0; j < NUM_QUEUES; j++) {
            for (i = 0; i < COUNT; ++i) {
                mf_recv(qids[j], (void *)bufptr, MAX_DATALEN);
                printf("%s\n", bufptr);
            }
        }

        for (j = 0; j < NUM_QUEUES; j++) {
            mf_close(qids[j]);
        }
        mf_disconnect();
        sem_post(sem2);
    }
    return 0;
}

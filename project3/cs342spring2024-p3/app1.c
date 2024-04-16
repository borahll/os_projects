// Include necessary headers
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <semaphore.h>
#include "mf.h"

// Define constants
#define COUNT 5
#define MAX_DATALEN 256
char *semname1 = "/semaphore1";
char *semname2 = "/semaphore2";
char *mqname1 = "msgqueue1";

int main(int argc, char **argv) {
    int ret, i, qid;
    sem_t *sem1, *sem2;

    // Initialize semaphores
    sem1 = sem_open(semname1, O_CREAT | O_EXCL, 0666, 0);
    sem2 = sem_open(semname2, O_CREAT | O_EXCL, 0666, 0);

    ret = fork();
    if (ret > 0) {
        // Parent process - P1
        char *bufptr = (char *)malloc(MAX_DATALEN);
        sem1 = sem_open(semname1, 0);
        sem2 = sem_open(semname2, 0);
        printf("pid number: %d", getpid());
        mf_connect();
        mf_create(mqname1, 16); // Create mq; 16 KB
        qid = mf_open(mqname1);
        sem_post(sem1);

        for (i = 0; i < COUNT; ++i) {
            sprintf(bufptr, "%s-%d", "MessageData", i);
            mf_send(qid, (void *)bufptr, strlen(bufptr) + 1);
        }
        mf_close(qid);
        mf_disconnect();
        sem_wait(sem2);
        mf_remove(mqname1); // Remove mq
        free(bufptr);
    } else if (ret == 0) {
        // Child process - P2
        char *bufptr = (char *)malloc(MAX_DATALEN);
        sem1 = sem_open(semname1, 0);
        sem2 = sem_open(semname2, 0);
        sem_wait(sem1);
        mf_connect();
        qid = mf_open(mqname1);
        for (i = 0; i < COUNT; ++i) {
            mf_recv(qid, (void *)bufptr, MAX_DATALEN);
            printf("%s\n", bufptr);
        }
        mf_close(qid);
        mf_disconnect();
        sem_post(sem2);
        free(bufptr);
    }

    // Close and unlink semaphores
    sem_close(sem1);
    sem_close(sem2);
    sem_unlink(semname1);
    sem_unlink(semname2);

    return 0;
}

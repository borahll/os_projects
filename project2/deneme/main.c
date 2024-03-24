#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include "tsl.h"

#define MAXCOUNT 50
#define YIELDPERIOD 100

void *foo(void *v)
{
    int count = 1;
    int mytid = tsl_gettid();

    printf("Thread %d started running (first time)\n", mytid);

    while (count <= MAXCOUNT) {
        printf("Thread %d is running (count=%d)\n", mytid, count);
        
        if (count % YIELDPERIOD == 0) {
            tsl_yield(TSL_ANY); // Yield to any other thread
        }

        count++;
    }

    tsl_exit(); // Terminate the thread
}

int main(int argc, char **argv)
{
    int *tids;
    int i;
    int numthreads = 0;

    if (argc != 2) {
        printf("usage: ./app numofthreads\n");
        exit(1); 
    }
    
    numthreads = atoi(argv[1]);

    tids = (int *) malloc(numthreads * sizeof(int));

    tids[0] = tsl_init(ALG_FCFS); // Main thread
    // at tid[0] we have the id of the main thread
    
    for (i = 1; i < numthreads; ++i) {
        tids[i] = tsl_create_thread((void *)&foo, NULL);
        printf("Thread %d created\n", tids[i]);
    }

    // Wait for all threads to finish
    for (i = 1; i < numthreads; ++i) {
        printf("Main: waiting for thread %d\n", tids[i]);
        tsl_join(tids[i]);
        printf("Main: thread %d finished\n", tids[i]);
    }
    
    printf("Main thread calling tsl_exit\n");
    tsl_exit();

    return 0;
}

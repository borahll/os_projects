// app.c
#include <stdio.h>
#include "tsl.h"

void *thread_func(void *arg) {
    long tid = (long)arg;
    printf("Thread %ld: Hello, World!\n", tid);
    return NULL;
}

int main() {
    // Initialize the thread library with FCFS scheduling algorithm
    tsl_init(ALG_FCFS);

    // Create threads
    int thread1, thread2;
    tsl_create_thread(thread_func, (void *)1);
    tsl_create_thread(thread_func, (void *)2);

    // This main thread will also execute the function
    thread_func((void *)0);

    // Main thread yields to other threads
    tsl_yield(TSL_ANY);

    return 0;
}

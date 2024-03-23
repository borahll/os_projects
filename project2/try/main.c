#include <stdio.h>
#include <stdlib.h>
#include <ucontext.h>
#include "library_interface.c"
// Thread start function 1
void thread_function1(void *arg) {
    for (int i = 0; i < 5; i++) {
        printf("Thread 1 is running\n");
        sleep(1); // Simulate some work
    }
    tsl_exit(); // Ensure to call tsl_exit to clean up and terminate the thread properly
}

// Thread start function 2
void thread_function2(void *arg) {
    for (int i = 0; i < 5; i++) {
        printf("Thread 2 is running\n");
        sleep(1); // Simulate some work
    }
    tsl_exit(); // Ensure to call tsl_exit to clean up and terminate the thread properly
}
int main() {
    // Initialize the Thread Support Library with a scheduling algorithm of your choice
    scheduler_init(RR); // Initialize the scheduler with Round Robin for example

    // Create two threads
    if (tsl_init(FIFO) != TSL_SUCCESS) { // Ensure the scheduler algorithm and tsl_init match
        fprintf(stderr, "Failed to initialize the threading library.\n");
        return EXIT_FAILURE;
    }
    int tid1 = tsl_create_thread(thread_function1, NULL);
    if (tid1 == TSL_ERROR) {
        printf("Failed to create thread 1\n");
        return 1;
    }

    int tid2 = tsl_create_thread(thread_function2, NULL);
    if (tid2 == TSL_ERROR) {
        printf("Failed to create thread 2\n");
        return 1;
    }

    printf("Main: Created threads %d and %d\n", tid1, tid2);

    // Now, let's wait for the threads to finish
    tsl_join(tid1);
    tsl_join(tid2);

    printf("Main: Threads have completed\n");

    return 0;
}

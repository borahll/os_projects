#include "tsl.h"
#include <stdlib.h>
#include <stdio.h>

// Structure to represent a thread
typedef struct {
    ucontext_t context;
    void (*func)(void *);
    void *arg;
    int id;
    int active; // 1 if the thread is active, 0 if it's terminated
} Thread;

// Array to hold all threads including the main thread
static Thread threads[TSL_MAXTHREADS];

// The id of the currently running thread
static int current_thread_id;

// The total number of threads (including the main thread)
static int total_threads = 1;

// The scheduling algorithm used
static int scheduling_algorithm;

// Initialize the thread library
int tsl_init(int salg) {
    // Initialize the main thread
    threads[TID_MAIN].id = TID_MAIN;
    threads[TID_MAIN].active = 1;
    current_thread_id = TID_MAIN;
    scheduling_algorithm = salg;
    return TSL_SUCCESS;
}

// Helper function to create a new context for a thread
void create_thread_context(Thread *thread, void (*func)(void *), void *arg) {
    getcontext(&thread->context);
    thread->context.uc_stack.ss_sp = malloc(TSL_STACKSIZE);
    thread->context.uc_stack.ss_size = TSL_STACKSIZE;
    thread->context.uc_link = NULL;
    thread->func = func;
    thread->arg = arg;
    thread->active = 1;
}

// The thread function wrapper
void thread_wrapper() {
    void (*func)(void *) = threads[current_thread_id].func;
    void *arg = threads[current_thread_id].arg;
    func(arg);
    threads[current_thread_id].active = 0; // Mark the thread as terminated
    tsl_yield(TSL_ANY); // Yield to another thread
}

// Create a new thread
int tsl_create_thread(void (*tsf)(void *), void *targ) {
    if (total_threads >= TSL_MAXTHREADS)
        return TSL_ERROR; // Exceeded maximum number of threads

    Thread *thread = &threads[total_threads++];
    create_thread_context(thread, tsf, targ);
    makecontext(&thread->context, thread_wrapper, 0);
    thread->id = total_threads - 1;

    return thread->id;
}

// Perform a yield to another thread
int tsl_yield(int tid) {
    // Find the next thread to switch to based on the scheduling algorithm
    int next_thread_id = tid;
    if (tid == TSL_ANY) {
        next_thread_id = current_thread_id + 1;
        if (next_thread_id >= total_threads)
            next_thread_id = 0; // Wrap around to the beginning
    }

    // Find the next active thread
    while (!threads[next_thread_id].active) {
        next_thread_id++;
        if (next_thread_id >= total_threads)
            next_thread_id = 0; // Wrap around to the beginning
    }

    // Switch to the next thread
    Thread *next_thread = &threads[next_thread_id];
    Thread *current_thread = &threads[current_thread_id];
    current_thread_id = next_thread_id;
    swapcontext(&current_thread->context, &next_thread->context);

    return TSL_SUCCESS;
}

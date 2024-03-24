#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <malloc.h>
#include <assert.h>
#include <stdbool.h>

/* We want the extra information from these definitions */
#ifndef __USE_GNU
#define __USE_GNU
#endif /* __USE_GNU */
#include <ucontext.h>

#include "tsl.h"

//ThreadControlBlock threads[TSL_MAX_THREADS];
int currentThread = -1; // TID of currently running thread
int nextTid = 0; // Next TID to be assigned
bool library_initialized = false; // Flag to ensure library is initialized

Scheduler scheduler;

void scheduler_init(SchedulingAlgorithm alg){
    scheduler.algorithm = alg;
    scheduler.currentThreadIndex = 0;
    scheduler.threadCount = 1;
    scheduler.runqueueCount = 0; 
    for(int i = 0; i < TSL_MAX_THREADS; i++) {
        scheduler.threads[i] = NULL;
    }
    for(int i = 0; i < TSL_MAX_THREADS; i++) {
        scheduler.runqueue[i] = NULL;
    }
}

void scheduler_add_thread(ThreadControlBlock* tcb) {
    if (scheduler.threadCount < TSL_MAX_THREADS) {
        scheduler.threads[scheduler.threadCount] = tcb;
        scheduler.threadCount++;
        tcb->resumed = false; // ? 

        // Add the new thread to the end of the ready queue
        scheduler.runqueue[scheduler.runqueueCount] = tcb;
        scheduler.runqueueCount++;

        //print runqueue
        printf("Ready queue created: \n");
        for(int i = 0; i < scheduler.runqueueCount; ++i){
            printf("Thread %d", scheduler.runqueue[i]->tid);
        }
        printf("\n");

    } else {
        printf("Maximum number of threads reached!\n");
        free(tcb->stack); // Free allocated stack memory
        free(tcb);        // Free allocated TCB memory
    }
}

int scheduler_next_thread() {
     const char *algorithm_names[] = { "FIFO", "RR" };

    // Declare a variable of type SchedulingAlgorithm
    SchedulingAlgorithm algorithm = scheduler.algorithm; // For example, FIFO is set here
    printf("Scheduler algorithm %d \n", scheduler.algorithm);
    // Print the corresponding string based on the enum value
    //printf("Selected Scheduling Algorithm: %s\n", algorithm_names[scheduler.algorithm]);
    ThreadControlBlock* nextThread = NULL; // Initialize to NULL
    switch (scheduler.algorithm) {
        case ALG_FCFS:
            printf("Using FCFS algorithm");
            if (scheduler.runqueueCount > 0 && scheduler.runqueue[0] != NULL) {
                // Choose the thread to be the first thread in ready queue
                printf("Selected ready queue %d", scheduler.runqueue[0]->tid);
                nextThread = scheduler.runqueue[0];
                // Shift elements to the left to remove the first thread
                for (int i = 1; i < scheduler.runqueueCount; i++) {
                    scheduler.runqueue[i - 1] = scheduler.runqueue[i];
                }
                // Decrement the runqueue count
                scheduler.runqueueCount--;
                // Set the last element to NULL (optional)
                scheduler.runqueue[scheduler.runqueueCount] = NULL;
            }
            break;
        case RR:
            for (int i = scheduler.currentThreadIndex + 1; i < scheduler.currentThreadIndex + 1 + TSL_MAX_THREADS; i++) {
                int idx = i % TSL_MAX_THREADS;
                //printf("INDEX : %d" ,idx);
                //i++;
                const char *state_names[] = { "READY", "RUNNING", "TERMINATED" };

                // Declare a variable of type ThreadState
                //ThreadState state = READY; // For example, READY is set here
                ThreadControlBlock* tcb;
                //Print the corresponding string based on the enum value
                if (scheduler.threads[idx] != NULL){
                    tcb = scheduler.threads[idx];
                    printf("Thread ID: %d\n", tcb->tid);
                printf("Is Active: %s\n", tcb->isActive ? "true" : "false");
                
                const char *state_names[] = { "READY", "RUNNING", "TERMINATED" };
                printf("State: %s\n", state_names[tcb->state]);

                printf("Stack Pointer: %p\n", tcb->stack);

                }
                
                if (scheduler.threads[idx] != NULL && scheduler.threads[idx]->state == READY  && idx ) {
                    
                    nextThread = idx;
                    
                    break;
                }
            }
            break;
        // Case for SJF and SRTF would go here
    }
    if (nextThread != NULL) {
        return nextThread->tid;
    } else {
        return -1; // Indicate no next thread found
    }
}


int tsl_init(int salg) {
    if (library_initialized) return TSL_ERROR; // Ensure this function is only called once

    library_initialized = true;

    // Allocate memory for the main thread's TCB
    ThreadControlBlock *main_tcb = malloc(sizeof(ThreadControlBlock));
    if (main_tcb == NULL) {
        // Handle memory allocation failure
        fprintf(stderr, "Failed to allocate memory for the main thread TCB.\n");
        exit(TSL_ERROR); // Or handle more gracefully
    }

    main_tcb->isActive = true;
    main_tcb->tid = TID_MAIN; // Assigning 0 as the TID for the main thread
    main_tcb->state = RUNNING; // Main thread is already running
    main_tcb->stack = NULL; // Main thread's stack is managed by the OS
    // Initialize the scheduler with the specified algorithm
    scheduler_init((SchedulingAlgorithm)salg);
    scheduler.threads[0] = main_tcb;

    // Use getcontext() to capture the current context of the main thread
    if (getcontext(&main_tcb->context) == -1) {
        // Handle error
        fprintf(stderr, "Failed to get context for the main thread.\n");
        free(main_tcb); // Clean up allocated memory before exiting
        exit(TSL_ERROR);
    }
    
    return TSL_SUCCESS;
}


int tsl_create_thread(void (*tsf)(void *), void *targ) {
    
    if (!library_initialized) {
        fprintf(stderr, "Error: Library not initialized.\n");
        return TSL_ERROR;
    }

    ThreadControlBlock* tcb = (ThreadControlBlock*)malloc(sizeof(ThreadControlBlock));
    if (!tcb) {
        fprintf(stderr, "Error: Failed to allocate memory for the new thread's TCB.\n");
        return TSL_ERROR; // Failed to allocate memory for TCB
    }
    
    tcb->stack = malloc(TSL_STACK_SIZE);
    if (!tcb->stack) {
        free(tcb); // Clean up partially created thread
        return TSL_ERROR; // Failed to allocate stack
    }
    printf("inside2");
    if (getcontext(&tcb->context) == -1) {
        free(tcb->stack);
        free(tcb);
        return TSL_ERROR; // Failed to initialize thread context
    }
printf("inside3");
// Initialize the new thread's TCB
    tcb->tid = scheduler.threadCount; // Assign a unique thread ID
    tcb->isActive = true; // The thread is active
    tcb->state = READY;

    tcb->context.uc_stack.ss_sp = tcb->stack;
    
    tcb->context.uc_stack.ss_size = TSL_STACK_SIZE;
   
    tcb->context.uc_stack.ss_flags = 0;
    
    tcb->context.uc_link = 0;

    printf("thread count: %d ready thread count %d \n", scheduler.threadCount, scheduler.runqueueCount);
    
    // Directly manipulating mcontext to set the instruction pointer and argument.
    // This is highly platform and implementation-specific.
    // The following lines are illustrative and might require adjustment for your specific environment
    // or might not work without inline assembly or other non-standard techniques.
     tcb->context.uc_mcontext.gregs[REG_EIP] = (greg_t)tsf; // Set instruction pointer to start function
     tcb->context.uc_mcontext.gregs[REG_EDI] = (greg_t)targ; // Set first argument to the start function

    // The scheduler is responsible for setting the thread's initial state and tid
    scheduler_add_thread(tcb);
   

    return tcb->tid; // The scheduler_add_thread function now assigns and returns the tid
}
int tsl_yield(int tid) {
    if (!library_initialized) {
        fprintf(stderr, "Error: Library not initialized.\n");
        return TSL_ERROR;
    }

    // Check if current thread index is valid
    if (scheduler.currentThreadIndex < 0 || scheduler.currentThreadIndex >= TSL_MAX_THREADS) {
        fprintf(stderr, "Error: Invalid current thread index.\n");
        return TSL_ERROR;
    }
    printf(" CURRENT:%d\n", scheduler.currentThreadIndex);

    //print the ready queue
    printf("Ready queue\n");

    for(int i = 0; i < scheduler.runqueueCount; i++){
        if (scheduler.runqueue[i] != NULL) {
            printf("Thread %d\n", scheduler.runqueue[i]->tid);
        } 
    }

    // Get the current thread's TCB
    ThreadControlBlock* current_tcb = scheduler.threads[scheduler.currentThreadIndex];

    // Only attempt to yield if the thread has not been marked as resumed
    if (!current_tcb->resumed) {
        // Capture the current context for later resumption
        if (getcontext(&current_tcb->context) == 0) {
            // Mark this thread as having saved its context to prevent immediate re-yielding
            current_tcb->resumed = true;

            // Determine the next thread to switch to
            int nextThread = scheduler_next_thread();
            if (nextThread != -1 && nextThread != scheduler.currentThreadIndex) {
                // Prepare the current thread for switching
                current_tcb->state = READY;

                // Add the yielded thread to ready queue
                scheduler.runqueue[scheduler.runqueueCount] = scheduler.threads[scheduler.currentThreadIndex];
                scheduler.runqueueCount++;

                // Switch to the next thread
                scheduler.currentThreadIndex = nextThread;
                scheduler.threads[nextThread]->state = RUNNING;
                scheduler.threads[nextThread]->resumed = false; // Ensure the next thread starts fresh

                setcontext(&scheduler.threads[nextThread]->context);
                // Execution will continue from here when this thread is resumed
            }
        }
    } else {
        // Clear the resumed flag to indicate that this thread is now actively running
        current_tcb->resumed = false;
    }

    return TSL_SUCCESS;
}






int tsl_join(int tid) {
    // Validate the tid. Assuming TSL_MAX_THREADS is the upper limit.
    printf("entered\n");
    if (tid < 0 || tid >= TSL_MAX_THREADS) {
        fprintf(stderr, "Error: Invalid thread ID passed to tsl_join.\n");
        return TSL_ERROR;
    }
    printf("entered\n");
    

    // Check if the target thread is valid and not yet terminated.
    ThreadControlBlock* target_tcb = scheduler.threads[tid];
    if (target_tcb == NULL) {
        fprintf(stderr, "Error: No thread with ID %d exists.\n", tid);
        return TSL_ERROR;
    }
    printf("Target thread %d found\n", tid);

    if (target_tcb->state == TERMINATED) {
        // If already terminated, simply clean up. This is a no-op if cleanup already happened.
        if (target_tcb->stack != NULL) {
            free(target_tcb->stack);
            target_tcb->stack = NULL;
        }
        scheduler.threads[tid] = NULL; // Remove from scheduler
        return TSL_SUCCESS;
    }
    printf("entered3\n");

    //initialize current thread to use in the yield 
    for(int i = 0; i < scheduler.threadCount; ++i){
        if (tid == scheduler.threads[i]->tid){
            scheduler.currentThreadIndex = i;
        }
    }

    // Wait for the thread to terminate, yielding to any thread if the target thread is still running.
    while (target_tcb->state != TERMINATED) {
        
         printf("Waiting for thread %d to terminate\n", tid);
        tsl_yield(TSL_ANY); // Yield to any available thread
        //printf("here1 \n");

    }

    // Post-termination cleanup. This might be redundant if `tsl_exit` already performs cleanup,
    // but it's here to ensure resources are freed if `tsl_exit` hasn't been called.
    printf("Thread %d terminated\n", tid);
    if (target_tcb->stack != NULL) {
        free(target_tcb->stack);
        target_tcb->stack = NULL;
    }
    scheduler.threads[tid] = NULL; // Mark the TCB slot as available for reuse

    return TSL_SUCCESS;
}



int tsl_exit() {
    if (scheduler.currentThreadIndex < 0 || scheduler.currentThreadIndex >= TSL_MAX_THREADS) {
        return -1;
    }

    ThreadControlBlock* currentTcb = scheduler.threads[scheduler.currentThreadIndex];
    if (currentTcb != NULL) {
        currentTcb->state = TERMINATED;

        // Free the stack allocated during thread creation
        free(currentTcb->stack);
        currentTcb->stack = NULL;

        // Mark the TCB slot as available for reuse
        scheduler.threads[scheduler.currentThreadIndex] = NULL;
        scheduler.threadCount--;

        // Ideally, we should switch to the next available thread
        int nextThread = scheduler_next_thread();
        if (nextThread != -1) {
            // There's another thread to run
            scheduler.currentThreadIndex = nextThread;
            scheduler.threads[nextThread]->state = RUNNING;
            setcontext(&scheduler.threads[nextThread]->context);
        } else {
            // No other threads to run; it might be appropriate to exit the application
            // or halt the scheduler if no other work is pending.
            printf("No more threads to run, exiting.\n");
            exit(0);
        }
    }
    return(0);

}


int tsl_cancel(int tid) {
    if (tid <= 0 || tid >= nextTid || scheduler.threads[tid]->state == TERMINATED) {
        return -1; // TSL_ERROR
    }

    // Mark the thread as terminated
    scheduler.threads[tid]->state = TERMINATED;
    return 0; // Success
}
int tsl_gettid() {
    return scheduler.currentThreadIndex;
}
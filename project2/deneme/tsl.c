#include "tsl.h"
#include <ucontext.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

#include <unistd.h>
#include <assert.h>

int currentThread = -1; // TID of currently running thread
int nextTid = 0; // Next TID to be assigned
bool library_initialized = false; // Flag to ensure library is initialized

Scheduler scheduler;

// Initialize the thread library
int tsl_init(int salg) {
    if (library_initialized) return TSL_ERROR; // Ensure this function is only called once

    library_initialized = true;

    // Allocate memory for the main thread's TCB
    ThreadControlBlock *main_tcb = (ThreadControlBlock*)malloc(sizeof(ThreadControlBlock));
    if (!main_tcb) {
        fprintf(stderr, "Failed to allocate memory for the main thread TCB.\n");
        return -1;
    }

    // Initialize main thread's TCB
    main_tcb->tid = TID_MAIN; // Assign ID to the main thread
    main_tcb->isActive = true; // Main thread is active
    main_tcb->state = RUNNING; // Main thread is initially running

    // Initialize the scheduler with the specified algorithm
    scheduler_init((SchedulingAlgorithm)salg);
    scheduler.threads[0] = main_tcb;

    // Use getcontext() to capture the current context of the main thread
    if (getcontext(&main_tcb->context) == -1) {
        // Handle error
        fprintf(stderr, "Failed to get context for the main thread.\n");
        exit(TSL_ERROR);
    }

    // Further initialization based on `salg` if needed
    return TSL_SUCCESS;
}

void scheduler_init(SchedulingAlgorithm alg) {
    scheduler.algorithm = alg;
    scheduler.currentThreadIndex = 0;
    scheduler.threadCount = 1;
    scheduler.runqueueCount = 0; 
    for(int i = 0; i < TSL_MAXTHREADS; i++) {
        scheduler.threads[i] = NULL;
    }
    for(int i = 0; i < TSL_MAXTHREADS; i++) {
        scheduler.runqueue[i] = NULL;
    }
}

int scheduler_next_thread() {
    printf("In next thread func \n");
    printf("Scheduler algorithm %d \n", scheduler.algorithm);
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
        case ALG_RANDOM:
            for (int i = scheduler.currentThreadIndex + 1; i < scheduler.currentThreadIndex + 1 + TSL_MAXTHREADS; i++) {
                int idx = i % TSL_MAXTHREADS;
                if (scheduler.threads[idx] != NULL && scheduler.threads[idx]->state == READY) {
                    nextThread = scheduler.threads[idx];
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


void scheduler_add_thread(ThreadControlBlock* tcb) {
    printf("add thread here\n");
    if (scheduler.threadCount < TSL_MAXTHREADS) {
        scheduler.threads[scheduler.threadCount] = tcb;
        scheduler.threadCount++;

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

int tsl_create_thread(void (*tsf)(void *), void *targ) {
    // Check if the library is initialized
    if (!library_initialized) {
        fprintf(stderr, "Error: Library not initialized.\n");
        return -1;
    }

    // Allocate memory for the new thread's TCB
    ThreadControlBlock* tcb = (ThreadControlBlock*)malloc(sizeof(ThreadControlBlock));
    if (!tcb) {
        fprintf(stderr, "Error: Failed to allocate memory for the new thread's TCB.\n");
        return -1;
    }

    // Allocate memory for the new thread's stack
    tcb->stack = malloc(TSL_STACKSIZE);
    if (!tcb->stack) {
        fprintf(stderr, "Error: Failed to allocate memory for the new thread's stack.\n");
        free(tcb);
        return -1;
    }

    // Initialize the new thread's TCB
    tcb->tid = scheduler.threadCount; // Assign a unique thread ID
    tcb->isActive = true; // The thread is active
    tcb->state = READY;
    tcb->context.uc_stack.ss_sp = tcb->stack;
    tcb->context.uc_stack.ss_size = TSL_STACKSIZE;
    tcb->context.uc_stack.ss_flags = 0;
    tcb->context.uc_link = 0;
    
    printf("thread count: %d ready thread count %d \n", scheduler.threadCount, scheduler.runqueueCount);
    // Add the new thread's TCB to the scheduler
    scheduler_add_thread(tcb);

    return tcb->tid; // Return the thread ID
}



int tsl_yield(int tid) {
    if (!library_initialized) {
        fprintf(stderr, "Error: Library not initialized.\n");
        return TSL_ERROR;
    }

    // Check if current thread index is valid
    if (scheduler.currentThreadIndex < 0 || scheduler.currentThreadIndex >= TSL_MAXTHREADS) {
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

    // Check if current TCB is not null
    if (scheduler.threads[scheduler.currentThreadIndex] == NULL) {
        fprintf(stderr, "Error: Current thread's TCB is null.\n");
        return TSL_ERROR;
    }

    // Yielding to any thread if tid is 0
    if (tid == TSL_ANY) {
        int nextThread = scheduler_next_thread();
        printf("Next thread is %d", nextThread);
        if (nextThread != -1) {
            // Save the current context and switch to the next thread
            if (getcontext(&scheduler.threads[scheduler.currentThreadIndex]->context) == 0) {
                scheduler.threads[scheduler.currentThreadIndex]->state = READY; // Change state to READY
                // Add the yielded thread to ready queue
                scheduler.runqueue[scheduler.runqueueCount] = scheduler.threads[scheduler.currentThreadIndex];
                scheduler.runqueueCount++;
                scheduler.currentThreadIndex = nextThread;
                scheduler.threads[nextThread]->state = RUNNING;
                
                setcontext(&scheduler.threads[nextThread]->context);
            }
            else{
                return TSL_ERROR;
            }
        }
        return nextThread;
    }


    // Specific thread yielding logic
    if (tid > 0 && tid < TSL_MAXTHREADS && scheduler.threads[tid] && scheduler.threads[tid]->state == READY) {
        if (scheduler.currentThreadIndex >= 0 && getcontext(&scheduler.threads[scheduler.currentThreadIndex]->context) == 0) {
            scheduler.threads[scheduler.currentThreadIndex]->state = READY; // Change state to READY
            // Add the yielded thread to ready queue
            scheduler.runqueue[scheduler.runqueueCount] = scheduler.threads[scheduler.currentThreadIndex];
            scheduler.runqueueCount++;
            scheduler.currentThreadIndex = tid;
            scheduler.threads[tid]->state = RUNNING;
            setcontext(&scheduler.threads[tid]->context);
        }
        else{
            return TSL_ERROR;
        }
    } else {
        fprintf(stderr, "Error: Invalid thread ID (%d) for yield.\n", tid);
        return TSL_ERROR;
    }

    return tid;
}


int tsl_join(int tid) {
    // Validate the tid. Assuming TSL_MAX_THREADS is the upper limit.
    if (tid < 0 || tid >= TSL_MAXTHREADS) {
        fprintf(stderr, "Error: Invalid thread ID passed to tsl_join.\n");
        return TSL_ERROR;
    }
    printf("Entered tsl_join for thread %d\n", tid);
    

    // Check if the target thread is valid and not yet terminated.
    ThreadControlBlock* target_tcb = scheduler.threads[tid];
    if (target_tcb == NULL) {
        fprintf(stderr, "Error: No thread with ID %d exists.\n", tid);
        return TSL_ERROR;
    }
    printf("Target thread %d found\n", tid);

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
        printf("here1 thread %d \n", tid);

        // Check if the target thread has terminated after yielding
        if (target_tcb->state == TERMINATED) {
            // Perform cleanup if needed
            printf("Thread %d has terminated\n", tid);
            if (target_tcb->stack != NULL) {
                free(target_tcb->stack);
                target_tcb->stack = NULL;
            }
            scheduler.threads[tid] = NULL; // Remove from scheduler
            return tid; // Return the tid of the terminated thread
        }
    }

    printf("Thread %d terminated\n", tid);
    if (target_tcb->stack != NULL) {
        free(target_tcb->stack);
        target_tcb->stack = NULL;
    }
    scheduler.threads[tid] = NULL; // Mark the TCB slot as available for reuse

    return TSL_SUCCESS;
}

void tsl_exit() {
    if (scheduler.currentThreadIndex < 0 || scheduler.currentThreadIndex >= TSL_MAXTHREADS) {
        return -1; // No current thread or out of bounds, should not happen in well-behaved code
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
    




    
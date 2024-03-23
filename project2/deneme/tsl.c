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

    // Initialize the scheduler with the specified algorithm
    scheduler.algorithm = (salg == 0) ? FIFO : RR; // Assuming 0 for FIFO, others for RR
    scheduler.currentThreadIndex = 0; // Set the main thread as the current thread
    scheduler.threadCount = 1; // Including the main thread

    // Initialize the TCB for the main thread
    ThreadControlBlock *main_tcb = &scheduler.threads[0];
    main_tcb->isActive = true;
    main_tcb->tid = 0; // Assigning 0 as the TID for the main thread
    main_tcb->state = RUNNING; // Main thread is already running
    main_tcb->stack = NULL; // Main thread's stack is managed by the OS, not by our library

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
    scheduler.currentThreadIndex = -1;
    scheduler.threadCount = 0;
    for(int i = 0; i < TSL_MAXTHREADS; i++) {
        scheduler.threads[i] = NULL;
    }
}

int scheduler_next_thread() {
    int nextThread = -1;
    switch (scheduler.algorithm) {
        case FIFO:
            for (int i = 0; i < TSL_MAXTHREADS; i++) {
                if (scheduler.threads[i] != NULL && scheduler.threads[i]->state == READY) {
                    nextThread = i;
                    break;
                }
            }
            break;
        case RR:
            for (int i = scheduler.currentThreadIndex + 1; i < scheduler.currentThreadIndex + 1 + TSL_MAXTHREADS; i++) {
                int idx = i % TSL_MAXTHREADS;
                if (scheduler.threads[idx] != NULL && scheduler.threads[idx]->state == READY) {
                    nextThread = idx;
                    break;
                }
            }
            break;
        // Case for SJF and SRTF would go here
    }
    return nextThread;
}

void scheduler_add_thread(ThreadControlBlock* tcb) {
    for (int i = 0; i < TSL_MAXTHREADS; i++) {
        if (scheduler.threads[i] == NULL) {
            scheduler.threads[i] = tcb;
            tcb->tid = i;
            tcb->state = READY;
            scheduler.threadCount++;
            break;
        }
    }
}

int tsl_create_thread(void (*tsf)(void *), void *targ) {
    
    if (!library_initialized) {
        return TSL_ERROR;
    }

    ThreadControlBlock* tcb = (ThreadControlBlock*)malloc(sizeof(ThreadControlBlock));
    if (!tcb) {
        return TSL_ERROR; // Failed to allocate memory for TCB
    }
    
    tcb->stack = malloc(TSL_STACKSIZE);
    if (!tcb->stack) {
        free(tcb); // Clean up partially created thread
        return TSL_ERROR; // Failed to allocate stack
    }
    printf("inside2\n");
    if (getcontext(&tcb->context) == -1) {
        free(tcb->stack);
        free(tcb);
        return TSL_ERROR; // Failed to initialize thread context
    }
printf("inside3\n");
    tcb->context.uc_stack.ss_sp = tcb->stack;
    
    tcb->context.uc_stack.ss_size = TSL_STACKSIZE;
   
    tcb->context.uc_stack.ss_flags = 0;
    
    tcb->context.uc_link = 0;
    
    // Directly manipulating mcontext to set the instruction pointer and argument.
    // This is highly platform and implementation-specific.
    // The following lines are illustrative and might require adjustment for your specific environment
    // or might not work without inline assembly or other non-standard techniques.
    // tcb->context.uc_mcontext.gregs[REG_RIP] = (greg_t)tsf; // Set instruction pointer to start function
    // tcb->context.uc_mcontext.gregs[REG_RDI] = (greg_t)targ; // Set first argument to the start function

    // The scheduler is responsible for setting the thread's initial state and tid
    scheduler_add_thread(tcb);
   

    return tcb->tid; // The scheduler_add_thread function now assigns and returns the tid
}

int tsl_yield(int tid) {
    if (!library_initialized) {
        fprintf(stderr, "Error: Library not initialized.\n");
        return TSL_ERROR;
    }
       printf(" CURRENT:%d\n", scheduler.currentThreadIndex);
    // Yielding to any thread if tid is -1
    if (tid == -1) {
        // Save the current context and let the scheduler decide the next thread
        if (scheduler.currentThreadIndex >= 0) {
            if (getcontext(&scheduler.threads[scheduler.currentThreadIndex]->context) == 0) {
                int nextThread = scheduler_next_thread();
                if (nextThread != -1) {                   
                    scheduler.threads[scheduler.currentThreadIndex]->state = READY; // Mark the current thread as ready 
                    scheduler.currentThreadIndex = nextThread;
                    scheduler.threads[nextThread]->state = RUNNING;
                    setcontext(&scheduler.threads[nextThread]->context);
                    printf("entered\n");
                }
                printf("entered1\n");
                // If no next thread is found, it's a no-op or you might want to handle this case specifically.
            }

        }
        return TSL_SUCCESS;
    }

    // Specific thread yielding logic
    if (tid >= 0 && tid < TSL_MAXTHREADS) {
        if (!scheduler.threads[tid] || scheduler.threads[tid]->state != READY) {
            // The specified thread is not in a READY state or does not exist
            fprintf(stderr, "Error: Thread %d is not ready or does not exist.\n", tid);
            return TSL_ERROR;
        }

        // Save the current context if we are currently running a thread
        if (scheduler.currentThreadIndex >= 0 && getcontext(&scheduler.threads[scheduler.currentThreadIndex]->context) == 0) {
            // Switch to the specified thread directly
            printf("the id: %d\n", tid);
            scheduler.currentThreadIndex = tid;
            scheduler.threads[tid]->state = RUNNING;
            setcontext(&scheduler.threads[tid]->context);
        }
    } else {
        fprintf(stderr, "Error: Invalid thread ID (%d) for yield.\n", tid);
        return TSL_ERROR;
    }

    return TSL_SUCCESS;
}


int tsl_join(int tid) {
    // Validate the tid. Assuming TSL_MAX_THREADS is the upper limit.
    printf("entered thread %d\n", tid);
    if (tid < 0 || tid >= TSL_MAXTHREADS) {
        fprintf(stderr, "Error: Invalid thread ID passed to tsl_join.\n");
        return TSL_ERROR;
    }
    printf("entered\n thread %d\n", tid);
    

    // Check if the target thread is valid and not yet terminated.
    ThreadControlBlock* target_tcb = scheduler.threads[tid];
    if (target_tcb == NULL) {
        fprintf(stderr, "Error: No thread with ID %d exists.\n", tid);
        return TSL_ERROR;
    }
    printf("entered2\n thread %d\n", tid);

    if (target_tcb->state == TERMINATED) {
        // If already terminated, simply clean up. This is a no-op if cleanup already happened.
        if (target_tcb->stack != NULL) {
            free(target_tcb->stack);
            target_tcb->stack = NULL;
        }
        scheduler.threads[tid] = NULL; // Remove from scheduler
        return TSL_SUCCESS;
    }
    printf("entered3\n thread %d\n", tid);

    // Wait for the thread to terminate, yielding to any thread if the target thread is still running.
    while (target_tcb->state != TERMINATED) {
        
        printf("here thread %d \n",tid);
        tsl_yield(-1); // Yield to any available thread
        printf("here1 thread %d \n", tid);
    }

    // Post-termination cleanup. This might be redundant if `tsl_exit` already performs cleanup,
    // but it's here to ensure resources are freed if `tsl_exit` hasn't been called.
    if (target_tcb->stack != NULL) {
        free(target_tcb->stack);
        target_tcb->stack = NULL;
    }
    scheduler.threads[tid] = NULL; // Mark the TCB slot as available for reuse

    return TSL_SUCCESS;
}

void tsl_exit() {
    if (scheduler.currentThreadIndex < 0 || scheduler.currentThreadIndex >= TSL_MAXTHREADS) {
        return; // No current thread or out of bounds, should not happen in well-behaved code
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
    return currentThread;
}
    
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
#define TSL_MAX_THREADS 1024
#define TSL_ERROR -1
#define TSL_SUCCESS 0
#define TSL_STACK_SIZE (1024*64)

typedef enum { FCFS = 1, RANDOM = 2, RR = 3} SchedulingAlgorithm;
typedef enum { READY, RUNNING, TERMINATED } ThreadState;

typedef struct ThreadControlBlock {
    ucontext_t context;
    int tid;
    bool isActive;  // Indicates if the thread slot is used
    ThreadState state;
    void* stack;
    bool resumed;   // Indicates if the thread is resuming from a yield
} ThreadControlBlock;


typedef struct Scheduler {
    SchedulingAlgorithm algorithm;
    ThreadControlBlock* threads[TSL_MAX_THREADS];
    int currentThreadIndex;
    int threadCount;
} Scheduler;

//ThreadControlBlock threads[TSL_MAX_THREADS];
int currentThread = -1; // TID of currently running thread
int nextTid = 0; // Next TID to be assigned
bool library_initialized = false; // Flag to ensure library is initialized



Scheduler scheduler;


void scheduler_init(){//(SchedulingAlgorithm alg) {
    // scheduler.algorithm = alg;
    // scheduler.currentThreadIndex = -1;
    // scheduler.threadCount = 1;
    // for(int i = 0; i < TSL_MAX_THREADS; i++) {
    //     scheduler.threads[i] = NULL;
    // }
    // ThreadControlBlock* tcb;
    //             // Print the corresponding string based on the enum value
                
    //                 tcb = scheduler.threads[9];
    //                 printf("Thread ID: %d\n", tcb->tid);
    //             printf("Is Active: %s\n", tcb->isActive ? "true" : "false");
                
    //             const char *state_names[] = { "READY", "RUNNING", "TERMINATED" };
    //             printf("State: %s\n", state_names[tcb->state]);

    //             printf("Stack Pointer: %p\n", tcb->stack);
}

void scheduler_add_thread(ThreadControlBlock* tcb) {
    for (int i = 0; i < TSL_MAX_THREADS; i++) {
        if (scheduler.threads[i] == NULL) {
            scheduler.threads[i] = tcb;
            tcb->tid = i;
            tcb->state = READY;
            scheduler.threadCount++;
            break;
        }
    }
}

int scheduler_next_thread() {
//const char *algorithm_names[] = { "FIFO", "RR" };

    // Declare a variable of type SchedulingAlgorithm
    SchedulingAlgorithm algorithm = scheduler.algorithm; // For example, FIFO is set here
    // Print the corresponding string based on the enum value
    //printf("Selected Scheduling Algorithm: %s\n", algorithm_names[scheduler.algorithm]);
    int nextThread = -1;
    switch (scheduler.algorithm) {
        case FCFS:
        //printf("TEST: \n");
            for (int i = 1  ; i < TSL_MAX_THREADS; i++) {
                if (scheduler.threads[i] != NULL && scheduler.threads[i]->state != TERMINATED) {
                    printf("thread id %d, state:%d\n", i, scheduler.threads[i]->state);
                    nextThread = i;
                    break;
                }
            }
            break;
        case RR:
            for (int i = scheduler.currentThreadIndex + 1; i < scheduler.currentThreadIndex + 1 + 256; i++) {
                int idx = i % TSL_MAX_THREADS;
                //printf("INDEX : %d" ,idx);
                //i++;
                const char *state_names[] = { "READY", "RUNNING", "TERMINATED" };

                
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
                printf("main: EIP (instruction pointer) saved in context structure con is 0x%x\n",  (unsigned int)  tcb->context.uc_mcontext.gregs[REG_EIP]);

                printf("main: ESP (stack pointer) saved in context structure con is 0x%x\n",  (unsigned int)  tcb->context.uc_mcontext.gregs[REG_ESP]);

                }
                
                
                if (scheduler.threads[idx] != NULL && scheduler.threads[idx]->state == READY  && idx!=0) {
                    
                    nextThread = idx;
                    // printf("NEXTTHERAD: %d\n", nextThread);
                    
                    break;
                }
            }
            break;
        // Case for SJF and SRTF would go here
    }
    return nextThread;
}


int tsl_init(int salg) {
    if (library_initialized) return TSL_ERROR; // Ensure this function is only called once

    library_initialized = true;
    scheduler.algorithm = salg; // Initially set to RR for example
    scheduler.currentThreadIndex = 0;
    scheduler.threadCount = 1;
    for(int i = 0; i < TSL_MAX_THREADS; i++) {
        scheduler.threads[i] = NULL;
    }

    // Allocate memory for the main thread's TCB
    ThreadControlBlock *main_tcb = malloc(sizeof(ThreadControlBlock));
    if (main_tcb == NULL) {
        // Handle memory allocation failure
        fprintf(stderr, "Failed to allocate memory for the main thread TCB.\n");
        exit(TSL_ERROR); // Or handle more gracefully
    }
    
    main_tcb->isActive = true;
    main_tcb->tid = 0; // Assigning 0 as the TID for the main thread
    main_tcb->state = RUNNING; // Main thread is already running
    main_tcb->stack = NULL; // Main thread's stack is managed by the OS
    scheduler.threads[0] = main_tcb;

    // Use getcontext() to capture the current context of the main thread
    if (getcontext(&main_tcb->context) == -1) {
        // Handle error
        fprintf(stderr, "Failed to get context for the main thread.\n");
        free(main_tcb); // Clean up allocated memory before exiting
        exit(TSL_ERROR);
    }

    scheduler_init(); // Assuming this is a function that initializes your scheduler further

    // Further initialization based on `salg` if needed
    // Example: setting scheduler.algorithm based on salg parameter
    
    return TSL_SUCCESS;
}


int tsl_create_thread(void (*tsf)(void *), void *targ) {
    
    if (!library_initialized) {
        return TSL_ERROR;
    }

    ThreadControlBlock* tcb = (ThreadControlBlock*)malloc(sizeof(ThreadControlBlock));
    if (!tcb) {
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


     char *stack_bottom = tcb->stack + TSL_STACKSIZE;
    // Make room on the stack for the arguments and a fake return address
    stack_bottom -= sizeof(void *); // Room for the 'targ' argument
    *(void **)stack_bottom = targ;

    stack_bottom -= sizeof(void *); // Room for the 'tsf' argument
    *(void **)stack_bottom = (void *)tsf;

    stack_bottom -= sizeof(void *); // Fake return address
    *(void **)stack_bottom = 0;
    // Setup the initial context for the thread
    getcontext(&tcb->context);

    // Set the instruction pointer to the stub function
    tcb->context.uc_mcontext.gregs[REG_EIP] = (greg_t)tsf;

    // Set the stack pointer. Leave space for the function arguments and fake return address
    tcb->context.uc_mcontext.gregs[REG_ESP] = (greg_t)stack_bottom;
    // tcb->context.uc_stack.ss_sp = tcb->stack;
    
    // tcb->context.uc_stack.ss_size = TSL_STACK_SIZE;
   
    // tcb->context.uc_stack.ss_flags = 0;
    
    // tcb->context.uc_link = 0;

     printf("CREATE---------------------------------------------- \n");
         printf("Thread ID: %d\n", tcb->tid);
                printf("Is Active: %s\n", tcb->isActive ? "true" : "false");
                printf("Is resumed: %s\n", tcb->resumed ? "true" : "false");
                
                const char *state_names[] = { "READY", "RUNNING", "TERMINATED" };
                printf("State: %s\n", state_names[tcb->state]);

                printf("Stack Pointer: %p\n", tcb->stack);
                 printf("main: EIP (instruction pointer) saved in context structure con is 0x%x\n",  (unsigned int)  tcb->context.uc_mcontext.gregs[REG_EIP]);

                printf("main: ESP (stack pointer) saved in context structure con is 0x%x\n",  (unsigned int)  tcb->context.uc_mcontext.gregs[REG_ESP]);
    
    // // Directly manipulating mcontext to set the instruction pointer and argument.
    // // This is highly platform and implementation-specific.
    // // The following lines are illustrative and might require adjustment for your specific environment
    // // or might not work without inline assembly or other non-standard techniques.
    //  tcb->context.uc_mcontext.gregs[REG_EIP] = (greg_t)tsf; // Set instruction pointer to start function
    //  tcb->context.uc_mcontext.gregs[REG_EDI] = (greg_t)targ; // Set first argument to the start function

    // The scheduler is responsible for setting the thread's initial state and tid
    scheduler_add_thread(tcb);
   

    return tcb->tid; // The scheduler_add_thread function now assigns and returns the tid
}
int tsl_yield(int tid) {
    if (!library_initialized) {
        fprintf(stderr, "Error: Library not initialized.\n");
        return TSL_ERROR;
    }

    // Get the current thread's TCB
    ThreadControlBlock* current_tcb = scheduler.threads[scheduler.currentThreadIndex];

    // if (!current_tcb->resumed) {
        
        if (getcontext(&(current_tcb->context)) == -1)
    {
        printf("Failed to get current context.\n");
        return TSL_ERROR;
    }
       
            // Mark this thread as having saved its context to prevent immediate re-yielding
            current_tcb->resumed = true;

            // Determine the next thread to switch to
            int nextThread = scheduler_next_thread();
            if (nextThread != -1 && nextThread != scheduler.currentThreadIndex) {
                // Prepare the current thread for switching
                current_tcb->state = READY;

                // Switch to the next thread
                scheduler.currentThreadIndex = nextThread;
                scheduler.threads[nextThread]->state = RUNNING;
                scheduler.threads[nextThread]->resumed = false; // Ensure the next thread starts fresh

                setcontext(&scheduler.threads[nextThread]->context);
                // Execution will continue from here when this thread is resumed
            }
        
    // } else {
    //     // Clear the resumed flag to indicate that this thread is now actively running
    //     current_tcb->resumed = false;
    // }

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
    printf("entered2\n");

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

    // Wait for the thread to terminate, yielding to any thread if the target thread is still running.
    if (target_tcb->state != TERMINATED) {
        
        //printf("here \n");
        tsl_yield(TSL_ANY); // Yield to any available thread
        //printf("here1 \n");

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
        scheduler.currentThreadIndex = 0;
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
            printf("returnÇ::: %d\n", nextThread);
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
    

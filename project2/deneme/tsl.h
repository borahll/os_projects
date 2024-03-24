#ifndef _TSL_H_
#define _TSL_H_

#include <ucontext.h>
#include <stdbool.h>

// do not change this header file (tsl.h) 
// it is the interface of the tsl library to the applications

typedef enum { ALG_FCFS = 1,  ALG_RANDOM = 2, RR = 3} SchedulingAlgorithm;
typedef enum { READY, RUNNING, TERMINATED } ThreadState;


#define TSL_MAX_THREADS 1024 // maximum number of threads (including the main thread) that an application can have.
#define TSL_STACK_SIZE (1024*64) // bytes, i.e., 32 KB. This is the stack size for a new thread. 

#define ALG_FCFS 1
#define ALG_RANDOM 2
#define ALG_RR 3

#define TID_MAIN 1 // tid of the main tread. this id is reserved for main thread.

#define TSL_ANY 0  // yield to a thread selected with a scheduling alg.

#define TSL_ERROR  -1  // there is an error in the function execution.
#define TSL_SUCCESS 0  // function execution success

typedef struct ThreadControlBlock {
    ucontext_t context;
    int tid;
    bool isActive;  // Indicates if the thread slot is used
    ThreadState state;
    void* stack;
    bool resumed;   // Indicates if the thread is resuming from a yield
    //int estimatedRuntime; // For SJF and SRTF
} ThreadControlBlock;

typedef struct Scheduler {
    SchedulingAlgorithm algorithm;
    ThreadControlBlock* threads[TSL_MAX_THREADS];
    int currentThreadIndex;
    int threadCount;
    ThreadControlBlock* runqueue[TSL_MAX_THREADS]; //list of ready queues 
    int runqueueCount;
    int currentReadyThread;
} Scheduler;


int tsl_init(int salg);
int tsl_create_thread (void (*tsf)(void *), void *targ);
int tsl_yield (int tid);
int tsl_exit();
int tsl_join(int tid);
int tsl_cancel(int tid);
int tsl_gettid();

#endif
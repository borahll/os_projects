#ifndef _TSL_H_
#define _TSL_H_

#include <ucontext.h>
#include <stdbool.h>

typedef enum { FIFO, RR} SchedulingAlgorithm;
typedef enum { READY, RUNNING, TERMINATED } ThreadState;

#define TSL_MAXTHREADS 256 // maximum number of threads (including the main thread) that an application can have.
#define TSL_STACKSIZE  32768 // bytes, i.e., 32 KB. This is the stack size for a new thread.

#define ALG_FCFS 1
#define ALG_RANDOM 2
#define ALG_MYALGORITHM 3

#define TID_MAIN 1 // tid of the main thread. This id is reserved for the main thread.

#define TSL_ANY 0  // yield to a thread selected with a scheduling algorithm.

#define TSL_ERROR  -1  // there is an error in the function execution.
#define TSL_SUCCESS 0  // function execution success

typedef struct ThreadControlBlock {
    ucontext_t context;
    int tid;
    bool isActive;  // Indicates if the thread slot is used
    ThreadState state;
    void* stack;
    //int estimatedRuntime; // For SJF and SRTF
} ThreadControlBlock;

typedef struct Scheduler {
    SchedulingAlgorithm algorithm;
    ThreadControlBlock* threads[TSL_MAXTHREADS];
    int currentThreadIndex;
    int threadCount;
} Scheduler;

int tsl_init(int salg);
int tsl_create_thread(void (*tsf)(void *), void *targ);
int tsl_yield(int tid);
void scheduler_init(SchedulingAlgorithm alg);
int scheduler_next_thread();
int tsl_create_thread(void (*tsf)(void *), void *targ);
int tsl_join(int tid);
void tsl_exit();
int tsl_cancel(int tid);


#endif

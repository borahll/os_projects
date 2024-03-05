#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <setjmp.h>
#include <unistd.h>

#define TSL_ERROR -1
#define TSL_ANY 0

typedef struct {
    jmp_buf context;
    int tid;
    int ready;
    int deleted;
    pthread_t pthread_id; // For cancellation
} TSL_Thread;

typedef struct {
    int scheduling_algorithm;
    int next_thread_id;
    int current_thread_id;
    TSL_Thread **threads;
    int num_threads;
} TSL_Library;

static TSL_Library tsl_library_instance = {0, 1, 0, NULL, 0};

// Forward declarations
void* thread_start_function(void *arg);
int tsl_exit();
int tsl_yield(int tid);
int tsl_create_thread(void* (*tsf)(void*), void* targ);
int tsl_cancel(int tid);
int tsl_join(int tid);
int tsl_gettid();
int tsl_init(int salg);

static TSL_Thread* find_thread_by_id(int tid) {
    for (int i = 0; i < tsl_library_instance.num_threads; i++) {
        if (tsl_library_instance.threads[i]->tid == tid) {
            return tsl_library_instance.threads[i];
        }
    }
    return NULL;
}

void* thread_start_function(void *arg) {
    int tid = *(int*)arg;

    // Simulate the execution of the thread start function
    printf("Thread %d is running.\n", tid);

    // Simulate some work done by the thread
    for (int i = 0; i < 3; i++) {
        printf("Thread %d is doing some work.\n", tid);
        // Simulate yielding to another thread
        usleep(10000); // Sleep for 10 milliseconds
    }

    // Mark the thread as deleted and exit
    tsl_exit();
    return NULL;
}

int tsl_exit() {
    // Find the current thread
    TSL_Thread *current_thread = find_thread_by_id(tsl_library_instance.current_thread_id);

    // Mark the thread as deleted
    current_thread->ready = 0;

    // Check if this is the last active thread
    int active_threads = 0;
    for (int i = 0; i < tsl_library_instance.num_threads; i++) {
        if (tsl_library_instance.threads[i]->ready) {
            active_threads++;
        }
    }

    if (active_threads == 0) {
        // If this is the last active thread, terminate the whole process
        exit(0);
    }

    // Yield to the next thread
    tsl_yield(TSL_ANY);

    // This point should never be reached
    fprintf(stderr, "Error: tsl_exit() should not return.\n");
    exit(EXIT_FAILURE);
}

int tsl_yield(int tid) {
    // Check if the library has been initialized
    if (tsl_library_instance.scheduling_algorithm == 0) {
        fprintf(stderr, "Error: Library not initialized. Call tsl_init first.\n");
        return TSL_ERROR;
    }

    // Find the thread to yield to based on the given tid
    TSL_Thread *next_thread = NULL;

    if (tid == TSL_ANY) {
        // Implement your scheduling algorithm to select the next thread
        // Here, a simple round-robin scheme is used for demonstration
        int next_index = (tsl_library_instance.current_thread_id + 1) % tsl_library_instance.num_threads;
        next_thread = tsl_library_instance.threads[next_index];
    } else if (tid > 0) {
        next_thread = find_thread_by_id(tid);
        if (next_thread == NULL || !next_thread->ready) {
            return TSL_ERROR; // No thread with the given tid or it's not ready
        }
    }

    // Save the context of the currently running thread
    if (setjmp(tsl_library_instance.threads[tsl_library_instance.current_thread_id]->context) == 0) {
        // Switch to the next thread
        tsl_library_instance.current_thread_id = next_thread->tid;

        // Load the context of the next thread
        longjmp(next_thread->context, 1);
    }

    // Simulate the execution of the next thread
    thread_start_function(&tsl_library_instance.current_thread_id);

    // Return the tid of the thread to whom the CPU is yielded
    return tsl_library_instance.current_thread_id;
}

int tsl_create_thread(void* (*tsf)(void*), void* targ) {
    // Create a new thread structure
    TSL_Thread *new_thread = (TSL_Thread*)malloc(sizeof(TSL_Thread));
    if (new_thread == NULL) {
        fprintf(stderr, "Error: Unable to allocate memory for the new thread.\n");
        return TSL_ERROR;
    }

    // Initialize the thread structure
    new_thread->tid = tsl_library_instance.next_thread_id++;
    new_thread->ready = 1;

    // Add the thread to the library's threads array
    tsl_library_instance.threads = realloc(tsl_library_instance.threads, (tsl_library_instance.num_threads + 1) * sizeof(TSL_Thread*));
    if (tsl_library_instance.threads == NULL) {
        fprintf(stderr, "Error: Unable to allocate memory for the new thread.\n");
        free(new_thread);
        return TSL_ERROR;
    }

    tsl_library_instance.threads[tsl_library_instance.num_threads++] = new_thread;

    // Create a new POSIX thread
    if (pthread_create(&(new_thread->pthread_id), NULL, tsf, targ) != 0) {
        fprintf(stderr, "Error: Unable to create a new thread.\n");
        free(new_thread);
        return TSL_ERROR;
    }

    return new_thread->tid;
}

int tsl_cancel(int tid) {
    // Find the target thread
    TSL_Thread *target_thread = find_thread_by_id(tid);

    // If the target thread doesn't exist or has already terminated, return immediately
    if (target_thread == NULL || !target_thread->ready) {
        return TSL_ERROR;
    }

    // Cancel the target thread asynchronously
    if (pthread_cancel(target_thread->pthread_id) != 0) {
        fprintf(stderr, "Error: Unable to cancel the target thread.\n");
        return TSL_ERROR;
    }

    return tid;
}

int tsl_join(int tid) {
    // Find the target thread
    TSL_Thread *target_thread = find_thread_by_id(tid);

    // If the target thread doesn't exist or has already terminated, return immediately
    if (target_thread == NULL || !target_thread->ready) {
        return TSL_ERROR;
    }

    // Wait for the target thread to terminate
    pthread_join(target_thread->pthread_id, NULL);

    // Deallocate resources used by the target thread (simulate for demonstration)
    printf("Deallocating resources for Thread %d.\n", tid);

    // Return the tid of the joined thread
    return tid;
}

int tsl_gettid() {
    // Return the thread id (tid) of the calling thread
    return tsl_library_instance.current_thread_id;
}

int tsl_init(int salg) {
    // Check if the library has already been initialized
    if (tsl_library_instance.threads != NULL) {
        fprintf(stderr, "Error: Library already initialized.\n");
        return TSL_ERROR;
    }

    // Set the scheduling algorithm
    tsl_library_instance.scheduling_algorithm = salg;

    // Initialize the threads array
    tsl_library_instance.threads = (TSL_Thread**)malloc(sizeof(TSL_Thread*));
    if (tsl_library_instance.threads == NULL) {
        fprintf(stderr, "Error: Unable to allocate memory for the threads array.\n");
        return TSL_ERROR;
    }

    return 0; // Success
}

int main() {
    // Example usage of the library
    tsl_init(0); // Initialize the library with a scheduling algorithm (round-robin in this case)

    int tid1, tid2;

    // Create threads after initializing the library
    tid1 = tsl_create_thread(thread_start_function, (void*)&tid1);
    tid2 = tsl_create_thread(thread_start_function, (void*)&tid2);

    printf("Main thread is running with tid: %d.\n", tsl_gettid());

    // Yield to the next thread
    tsl_yield(TSL_ANY);

    // Cancel Thread 1 asynchronously
    int canceled_tid = tsl_cancel(tid1);
    if (canceled_tid != TSL_ERROR) {
        printf("Thread %d has been canceled.\n", canceled_tid);
    } else {
        printf("Thread %d does not exist or has already terminated.\n", tid1);
    }

    // Wait for Thread 2 to terminate
    int joined_tid = tsl_join(tid2);
    if (joined_tid != TSL_ERROR) {
        printf("Main thread joined Thread %d.\n", joined_tid);
    } else {
        printf("Thread %d does not exist or has already terminated.\n", tid2);
    }

    // This point will be reached only if all threads have terminated
    printf("All threads have terminated. Exiting.\n");

    return 0;
}


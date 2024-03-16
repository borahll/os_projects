#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <ucontext.h>
#define TSL_MAXTHREADS 256 // maximum number of threads (including the main thread) that an application can have.
#define TSL_STACKSIZE  32768 // bytes, i.e., 32 KB. This is the stack size for a new thread. 

#define ALG_FCFS 1
#define ALG_RANDOM 2
#define ALG_MYALGORITHM 3

#define TID_MAIN 1 // tid of the main tread. this id is reserved for main thread.

#define TSL_ANY 0  // yield to a thread selected with a scheduling alg.

#define TSL_ERROR  -1  // there is an error in the function execution.
#define TSL_SUCCESS 0  // function execution success

typedef struct {
    int tid; // thread identifier
    unsigned int state; // thread state
    ucontext_t context; // pointer to context structure
    char *stack; // pointer to stack
} TCB;

typedef struct {
    int scheduling_algorithm;
    int next_thread_id;
    int current_thread_id;
    TCB **threads;
    int num_threads;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} TSL_Library;

static TSL_Library tsl_library_instance = {0, 1, 0, NULL, 0, PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER};

// Forward declarations
void* thread_start_function(void *arg);
int tsl_exit();
int tsl_create_thread(void* (*tsf)(void*), void* targ);
int tsl_cancel(int tid);
int tsl_join(int tid);
int tsl_gettid();
int tsl_init(int salg);

static TCB* find_thread_by_id(int tid) {
    for (int i = 0; i < tsl_library_instance.num_threads; i++) {
        if (tsl_library_instance.threads[i]->tid == tid) {
            return tsl_library_instance.threads[i];
        }
    }
    return NULL;
}

void cleanup_thread(TCB *thread) {
    // Clean up resources (for demonstration purposes)
    printf("Cleaning up resources for Thread %d.\n", thread->tid);

    // Mark the thread as not state
    thread->state = 0;

    // Signal waiting threads
    pthread_cond_signal(&tsl_library_instance.cond);

    // Join the thread to release resources
    pthread_join(thread->tid, NULL);

    // Free the resources
    free(thread);
}

void* thread_start_function(void *arg) {
    int tid = *(int*)arg;

    printf("Thread %d is running.\n", tid);

    for (int i = 0; i < 3; i++) {
        printf("Thread %d is doing some work.\n", tid);
        usleep(10000); // Simulate work by sleeping

        tsl_yield(); // Yield execution back to the scheduler
    }

    tsl_exit(); // Terminate the thread
    return NULL; // Unreachable, here to satisfy the compiler
}

void tsl_yield() {
    int current_index = tsl_library_instance.current_thread_id;
    int next_index = (current_index + 1) % tsl_library_instance.num_threads;
    TCB *current_thread = tsl_library_instance.threads[current_index];
    TCB *next_thread = tsl_library_instance.threads[next_index];

    if (getcontext(&current_thread->context) == -1) {
        fprintf(stderr, "Error: Failed to get current context.\n");
        exit(EXIT_FAILURE);
    }

    tsl_library_instance.current_thread_id = next_index; // Update the current thread index to the next thread

    if (setcontext(&next_thread->context) == -1) {
        fprintf(stderr, "Error: Failed to set next context.\n");
        exit(EXIT_FAILURE);
    }
}

int tsl_exit() {
    pthread_mutex_lock(&tsl_library_instance.mutex);

    // Find the current thread's index and mark it as not state
    int current_thread_index = -1;
    TCB *current_thread = NULL;
    for (int i = 0; i < tsl_library_instance.num_threads; i++) {
        if (tsl_library_instance.threads[i]->tid == tsl_library_instance.current_thread_id) {
            current_thread = tsl_library_instance.threads[i];
            current_thread_index = i;
            break;
        }
    }

    if (current_thread != NULL) {
        current_thread->state = 0;

        // Remove the thread from the scheduling queue
        // This involves shifting remaining threads in the array to fill the gap
        for (int i = current_thread_index; i < tsl_library_instance.num_threads - 1; i++) {
            tsl_library_instance.threads[i] = tsl_library_instance.threads[i + 1];
        }
        tsl_library_instance.num_threads--;
        // Optionally, you could resize the threads array to free unused space

        // Signal that a thread has exited, in case any threads are waiting to join
        pthread_cond_broadcast(&tsl_library_instance.cond);
    }

    pthread_mutex_unlock(&tsl_library_instance.mutex);

    if (current_thread != NULL) {
        // Clean up the TSL_Thread structure for the current thread
        // Note: Actual memory deallocation should happen outside the lock
        // to avoid holding the lock while calling pthread_exit() which could
        // lead to deadlock if another thread is trying to join this one.
        free(current_thread);
    }

    // Exit the pthread, allowing it to be joined
    pthread_exit(NULL);

    // Unreachable code due to pthread_exit, but included to satisfy compiler
    return 0; // Success
}


int tsl_create_thread(void* (*tsf)(void*), void* targ) {
    // Check if the library has been initialized
    if (tsl_library_instance.threads == NULL) {
        fprintf(stderr, "Error: Library not initialized. Call tsl_init first.\n");
        return TSL_ERROR;
    }

    // Lock the mutex
    pthread_mutex_lock(&tsl_library_instance.mutex);

    // Create a new thread structure
    TCB *new_thread = (TCB*)malloc(sizeof(TCB));
    if (new_thread == NULL) {
        fprintf(stderr, "Error: Unable to allocate memory for the new thread.\n");
        // Unlock the mutex before returning
        pthread_mutex_unlock(&tsl_library_instance.mutex);
        return TSL_ERROR;
    }

    // Initialize the thread structure
    new_thread->tid = tsl_library_instance.next_thread_id++;
    new_thread->state = 1;

    // Add the thread to the library's threads array
    tsl_library_instance.threads = realloc(tsl_library_instance.threads, (tsl_library_instance.num_threads + 1) * sizeof(TCB*));
    if (tsl_library_instance.threads == NULL) {
        fprintf(stderr, "Error: Unable to allocate memory for the new thread.\n");
        free(new_thread);
        // Unlock the mutex before returning
        pthread_mutex_unlock(&tsl_library_instance.mutex);
        return TSL_ERROR;
    }

    tsl_library_instance.threads[tsl_library_instance.num_threads++] = new_thread;

    // Create a new POSIX thread
    if (pthread_create(&(new_thread->tid), NULL, tsf, (void*)&new_thread->tid) != 0) {
        fprintf(stderr, "Error: Unable to create a new thread.\n");
        cleanup_thread(new_thread);
        // Unlock the mutex before returning
        pthread_mutex_unlock(&tsl_library_instance.mutex);
        return TSL_ERROR;
    }

    // Unlock the mutex
    pthread_mutex_unlock(&tsl_library_instance.mutex);

    return new_thread->tid;
}

int tsl_cancel(int tid) {
    // Find the target thread
    TCB *target_thread = find_thread_by_id(tid);

    // If the target thread doesn't exist or has alstate terminated, return immediately
    if (target_thread == NULL || !target_thread->state) {
        return TSL_ERROR;
    }

    // Cancel the target thread asynchronously
    if (pthread_cancel(target_thread->tid) != 0) {
        fprintf(stderr, "Error: Unable to cancel the target thread.\n");
        return TSL_ERROR;
    }

    // Mark the thread as not state
    target_thread->state = 0;

    // Signal waiting threads
    pthread_cond_signal(&tsl_library_instance.cond);

    // Clean up resources associated with the canceled thread
    cleanup_thread(target_thread);

    return tid;
}

int tsl_join(int tid) {
    // Find the target thread
    TCB *target_thread = find_thread_by_id(tid);

    // If the target thread doesn't exist or has alstate terminated, return immediately
    if (target_thread == NULL || !target_thread->state) {
        return TSL_ERROR;
    }

    // Wait for the target thread to terminate
    pthread_join(target_thread->tid, NULL);

    // Clean up resources used by the target thread
    cleanup_thread(target_thread);

    // Return the tid of the joined thread
    return tid;
}

int tsl_gettid() {
    // Return the thread id (tid) of the calling thread
    return tsl_library_instance.current_thread_id;
}

int tsl_init(int salg) {
    // Check if the library has alstate been initialized
    if (tsl_library_instance.threads != NULL) {
        fprintf(stderr, "Error: Library alstate initialized.\n");
        return TSL_ERROR;
    }

    // Set the scheduling algorithm
    tsl_library_instance.scheduling_algorithm = salg;

    // Initialize the threads array
    tsl_library_instance.threads = (TCB**)malloc(sizeof(TCB*));
    if (tsl_library_instance.threads == NULL) {
        fprintf(stderr, "Error: Unable to allocate memory for the threads array.\n");
        return TSL_ERROR;
    }

    return 0; // Success
}

int main() {
    // Example usage of the library
    if (tsl_init(0) != 0) { // Initialize the library with a scheduling algorithm (round-robin in this case)
        fprintf(stderr, "Error: Unable to initialize the library.\n");
        return EXIT_FAILURE;
    }

    int tid1, tid2;

    tid1 = tsl_create_thread(thread_start_function, (void*)&tid1);
    tid2 = tsl_create_thread(thread_start_function, (void*)&tid2);

    printf("Main thread is running with tid: %d.\n", tsl_gettid());

    // Wait for the threads to finish
    pthread_join(tsl_library_instance.threads[0]->tid, NULL);
    pthread_join(tsl_library_instance.threads[1]->tid, NULL);
    // This point will be reached only if all threads have terminated
    printf("All threads have terminated. Exiting.\n");

    return 0;
}

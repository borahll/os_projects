#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>

#define TSL_ERROR -1
#define TSL_ANY 0

typedef struct {
    int tid;
    int ready;
    pthread_t pthread_id;
} TSL_Thread;

typedef struct {
    int scheduling_algorithm;
    int next_thread_id;
    int current_thread_id;
    TSL_Thread **threads;
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

static TSL_Thread* find_thread_by_id(int tid) {
    for (int i = 0; i < tsl_library_instance.num_threads; i++) {
        if (tsl_library_instance.threads[i]->tid == tid) {
            return tsl_library_instance.threads[i];
        }
    }
    return NULL;
}

void cleanup_thread(TSL_Thread *thread) {
    // Clean up resources (for demonstration purposes)
    printf("Cleaning up resources for Thread %d.\n", thread->tid);

    // Mark the thread as not ready
    thread->ready = 0;

    // Signal waiting threads
    pthread_cond_signal(&tsl_library_instance.cond);

    // Join the thread to release resources
    pthread_join(thread->pthread_id, NULL);

    // Free the resources
    free(thread);
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

        // Loop to simulate thread switching
        while (1) {
            pthread_mutex_lock(&tsl_library_instance.mutex);
            if (tsl_library_instance.threads[(tsl_library_instance.current_thread_id + 1) % tsl_library_instance.num_threads]->ready) {
                tsl_library_instance.current_thread_id = (tsl_library_instance.current_thread_id + 1) % tsl_library_instance.num_threads;
                pthread_mutex_unlock(&tsl_library_instance.mutex);
                break;
            }
            pthread_mutex_unlock(&tsl_library_instance.mutex);
            usleep(1000); // Sleep for 1 millisecond
        }
    }

    // Mark the thread as deleted and exit
    tsl_exit();
    return NULL;
}

int tsl_exit() {
    pthread_mutex_lock(&tsl_library_instance.mutex);

    // Find the current thread's index and mark it as not ready
    int current_thread_index = -1;
    TSL_Thread *current_thread = NULL;
    for (int i = 0; i < tsl_library_instance.num_threads; i++) {
        if (tsl_library_instance.threads[i]->tid == tsl_library_instance.current_thread_id) {
            current_thread = tsl_library_instance.threads[i];
            current_thread_index = i;
            break;
        }
    }

    if (current_thread != NULL) {
        current_thread->ready = 0;

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
    TSL_Thread *new_thread = (TSL_Thread*)malloc(sizeof(TSL_Thread));
    if (new_thread == NULL) {
        fprintf(stderr, "Error: Unable to allocate memory for the new thread.\n");
        // Unlock the mutex before returning
        pthread_mutex_unlock(&tsl_library_instance.mutex);
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
        // Unlock the mutex before returning
        pthread_mutex_unlock(&tsl_library_instance.mutex);
        return TSL_ERROR;
    }

    tsl_library_instance.threads[tsl_library_instance.num_threads++] = new_thread;

    // Create a new POSIX thread
    if (pthread_create(&(new_thread->pthread_id), NULL, tsf, (void*)&new_thread->tid) != 0) {
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

    // Mark the thread as not ready
    target_thread->ready = 0;

    // Signal waiting threads
    pthread_cond_signal(&tsl_library_instance.cond);

    // Clean up resources associated with the canceled thread
    cleanup_thread(target_thread);

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
    if (tsl_init(0) != 0) { // Initialize the library with a scheduling algorithm (round-robin in this case)
        fprintf(stderr, "Error: Unable to initialize the library.\n");
        return EXIT_FAILURE;
    }

    int tid1, tid2;

    tid1 = tsl_create_thread(thread_start_function, (void*)&tid1);
    tid2 = tsl_create_thread(thread_start_function, (void*)&tid2);

    printf("Main thread is running with tid: %d.\n", tsl_gettid());

    // Wait for the threads to finish
    pthread_join(tsl_library_instance.threads[0]->pthread_id, NULL);
    pthread_join(tsl_library_instance.threads[1]->pthread_id, NULL);

    // This point will be reached only if all threads have terminated
    printf("All threads have terminated. Exiting.\n");

    return 0;
}

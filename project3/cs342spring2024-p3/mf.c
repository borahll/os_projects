#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <malloc.h>
#include <assert.h>
#include <string.h>
#include <ctype.h>
#include "mf.h"
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <semaphore.h>
#define SHM_NAME "/mf_shared_memory"
#define MAX_QUEUES 10
#define MAX_SEM_NAME_SIZE 64
#define MAX_MQNAMESIZE 128
#define SHM_SIZE (1024 * 1024) // Example size
#define MF_SUCCESS 0
#define MF_ERROR -1

typedef struct {
    char mqname[MAX_MQNAMESIZE];
    char sem_name[MAX_SEM_NAME_SIZE]; // Name of the semaphore for this queue
    unsigned int start_offset, end_offset;
    unsigned int head, tail;
    unsigned int capacity;
    unsigned int size;
    int isActive;
} MQMetadata;

typedef struct {
    MQMetadata queues[MAX_QUEUES]; // Metadata for each queue
    unsigned int queue_count; // Total number of active queues
} ManagementSection;
// Assuming a global pointer to the start of the shared memory segment
void* shm_start = NULL;
unsigned int shm_size = 0; // Total size of the shared memory
unsigned int next_free_offset = sizeof(MQMetadata) * 10; // Reserve space for 10 metadata entries

// Structure to hold the shared memory's mapping and size for use in the process
// This is an example; your actual implementation might differ.
typedef struct {
    void* ptr; // Pointer to the mapped shared memory
    size_t size; // Size of the mapped shared memory
} SharedMemoryContext;
SharedMemoryContext shm_context;

int parse_config(const char* filename, int* shmem_size) {
    FILE* file = fopen(filename, "r");
    if (file == NULL) {
        perror("Failed to open the config file");
        return MF_ERROR;
    }

    char buffer[256];
    while (fgets(buffer, sizeof(buffer), file)) {
        if (sscanf(buffer, "SHMEMSIZE=%d", shmem_size) == 1) {
            // Found the shared memory size configuration
            break;
        }
    }
    fclose(file);
    return MF_SUCCESS;
}

// HELPER Utility function to generate a semaphore name for a queue
void generate_semaphore_name(char* dest, const char* mqname, int index) {
    snprintf(dest, MAX_SEM_NAME_SIZE, "%s%s_%d", SEM_NAME_PREFIX, mqname, index);
}

// Initialize semaphore for a message queue
int initialize_semaphore_for_queue(MQMetadata* queue, int index) {
    char sem_name[MAX_SEM_NAME_SIZE];
    generate_semaphore_name(sem_name, queue->mqname, index);
    strncpy(queue->sem_name, sem_name, MAX_SEM_NAME_SIZE);

    sem_t* sem = sem_open(sem_name, O_CREAT | O_EXCL, 0644, 1); // Initial value is 1 for unlocked
    if (sem == SEM_FAILED) {
        perror("Failed to create semaphore");
        return MF_ERROR;
    }
    sem_close(sem); // Close handle, semaphore is not removed
    return MF_SUCCESS;
}

// Acquire semaphore for a message queue
int lock_queue(MQMetadata* queue) {
    sem_t* sem = sem_open(queue->sem_name, 0);
    if (sem == SEM_FAILED) {
        perror("sem_open failed");
        return MF_ERROR;
    }
    if (sem_wait(sem) != 0) {
        perror("sem_wait failed");
        return MF_ERROR;
    }
    sem_close(sem);
    return MF_SUCCESS;
}

// Release semaphore for a message queue
int unlock_queue(MQMetadata* queue) {
    sem_t* sem = sem_open(queue->sem_name, 0);
    if (sem == SEM_FAILED) {
        perror("sem_open failed");
        return MF_ERROR;
    }
    if (sem_post(sem) != 0) {
        perror("sem_post failed");
        return MF_ERROR;
    }
    sem_close(sem);
    return MF_SUCCESS;
}

// Remove semaphore for a message queue
int remove_semaphore_for_queue(MQMetadata* queue) {
    if (sem_unlink(queue->sem_name) != 0) {
        perror("sem_unlink failed");
        return MF_ERROR;
    }
    return MF_SUCCESS;
}


// Initialize the shared memory and management section
int mf_init() {
    int shmem_size = 0;
    if (parse_config(CONFIG_FILENAME, &shmem_size) != MF_SUCCESS) {
        return MF_ERROR;
    }

    if (shmem_size < MIN_SHMEMSIZE || shmem_size > MAX_SHMEMSIZE) {
        fprintf(stderr, "Shared memory size in the config file is out of valid range\n");
        return MF_ERROR;
    }

    shmem_size *= 1024; // Convert from KB to bytes

    int fd = shm_open("/mf_shared_memory", O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
    if (fd == -1) {
        perror("shm_open failed");
        return MF_ERROR;
    }

    if (ftruncate(fd, shmem_size) == -1) {
        perror("ftruncate failed");
        close(fd);
        shm_unlink("/mf_shared_memory");
        return MF_ERROR;
    }

    // Mapping the shared memory for access
    shm_start = mmap(NULL, shmem_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (shm_start == MAP_FAILED) {
        perror("mmap failed");
        close(fd);
        shm_unlink("/mf_shared_memory");
        return MF_ERROR;
    }

    close(fd); // Close the file descriptor as it's no longer needed

    // Initialize the management section
    ManagementSection* mgmt = (ManagementSection*)shm_start;
    memset(mgmt, 0, sizeof(ManagementSection)); // Clear the management section to initialize

    // Optionally, initialize semaphores or other synchronization mechanisms here
    // Note: Detailed semaphore initialization for each queue is more contextually appropriate during mf_create

    return MF_SUCCESS;
}

int mf_destroy() {
    if (shm_start == NULL) {
        fprintf(stderr, "Shared memory is not initialized.\n");
        return MF_ERROR;
    }

    // Access the management section at the beginning of the shared memory
    ManagementSection* mgmt = (ManagementSection*)shm_start;

    // Iterate over all message queues to unlink their semaphores
    for (int i = 0; i < mgmt->queue_count; i++) {
        MQMetadata* queue = &mgmt->queues[i];
        if (queue->isActive) {
            // The semaphore name is stored in each queue's metadata
            if (sem_unlink(queue->sem_name) == -1) {
                perror("sem_unlink failed for a message queue semaphore");
                // Continue trying to clean up other resources
            }
        }
    }

    // Unmap the shared memory before unlinking it
    if (munmap(shm_start, SHM_SIZE) == -1) { // Assume SHM_SIZE is globally defined or otherwise retrievable
        perror("munmap failed");
        return MF_ERROR;
    }

    // Unlink the shared memory object
    if (shm_unlink(SHM_NAME) == -1) {
        perror("shm_unlink failed");
        return MF_ERROR;
    }

    shm_start = NULL; // Reset the global pointer as the memory is no longer mapped

    return MF_SUCCESS;
}

int mf_connect() {
    // Open the shared memory object
    int shm_fd = shm_open(SHM_NAME, O_RDWR, 0);
    if (shm_fd == -1) {
        perror("shm_open in mf_connect failed");
        return MF_ERROR;
    }

    // Retrieve the size of the shared memory object
    struct stat shm_stat;
    if (fstat(shm_fd, &shm_stat) == -1) {
        perror("fstat failed in mf_connect");
        close(shm_fd);
        return MF_ERROR;
    }
    shm_context.size = shm_stat.st_size;

    // Map the shared memory object
    shm_context.ptr = mmap(NULL, shm_context.size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shm_context.ptr == MAP_FAILED) {
        perror("mmap failed in mf_connect");
        close(shm_fd);
        return MF_ERROR;
    }

    // Close the file descriptor as it's no longer needed after mmap
    close(shm_fd);

    // The process is now connected to the shared memory and ready for synchronized operations on message queues.
    return MF_SUCCESS;
}


int mf_disconnect() {
    // Example semaphore operation if there's a need to synchronize disconnect operations
    // lock_global(); // Hypothetical function to lock a global semaphore if needed
    // unlock_global(); // Hypothetical function to unlock it after certain operations

    // Unmap the shared memory object from the process's address space
    if (munmap(shm_context.ptr, shm_context.size) == -1) {
        perror("munmap failed in mf_disconnect");
        return MF_ERROR;
    }

    // Perform any cleanup of process-specific resources
    // that were allocated or initialized in mf_connect or during the use of the MF library.

    return MF_SUCCESS;
}

// Aligns size to the next multiple of 4 bytes
unsigned int align_up(unsigned int size) {
    return (size + 3) & ~3;
}

// Mock function to illustrate the concept - real implementation needed
unsigned int allocate_space_in_shared_memory(unsigned int size) {
    if (next_free_offset + size > shm_size) {
        return (unsigned int)-1; // Not enough space
    }
    unsigned int allocation_start = next_free_offset;
    next_free_offset += size; // Reserve space
    return allocation_start;
}

// mf_create function with semaphore integration
int mf_create(const char* mqname, int mqsize) {
    ManagementSection* mgmt = (ManagementSection*)shm_start;
    if (mgmt->queue_count >= MAX_QUEUES) {
        printf("Maximum number of queues reached.\n");
        return MF_ERROR;
    }

    for (int i = 0; i < MAX_QUEUES; i++) {
        if (!mgmt->queues[i].isActive) {
            strncpy(mgmt->queues[i].mqname, mqname, MAX_MQNAMESIZE - 1);
            mgmt->queues[i].mqname[MAX_MQNAMESIZE - 1] = '\0'; // Ensure null-termination
            mgmt->queues[i].isActive = 1;
            mgmt->queues[i].capacity = mqsize; // Assume direct assignment for simplicity
            // Initialize semaphore for the queue
            if (initialize_semaphore_for_queue(&mgmt->queues[i], i) != MF_SUCCESS) {
                printf("Failed to initialize semaphore for queue.\n");
                mgmt->queues[i].isActive = 0; // Mark as inactive due to failure
                return MF_ERROR;
            }
            mgmt->queue_count++;
            return i; // Return the index as the queue identifier
        }
    }

    return MF_ERROR; // Should not reach here if queue_count is accurate
}
int mf_remove(char *mqname) {
    if (shm_start == NULL) {
        printf("Shared memory not initialized.\n");
        return MF_ERROR;
    }

    MQMetadata* directory = (MQMetadata*)shm_start;
    for (int i = 0; i < 10; i++) { // Assuming a maximum of 10 queues for simplicity
        if (strncmp(directory[i].mqname, mqname, MAX_MQNAMESIZE) == 0) {
            // Found the queue to remove, now lock it to ensure exclusive access
            if (lock_queue(&directory[i]) != MF_SUCCESS) {
                printf("Failed to lock message queue '%s' for removal.\n", mqname);
                return MF_ERROR; // Early return if we can't lock the queue safely
            }

            // Proceed to clear the metadata to "remove" the queue
            memset(&directory[i], 0, sizeof(MQMetadata));
            printf("Message queue '%s' removed successfully.\n", mqname);

            // After removing the queue, unlock and remove its semaphore
            unlock_queue(&directory[i]); // Ignore return value as we're destroying the semaphore next
            if (remove_semaphore_for_queue(&directory[i]) != MF_SUCCESS) {
                printf("Failed to remove semaphore for queue '%s'.\n", mqname);
                // Consider how you want to handle semaphore removal failure. Here we just log it.
            }

            return MF_SUCCESS;
        }
    }

    printf("Message queue '%s' not found.\n", mqname);
    return MF_ERROR;
}



int mf_open(char *mqname) {
    if (shm_start == NULL) {
        printf("Shared memory not initialized.\n");
        return MF_ERROR;
    }

    ManagementSection* mgmt = (ManagementSection*)shm_start; // Assuming the first part of shm_start is ManagementSection
    for (int i = 0; i < mgmt->queue_count; i++) { // Loop through the directory
        MQMetadata* queue = &mgmt->queues[i];
        if (strncmp(queue->mqname, mqname, MAX_MQNAMESIZE) == 0) {
            // Attempt to lock the queue for exclusive access
            if (lock_queue(queue) != MF_SUCCESS) {
                printf("Failed to lock message queue '%s'.\n", mqname);
                return MF_ERROR;
            }

            // Optionally perform actions that require exclusive access
            // ...

            // Unlock the queue after done
            if (unlock_queue(queue) != MF_SUCCESS) {
                printf("Failed to unlock message queue '%s'.\n", mqname);
                // Even if unlocking fails, we consider the queue open, so this is not necessarily a fatal error
            }

            printf("Message queue '%s' opened successfully.\n", mqname);
            return i; // Return the index as the queue identifier
        }
    }

    printf("Message queue '%s' not found.\n", mqname);
    return MF_ERROR; // Indicate failure if the queue is not found
}


// Function to close a message queue
int mf_close(int qid) {
    if (shm_start == NULL) {
        printf("Shared memory not initialized.\n");
        return MF_ERROR;
    }

    ManagementSection* mgmt = (ManagementSection*)shm_start;
    if (qid < 0 || qid >= MAX_QUEUES) { // Use MAX_QUEUES defined globally
        printf("Invalid queue identifier.\n");
        return MF_ERROR;
    }

    MQMetadata* queue = &mgmt->queues[qid];
    if (!queue->isActive || queue->mqname[0] == '\0') {
        // The queue at this index is not in use
        printf("Queue identifier does not refer to an open queue.\n");
        return MF_ERROR;
    }

    // Lock the queue before making changes
    if (lock_queue(queue) != MF_SUCCESS) {
        printf("Failed to lock the queue.\n");
        return MF_ERROR;
    }

    // Simulate closing the queue, decrementing reference count, etc.
    // Actual implementation would likely involve more than just printing a message
    printf("Message queue '%s' closed successfully.\n", queue->mqname);

    // Optionally, perform cleanup if reference count reaches 0, etc.
    // This might include setting isActive to 0, clearing mqname, etc.
    // For this example, just show the unlock step
    if (unlock_queue(queue) != MF_SUCCESS) {
        printf("Failed to unlock the queue.\n");
        // Even if unlocking fails, proceed to attempt removal of the semaphore
    }

    // If you're removing the queue entirely, also remove its semaphore
    // This step would depend on your actual implementation details
    // For example, it might be tied to reference counting logic
    // if (remove_semaphore_for_queue(queue) != MF_SUCCESS) {
    //     printf("Failed to remove the semaphore for queue.\n");
    //     return MF_ERROR;
    // }

    return MF_SUCCESS;
}


// Simplified function to find the end of the queue and check for available space.
// Returns pointer to where the new message should be written, or NULL if not enough space.
void* find_message_insertion_point(MQMetadata* mq, int messageSize) {
    // Assuming the first message starts immediately after MQMetadata
    unsigned int currentOffset = mq->start_offset + sizeof(MQMetadata);
    void* queueEnd = shm_start + mq->end_offset;

    while (currentOffset + sizeof(unsigned int) + messageSize <= (unsigned int)queueEnd) {
        unsigned int* messageSizePtr = (unsigned int*)(shm_start + currentOffset);
        if (*messageSizePtr == 0) { // Found available space
            return messageSizePtr;
        }
        // Move to the next message
        currentOffset += sizeof(unsigned int) + *messageSizePtr;
    }

    return NULL; // Not enough space
}

int mf_send(int qid, void *bufptr, int datalen) {
    if (shm_start == NULL || bufptr == NULL) {
        printf("Invalid parameters or shared memory not initialized.\n");
        return MF_ERROR;
    }

    if (qid < 0 || qid >= 10) { // Assuming a maximum of 10 queues
        printf("Invalid queue identifier.\n");
        return MF_ERROR;
    }

    MQMetadata* mq = &((MQMetadata*)shm_start)[qid];
    if (mq->mqname[0] == '\0') { // Queue is not in use
        printf("Queue identifier does not refer to an active queue.\n");
        return MF_ERROR;
    }

    // Find where to insert the new message
    void* insertionPoint = find_message_insertion_point(mq, datalen);
    if (!insertionPoint) {
        printf("Not enough space in the queue to send the message.\n");
        return MF_ERROR;
    }

    // Write the message size and data to the queue
    *((unsigned int*)insertionPoint) = datalen;
    memcpy((char*)insertionPoint + sizeof(unsigned int), bufptr, datalen);

    printf("Message sent successfully to queue '%s'.\n", mq->mqname);
    return MF_SUCCESS;
}
// Utility function to print a preview of a message's content
void print_message_preview(void* message, unsigned int size) {
    printf("Message Size: %u, Preview: ", size);
    for (unsigned int i = 0; i < size && i < 10; ++i) { // Limit preview to 10 bytes
        char c = *((char*)message + i);
        printf("%c", isprint(c) ? c : '.'); // Print a dot for non-printable characters
    }
    printf("\n");
}

void mf_print() {
    if (shm_start == NULL) {
        printf("Shared memory not initialized.\n");
        return;
    }

    MQMetadata* directory = (MQMetadata*)shm_start;
    for (int i = 0; i < 10; i++) { // Assuming a maximum of 10 queues for simplicity
        if (directory[i].mqname[0] != '\0') { // Queue is in use
            printf("Queue '%s':\n", directory[i].mqname);

            // Iterate through messages in the queue
            unsigned int currentOffset = directory[i].start_offset + sizeof(MQMetadata);
            while (currentOffset < directory[i].end_offset) {
                unsigned int* messageSizePtr = (unsigned int*)(shm_start + currentOffset);
                if (*messageSizePtr == 0) { // No more messages
                    break;
                }
                print_message_preview((char*)messageSizePtr + sizeof(unsigned int), *messageSizePtr);
                currentOffset += sizeof(unsigned int) + *messageSizePtr; // Move to next message
            }
        }
    }
}
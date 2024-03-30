#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <malloc.h>
#include <assert.h>
#include <string.h>
#include "mf.h"

// Simplified structure for message queue metadata within shared memory
typedef struct {
    char mqname[MAX_MQNAMESIZE]; // Name of the message queue
    unsigned int mqsize; // Size of the message queue in bytes
    unsigned int start_offset; // Start offset of the queue within shared memory
    unsigned int end_offset; // End offset of the queue within shared memory
    // Add more fields as needed for management (e.g., reference count, message count)
} MQMetadata;
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

int mf_init() {
    int shmem_size = 0;
    if (parse_config(CONFIG_FILENAME, &shmem_size) != MF_SUCCESS) {
        return MF_ERROR;
    }

    // Validate shmem_size against defined min and max
    if (shmem_size < MIN_SHMEMSIZE || shmem_size > MAX_SHMEMSIZE) {
        fprintf(stderr, "Shared memory size in the config file is out of valid range\n");
        return MF_ERROR;
    }

    // Convert shmem_size from KB to bytes for mmap
    shmem_size *= 1024;

    // Create the shared memory object
    int fd = shm_open("/mf_shared_memory", O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
    if (fd == -1) {
        perror("shm_open failed");
        return MF_ERROR;
    }

    // Size the shared memory object
    if (ftruncate(fd, shmem_size) == -1) {
        perror("ftruncate failed");
        shm_unlink("/mf_shared_memory");
        return MF_ERROR;
    }

    // Map the shared memory object
    SharedMemoryLayout* shm_ptr = mmap(NULL, shmem_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (shm_ptr == MAP_FAILED) {
        perror("mmap failed");
        shm_unlink("/mf_shared_memory");
        return MF_ERROR;
    }

    // TODO Initialize synchronization primitives and other setup here
    // ...

    // Don't forget to unmap and unlink the shared memory object when destroying it
    // munmap(shm_ptr, shmem_size);
    // shm_unlink("/mf_shared_memory");

    return MF_SUCCESS;
}

int mf_destroy() {
    // Close and unlink the shared memory object
    if (shm_unlink(SHM_NAME) == -1) {
        perror("shm_unlink failed");
        return MF_ERROR;
    }

    // TODO Destroy any semaphores or other synchronization objects
    // For example, if you have a semaphore named "/mf_semaphore"
    // if (sem_unlink("/mf_semaphore") == -1) {
    //     perror("sem_unlink failed");
    //     return MF_ERROR;
    // }

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

    // Here you might want to perform additional initialization specific to this process
    // For example, setting up pointers to specific parts of the shared memory, etc.

    return MF_SUCCESS;
}

int mf_disconnect() {
    // Unmap the shared memory object from the process's address space
    if (munmap(shm_context.ptr, shm_context.size) == -1) {
        perror("munmap failed in mf_disconnect");
        return MF_ERROR;
    }

    // Here, you would also perform any cleanup of process-specific resources
    // that were allocated or initialized in mf_connect or during the use of the MF library.

    return MF_SUCCESS;


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

int mf_create(char *mqname, int mqsize) {
    if (shm_start == NULL) {
        printf("Shared memory not initialized.\n");
        return MF_ERROR;
    }

    if (mqsize < MIN_MQSIZE || mqsize > MAX_MQSIZE) {
        printf("Message queue size out of bounds.\n");
        return MF_ERROR;
    }

    mqsize = align_up(mqsize);

    MQMetadata* directory = (MQMetadata*)shm_start;
    for (int i = 0; i < 10; i++) { // Assuming a maximum of 10 queues for simplicity
        if (directory[i].mqname[0] == '\0') { // Empty slot found
            strncpy(directory[i].mqname, mqname, MAX_MQNAMESIZE);
            directory[i].mqsize = mqsize;
            directory[i].start_offset = allocate_space_in_shared_memory(mqsize);
            if (directory[i].start_offset == (unsigned int)-1) {
                printf("Failed to allocate space in shared memory.\n");
                return MF_ERROR;
            }
            directory[i].end_offset = directory[i].start_offset + mqsize;
            printf("Message queue '%s' created successfully.\n", mqname);
            return MF_SUCCESS;
        }
    }

    printf("Maximum number of message queues reached.\n");
    return MF_ERROR;
}
int mf_remove(char *mqname) {
    if (shm_start == NULL) {
        printf("Shared memory not initialized.\n");
        return MF_ERROR;
    }

    MQMetadata* directory = (MQMetadata*)shm_start;
    for (int i = 0; i < 10; i++) { // Assuming a maximum of 10 queues for simplicity
        if (strncmp(directory[i].mqname, mqname, MAX_MQNAMESIZE) == 0) {
            // Found the queue to remove
            // In a real implementation, check the reference count and other conditions here

            memset(&directory[i], 0, sizeof(MQMetadata)); // Clear the metadata to "remove" the queue
            printf("Message queue '%s' removed successfully.\n", mqname);
            return MF_SUCCESS;
        }
    }

    printf("Message queue '%s' not found.\n", mqname);
    return MF_ERROR;
}


// Function to open a message queue
int mf_open(char *mqname) {
    if (shm_start == NULL) {
        printf("Shared memory not initialized.\n");
        return MF_ERROR;
    }

    MQMetadata* directory = (MQMetadata*)shm_start;
    for (int i = 0; i < 10; i++) { // Loop through the directory
        if (strncmp(directory[i].mqname, mqname, MAX_MQNAMESIZE) == 0) {
            // Queue found, prepare it for use (e.g., increment reference count here)
            printf("Message queue '%s' opened successfully.\n", mqname);

            // For simplicity, return the index as the queue identifier
            // In a real implementation, you might return a more complex handle
            return i;
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

    MQMetadata* directory = (MQMetadata*)shm_start;
    // Check if the qid is within valid range
    if (qid < 0 || qid >= 10) { // Assuming a maximum of 10 queues for simplicity
        printf("Invalid queue identifier.\n");
        return MF_ERROR;
    }

    if (directory[qid].mqname[0] == '\0') {
        // The queue at this index is not in use
        printf("Queue identifier does not refer to an open queue.\n");
        return MF_ERROR;
    }

    // Here, you would decrement the reference count. Since we're not maintaining
    // a reference count in this simplified example, we'll just simulate the operation.
    printf("Message queue '%s' closed successfully.\n", directory[qid].mqname);

    // In a real implementation, you might also check if the reference count has
    // reached 0 to perform additional cleanup, such as removing the queue.

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
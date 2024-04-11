#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <malloc.h>
#include <assert.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include "mf.h"
#include <time.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <semaphore.h>
#define GLOBAL_MANAGEMENT_SEM_NAME_PREFIX "/mf_global_management_sem"
#define SEM_NAME_PREFIX "/mf_sem_"
// #define MAX_QUEUES 10
#define MAX_SEM_NAME_SIZE 64
// #define SHM_SIZE (1024 * 1024) // Example size
#define MF_SUCCESS 0
#define MF_ERROR -1
#define INITIAL_CAPACITY 10

char GLOBAL_MANAGEMENT_SEM_NAME[MAX_SEM_NAME_SIZE];

typedef struct {
    char shmem_name[MAX_MQNAMESIZE];
    int shmem_size;       // In KB
    int max_msgs_in_queue;
    int max_queues_in_shmem;
} MFConfig;
MFConfig config;

typedef struct {
    pid_t process_id;
    // Add more fields as necessary, e.g., for tracking open queues
} ActiveProcess;

typedef struct {
    ActiveProcess* processes; // Dynamic array of active processes
    size_t size;              // Current number of active processes
    size_t capacity;          // Current capacity of the array
} ActiveProcessList;

ActiveProcessList activeProcessList = {NULL, 0, 0};

typedef struct {
    char mqname[MAX_MQNAMESIZE];
    char sem_name[MAX_SEM_NAME_SIZE]; // Name of the semaphore for this queue
    unsigned int start_offset, end_offset;
    unsigned int head, tail;
    unsigned int capacity;
    unsigned int size;
    int isActive;
    int referenceCount;
} MQMetadata;

typedef struct {
    MQMetadata* queues; // Metadata for each queue
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


void generate_general_semaphore_name() {
    time_t now = time(NULL);
    snprintf(GLOBAL_MANAGEMENT_SEM_NAME, MAX_SEM_NAME_SIZE, "%s_%ld", GLOBAL_MANAGEMENT_SEM_NAME_PREFIX, now);
}

int read_configuration(const char* filename, MFConfig* config) {
    FILE* file = fopen(filename, "r");
    if (!file) {
        perror("Failed to open configuration file");
        return -1;
    }

    char line[1024];
    while (fgets(line, sizeof(line), file)) {
        // Ignore comments
        if (line[0] == '#') {
            continue;
        }

        if (strncmp(line, "SHMEM_NAME", 10) == 0) {
            // Extract the shared memory name
            sscanf(line, "SHMEM_NAME \"%[^\"]\"", config->shmem_name);
            printf(config->shmem_name);
        } else if (strncmp(line, "SHMEM_SIZE", 10) == 0) {
            // Extract the shared memory size
            sscanf(line, "SHMEM_SIZE %d", &config->shmem_size);
        } else if (strncmp(line, "MAX_MSGS_IN_QUEUE", 17) == 0) {
            // Extract the maximum messages in a queue
            sscanf(line, "MAX_MSGS_IN_QUEUE %d", &config->max_msgs_in_queue);
        } else if (strncmp(line, "MAX_QUEUES_IN_SHMEM", 19) == 0) {
            // Extract the maximum queues in shared memory
            sscanf(line, "MAX_QUEUES_IN_SHMEM %d", &config->max_queues_in_shmem);
        }
    }

    fclose(file);
    return 0;
}

void initActiveProcessList() {
    activeProcessList.processes = (ActiveProcess*)malloc(INITIAL_CAPACITY * sizeof(ActiveProcess));
    if (activeProcessList.processes == NULL) {
        perror("Failed to allocate activeProcessList");
        exit(EXIT_FAILURE);
    }
    activeProcessList.size = 0;
    activeProcessList.capacity = INITIAL_CAPACITY;
}

void resizeActiveProcessList(size_t new_capacity) {
    ActiveProcess* new_array = (ActiveProcess*)realloc(activeProcessList.processes, new_capacity * sizeof(ActiveProcess));
    if (new_array == NULL) {
        perror("Failed to reallocate activeProcessList");
        free(activeProcessList.processes); // Clean up original array
        exit(EXIT_FAILURE);
    }
    activeProcessList.processes = new_array;
    activeProcessList.capacity = new_capacity;
}

void addActiveProcess(pid_t pid) {
    if (activeProcessList.size == activeProcessList.capacity) {
        // Resize the array if necessary
        resizeActiveProcessList(activeProcessList.capacity * 2);
    }
    activeProcessList.processes[activeProcessList.size].process_id = pid;
    activeProcessList.size++;
}

void removeActiveProcess(pid_t pid) {
    for (size_t i = 0; i < activeProcessList.size; i++) {
        if (activeProcessList.processes[i].process_id == pid) {
            // Shift elements down to fill the gap
            for (size_t j = i; j < activeProcessList.size - 1; j++) {
                activeProcessList.processes[j] = activeProcessList.processes[j + 1];
            }
            activeProcessList.size--;
            return;
        }
    }
    printf("Process ID %d not found in active process list.\n", pid);
}

void freeActiveProcessList() {
    free(activeProcessList.processes);
    activeProcessList.processes = NULL;
    activeProcessList.size = 0;
    activeProcessList.capacity = 0;
}
/*
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
 */
// Utility function to retrieve and remove the first message from the queue
int retrieve_first_message(MQMetadata* mq, void* bufptr, int bufsize) {
    if (mq->size == 0) {
        // No messages to retrieve
        return MF_ERROR;
    }

    // The queue's buffer starts right after the MQMetadata structure in memory
    char* queueBuffer = ((char*)mq) + sizeof(MQMetadata);

    // Read the size of the first message
    unsigned int messageSize;
    memcpy(&messageSize, queueBuffer + mq->head, sizeof(unsigned int));

    // Check if the provided buffer is large enough
    if (bufsize < messageSize) {
        printf("Provided buffer is too small for the message.\n");
        return MF_ERROR;
    }

    // Calculate the position of the message data
    unsigned int messageDataPos = mq->head + sizeof(unsigned int);
    if (messageDataPos >= mq->capacity) {
        // Wrap around if necessary
        messageDataPos -= mq->capacity;
    }

    // Copy the message to the provided buffer
    memcpy(bufptr, queueBuffer + messageDataPos, messageSize);

    // Update the head pointer and the queue size
    unsigned int newHead = mq->head + sizeof(unsigned int) + messageSize;
    if (newHead >= mq->capacity) {
        // Wrap around if the new head exceeds the queue capacity
        newHead -= mq->capacity;
    }
    mq->head = newHead;
    mq->size -= (sizeof(unsigned int) + messageSize); // Update size to reflect the removal

    return messageSize; // Return the size of the message retrieved
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

int lock_global_management() {
    sem_t* sem = sem_open(GLOBAL_MANAGEMENT_SEM_NAME, 0);
    if (sem == SEM_FAILED) {
        perror("Global management sem_open failed");
        return MF_ERROR;
    }

    if (sem_wait(sem) != 0) {
        perror("Global management sem_wait failed");
        sem_close(sem); // Attempt to close even in case of failure
        return MF_ERROR;
    }

    sem_close(sem); // Close the semaphore handle
    return MF_SUCCESS;
}

int unlock_global_management() {
    sem_t* sem = sem_open(GLOBAL_MANAGEMENT_SEM_NAME, 0);
    if (sem == SEM_FAILED) {
        perror("Global management sem_open failed");
        return MF_ERROR;
    }

    if (sem_post(sem) != 0) {
        perror("Global management sem_post failed");
        sem_close(sem); // Attempt to close even in case of failure
        return MF_ERROR;
    }

    sem_close(sem); // Close the semaphore handle
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
    // Part of mf_init function

    generate_general_semaphore_name();

    sem_t* globalSem = sem_open(GLOBAL_MANAGEMENT_SEM_NAME, O_CREAT | O_EXCL, 0644, 1);
    if (globalSem == SEM_FAILED) {
        if (errno == EEXIST) {
            globalSem = sem_open(GLOBAL_MANAGEMENT_SEM_NAME, 0); // Open existing semaphore
        } else {
            perror("Failed to create global management semaphore");
            return MF_ERROR;
        }
    }

    if (globalSem == SEM_FAILED) {
        perror("Failed to open global management semaphore");
        return MF_ERROR;
    }
    printf("here\n");
    sem_close(globalSem); // Close the handle; the semaphore itself remains in the system
    // Dynamically allocate memory for the number of queues
    // Initialize or clear the allocated memory if necessary
    printf("here1\n");
    read_configuration(CONFIG_FILENAME, &config);
    printf(config.shmem_name,"\n");
    if (config.shmem_size < MIN_SHMEMSIZE || config.shmem_size > MAX_SHMEMSIZE) {
        fprintf(stderr, "Shared memory size in the config file is out of valid range\n");
        return MF_ERROR;
    }
    printf("here2\n");

    config.shmem_size *= 1024; // Convert from KB to bytes
    initActiveProcessList();
    printf("here3\n");
    int fd = shm_open(config.shmem_name, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
    if (fd == -1) {
        perror("shm_open failed");
        return MF_ERROR;
    }
    printf("here4\n");

    if (ftruncate(fd, config.shmem_size) == -1) {
        perror("ftruncate failed");
        close(fd);
        shm_unlink(config.shmem_name);
        return MF_ERROR;
    }
    printf("here5\n");

    // Mapping the shared memory for access
    shm_start = mmap(NULL, config.shmem_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (shm_start == MAP_FAILED) {
        perror("mmap failed");
        close(fd);
        shm_unlink(config.shmem_name);
        return MF_ERROR;
    }
    printf("here6\n");

    close(fd); // Close the file descriptor as it's no longer needed
    printf("here7\n");

    // Initialize the management section
    ManagementSection* mgmt = (ManagementSection*)shm_start;
    printf("here8\n");
    memset(mgmt, 0, sizeof(ManagementSection)); // Clear the management section to initialize
    printf("here9\n");

    mgmt->queues = (MQMetadata*)malloc(config.max_queues_in_shmem * sizeof(MQMetadata));
    if (mgmt->queues == NULL) {
        // Handle memory allocation failure
        perror("Failed to allocate memory for message queues");
        return -1;
    }

    memset(mgmt->queues, 0, config.max_queues_in_shmem * sizeof(MQMetadata));
    printf("here10\n");

    // Optionally, initialize semaphores or other synchronization mechanisms here
    // Note: Detailed semaphore initialization for each queue is more contextually appropriate during mf_create


    return MF_SUCCESS;
}

int mf_destroy() {
    if (shm_start == NULL) {
        fprintf(stderr, "Shared memory is not initialized.\n");
        return MF_ERROR;
    }

    // Optionally lock the global management structure if concurrent access is possible
    if (lock_global_management() != MF_SUCCESS) {
        fprintf(stderr, "Failed to lock global management for destruction.\n");
        // Decide if failure to lock should abort the destruction process
    }

    ManagementSection* mgmt = (ManagementSection*)shm_start;

    // Iterate over all message queues to unlink their semaphores
    for (int i = 0; i < mgmt->queue_count; i++) {
        MQMetadata* queue = &mgmt->queues[i];
        if (queue->isActive) {
            if (sem_unlink(queue->sem_name) == -1) {
                perror("sem_unlink failed for a message queue semaphore");
            }
            queue->referenceCount = 0;
        }
    }
    if (mgmt->queues != NULL) {
        free(mgmt->queues);
        mgmt->queues = NULL;
    }
    // Ensure all dynamically allocated memory is freed, if any
    freeActiveProcessList(); // Make sure this function properly frees all allocated memory

    // Unmap the shared memory
    if (munmap(shm_start, config.shmem_size) == -1) {
        perror("munmap failed");
        return MF_ERROR;
    }

    // Unlink the shared memory object
    if (shm_unlink(config.shmem_name) == -1) {
        perror("shm_unlink failed");
        return MF_ERROR;
    }

    // Unlink the global management semaphore
    if (sem_unlink(GLOBAL_MANAGEMENT_SEM_NAME) != 0) {
        perror("Failed to unlink global management semaphore");
    }

    shm_start = NULL; // Reset the global pointer

    // Optionally unlock the global management structure if it was locked
    unlock_global_management();

    return MF_SUCCESS;
}


int mf_connect() {
    // Optionally read the shared memory details from a configuration file
    // char shm_name[MAX_SEM_NAME_SIZE]; // Placeholder for shared memory name
    // Open the shared memory object using the name from the configuration
    MFConfig config2;
    read_configuration(CONFIG_FILENAME, &config2);
  printf("Inside: %s\n", config2.shmem_name);

    int shm_fd = shm_open(config2.shmem_name, O_RDWR, 0);
    //printf("%sTest1", config.shmem_name);
    printf("\n%sTest2","\n");
    if (shm_fd == -1) {
        perror("shm_opessn in mf_connect failed");
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

    // Lock global management before modifying shared data structures
    if (lock_global_management() != MF_SUCCESS) {
        munmap(shm_context.ptr, shm_context.size); // Cleanup
        printf("Failed to lock global management.\n");
        return MF_ERROR;
    }

    // Add the current process to the active process list
    addActiveProcess(getpid());

    // Unlock global management after updates
    unlock_global_management();

    // Close the file descriptor as it's no longer needed after mmap
    close(shm_fd);

    return MF_SUCCESS;
}


int mf_disconnect() {
    if (shm_context.ptr == NULL || shm_context.size == 0) {
        printf("Shared memory not initialized or already disconnected.\n");
        return MF_ERROR; // or MF_SUCCESS if you treat this as a non-error state
    }
    int pid = getpid(); // Use appropriate method to get the current process's ID

    // Lock the global management structure to safely update the list of active processes
    if (lock_global_management() != MF_SUCCESS) {
        printf("Failed to lock global management for disconnect.\n");
        return MF_ERROR;
    }

    // Update the list of active processes to remove the current process
    // This step assumes there's a mechanism in place to identify and remove the current process
    // e.g., remove_process_from_active_list(current_process_id);
    // For demonstration, this line is commented out:
    removeActiveProcess(pid);


    // Perform cleanup of process-specific resources
    // For example, closing any open message queues the process might have
    // close_open_queues_for_process(current_process_id); // Hypothetical function

    // Unlock the global management structure after updates
    if (unlock_global_management() != MF_SUCCESS) {
        printf("Failed to unlock global management after disconnect.\n");
        // Continue with disconnect even if unlocking fails
    }

    // Unmap the shared memory object from the process's address space
    if (munmap(shm_context.ptr, shm_context.size) == -1) {
        perror("munmap failed in mf_disconnect");
        return MF_ERROR;
    }

    // Optionally reset the shm_context to prevent reuse
    shm_context.ptr = NULL;
    shm_context.size = 0;

    printf("Process disconnected successfully.\n");
    return MF_SUCCESS;
}


// Aligns size to the next multiple of 4 bytes
unsigned int align_up(unsigned int size) {
    return (size + 3) & ~3;
}

// Mock function to illustrate the concept - real implementation needed
unsigned int allocate_space_in_shared_memory(unsigned int size) {
    if (next_free_offset + size > config.shmem_size) {
        return (unsigned int)-1; // Not enough space
    }
    unsigned int allocation_start = next_free_offset;
    next_free_offset += size; // Reserve space
    return allocation_start;
}

int mf_create(char* mqname, int mqsize) {
    if (shm_start == NULL) {
        printf("Shared memory not initialized.\n");
        return MF_ERROR;
    }

    if (mqname == NULL || mqsize < MIN_MQSIZE || mqsize > MAX_MQSIZE) {
        printf("Invalid queue name or size.\n");
        return MF_ERROR;
    }

    // Lock global management structure
    if (lock_global_management() != MF_SUCCESS) {
        printf("Failed to lock global management.\n");
        return MF_ERROR;
    }

    ManagementSection* mgmt = (ManagementSection*)shm_start;
    if (mgmt->queue_count >= config.max_queues_in_shmem) {
        printf("Maximum number of queues reached.\n");
        unlock_global_management();
        return MF_ERROR;
    }

    // Check for an existing queue with the same name
    for (int i = 0; i < mgmt->queue_count; i++) {
        if (strncmp(mgmt->queues[i].mqname, mqname, MAX_MQNAMESIZE) == 0 && mgmt->queues[i].isActive) {
            printf("Queue with the name '%s' already exists.\n", mqname);
            unlock_global_management();
            return MF_ERROR;
        }
    }

    // Find an available slot for the new queue
    for (int i = 0; i < config.max_queues_in_shmem; i++) {
        if (!mgmt->queues[i].isActive) {
            strncpy(mgmt->queues[i].mqname, mqname, MAX_MQNAMESIZE - 1);
            mgmt->queues[i].mqname[MAX_MQNAMESIZE - 1] = '\0'; // Ensure null-termination
            mgmt->queues[i].isActive = 1;
            mgmt->queues[i].capacity = mqsize;
            // Initialize semaphore for the queue
            if (initialize_semaphore_for_queue(&mgmt->queues[i], i) != MF_SUCCESS) {
                printf("Failed to initialize semaphore for queue '%s'.\n", mqname);
                mgmt->queues[i].isActive = 0; // Revert activation due to failure
                unlock_global_management();
                return MF_ERROR;
            }
            mgmt->queue_count++;
            unlock_global_management();
            return i; // Return the index as the queue identifier
        }
    }

    unlock_global_management();
    return MF_ERROR; // No available slot found
}

int mf_remove(char *mqname) {
    if (shm_start == NULL) {
        printf("Shared memory not initialized.\n");
        return MF_ERROR;
    }

    // Lock global management structure here
    if (lock_global_management() != MF_SUCCESS) {
        printf("Failed to lock global management for queue removal.\n");
        return MF_ERROR;
    }

    MQMetadata* directory = (MQMetadata*)shm_start;
    for (int i = 0; i < config.max_queues_in_shmem; i++) { // Use MAX_QUEUES instead of hardcoded value
        if (strncmp(directory[i].mqname, mqname, MAX_MQNAMESIZE) == 0) {
            // Found the queue to remove
            if (directory[i].referenceCount > 0) {
                printf("Cannot remove queue '%s' as it is still in use.\n", mqname);
                unlock_global_management(); // Important to unlock before returning
                return MF_ERROR;
            }

            if (lock_queue(&directory[i]) != MF_SUCCESS) {
                printf("Failed to lock message queue '%s' for removal.\n", mqname);
                unlock_global_management(); // Unlock global management before returning
                return MF_ERROR;
            }

            memset(&directory[i], 0, sizeof(MQMetadata)); // Clear the queue metadata
            printf("Message queue '%s' removed successfully.\n", mqname);

            // It's important to remove the semaphore associated with the queue
            if (remove_semaphore_for_queue(&directory[i]) != MF_SUCCESS) {
                printf("Failed to remove semaphore for queue '%s'.\n", mqname);
            }

            unlock_queue(&directory[i]); // Proceed to unlock even if semaphore removal failed

            unlock_global_management(); // Unlock global management after operation
            return MF_SUCCESS;
        }
    }

    unlock_global_management(); // Ensure to unlock global management if queue not found
    printf("Message queue '%s' not found.\n", mqname);
    return MF_ERROR;
}


int mf_open(char *mqname) {
    if (shm_start == NULL) {
        printf("Shared memory not initialized.\n");
        return MF_ERROR;
    }

    if (mqname == NULL || mqname[0] == '\0') {
        printf("Invalid queue name.\n");
        return MF_ERROR;
    }

    // Lock global management structure here (Assuming a function or mechanism for this)
    lock_global_management(); // Pseudocode - Implement based on your synchronization mechanism

    ManagementSection* mgmt = (ManagementSection*)shm_start;
    for (int i = 0; i < config.max_queues_in_shmem; i++) { // Iterate through all possible queues, not just mgmt->queue_count
        MQMetadata* queue = &mgmt->queues[i];
        if (strncmp(queue->mqname, mqname, MAX_MQNAMESIZE) == 0 && queue->isActive) {
            // Found an active queue with the matching name
            queue->referenceCount++;
            // Unlock global management structure here
            unlock_global_management(); // Pseudocode - Implement unlocking

            printf("Message queue '%s' opened successfully.\n", mqname);
            return i; // Return the index as the queue identifier
        }
    }

    // Optionally, create a new queue if not found and if system design permits
    // int newQueueID = create_new_queue(mqname);
    // if (newQueueID != MF_ERROR) {
    //     unlock_global_management(); // Ensure to unlock before returning
    //     return newQueueID;
    // }

    // Unlock global management structure here if no queue is found and no new queue is created
    unlock_global_management(); // Pseudocode - Implement unlocking

    printf("Message queue '%s' not found.\n", mqname);
    return MF_ERROR; // Queue not found
}



int mf_close(int qid) {
    if (shm_start == NULL) {
        printf("Shared memory not initialized.\n");
        return MF_ERROR;
    }

    ManagementSection* mgmt = (ManagementSection*)shm_start;
    if (qid < 0 || qid >= config.max_queues_in_shmem) {
        printf("Invalid queue identifier.\n");
        return MF_ERROR;
    }

    MQMetadata* queue = &mgmt->queues[qid];
    if (!queue->isActive) {
        printf("Queue identifier does not refer to an active queue.\n");
        return MF_ERROR;
    }

    // Lock the queue before making changes
    if (lock_queue(queue) != MF_SUCCESS) {
        printf("Failed to lock the queue.\n");
        return MF_ERROR;
    }

    // Properly deactivate the queue and clear its metadata
    queue->isActive = 0;
    memset(queue->mqname, 0, MAX_MQNAMESIZE); // Clear the queue name
    queue->head = 0;
    queue->tail = 0;
    queue->size = 0;
    queue->referenceCount = queue->referenceCount == 0 ? 0 : queue->referenceCount--;
    // Remove the semaphore associated with the queue
    if (remove_semaphore_for_queue(queue) != MF_SUCCESS) {
        printf("Failed to remove the semaphore for queue '%s'.\n", queue->mqname);
        // Attempt to unlock the queue before returning with error
        unlock_queue(queue);
        return MF_ERROR;
    }

    printf("Message queue '%s' closed successfully.\n", queue->mqname);

    // Unlock the queue after operation
    if (unlock_queue(queue) != MF_SUCCESS) {
        printf("Warning: Failed to unlock the queue. This might lead to deadlocks.\n");
        // In a real implementation, consider logging this error for further investigation
    }

    return MF_SUCCESS;
}



// Simplified function to find the end of the queue and check for available space.
// Returns pointer to where the new message should be written, or NULL if not enough space.
void* find_message_insertion_point(MQMetadata* mq, int messageSize) {
    // Assuming the first message starts immediately after MQMetadata
    unsigned int currentOffset = mq->start_offset + sizeof(MQMetadata);
    unsigned int queueEnd = shm_start + mq->end_offset;

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
    if (shm_start == NULL || bufptr == NULL || datalen <= 0) {
        printf("Invalid parameters or shared memory not initialized.\n");
        return MF_ERROR;
    }

    if (qid < 0 || qid >= config.max_queues_in_shmem) {
        printf("Invalid queue identifier.\n");
        return MF_ERROR;
    }

    MQMetadata* mq = &((MQMetadata*)shm_start)[qid];
    if (!mq->isActive) {
        printf("Queue identifier does not refer to an active queue.\n");
        return MF_ERROR;
    }

    // Ensure data length, including its size prefix, can fit in the queue
    unsigned int totalDataLength = datalen + sizeof(unsigned int); // Include size prefix
    if (mq->capacity - mq->size < totalDataLength) {
        printf("Not enough space in the queue to send the message.\n");
        return MF_ERROR;
    }

    // Lock the queue
    if (lock_queue(mq) != MF_SUCCESS) {
        printf("Failed to lock the queue.\n");
        return MF_ERROR;
    }

    // Calculate insertion point based on tail
    char* insertionPoint = (char*)mq + mq->tail;

    // Write the message size and data
    *((unsigned int*)insertionPoint) = datalen; // Prefix with size
    memcpy(insertionPoint + sizeof(unsigned int), bufptr, datalen); // Copy message

    // Update metadata
    mq->tail = (mq->tail + totalDataLength) % mq->capacity; // Advance tail, wrap if necessary
    mq->size += totalDataLength; // Update current size

    printf("Message sent successfully to queue '%s'.\n", mq->mqname);

    // Unlock the queue
    if (unlock_queue(mq) != MF_SUCCESS) {
        printf("Failed to unlock the queue.\n");
        // Consider error handling if unlock fails; primary operation has succeeded
    }

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

int mf_print() {
    if (shm_start == NULL) {
        printf("Shared memory not initialized.\n");
        return MF_ERROR;
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
    return MF_SUCCESS;
}

int mf_recv(int qid, void* bufptr, int bufsize) {
    if (shm_start == NULL || bufptr == NULL) {
        printf("Invalid parameters or shared memory not initialized.\n");
        return MF_ERROR;
    }

    if (qid < 0 || qid >= config.max_queues_in_shmem) {
        printf("Invalid queue identifier.\n");
        return MF_ERROR;
    }

    MQMetadata* mq = &((MQMetadata*)shm_start)[qid];
    if (!mq->isActive) {
        printf("Queue identifier does not refer to an active queue.\n");
        return MF_ERROR;
    }

    // Lock the queue for exclusive access
    if (lock_queue(mq) != MF_SUCCESS) {
        printf("Failed to lock the queue.\n");
        return MF_ERROR;
    }

    // Check if the queue has messages
    if (mq->size == 0) {
        printf("The queue is empty.\n");
        unlock_queue(mq);
        return MF_ERROR;
    }

    // Assume retrieve_first_message handles reading and updating the queue's metadata
    int messageSize = retrieve_first_message(mq, bufptr, bufsize);
    if (messageSize <= 0) {
        printf("Failed to retrieve a message from the queue '%s'.\n", mq->mqname);
        unlock_queue(mq); // Ensure to unlock the queue before returning
        return MF_ERROR;
    }

    printf("Message retrieved successfully from queue '%s'.\n", mq->mqname);

    // Unlock the queue after operation
    unlock_queue(mq);

    return messageSize;
}

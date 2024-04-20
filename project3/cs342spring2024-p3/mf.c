/*
 * Includes of the mf.c
 */
#include <stdio.h>
#include <unistd.h>
#include <malloc.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include "mf.h"
#include <sys/mman.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <semaphore.h>
/*
 * Defines of the mf.c
 */
#define GLOBAL_MANAGEMENT_SEM_NAME_PREFIX "/mf_global_management_sem"
#define SEM_NAME_PREFIX "/mf_sem_"
#define MAX_SEM_NAME_SIZE 64
#define MF_SUCCESS 0
#define MF_ERROR -1
#define INITIAL_CAPACITY 10
char GLOBAL_MANAGEMENT_SEM_NAME[MAX_SEM_NAME_SIZE];

/*
 * This structure, MFConfig, describes the configuration descriptor of the framework.
 * It includes the parameters for configuring shared memory and message queue behavior.
 * The shmem_name field will be the name of the shared memory segment. The name is relevant in the correct mapping and access of the shared memory by processes.
 * The field shmem_size indicates the shared memory segment size in kilobytes to hold a specific amount of data.
 * With the max_msgs_in_queue field, this will set the maximum number of messages that any single queue can handle and manage its capacity effectively.
 * Finally, max_queues_in_shmem sets the maximum number of different message queues that can exist simultaneously in shared memory.
 * This would allow, in the same shared memory segment, a facility to support multiple distinct communication channels.
 */
typedef struct {
    char shmem_name[MAX_MQNAMESIZE];
    int shmem_size;       // In KB
    int max_msgs_in_queue;
    int max_queues_in_shmem;
} MFConfig;
MFConfig config;


/*
 * The structure ActiveProcessList represents a list of active processes within a system. It contains an array of ActiveProcess structures, one for each active process.
 * Currently, the only identifier defined for ActiveProcess is a pid_t process ID. The ActiveProcessList structure should have three prominent members.
 * It should have the array processes of ActiveProcess structures, which shall be responsible for holding the details of every active process.
 */
typedef struct {
    pid_t process_id;
} ActiveProcess;

typedef struct {
    ActiveProcess processes[MAX_SHMEMSIZE];
    size_t size;
    size_t capacity;
} ActiveProcessList;

ActiveProcessList* activeProcessList;



/*
 * This structure acts as the central administrative entity within a shared memory segment and is used to manage a number of message queues.
 * Each ManagementSection houses an array of MQMetadata representing the detailed information of a message queue.
 * To discuss in this context, these queues are allocated statically, based on the calculated maximum queue number that fits into the size of shared memory (MAX_SHMEMSIZE divided by MIN_MQSIZE).
 * The entire structure of MQMetadata related to each message queue consists of:
 * mqname: Name of the Message Queue (name);
 * sem_name: Semaphore name used for synchronizing access;
 * start_offset: Start byte offset in shared memory;
 * end_offset: End byte offset in that shared memory for the given queue;
 * head and tail indices (head, tail) indicating the size of the current queue;
 * capacity representing the maximum size for a queue;
 * flags indicating the queue activity state (isActive) and its referencing count by processes (referenceCount).
 * It is designed to permit more than one process to send and receive messages to each other through a set of shared queues.
 *
 */
typedef struct {
    char mqname[MAX_MQNAMESIZE];
    char sem_name[MAX_SEM_NAME_SIZE];
    unsigned int start_offset, end_offset;
    unsigned int head, tail;
    unsigned int capacity;
    unsigned int size;
    int isActive;
    int referenceCount;
} MQMetadata;

typedef struct {
    MQMetadata queues[MAX_SHMEMSIZE / MIN_MQSIZE];
    unsigned int queue_count;
} ManagementSection;
ManagementSection* mgmt;
void* shm_start = NULL;
unsigned int shm_size = 0;
unsigned int next_free_offset = sizeof(MQMetadata) * 10;

typedef struct {
    void* ptr;
    size_t size;
} SharedMemoryContext;
SharedMemoryContext shm_context;


void generate_general_semaphore_name() {
    snprintf(GLOBAL_MANAGEMENT_SEM_NAME, MAX_SEM_NAME_SIZE, "%s_%s", GLOBAL_MANAGEMENT_SEM_NAME_PREFIX, "c583b0f3-addc-4567-8f1a-d4b544c30076");
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
            printf("%s\n", config->shmem_name);
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

void resizeActiveProcessList() {
    activeProcessList->capacity *= 2;
}
void addActiveProcess(pid_t pid) {
    if (activeProcessList->size == activeProcessList->capacity) {
        // Resize the array if necessary
        resizeActiveProcessList();
    }
    activeProcessList->processes[activeProcessList->size].process_id = pid;
    activeProcessList->size++;
}

void removeActiveProcess(pid_t pid) {
    for (size_t i = 0; i < activeProcessList->size; i++) {
        if (activeProcessList->processes[i].process_id == pid) {
            for (size_t j = i; j < activeProcessList->size - 1; j++) {
                activeProcessList->processes[j] = activeProcessList->processes[j + 1];
            }
            activeProcessList->size--;
            return;
        }
    }
    printf("Process %d not found in active process list.\n", pid);
}

void freeActiveProcessList() {
    free(activeProcessList->processes);
    activeProcessList->size = 0;
    activeProcessList->capacity = 0;
}

/*
 * The retrieve_first_message function should retrieve the first message in the given message queue.
 * The following three parameters exist:
 * a pointer to the MQMetaData structure for this queue (mq),
 * a buffer (bufptr) into which the message is to be put,
 * and the size of this buffer (bufsize).
 */
int retrieve_first_message(MQMetadata* mq, void* bufptr, int bufsize) {
    if (mq->size == 0) {
        printf("No messages to retrieve: Queue is empty.\n");
        return MF_ERROR;
    }

    char* queueBuffer = (char*)shm_start + mq->start_offset;
    unsigned int messageSize;
    memcpy(&messageSize, queueBuffer + mq->head, sizeof(unsigned int));
    if (bufsize < messageSize) {
        printf("Buffer too small: Needed %u, Provided %d.\n", messageSize, bufsize);
        return MF_ERROR;
    }

    unsigned int messageDataPos = mq->head + sizeof(unsigned int);
    if (messageDataPos + messageSize > mq->capacity * 1024) {
        int firstPartSize = mq->capacity * 1024 - messageDataPos;
        memcpy(bufptr, queueBuffer + messageDataPos, firstPartSize);
        memcpy((char*)bufptr + firstPartSize, queueBuffer, messageSize - firstPartSize);
    } else {
        memcpy(bufptr, queueBuffer + messageDataPos, messageSize);
    }

    mq->head = (mq->head + sizeof(unsigned int) + messageSize) % (mq->capacity * 1024);
    mq->size -= (sizeof(unsigned int) + messageSize);
    printf("Message retrieved: New head %u, Remaining size %u.\n", mq->head, mq->size);
    return messageSize;
}

void generate_semaphore_name(char* dest, const char* mqname, int index) {
    snprintf(dest, MAX_SEM_NAME_SIZE, "%s%s_%d", SEM_NAME_PREFIX, mqname, index);
}
/*
 *
 * The initialize_semaphore_for_queue function will basically initialize the semaphore for a queue of messages.
 * It takes two parameters: a pointer to MQMetadata, which represents a queue, and an integer index that uniquely identifies the queue.
 */
int initialize_semaphore_for_queue(MQMetadata* queue, int index) {
    char sem_name[MAX_SEM_NAME_SIZE];
    generate_semaphore_name(sem_name, queue->mqname, index);

    strncpy(queue->sem_name, sem_name, MAX_SEM_NAME_SIZE);
    //printf("\033[0;32m pass 4.6.1 \033[0m\n");

    sem_t* sem = sem_open(sem_name, O_CREAT | O_EXCL, 0644, 1);
    //printf("\033[0;32m pass 4.7 \033[0m\n");

    if (sem == SEM_FAILED) {
        perror("Failed to create semaphore");
        return MF_ERROR;
    }
    sem_close(sem); // Close handle, semaphore is not removed
    //printf("\033[0;32m pass 4.8 \033[0m\n");

    return MF_SUCCESS;
}
/*
 *
 * lock_queue is used to synchronize access to a concrete message queue.
 * It represents a structure MQMetadata and uses POSIX semaphore synchronization; hence, at a time, the states of the process shouldn't exceed one process.
 */
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
int mf_init() {
    read_configuration(CONFIG_FILENAME, &config);
    int fd = shm_open(config.shmem_name, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
    if (fd == -1) {
        perror("shm_open failed");
        return MF_ERROR;
    }
    if (ftruncate(fd, sizeof(ManagementSection)) == -1) {
        perror("ftruncate failed");
        close(fd);
        shm_unlink(config.shmem_name);
        return MF_ERROR;
    }
    int fd2 = shm_open("/sharedmemoryname-236545af-f246-4f96-abd8-3d4f6a3befa7", O_CREAT | O_RDWR, 0666);
    if (fd2 == -1) {
        perror("sharedmemoryname-236545af-f246-4f96-abd8-3d4f6a3befa7 shm_open failed");
        return MF_ERROR;
    }
    generate_general_semaphore_name();
    shm_start = mmap(NULL, sizeof(ManagementSection), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (shm_start == MAP_FAILED) {
        perror("mmap failed");
        close(fd);
        shm_unlink(config.shmem_name);
        return MF_ERROR;
    }

    close(fd);


    size_t shm_size = sizeof(ActiveProcessList);
    if (ftruncate(fd2, shm_size) == -1) {
        perror("ftruncate failed");
        close(fd2);
        shm_unlink("/sharedmemoryname-236545af-f246-4f96-abd8-3d4f6a3befa7");
        return MF_ERROR;
    }

    void* shm_ptr = mmap(NULL, shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd2, 0);
    if (shm_ptr == MAP_FAILED) {
        perror("mmap failed");
        close(fd2);
        shm_unlink("/sharedmemoryname-236545af-f246-4f96-abd8-3d4f6a3befa7");
        return MF_ERROR;
    }

    activeProcessList = (ActiveProcessList*) shm_ptr;
    activeProcessList->size = 0;
    activeProcessList->capacity = INITIAL_CAPACITY;

    ManagementSection* mgmt = (ManagementSection*)shm_start;
    memset(mgmt, 0, shm_size);
    mgmt->queue_count = 0;

    munmap(shm_ptr, shm_size);
    close(fd2);

    printf("Initialization complete. Shared memory: %s\n", "/sharedmemoryname-236545af-f246-4f96-abd8-3d4f6a3befa7");
    return MF_SUCCESS;
}

int mf_destroy() {
    if (shm_start == NULL) {
        fprintf(stderr, "Shared memory is not initialized.\n");
        return MF_ERROR;
    }
    sem_t* globalSem = sem_open(GLOBAL_MANAGEMENT_SEM_NAME, O_CREAT | O_EXCL, 0644, 1);
    if (globalSem == SEM_FAILED) {
        if (errno == EEXIST) {
            globalSem = sem_open(GLOBAL_MANAGEMENT_SEM_NAME, 0);
        } else {
            perror("Failed to create global management semaphore");
            return MF_ERROR;
        }
    }
    mgmt = (ManagementSection*)shm_start;
    for (int i = 0; i < mgmt->queue_count; i++) {
        MQMetadata* queue = &mgmt->queues[i];
        if (queue->isActive) {
            if (sem_unlink(queue->sem_name) == -1) {
                perror("sem_unlink failed for a message queue semaphore");
            }
            queue->referenceCount = 0;
            queue->isActive = 0;
        }
    }
    freeActiveProcessList();
    if (munmap(shm_start, config.shmem_size) == -1) {
        perror("munmap failed");
        return MF_ERROR;
    }
    if (shm_unlink(config.shmem_name) == -1) {
        perror("shm_unlink failed");
        return MF_ERROR;
    }
    if (sem_unlink(GLOBAL_MANAGEMENT_SEM_NAME) != 0) {
        perror("Failed to unlink global management semaphore");
    }
    shm_start = NULL;
    sem_close(globalSem);

    return MF_SUCCESS;
}


int mf_connect() {
    strcpy(GLOBAL_MANAGEMENT_SEM_NAME, "mf_global_management_sem_c583b0f3-addc-4567-8f1a-d4b544c30076");
    sem_t* globalSem = sem_open(GLOBAL_MANAGEMENT_SEM_NAME, O_CREAT | O_EXCL, 0644, 1);
    if (globalSem == SEM_FAILED) {
        if (errno == EEXIST) {
            globalSem = sem_open(GLOBAL_MANAGEMENT_SEM_NAME, 0); // Open existing semaphore
        } else {
            perror("Failed to create global management semaphore");
            return MF_ERROR;
        }
    }
    int shm_fd2 = shm_open("/sharedmemoryname-236545af-f246-4f96-abd8-3d4f6a3befa7", O_RDWR, 0666);
    if (shm_fd2 == -1) {
        perror("shm_open in mf_connect failed");
        return MF_ERROR;
    }
    struct stat shm_stat2;
    if (fstat(shm_fd2, &shm_stat2) == -1) {
        perror("fstat failed in mf_connect");
        close(shm_fd2);
        return MF_ERROR;
    }
    void* shm_ptr = mmap(NULL, shm_stat2.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd2, 0);
    if (shm_ptr == MAP_FAILED) {
        perror("mmap failed in mf_connect");
        close(shm_fd2);
        return MF_ERROR;
    }
    if (globalSem == SEM_FAILED) {
        perror("Failed to open global management semaphore");
        return MF_ERROR;
    }
    read_configuration(CONFIG_FILENAME, &config);

    if (config.shmem_size < MIN_SHMEMSIZE || config.shmem_size  > MAX_SHMEMSIZE) {
        fprintf(stderr, "Shared memory size in the config file is out of valid range\n");
        return MF_ERROR;
    }
    //printf("\033[0;32m pass 3 \033[0m\n");

    int fd = shm_open(config.shmem_name, O_RDWR, 0666);
    shm_start = mmap(NULL, sizeof(ManagementSection), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);


    int fd3 = shm_open("/sharedmemoryname-236545af-f246-4f96-abd8-3d4f6a3befa7", O_RDWR, 0666);
    void* shm_start3 = mmap(NULL, sizeof(ActiveProcessList), PROT_READ | PROT_WRITE, MAP_SHARED, fd3, 0);
    close(fd3);

    mgmt = (ManagementSection*)shm_start;
    activeProcessList = (ActiveProcessList*)shm_start3;

    printf("Connected to shared memory: %s. Active queues: %u\n", "/sharedmemoryname-236545af-f246-4f96-abd8-3d4f6a3befa7", mgmt->queue_count);
    for (int i = 0; i < mgmt->queue_count; i++) {
        if (mgmt->queues[i].isActive) {
            printf("Queue %d is active.\n", i);
        }
    }

    sem_close(globalSem);
    //printf("\033[0;32m pass 5 \033[0m\n");
    int shm_fd = shm_open(config.shmem_name, O_RDWR, 0);
    if (shm_fd == -1) {
        perror("shm_opessn in mf_connect failed");
        return MF_ERROR;
    }
    struct stat shm_stat;
    if (fstat(shm_fd, &shm_stat) == -1) {
        perror("fstat failed in mf_connect");
        close(shm_fd);
        return MF_ERROR;
    }
    shm_context.size = shm_stat.st_size;
    //printf("\033[0;32m pass 6 \033[0m\n");

    // Map the shared memory object
    shm_context.ptr = mmap(NULL, shm_context.size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shm_context.ptr == MAP_FAILED) {
        perror("mmap failed in mf_connect");
        close(shm_fd);
        return MF_ERROR;
    }
    //printf("\033[0;32m pass 7 \033[0m\n");

    if (sem_open(GLOBAL_MANAGEMENT_SEM_NAME, 0) == SEM_FAILED) {
        perror("Failed to open global management semaphore");
        return MF_ERROR;
    }

   // printf("\033[0;32m pass 8 \033[0m\n");
    addActiveProcess(getpid());
    //printf("\033[0;32m pass 9 \033[0m\n");
    sem_close(globalSem);
    //printf("\033[0;32m pass 10 \033[0m\n");
    close(shm_fd);
    //printf("\033[0;32m pass 11 \033[0m\n");

    return MF_SUCCESS;
}


int mf_disconnect() {
    if (shm_context.ptr == NULL || shm_context.size == 0) {
        printf("Shared memory not initialized or already disconnected.\n");
        return MF_ERROR;
    }
    int pid = getpid();
    sem_t* globalSem = sem_open(GLOBAL_MANAGEMENT_SEM_NAME, O_CREAT | O_EXCL, 0644, 1);
    if (globalSem == SEM_FAILED) {
        if (errno == EEXIST) {
            globalSem = sem_open(GLOBAL_MANAGEMENT_SEM_NAME, 0);
        } else {
            perror("Failed to create global management semaphore");
            return MF_ERROR;
        }
    }
    removeActiveProcess(pid);
    sem_close(globalSem);
    shm_context.ptr = NULL;
    shm_context.size = 0;

    printf("Process disconnected successfully.\n");
    return MF_SUCCESS;
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

    if (mgmt->queue_count >= config.max_queues_in_shmem) {
        printf("Maximum number of queues reached.\n");
        sem_close(globalSem);
        return MF_ERROR;
    }
    for (int i = 0; i < mgmt->queue_count; i++) {
        if (strncmp(mgmt->queues[i].mqname, mqname, MAX_MQNAMESIZE) == 0 && mgmt->queues[i].isActive) {
            printf("Queue with the name '%s' already exists.\n", mqname);
            sem_close(globalSem);
            return MF_ERROR;
        }
    }
    for (int i = 0; i < config.max_queues_in_shmem; i++) {
        if (!mgmt->queues[i].isActive) {
            strncpy(mgmt->queues[i].mqname, mqname, MAX_MQNAMESIZE - 1);
            mgmt->queues[i].mqname[MAX_MQNAMESIZE - 1] = '\0';
            mgmt->queues[i].isActive = 1;
            mgmt->queues[i].capacity = mqsize;
            mgmt->queues[i].size = 0;
            mgmt->queues[i].head = 0;
            mgmt->queues[i].tail = 0;
            mgmt->queues[i].referenceCount = 0;
            if (i == 0) {
                mgmt->queues[i].start_offset = sizeof(ManagementSection);
            } else {
                mgmt->queues[i].start_offset = mgmt->queues[i - 1].end_offset;
            }
            mgmt->queues[i].end_offset = mgmt->queues[i].start_offset + mqsize;
            if (initialize_semaphore_for_queue(&mgmt->queues[i], i) != MF_SUCCESS) {
                printf("Failed to initialize semaphore for queue '%s'.\n", mqname);
                mgmt->queues[i].isActive = 0;
                sem_close(globalSem);
                return MF_ERROR;
            }

            mgmt->queue_count++;
            sem_close(globalSem);
            printf("Queue '%s' created successfully with index %d.\n", mqname, i);
            return i;
        }
    }
    sem_close(globalSem);
    return MF_ERROR;
}


int mf_remove(char *mqname) {
    if (shm_start == NULL) {
        printf("Shared memory not initialized.\n");
        return MF_ERROR;
    }
    sem_t* globalSem = sem_open(GLOBAL_MANAGEMENT_SEM_NAME, O_CREAT | O_EXCL, 0644, 1);
    if (globalSem == SEM_FAILED) {
        if (errno == EEXIST) {
            globalSem = sem_open(GLOBAL_MANAGEMENT_SEM_NAME, 0); // Open existing semaphore
        } else {
            perror("Failed to create global management semaphore");
            return MF_ERROR;
        }
    }
    for (int i = 0; i < mgmt->queue_count; i++) {
        MQMetadata* queue = &mgmt->queues[i];
        if (strncmp(queue->mqname, mqname, MAX_MQNAMESIZE) == 0) {
            if (queue->referenceCount > 0) {
                printf("Cannot remove queue '%s' as it is still in use (reference count: %d).\n", mqname, queue->referenceCount);
                sem_close(globalSem);
                return MF_ERROR;
            }
            if (lock_queue(queue) != MF_SUCCESS) {
                printf("Failed to lock message queue '%s' for removal.\n", mqname);
                sem_close(globalSem);
                return MF_ERROR;
            }
            memset(queue, 0, sizeof(MQMetadata));
            printf("Message queue '%s' removed successfully.\n", mqname);
            if (remove_semaphore_for_queue(queue) != MF_SUCCESS) {
                printf("Failed to remove semaphore for queue '%s'.\n", mqname);
            }
            queue->isActive = 0;
            unlock_queue(queue);
            mgmt->queue_count--;
            sem_close(globalSem);
            return MF_SUCCESS;
        }
    }
    sem_close(globalSem);
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
    sem_t* globalSem = sem_open(GLOBAL_MANAGEMENT_SEM_NAME, O_CREAT | O_EXCL, 0644, 1);
    printf("isActive: %d \n", mgmt->queues[0].isActive);
    if (globalSem == SEM_FAILED) {
        if (errno == EEXIST) {
            globalSem = sem_open(GLOBAL_MANAGEMENT_SEM_NAME, 0);
        } else {
            perror("Failed to create global management semaphore");
            return MF_ERROR;
        }
    }
    if (globalSem == SEM_FAILED) {
        perror("Failed to open global management semaphore");
        return MF_ERROR;
    }

    for (int i = 0; i < config.max_queues_in_shmem; i++) {
        MQMetadata* queue = &mgmt->queues[i];
        if (strncmp(queue->mqname, mqname, MAX_MQNAMESIZE) == 0 && queue->isActive) {
            queue->referenceCount++;
            sem_close(globalSem);

            printf("Message queue '%s' opened successfully.\n", mqname);
            return i;
        }
    }
    sem_close(globalSem);
    printf("Message queue '%s' not found.\n", mqname);
    return MF_ERROR;
}



int mf_close(int qid) {
    if (shm_start == NULL) {
        printf("Shared memory not initialized.\n");
        return MF_ERROR;
    }
    if (qid < 0 || qid >= config.max_queues_in_shmem) {
        printf("Invalid queue identifier.\n");
        return MF_ERROR;
    }
    MQMetadata* queue = &mgmt->queues[qid];
    if (!queue->isActive) {
        printf("Queue identifier does not refer to an active queue.\n");
        return MF_ERROR;
    }
    if (lock_queue(queue) != MF_SUCCESS) {
        printf("Failed to lock the queue.\n");
        return MF_ERROR;
    }
    if (queue->referenceCount > 0) {
        queue->referenceCount--;
        if (queue->referenceCount == 0) {
            queue->isActive = 0;
            printf("Queue '%s' is now inactive as no more references exist.\n", queue->mqname);
        }
    }
    // Unlock the queue after operation
    if (unlock_queue(queue) != MF_SUCCESS) {
        printf("Warning: Failed to unlock the queue. This might lead to deadlocks.\n");
    }
    return MF_SUCCESS;
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
    MQMetadata* mq = &mgmt->queues[qid];
    if (!mq->isActive) {
        printf("Queue identifier does not refer to an active queue.\n");
        return MF_ERROR;
    }
    unsigned int totalDataLength = datalen + sizeof(unsigned int);
    if (mq->capacity * 1024 - mq->size < totalDataLength) {
        printf("Not enough space in the queue to send the message. Needed: %u, Available: %u\n", totalDataLength, (mq->capacity * 1024) - mq->size);
        return MF_ERROR;
    }
    if (lock_queue(mq) != MF_SUCCESS) {
        printf("Failed to lock the queue.\n");
        return MF_ERROR;
    }
    char* bufferStart = (char*)shm_start + mq->start_offset;
    char* insertionPoint = bufferStart + mq->tail;
    *((unsigned int*)insertionPoint) = datalen;
    memcpy(insertionPoint + sizeof(unsigned int), bufptr, datalen);

    mq->tail = (mq->tail + totalDataLength) % (mq->capacity * 1024);
    mq->size += totalDataLength;
    if (unlock_queue(mq) != MF_SUCCESS) {
        printf("Failed to unlock the queue.\n");
    }
    printf("Message sent successfully to queue '%s'.\n", mq->mqname);
    unsigned int checkSize;
    memcpy(&checkSize, insertionPoint, sizeof(unsigned int));
    if (checkSize != datalen) {
        printf("Error: Message size written (%u) does not match data length (%d).\n", checkSize, datalen);
    }
    return MF_SUCCESS;
}
/*
 * Helper function for the mf_print method.
 */
void print_message_preview(void* message, unsigned int size) {
    printf("Message Size: %u, Preview: ", size);
    for (unsigned int i = 0; i < size && i < 10; ++i) {
        char c = *((char*)message + i);
        printf("%c", isprint(c) ? c : '.');
    }
    printf("\n");
}

int mf_print() {
    if (shm_start == NULL) {
        printf("Shared memory not initialized.\n");
        return MF_ERROR;
    }
    MQMetadata* directory = (MQMetadata*)shm_start;
    for (int i = 0; i < mgmt->queue_count; i++) {
        if (directory[i].mqname[0] != '\0') {
            printf("Queue '%s':\n", directory[i].mqname);
            unsigned int currentOffset = directory[i].start_offset + sizeof(MQMetadata);
            while (currentOffset < directory[i].end_offset) {
                unsigned int* messageSizePtr = (unsigned int*)(shm_start + currentOffset);
                if (*messageSizePtr == 0) {
                    break;
                }
                print_message_preview((char*)messageSizePtr + sizeof(unsigned int), *messageSizePtr);
                currentOffset += sizeof(unsigned int) + *messageSizePtr;
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
    MQMetadata* mq = &mgmt->queues[qid];
    if (!mq->isActive) {
        printf("Queue identifier does not refer to an active queue.\n");
        return MF_ERROR;
    }
    if (lock_queue(mq) != MF_SUCCESS) {
        printf("Failed to lock the queue.\n");
        return MF_ERROR;
    }
    if (mq->size == 0) {
        printf("The queue is empty.\n");
        unlock_queue(mq);
        return MF_ERROR;
    }
    int messageSize = retrieve_first_message(mq, bufptr, bufsize);
    if (messageSize <= 0) {
        printf("Failed to retrieve a message from the queue '%s'.\n", mq->mqname);
        unlock_queue(mq);
        return MF_ERROR;
    }
    printf("Message retrieved successfully from queue '%s'. Size: %d bytes.\n", mq->mqname, messageSize);
    unlock_queue(mq);
    return messageSize;
}


#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <mqueue.h>
#include <string.h>

#define MAX_MSG_SIZE 256
#define QUEUE_PERMISSIONS 0660
#define CONNECTION_REQUEST 1
#define CONNECTION_REPLY 2 //
#define COMMAND_LINE 3
#define COMMAND_RESULT 4 //
#define QUIT_REQUEST 5
#define QUIT_REPLY 6 //
#define QUIT_ALL_REQUEST 7
#define BUFFER_SIZE 1024



void handle_client(char *csPipeName, char *scPipeName, int wSize);

// Function to save the value to a file
void save_value(int value) {
    FILE *file = fopen("storage.txt", "w");
    if (file != NULL) {
        fprintf(file, "%d", value);
        fclose(file);
    } else {
        perror("Error opening file for writing");
    }
}

// Function to read the value from a file
int read_value() {
    int value = 0;
    FILE *file = fopen("storage.txt", "r");
    if (file != NULL) {
        fscanf(file, "%d", &value);
        fclose(file);
    } else {
        perror("Error opening file for reading");
    }
    return value;
}
char* getSubstringFromSecondSpace(char* input) {
    int spaceCount = 0;
    const char* current = input;

    // Find the position of the 2nd space
    while (*current != '\0') {
        if (*current == ' ') {
            spaceCount++;
            if (spaceCount == 2) {
                break;
            }
        }
        current++;
    }

    // Calculate the length of the substring
    size_t substringLength = 0;
    const char* substringStart = current + 1; // Start from the character after the 2nd space

    while (*current != '\0') {
        substringLength++;
        current++;
    }

    // Allocate memory for the substring
    char* substring = (char*)malloc(substringLength + 1);

    // Copy the substring into the allocated memory
    strncpy(substring, substringStart, substringLength);
    substring[substringLength] = '\0'; // Null-terminate the substring

    // Remove leading spaces in the substring
    size_t leadingSpaces = strspn(substring, " ");
    memmove(substring, substring + leadingSpaces, substringLength - leadingSpaces + 1);

    return substring;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <MQNAME>\n", argv[0]);
                fflush(stdout);

        exit(EXIT_FAILURE);
    }

    // The MQNAME is obtained from the command-line arguments
    char *mqName = argv[1];
    mqd_t mq;
    struct mq_attr attr = {
        .mq_flags = 0,       // or O_NONBLOCK for non-blocking operations
        .mq_maxmsg = 10,     // Maximum number of messages allowed in queue
        .mq_msgsize = MAX_MSG_SIZE, // Maximum message size
        .mq_curmsgs = 0      // Number of messages currently in the queue (set by the system)
    };

    // Create or open the message queue using MQNAME
    printf(" %s\n", "bef mq_open");
    fflush(stdout);
    mq = mq_open(mqName, O_RDWR | O_CREAT, QUEUE_PERMISSIONS, &attr);
    printf(" %s\n", "after mq_open");
    fflush(stdout);
    if (mq == (mqd_t)-1) {
        printf("%s \n", "done");
        fflush(stdout);

        perror("mq_open");
        exit(EXIT_FAILURE);
    }

    printf("Server is running. Waiting for connections on message queue '%s'...\n", mqName);
    fflush(stdout);
    while (1) {
        char buffer[MAX_MSG_SIZE];
        memset(buffer, 0, MAX_MSG_SIZE); // Clear the buffer

        // Wait to receive a connection request message
        if (mq_receive(mq, buffer, MAX_MSG_SIZE, NULL) == -1) {
            perror("mq_receive");
            printf("%s \n", "entered mq recieve");
            fflush(stdout);

            continue; // Continue to next iteration if an error occurs
        }

        // Parse the received message to obtain client's named pipe names and WSIZE
        char csPipeName[100], scPipeName[100];
        int wSize;
        printf("%s\n", buffer);
        fflush(stdout);
        //connection_info_len, CONNECTION_REQUEST, "", connection_info
        int connection_info_len = 0;
        int connection_request = 0; //DELETE THIS. ONLY FOR DEVELOPMENT!!!!
        sscanf(buffer, "%d %d %s %s %d", &connection_info_len, &connection_request, csPipeName ,scPipeName, &wSize);
        printf("The wsize : %d \n", wSize);
        // Fork a new process to handle the client
        pid_t pid = fork();
        if (pid == 0) { // Child process
            printf("%s \n", "main hande client");
                    fflush(stdout);

            handle_client(csPipeName, scPipeName, wSize);
            exit(EXIT_SUCCESS); // Ensure child process exits after handling
        }
        else if (pid < 0) {
            perror("fork");
                printf("%s \n", "err");
                        fflush(stdout);

            continue; // If fork fails, continue to next iteration to keep server running
        }
        // Parent process (server) does not wait for child processes to exit
        // wait(NULL); // Optionally wait for child processes if required
    }
        printf("%s \n", "done");
        fflush(stdout);

    // Cleanup before exiting
    mq_close(mq);
    mq_unlink(mqName);

    return 0;
}

void handle_client(char *csPipeName, char *scPipeName, int wSize) {
    // Print paths in server code
    int client_count = read_value();
    printf("Server - cssc_pipe_name: %s\n", csPipeName);
    fflush(stdout);

    //printf("Server - sc_pipe: %s\n", scPipeName);
    int csPipe = open(csPipeName, O_RDWR);
    printf(" %s\n", "bef open");
    fflush(stdout);
    client_count = read_value();
    client_count = client_count + 1;
    save_value(client_count);
    int scPipe = open(scPipeName, O_RDWR);
    printf("%s\n", "after open");
    fflush(stdout);
    printf("Server - cssc_pipe_name: %s\n", csPipeName);
    printf("Server - sc_pipe: %s\n", scPipeName);
    printf("Server - client count: %d\n", client_count);
        fflush(stdout);


            
    char *cmdBufferFull = (char *)malloc(MAX_MSG_SIZE * sizeof(char));
    char responseBuffer[MAX_MSG_SIZE];

    FILE *fp;
    const char *tempFileName = "comserver_temp";

    if (csPipe == -1 || scPipe == -1) {
        perror("Opening pipes");
        return;
    }

    // Send connection established message
    strcpy(responseBuffer, "Connection established");

    int data_len = strlen(responseBuffer) + 1; // Include the null terminator
    int message_len = 7 + data_len;   // Length + Type + Padding

    // Prepare the message
    char message[BUFFER_SIZE];
    sprintf(message, "%4d%1d%3s%s", message_len, CONNECTION_REPLY, "", responseBuffer);
	
    write(scPipe, message, wSize);

    while (1) {
        printf("%s \n", "entered handle client");
        fflush(stdout);
        int bytesRead = read(csPipe, cmdBufferFull, MAX_MSG_SIZE - 1);
        if (bytesRead <= 0) {
            break; // Break the loop if read fails or when "quit" command is received
        }
        
        if (bytesRead < MAX_MSG_SIZE - 1) {
            cmdBufferFull[bytesRead] = '\0';
        } else {
            // Handle the case where the buffer is full
            cmdBufferFull[MAX_MSG_SIZE - 1] = '\0';
        }
        //cmdBuffer = getSubstringFromSecondSpace(cmdBuffer);
        char* abc = getSubstringFromSecondSpace(cmdBufferFull);
        char* cmdBuffer = getSubstringFromSecondSpace(abc);
        if (strcmp(cmdBuffer, "quit") == 0) {
            client_count = read_value();
            client_count = client_count - 1;
            save_value(client_count);
            strcpy(responseBuffer, "quit-ack");
            printf("Server - client count: %d\n", client_count);
            write(scPipe, responseBuffer, strlen(responseBuffer) + 1);
            //break;
        }
        // Execute command and write output to a temporary file
        fp = fopen("comserver_temp", "w+");
        if (fp == NULL) {
            perror("Opening temporary file");
            break;
        }
        pid_t pid = fork();
        if (pid == 0) { // Child process executes the command
            dup2(fileno(fp), STDOUT_FILENO); // Redirect stdout to temporary file
            execlp("sh", "sh", "-c", cmdBuffer, (char *)NULL);
            exit(EXIT_FAILURE); // Exec should not return, exit if it does
        }
        else if(pid > 0){

        }
        wait(NULL); // Wait for the command execution to finish
        fseek(fp, 0, SEEK_SET); // Go to the beginning of the file

        // Read the command output from the file and send it to the client
        while ((bytesRead = fread(responseBuffer, 1, sizeof(responseBuffer), fp)) > 0) {
            int data_len = strlen(responseBuffer) + 1; // Include the null terminator
            int message_len = 7 + data_len;   // Length + Type + Padding

            // Prepare the message
            char message[BUFFER_SIZE];
            sprintf(message, "%4d%1d%3s%s", message_len, COMMAND_RESULT, "", responseBuffer);
            printf("The message from the server: %s \n ", message);
            fflush(stdout);
            write(scPipe, message, bytesRead + 6 + 1);
        }
        fclose(fp);
    }

    close(csPipe);
    close(scPipe);
}


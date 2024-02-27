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

void handle_client(char *csPipeName, char *scPipeName, int wSize);

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
    printf("Server - cssc_pipe_name: %s\n", csPipeName);
    fflush(stdout);

    //printf("Server - sc_pipe: %s\n", scPipeName);
    int csPipe = open(csPipeName, O_RDWR);
    printf(" %s\n", "bef open");
    fflush(stdout);

    int scPipe = open(scPipeName, O_RDWR);
    printf("%s\n", "after open");
    fflush(stdout);

    printf("Server - cssc_pipe_name: %s\n", csPipeName);
    printf("Server - sc_pipe: %s\n", scPipeName);
        fflush(stdout);


            
    char cmdBuffer[MAX_MSG_SIZE];
    char responseBuffer[MAX_MSG_SIZE];

    FILE *fp;
    const char *tempFileName = "comserver_temp";

    if (csPipe == -1 || scPipe == -1) {
        perror("Opening pipes");
        return;
    }

    // Send connection established message
    strcpy(responseBuffer, "Connection established");

	
    write(scPipe, responseBuffer, strlen(responseBuffer) + 1);
    printf("%d \n", *scPipeName);
    fflush(stdout);

    while (1) {
        printf("%s \n", "entered handle client");
        fflush(stdout);
        printf("%s\n", cmdBuffer);
        fflush(stdout);
        int bytesRead = read(csPipe, cmdBuffer, MAX_MSG_SIZE - 1);
        if (bytesRead <= 0) {
            break; // Break the loop if read fails or when "quit" command is received
        }
        
        cmdBuffer[bytesRead] = '\0';
        printf("%s\n", cmdBuffer);
        fflush(stdout);
        if (strcmp(cmdBuffer, "quit") == 0) {
            strcpy(responseBuffer, "quit-ack");
            write(scPipe, responseBuffer, strlen(responseBuffer) + 1);
            break;
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
        wait(NULL); // Wait for the command execution to finish
        fseek(fp, 0, SEEK_SET); // Go to the beginning of the file

        // Read the command output from the file and send it to the client
        while ((bytesRead = fread(responseBuffer, 1, sizeof(responseBuffer), fp)) > 0) {
            write(scPipe, responseBuffer, bytesRead);
        }
        fclose(fp);
    }

    close(csPipe);
    close(scPipe);
}


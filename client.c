#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <mqueue.h>
#include <signal.h>

#define BUFFER_SIZE 1024
#define MAXARGS 10

// Message types
#define CONNECTION_REQUEST 1
#define CONNECTION_REPLY 2
#define COMMAND_LINE 3
#define COMMAND_RESULT 4
#define QUIT_REQUEST 5
#define QUIT_REPLY 6
#define QUIT_ALL_REQUEST 7

// Function to create named pipes
void create_named_pipes(const char* cs_pipe_name, const char* sc_pipe_name) {
    mkfifo(cs_pipe_name, 0666);
    mkfifo(sc_pipe_name, 0666);
}

// Function to connect to the server
void connect_to_server(const char* mq_name, const char* cs_pipe_name, const char* sc_pipe_name, int wsize) {
    // Send connection request to the server
    mqd_t mqd = mq_open(mq_name, O_WRONLY);
    if (mqd == -1) {
        perror("Error opening server message queue for connection request");
        exit(EXIT_FAILURE);
    }

    char connection_info[BUFFER_SIZE];
    sprintf(connection_info, "%s %s %d", cs_pipe_name, sc_pipe_name, wsize);

    // Increase buffer size to avoid overflow warning
    char connection_request[BUFFER_SIZE + 5 + 1000]; // Length + Type + Padding + Extra space
    int connection_info_len = strlen(connection_info) + 1; // Include the null terminator
    sprintf(connection_request, "%4d%1d%3s%s", connection_info_len, CONNECTION_REQUEST, "", connection_info);

    if (mq_send(mqd, connection_request, connection_info_len + 5, 0) == -1) {
        perror("Error sending connection request to server");
        mq_close(mqd);
        exit(EXIT_FAILURE);
    }

    //mq_close(mqd);
}

// Function to wait for connection confirmation from the server
void wait_for_connection_confirmation(const char* sc_pipe_name) {
    int sc_pipe = open(sc_pipe_name, O_RDONLY);
    char confirmation[BUFFER_SIZE];
    read(sc_pipe, confirmation, BUFFER_SIZE);

    if (strcmp(confirmation, "Connection established") != 0) {
        fprintf(stderr, "Error: Connection not established by the server\n");
        close(sc_pipe);
        exit(EXIT_FAILURE);
    }

    printf("Connection established with the server\n");
    //close(sc_pipe);
}

// Function to send a message to the server
void send_message(const char* cs_pipe_name, int type, const char* data) {
    int cs_pipe = open(cs_pipe_name, O_WRONLY);

    // Calculate the length of the message
    int data_len = strlen(data) + 1; // Include the null terminator
    int message_len = 5 + data_len;   // Length + Type + Padding

    // Prepare the message
    char message[BUFFER_SIZE];
    sprintf(message, "%4d%1d%3s%s", message_len, type, "", data);

    write(cs_pipe, message, message_len);

    //close(cs_pipe);
}

// Function to receive a message from the server
void receive_message(const char* sc_pipe_name, char* data) {
    int sc_pipe = open(sc_pipe_name, O_RDONLY);
    char length_buffer[4];
    read(sc_pipe, length_buffer, 4);
    int message_len = *(int *)length_buffer;

    // Extract data from the message
    char message[BUFFER_SIZE];
    read(sc_pipe, message, message_len - 4);

    // Extract type and data from the message
    int type;
    sscanf(message, "%1d%*3s", &type);
    snprintf(data, message_len - 5, "%s", message + 5);

    //close(sc_pipe);
}

// Function to send quit request to the server
void send_quit_request(const char* cs_pipe_name) {
    send_message(cs_pipe_name, QUIT_ALL_REQUEST, "");
}

// Signal handler for handling termination signals
void handle_termination(int signum) {
    // Perform cleanup tasks if needed
    exit(EXIT_SUCCESS);
}

int main(int argc, char *argv[]) {
    // Register signal handler for termination signals
    signal(SIGTERM, handle_termination);
    signal(SIGINT, handle_termination);

    if (argc < 2) {
        fprintf(stderr, "Usage: %s MQNAME [-b COMFILE] [-s WSIZE]\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    char cs_pipe_name[BUFFER_SIZE];
    char sc_pipe_name[BUFFER_SIZE];

    // Default values
    char* mq_name = argv[1];
    char* comfile = NULL;
    int wsize = BUFFER_SIZE;

    // Parse command line arguments
    int opt;
    while ((opt = getopt(argc, argv, "b:s:")) != -1) {
        switch (opt) {
            case 'b':
                comfile = optarg;
                break;
            case 's':
                wsize = atoi(optarg);
                break;
            default:
                fprintf(stderr, "Usage: %s MQNAME [-b COMFILE] [-s WSIZE]\n", argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    // Create named pipes
    create_named_pipes(cs_pipe_name, sc_pipe_name);

    // Connect to the server
    connect_to_server(mq_name, cs_pipe_name, sc_pipe_name, wsize);

    // Wait for connection confirmation from the server
    wait_for_connection_confirmation(sc_pipe_name);

    if (comfile != NULL) {
        // Batch mode: read commands from file
        FILE* file = fopen(comfile, "r");
        if (file == NULL) {
            perror("Error opening command file");
            exit(EXIT_FAILURE);
        }

        // Handle batch mode commands
        char command[BUFFER_SIZE];
        while (fgets(command, BUFFER_SIZE, file) != NULL) {
            // Remove newline character
            command[strcspn(command, "\n")] = '\0';

            // Send command to the server
            send_message(cs_pipe_name, COMMAND_LINE, command);

            // Receive and print the result from the server
            char result[BUFFER_SIZE];
            receive_message(sc_pipe_name, result);
            printf("Result from server: %s\n", result);
        }

        fclose(file);
    } else {
        // Interactive mode: read commands from user
        char command[BUFFER_SIZE];
        printf("Enter commands (type 'quit' or 'quitall' to end):\n");

        while (1) {
            printf("> ");
            fgets(command, BUFFER_SIZE, stdin);

            // Remove newline character
            command[strcspn(command, "\n")] = '\0';

            // Check if the user wants to quit or quitall
            if (strcmp(command, "quit") == 0 || strcmp(command, "quitall") == 0) {
                if (strcmp(command, "quitall") == 0) {
                    // Send quitall request to the server
                    send_quit_request(cs_pipe_name);
                }

                // Wait for quit acknowledgment from the server
                char result[BUFFER_SIZE];
                receive_message(sc_pipe_name, result);
                printf("Result from server: %s\n", result);

                break;
            }

            // Send command to the server
            send_message(cs_pipe_name, COMMAND_LINE, command);

            // Receive and print the result from the server
            char result[BUFFER_SIZE];
            receive_message(sc_pipe_name, result);
            printf("Result from server: %s\n", result);
        }
    }

    // Clean up: Remove the named pipes
    unlink(cs_pipe_name);
    unlink(sc_pipe_name);

    return 0;
}

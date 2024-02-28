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
#define CONNECTION_REQUEST 1
#define CONNECTION_REPLY 2
#define COMMAND_LINE 3
#define COMMAND_RESULT 4
#define QUIT_REQUEST 5
#define QUIT_REPLY 6
#define QUIT_ALL_REQUEST 7
char* getSubstringFromSecondSpace(char* input) {
    int spaceCount = 0;
    const char* current = input;
    while (*current != '\0') {
        if (*current == ' ') {
            spaceCount++;
            if (spaceCount == 2) {
                break;
            }
        }
        current++;
    }
    size_t substringLength = 0;
    const char* substringStart = current + 1;
    while (*current != '\0') {
        substringLength++;
        current++;
    }
    char* substring = (char*)malloc(substringLength + 1);
    strncpy(substring, substringStart, substringLength);
    substring[substringLength] = '\0';
    size_t leadingSpaces = strspn(substring, " ");
    memmove(substring, substring + leadingSpaces, substringLength - leadingSpaces + 1);
    return substring;
}
/**
 * @brief Create a named pipes object
 * 
 * @param cs_pipe_name 
 * @param sc_pipe_name 
 * @param pid 
 * creste only one pipe and change the direction of read write.
 * both should be reading at all times.
 * when writing it will temporarily change to write and then change to read.
 */
void create_named_pipes(char* cs_pipe_name, char* sc_pipe_name, pid_t pid) {
    sprintf(cs_pipe_name, "cs_pipe_%d", pid);
    sprintf(sc_pipe_name, "sc_pipe_%d", pid);

    if (mkfifo(cs_pipe_name, 0666) == -1 || mkfifo(sc_pipe_name, 0666) == -1) {
        perror("Error creating named pipes");
        exit(EXIT_FAILURE);
    }
}
void connect_to_server(const char* mq_name, const char* cs_pipe_name, const char* sc_pipe_name, int wsize) {
printf("%s\n", cs_pipe_name);
    mqd_t mqd = mq_open(mq_name, O_RDWR);
    if (mqd == -1) {
        perror("Error opening server message queue for connection request");
        exit(EXIT_FAILURE);
    }
    char connection_info[BUFFER_SIZE];
    sprintf(connection_info, "%s %s %d", cs_pipe_name, sc_pipe_name, wsize);
    char connection_request[BUFFER_SIZE + 5 + 1000];
    int connection_info_len = strlen(connection_info) + 1;
    sprintf(connection_request, "%d %d %s %s", connection_info_len, CONNECTION_REQUEST, "", connection_info);
    if (mq_send(mqd, connection_request, connection_info_len + 5, 0) == -1) {
        perror("Error sending connection request to server");
        mq_close(mqd);
        exit(EXIT_FAILURE);
    }
	printf("%s\n", cs_pipe_name);
    mq_close(mqd);
}
void wait_for_connection_confirmation(const char* sc_pipe_name) {
    int sc_pipe = open(sc_pipe_name, O_RDWR);
    printf("%s\n", "checkkkk");
    char confirmation[BUFFER_SIZE];
    read(sc_pipe, confirmation, BUFFER_SIZE);
    printf("%s \n", confirmation);
    char* abc = getSubstringFromSecondSpace(confirmation);
    char* confirmation_done = getSubstringFromSecondSpace(abc);
    if (strcmp(confirmation_done, "Connection established") != 0) {
        fprintf(stderr, "Error: Connection not established by the server\n");
        close(sc_pipe);
        exit(EXIT_FAILURE);
    }
    printf("Connection established with the server\n");
    close(sc_pipe);
}
void send_message(const char* cs_pipe_name, int type, const char* data) {
    int cs_pipe = open(cs_pipe_name, O_RDWR);
    int data_len = strlen(data) + 1;
    int message_len = 7 + data_len;
    char message[BUFFER_SIZE];
    sprintf(message, "%4d%1d%3s%s", message_len, type, "", data);
    write(cs_pipe, message, message_len);
    close(cs_pipe);
}
void receive_message(const char* sc_pipe_name, char* data) {
    char *cmdBufferFull = (char *)malloc(256 * sizeof(char));
    int sc_pipe = open(sc_pipe_name, O_RDWR);
    int bytesRead = read(sc_pipe, cmdBufferFull, 256 - 1);
    if (bytesRead < 256 - 1) {
        cmdBufferFull[bytesRead] = '\0';
    } else {
        cmdBufferFull[256 - 1] = '\0';
    }
    char* cmdBuffer = getSubstringFromSecondSpace(cmdBufferFull);
    snprintf(data, 255, "%s", cmdBuffer);
    printf(" The full buffer %s\n", data);
    fflush(stdout);
    close(sc_pipe);
}
void send_quit_request(const char* cs_pipe_name) {
    send_message(cs_pipe_name, QUIT_ALL_REQUEST, "");
}
void handle_termination(int signum) {
    exit(EXIT_SUCCESS);
}

int main(int argc, char *argv[]) {
    signal(SIGTERM, handle_termination);
    signal(SIGINT, handle_termination);
    if (argc < 2) {
        fprintf(stderr, "Usage: %s MQNAME [-b COMFILE] [-s WSIZE]\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    char* mq_name = argv[1];
    char* comfile = NULL;
    int wsize = BUFFER_SIZE;
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
    char cs_pipe_name[BUFFER_SIZE];
    char sc_pipe_name[BUFFER_SIZE];
    create_named_pipes(cs_pipe_name, sc_pipe_name, getpid());
printf("Client - cs_pipe_name: %s\n", cs_pipe_name);
        fflush(stdout);
    connect_to_server(mq_name, cs_pipe_name, sc_pipe_name, wsize);
    wait_for_connection_confirmation(sc_pipe_name);
    if (comfile != NULL) {
        FILE* file = fopen(comfile, "r");
        if (file == NULL) {
            perror("Error opening command file");
            exit(EXIT_FAILURE);
        }
        char command[BUFFER_SIZE];
        while (fgets(command, BUFFER_SIZE, file) != NULL) {
            command[strcspn(command, "\n")] = '\0';
            send_message(cs_pipe_name, COMMAND_LINE, command);
            char result[BUFFER_SIZE];
            receive_message(sc_pipe_name, result);
            printf("Result from server: %s\n", result);
        }

        fclose(file);
    } else {
        char command[BUFFER_SIZE];
        printf("Enter commands (type 'quit' or 'quitall' to end):\n");

        while (1) {
            printf("> ");
            fgets(command, BUFFER_SIZE, stdin);
            command[strcspn(command, "\n")] = '\0';
            if (strcmp(command, "quit") == 0 || strcmp(command, "quitall") == 0) {
                if (strcmp(command, "quitall") == 0) {
                    send_quit_request(cs_pipe_name);
                }
                send_message(cs_pipe_name, QUIT_REQUEST, command);
                char result[BUFFER_SIZE];
                receive_message(sc_pipe_name, result);
                printf("Result from server: %s\n", result);

                break;
            }
            send_message(cs_pipe_name, COMMAND_LINE, command);
            char result[BUFFER_SIZE];
            receive_message(sc_pipe_name, result);
            printf("Result from server: %s\n", result);
        }
    }
    unlink(cs_pipe_name);
    unlink(sc_pipe_name);
    return 0;
}

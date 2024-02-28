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
#define CONNECTION_REQ 1
#define CONNECTION_REP 2
#define SEND_COMMAND 3
#define COMMAND_RES 4
#define QUIT_REQ 5
#define QUIT_REP 6
#define QUIT_ALL_REQ 7
/**
 * @brief Get the Substring From Second Space object
 * 
 * @param input 
 * @return char* 
 */
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
    char* result = (char*)malloc(substringLength + 1);
    strncpy(result, substringStart, substringLength);
    size_t leadingSpaces = strspn(result, " ");
    memmove(result, result + leadingSpaces, substringLength - leadingSpaces + 1);
    return result;
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
void create_pipes(char* cs_pipe_name, char* sc_pipe_name, pid_t pid) {
    sprintf(cs_pipe_name, "cs_pipe_%d", pid);
    sprintf(sc_pipe_name, "sc_pipe_%d", pid);

    if (mkfifo(cs_pipe_name, 0666) == -1 || mkfifo(sc_pipe_name, 0666) == -1) {
        perror("Error when creating named pipes");
        exit(EXIT_FAILURE);
    }
}
/**
 * @brief 
 * 
 * @param mq_name 
 * @param cs_pipe_name 
 * @param sc_pipe_name 
 * @param wsize 
 */
void connect_server(const char* mq_name, const char* cs_pipe_name, const char* sc_pipe_name, int wsize) {
// printf("%s\n", cs_pipe_name);
    mqd_t mqd = mq_open(mq_name, O_RDWR);
    if (mqd == -1) {
        perror("Error opening server message queue for connection request");
        exit(EXIT_FAILURE);
    }
    char connection_info[BUFFER_SIZE];
    sprintf(connection_info, "%s %s %d", cs_pipe_name, sc_pipe_name, wsize);
    char connection_request[BUFFER_SIZE + 5 + 1000];
    int connection_info_len = strlen(connection_info) + 1;
    sprintf(connection_request, "%d %d %s %s", connection_info_len, CONNECTION_REQ, "", connection_info);
    if (mq_send(mqd, connection_request, connection_info_len + 5, 0) == -1) {
        perror("Error when sending connection request to server");
        mq_close(mqd);
        exit(EXIT_FAILURE);
    }
	//printf("%s\n", cs_pipe_name);
    mq_close(mqd);
}
/**
 * @brief 
 * 
 * @param sc_pipe_name 
 */
void wait_con_confirmation(const char* sc_pipe_name) {
    int sc_pipe = open(sc_pipe_name, O_RDWR);
    // printf("%s\n", "checkkkk");
    char confirmation[BUFFER_SIZE];
    read(sc_pipe, confirmation, BUFFER_SIZE);
    // printf("%s \n", confirmation);
    char* abc = getSubstringFromSecondSpace(confirmation);
    char* confirmation_done = getSubstringFromSecondSpace(abc);
    if (strcmp(confirmation_done, "Connection established") != 0) {
        fprintf(stderr, "Error: Connection is not established by the server\n");
        close(sc_pipe);
        exit(EXIT_FAILURE);
    }
    printf("Connection is stablished with the server\n");
    close(sc_pipe);
}
/**
 * @brief 
 * 
 * @param cs_pipe_name 
 * @param type 
 * @param data 
 */
void send_message(const char* cs_pipe_name, int type, const char* data) {
    int cs_pipe = open(cs_pipe_name, O_RDWR);
    int data_len = strlen(data) + 1;
    int message_len = 7 + data_len;
    char message[BUFFER_SIZE];
    sprintf(message, "%4d%1d%3s%s", message_len, type, "", data);
    write(cs_pipe, message, message_len);
    close(cs_pipe);
}
/**
 * @brief 
 * 
 * @param sc_pipe_name 
 * @param data 
 */
void receive_message_from_server(const char* sc_pipe_name, char* data) {
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
    // printf(" The full buffer %s\n", data);
    // fflush(stdout);
    close(sc_pipe);
}
/**
 * @brief 
 * 
 * @param cs_pipe_name 
 */
void send_quit_request(const char* cs_pipe_name) {
    send_message(cs_pipe_name, QUIT_ALL_REQ, "");
}
/**
 * @brief 
 * 
 * @param signum 
 */
void handle_termination_request(int signum) {
    exit(EXIT_SUCCESS);
}
/**
 * @brief 
 * 
 * @param argc 
 * @param argv 
 * @return int 
 */
int main(int argc, char *argv[]) {
    signal(SIGTERM, handle_termination_request);
    signal(SIGINT, handle_termination_request);
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
    create_pipes(cs_pipe_name, sc_pipe_name, getpid());
// printf("Client - cs_pipe_name: %s\n", cs_pipe_name);
//         fflush(stdout);
    connect_server(mq_name, cs_pipe_name, sc_pipe_name, wsize);
    wait_con_confirmation(sc_pipe_name);
    if (comfile != NULL) {
        FILE* file = fopen(comfile, "r");
        if (file == NULL) {
            perror("Error opening command file");
            exit(EXIT_FAILURE);
        }
        char command[BUFFER_SIZE];
        while (fgets(command, BUFFER_SIZE, file) != NULL) {
            command[strcspn(command, "\n")] = '\0';
            send_message(cs_pipe_name, SEND_COMMAND, command);
            char result[BUFFER_SIZE];
            receive_message_from_server(sc_pipe_name, result);
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
                send_message(cs_pipe_name, QUIT_REQ, command);
                char result[BUFFER_SIZE];
                receive_message_from_server(sc_pipe_name, result);
                printf("Result from server: %s\n", result);

                break;
            }
            send_message(cs_pipe_name, SEND_COMMAND, command);
            char result[BUFFER_SIZE];
            receive_message_from_server(sc_pipe_name, result);
            printf("Result from server: %s\n", result);
        }
    }
    unlink(cs_pipe_name);
    unlink(sc_pipe_name);
    return 0;
}

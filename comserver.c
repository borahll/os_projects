#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <mqueue.h>
#include <string.h>
#include <ctype.h>
#define MAX_MSG_SIZE 256
#define QUEUE_PERMISSIONS 0660
#define BUFFER_SIZE 1024
#define CONNECTION_REQ 1
#define CONNECTION_REP 2
#define SEND_COMMAND 3
#define COMMAND_RES 4
#define QUIT_REQ 5
#define QUIT_REP 6
#define QUIT_ALL_REQ 7
/**
 * @brief 
 * 
 * @param input 
 */
char* extract_number(const char* input) {
    char* output = (char *)malloc(strlen(input) * sizeof(char));
    char* outputStart = output;

    while (*input) {
        if (isdigit(*input)) {
            *output = *input;
            ++output;
        }
        ++input;
    }
    *output = '\0';
    return outputStart;
}
/**
 * @brief 
 * 
 * @param csPipeName 
 * @param scPipeName 
 * @param wSize 
 */
void handle_client_request(char *csPipeName, char *scPipeName, int wSize);
/**
 * @brief 
 * 
 * @param value 
 */
void save_value_to_file(int value) {
    FILE *file = fopen(".client_num_storage.txt", "w");
    if (file != NULL) {
        fprintf(file, "%d", value);
        fclose(file);
    } else {
        perror("Could not open file");
    }
}
/**
 * @brief 
 * 
 * @return int 
 */
int read_value_from_file() {
    int value = 0;
    FILE *file = fopen(".client_num_storage.txt", "r");
    if (file != NULL) {
        fscanf(file, "%d", &value);
        fclose(file);
    } else {
        perror("Could not open file");
    }
    return value;
}
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
 * @brief 
 * 
 * @param argc 
 * @param argv 
 * @return int 
 */
int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage of the server: %s <MQNAME>\n", argv[0]);
                fflush(stdout);

        exit(EXIT_FAILURE);
    }
    char *mqName = argv[1];
    mqd_t mq;
    struct mq_attr attr = {
        .mq_flags = 0,     
        .mq_maxmsg = 10,    
        .mq_msgsize = MAX_MSG_SIZE, 
        .mq_curmsgs = 0    
    };
    mq = mq_open(mqName, O_RDWR | O_CREAT, QUEUE_PERMISSIONS, &attr);
    if (mq == (mqd_t)-1) {
        perror("mq_open");
        exit(EXIT_FAILURE);
    }
    printf("Server is running and waiting for connections on message queue '%s'\n", mqName);
    fflush(stdout);
    while (1) {
        char buffer[MAX_MSG_SIZE];
        memset(buffer, 0, MAX_MSG_SIZE);
        if (mq_receive(mq, buffer, MAX_MSG_SIZE, NULL) == -1) {
            perror("mq_receive error");
            continue;
        }
        char csPipeName[100], scPipeName[100];
        int wSize;
        int connection_info_len = 0;
        int connection_request = 0; //DELETE THIS. ONLY FOR DEVELOPMENT!!!!
        sscanf(buffer, "%d %d %s %s %d", &connection_info_len, &connection_request, csPipeName ,scPipeName, &wSize);
        pid_t pid = fork();
        if (pid == 0) {
            // printf("%s \n", "main hande client");
            //         fflush(stdout);

            handle_client_request(csPipeName, scPipeName, wSize);
            exit(EXIT_SUCCESS); 
        }
        else if (pid < 0) {
            perror("fork error");
                // printf("%s \n", "err");
                //         fflush(stdout);

            continue; 
        }
    }
        // printf("%s \n", "done");
        // fflush(stdout);
    mq_close(mq);
    mq_unlink(mqName);

    return 0;
}
/**
 * @brief 
 * 
 * @param csPipeName 
 * @param scPipeName 
 * @param wSize 
 */
void handle_client_request(char *csPipeName, char *scPipeName, int wSize) {
    int client_count = read_value_from_file();
    // printf("Server - cssc_pipe_name: %s\n", csPipeName);
    // fflush(stdout);
    int csPipe = open(csPipeName, O_RDWR);
    // printf(" %s\n", "bef open");
    // fflush(stdout);
    client_count = read_value_from_file();
    client_count = client_count + 1;
    save_value_to_file(client_count);
    int scPipe = open(scPipeName, O_RDWR);
    // printf("%s\n", "after open");
    // fflush(stdout);
    // printf("Server - cssc_pipe_name: %s\n", csPipeName);
    // printf("Server - sc_pipe: %s\n", scPipeName);
    printf("Server-client count: %d\n", client_count);
        fflush(stdout);
    char *cmdBufferFull = (char *)malloc(MAX_MSG_SIZE * sizeof(char));
    char responseBuffer[MAX_MSG_SIZE];
    FILE *fp;
    //const char *tempFileName = "comserver_temp";
    if (csPipe == -1 || scPipe == -1) {
        perror("Error when opening pipes");
        return;
    }
    //server main: CONREQUEST message received: pid=13153, cs=FIFO-CS-13153 sc=FIFO-SC-13153, wsize=1
    printf("server main: CONREQUEST message recieved pid = %s, cs= %s, sc= %s, wsize= %d \n", extract_number(scPipeName), csPipeName, scPipeName, wSize);
    fflush(stdout);
    strcpy(responseBuffer, "Connection established");
    int data_len = strlen(responseBuffer) + 1; 
    int message_len = 7 + data_len; 
    char message[BUFFER_SIZE];
    sprintf(message, "%4d%1d%3s%s", message_len, CONNECTION_REP, "", responseBuffer);
    write(scPipe, message, wSize);
    while (1) {
        int bytesRead = read(csPipe, cmdBufferFull, MAX_MSG_SIZE - 1);
        if (bytesRead <= 0) {
            break;
        }
        if (bytesRead < MAX_MSG_SIZE - 1) {
            cmdBufferFull[bytesRead] = '\0';
        } else {
            cmdBufferFull[MAX_MSG_SIZE - 1] = '\0';
        }
        int lenght, type;
        char data[wSize];
        sscanf(cmdBufferFull, "%d%d%s", &lenght, &type, data);
        /*
            #define CONNECTION_REQ 1
            #define CONNECTION_REP 2
            #define SEND_COMMAND 3
            #define COMMAND_RES 4
            #define QUIT_REQ 5
            #define QUIT_REP 6
            #define QUIT_ALL_REQ 7
        */
       //server child: COMLINE message received: len=27, type=3, data=cat atextfile.txt
        switch (type)
        {
        case CONNECTION_REQ:
            printf("server child: COMLINE message received:");
            fflush(stdout);
            break;
        case SEND_COMMAND:
            printf("server child: COMLINE message received:");
            fflush(stdout);
            break;
        case QUIT_REQ:
            break;
        case QUIT_ALL_REQ:
            break;
        default:
            break;
        }
        char* abc = getSubstringFromSecondSpace(cmdBufferFull);
        //char* acb = getSubstringFromSecondSpace(abc);
        char* cmdBuffer = getSubstringFromSecondSpace(abc);
        if (strcmp(cmdBuffer, "quit") == 0) {
            client_count = read_value_from_file();
            client_count = client_count - 1;
            save_value_to_file(client_count);
            strcpy(responseBuffer, "quit-ack");
            printf("Server-client count: %d\n", client_count);
            write(scPipe, responseBuffer, strlen(responseBuffer) + 1);
        }
        fp = fopen("comserver_temp", "w+");
        if (fp == NULL) {
            perror("Error when opening temporary file");
            break;
        }
        pid_t pid = fork();
        if (pid == 0) {
            dup2(fileno(fp), STDOUT_FILENO);
            if(strcmp(cmdBuffer, "quit") != 0){
                execlp("sh", "sh", "-c", cmdBuffer, (char *)NULL);
            }
            exit(EXIT_FAILURE); 
        }
        wait(NULL);
        fseek(fp, 0, SEEK_SET);
        while ((bytesRead = fread(responseBuffer, 1, sizeof(responseBuffer), fp)) > 0) {
            int data_len = strlen(responseBuffer) + 1;
            int message_len = 7 + data_len;
            char message[BUFFER_SIZE];
            sprintf(message, "%4d%1d%3s%s", message_len, COMMAND_RES, "", responseBuffer);
            // printf("The message from the server: %s \n ", message);
            // fflush(stdout);
            write(scPipe, message, bytesRead + 6 + 1);
        }
        fclose(fp);
    }
    close(csPipe);
    close(scPipe);
}


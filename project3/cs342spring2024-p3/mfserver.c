#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include "mf.h"

// Signal handler function
void sig_handler(int signo) {
    if (signo == SIGINT || signo == SIGTERM) {
        printf("Received termination signal. Cleaning up...\n");
        mf_destroy(); // Call mf_destroy to clean up resources
        exit(signo); // Exit with the received signal number
    }
}

int main(int argc, char *argv[]) {
    printf("mfserver pid=%d\n", (int)getpid());

    // Register the signal handler function
    if (signal(SIGINT, sig_handler) == SIG_ERR) {
        perror("Failed to register signal handler for SIGINT");
        exit(EXIT_FAILURE);
    }
    if (signal(SIGTERM, sig_handler) == SIG_ERR) {
        perror("Failed to register signal handler for SIGTERM");
        exit(EXIT_FAILURE);
    }

    mf_init(); // will read the config file

    while (1)
        sleep(1000);

    exit(0);
}

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>

int main(void) {
/* Intentionally leak a regular disk file */
    int file_fd = open("/dev/null", O_RDONLY);
    if (file_fd < 0) {
        perror("[test_leak] open failed");
        return EXIT_FAILURE;
    }

/* Intentionally leak one end of an IPC pipe while closing the other */
    int pipe_fds[2];
    if (pipe(pipe_fds) < 0) {
        perror("[test_leak] pipe failed");
        return EXIT_FAILURE;
    }
    /* Close read end, deliberately leave write end (pipe_fds[1]) open */
    close(pipe_fds[0]);

/* Intentionally leak a network socket */
    int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        perror("[test_leak] socket failed");
        return EXIT_FAILURE;
    }

    printf("[test_leak] Program exiting deliberately leaving fds open:\n");
    printf("            file_fd=%d, pipe_write_fd=%d, sock_fd=%d\n", 
           file_fd, pipe_fds[1], sock_fd);

    /* Exit without closing file_fd, pipe_fds[1], or sock_fd */
    return EXIT_SUCCESS;
}
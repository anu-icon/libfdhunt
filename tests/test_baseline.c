/* Test zero FP across standard files, pipes, net sockets & some specialized Linux descriptors*/
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/epoll.h>

int main(){
/*File open FD*/
    int file_fd = open("/dev/null", O_RDONLY);
    if (file_fd < 0){
        perror("[test_baseline] open failed.");
        return EXIT_FAILURE;
    }

    if (close(file_fd) < 0){
        perror("[test_baseline] close(file_fd) failed");
        return EXIT_FAILURE;
    }
/*IPC descriptors via pipe() */
    int pipe_fds[2];
    if (pipe(pipe_fds) < 0){
        perror("[test_baseline] pipe_fd failed");
        return EXIT_FAILURE;
    }
    
    if(close(pipe_fds[0]) <0 || close(pipe_fds[1]) < 0) {
        perror("[test_baseline] close(pipe_fd) failed");
        return EXIT_FAILURE;
    }

 /*Network descriptor via socket*/
    int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0){
        perror("[test_baseline] socket failed");
        return EXIT_FAILURE;
    }    

    if (close(sock_fd) < 0) {
        perror("[test_baseline] close(sock_fd) failed");
        return EXIT_FAILURE;
    }

/* Specialized kernel descriptor via epoll_create1()*/
    int epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) {
        perror("[test_baseline] epoll_create1 failed");
        return EXIT_FAILURE;
    }
    if (close(epoll_fd) < 0) {
        perror("[test_baseline] close(epoll_fd) failed");
        return EXIT_FAILURE;
    }

    printf("[test_baseline] All descriptors opened and closed successfully.\n");
    return EXIT_SUCCESS;
}
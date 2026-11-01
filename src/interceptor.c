/* This program intercepts all file descriptors operations -  files, network sockets, IPC's, event-loops, etc. 
*  to monitor the program under scrutiny for unclosed file descriptor leaks.
* 
* It intercepts standard C library functions and dynamically resolves the original function using dlsym
* Since dlsym can internally trigger the very function we are trying to intercept and resolve, 
* a thread-local flag is used here to prevent this self-recursion. The flag detects if the dlsym hooks
* has re-entered itself. If it has, it bypasses the C library entirely and routes directly to the 
* kernel using a raw syscall C library function instead. 

* All intercepted file descriptors are logged in the tracker table by calling tracker.c
* When close() is called, it's intercepted and fd passed to be removed from tracker table 
*/

#define _GNU_SOURCE // Required to use RTLD_NEXT with dlsym, and syscalls - accept4 and pipe2
#include <stdio.h>
#include <stdarg.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <sys/syscall.h>
#include <sys/socket.h>
#include <signal.h>
#include "tracker.h"

/* Preprocessor safety check to ensure compilation for older toolchains with outdated C libraries.
* If OS already has standard O_TMPFILE, skip.
* If not, check if OS has lower-level flags. If found, bitwise OR them to build O_TMPFILE.
* If even that's missing (very old) hardcode exact octal bitmask values to use this flag.
 */
#ifndef O_TMPFILE
#if defined(__O_TMPFILE) && defined(O_DIRECTORY)
#define O_TMPFILE (__O_TMPFILE | O_DIRECTORY)
#else
#define O_TMPFILE (020000000 | 00200000)
#endif
#endif

/* Function pointer signatures for the real
* C library functions that will be intercepted */
typedef int (*interceptor_open_t)(const char *, int, ...);
typedef int (*interceptor_openat_t)(int, const char *, int, ...);
typedef int (*interceptor_close_t)(int);
typedef int (*interceptor_dup_t)(int);
typedef int (*interceptor_dup2_t)(int, int);
typedef int (*interceptor_dup3_t)(int, int, int);
typedef int (*interceptor_socket_t)(int, int, int);
typedef int (*interceptor_accept_t)(int, struct sockaddr *, socklen_t *);
typedef int (*interceptor_accept4_t)(int, struct sockaddr *, socklen_t *, int);
typedef int (*interceptor_pipe_t)(int[2]);
typedef int (*interceptor_pipe2_t)(int[2], int);
typedef int (*interceptor_epoll_create_t)(int);
typedef int (*interceptor_epoll_create1_t)(int);
typedef int (*interceptor_eventfd_t)(unsigned int, int);
typedef int (*interceptor_timerfd_create_t)(int, int);
typedef int (*interceptor_signalfd_t)(int, const sigset_t *, int);
typedef int (*interceptor_inotify_init_t)(void);
typedef int (*interceptor_inotify_init1_t)(int);
typedef int (*interceptor_socketpair_t)(int, int, int, int[2]);
typedef int (*interceptor_memfd_create_t)(const char *, unsigned int);
typedef int (*interceptor_pidfd_open_t)(pid_t, unsigned int);
typedef int (*interceptor_io_uring_setup_t)(unsigned, void *); // altering signature  with void * to avoid importing kernel headers directly

/* Initialize function pointers to hold the memory addresses once intercepted*/
static interceptor_open_t interceptor_open = NULL;
static interceptor_openat_t interceptor_openat = NULL;
static interceptor_close_t interceptor_close = NULL;
static interceptor_socket_t interceptor_socket = NULL;
static interceptor_accept_t interceptor_accept = NULL;
static interceptor_accept4_t interceptor_accept4 = NULL;
static interceptor_pipe_t interceptor_pipe = NULL;
static interceptor_pipe2_t interceptor_pipe2 = NULL;
static interceptor_epoll_create_t interceptor_epoll_create = NULL;
static interceptor_epoll_create1_t interceptor_epoll_create1 = NULL;
static interceptor_dup_t interceptor_dup = NULL;
static interceptor_dup2_t interceptor_dup2 = NULL;
static interceptor_dup3_t interceptor_dup3 = NULL;
static interceptor_eventfd_t interceptor_eventfd = NULL;
static interceptor_timerfd_create_t interceptor_timerfd_create = NULL;
static interceptor_signalfd_t interceptor_signalfd = NULL;
static interceptor_inotify_init_t interceptor_inotify_init = NULL;
static interceptor_inotify_init1_t interceptor_inotify_init1 = NULL;
static interceptor_socketpair_t interceptor_socketpair = NULL;
static interceptor_memfd_create_t interceptor_memfd_create = NULL;
static interceptor_io_uring_setup_t interceptor_io_uring_setup = NULL;
static interceptor_pidfd_open_t interceptor_pidfd_open = NULL;

/* Thread-local safety flag to check for self-recursion */
static __thread int is_intercepted = 0;

/* Teardown function which will get called and print unclosed FD report once program under scrutiny exits */
static void  __attribute__((destructor)) libfdhunt_cleanup(void){
    report_fd_leaks();
}

/***************************************
Intercept file operations 
***************************************/

int open(const char *pathname, int flags, ...){
    mode_t mode = 0;
    /* Check how open() was invoked - with or without file permissions create mode 
    * Check if caller invoked to create a regular or temp file
    * Since O_TMPFILE is internally a composite bitmask by combining __O_TMPFILE | O_DIRECTORY 
    * we need to check the plain bit operation == O_TMPFILE to ensure all bits match */
    if ((flags & O_CREAT) || ((flags & O_TMPFILE) == O_TMPFILE)){  // open() called with variadic arguments
        va_list args;
        va_start(args, flags);
        mode = va_arg(args, mode_t);
        va_end(args);
    }
    if (is_intercepted)
        return (int)syscall(__NR_openat, AT_FDCWD, pathname, flags, mode);
    is_intercepted = 1;
    if(!interceptor_open)
        interceptor_open = (interceptor_open_t)dlsym(RTLD_NEXT, "open"); 
    int fd = interceptor_open(pathname, flags, mode);
    if (fd >= 0)
        add_fd_to_tracker(fd, pathname);
    is_intercepted = 0;
    return fd;
}

int openat(int dirfd, const char *pathname, int flags, ...){
    mode_t mode = 0;
    if ((flags & O_CREAT) || ((flags & O_TMPFILE) == O_TMPFILE)){
        va_list args;
        va_start(args, flags);
        mode = va_arg(args, mode_t);
        va_end(args);
    }
    if (is_intercepted)
        return (int)syscall(__NR_openat, dirfd, pathname, flags, mode);
    is_intercepted = 1;
    if(!interceptor_openat)
        interceptor_openat = (interceptor_openat_t)dlsym(RTLD_NEXT, "openat"); 
    int fd = interceptor_openat(dirfd, pathname, flags, mode);
    if (fd >= 0)
        add_fd_to_tracker(fd, pathname);
    is_intercepted = 0;
    return fd;
}

int close(int fd){
    if (is_intercepted)
        return (int)syscall(__NR_close, fd);
    is_intercepted = 1;
    if(!interceptor_close)
        interceptor_close = (interceptor_close_t)dlsym(RTLD_NEXT, "close");
    int rc = interceptor_close(fd);
    if (rc == 0)
        remove_fd_from_tracker(fd);
    is_intercepted = 0;
    return rc;
}

/***************************************
Intercept standard sockets and pipes 
***************************************/

int socket(int domain, int type, int protocol){
    if (is_intercepted)
        return (int)syscall(__NR_socket, domain, type, protocol);
    is_intercepted = 1;
    if (!interceptor_socket)
        interceptor_socket = (interceptor_socket_t)dlsym(RTLD_NEXT, "socket");
    int fd = interceptor_socket(domain, type, protocol);
    if (fd >= 0){
        char buffer[100];   // Temporary buffer array for record keeping
        // format the integer FD's into readable string and write text to buffer array 
        snprintf(buffer, sizeof(buffer), "socket(domain=%d, type=%d)", domain, type); 
        add_fd_to_tracker(fd, buffer);
    }
    is_intercepted = 0;
    return fd;
}

int accept(int sockfd, struct sockaddr *address, socklen_t *address_length){
    if (is_intercepted)
        return (int)syscall(__NR_accept, sockfd, address, address_length);
    is_intercepted = 1;
    if (!interceptor_accept)
        interceptor_accept = (interceptor_accept_t)dlsym(RTLD_NEXT, "accept");
    int fd = interceptor_accept(sockfd, address, address_length);
    if (fd >= 0 )
        add_fd_to_tracker(fd, "socket:client (accept)");
    is_intercepted = 0;
    return fd;
}

int accept4(int sockfd, struct sockaddr *address, socklen_t *address_length, int flags){
    if (is_intercepted){
#ifdef __NR_accept4
        return (int)syscall(__NR_accept4, sockfd, address, address_length, flags);
#else
        return -1;
#endif
    }
    is_intercepted = 1;
    if (!interceptor_accept4)
        interceptor_accept4 = (interceptor_accept4_t)dlsym(RTLD_NEXT, "accept4");
    int fd = interceptor_accept4(sockfd, address, address_length, flags);
    if (fd >= 0 )
        add_fd_to_tracker(fd, "socket:client (accept4)");
    is_intercepted = 0;
    return fd;
}

int pipe(int pipefd[2])
{
    if (is_intercepted)
        return (int)syscall(__NR_pipe2, pipefd, 0);
    is_intercepted = 1;
    if(!interceptor_pipe)
        interceptor_pipe = (interceptor_pipe_t)dlsym(RTLD_NEXT, "pipe");
    int rc = interceptor_pipe(pipefd);
    if (rc == 0){
        add_fd_to_tracker(pipefd[0], "pipe:read FD");
        add_fd_to_tracker(pipefd[1], "pipe:write FD");
    }
    is_intercepted = 0;
    return rc;
}

int pipe2(int pipefd[2], int flags)
{
    if (is_intercepted)
        return (int)syscall(__NR_pipe2, pipefd, flags);
    is_intercepted = 1;
    if(!interceptor_pipe2)
        interceptor_pipe2 = (interceptor_pipe2_t)dlsym(RTLD_NEXT, "pipe2");
    int rc = interceptor_pipe2(pipefd, flags);
    if (rc == 0){
        add_fd_to_tracker(pipefd[0], "pipe2:read FD");
        add_fd_to_tracker(pipefd[1], "pipe2:write FD");
    }
    is_intercepted = 0;
    return rc;
}

/***************************************
Intercept duplication
***************************************/

int dup(int old_fd){
    if(is_intercepted)
        return (int)syscall(__NR_dup, old_fd);
    is_intercepted = 1;
    if(!interceptor_dup)
        interceptor_dup = (interceptor_dup_t)dlsym(RTLD_NEXT, "dup");
    int fd = interceptor_dup(old_fd);
    if (fd >= 0)
        add_fd_to_tracker(fd, "dup");
    is_intercepted = 0;
    return fd;
}

int dup2(int old_fd, int new_fd){
    if(is_intercepted)
#ifdef __NR_dup2
        return (int)syscall(__NR_dup2, old_fd, new_fd);
#else
        return (int)syscall(__NR_dup3, old_fd, new_fd, 0);
#endif
    is_intercepted = 1;
    if(!interceptor_dup2)
        interceptor_dup2 = (interceptor_dup2_t)dlsym(RTLD_NEXT, "dup2");
    int rc = interceptor_dup2(old_fd, new_fd);
    if (rc >= 0)
        add_fd_to_tracker(rc, "dup2");
    is_intercepted = 0;
    return rc;
}

int dup3(int old_fd, int new_fd, int flags){
    if(is_intercepted)
        return (int)syscall(__NR_dup3, old_fd, new_fd, flags);
    is_intercepted = 1;
    if(!interceptor_dup3)
        interceptor_dup3 = (interceptor_dup3_t)dlsym(RTLD_NEXT, "dup3");
    int rc = interceptor_dup3(old_fd, new_fd, flags);
    if (rc >= 0)
        add_fd_to_tracker(rc, "dup3");
    is_intercepted = 0;
    return rc;
}

/********************************************************************************
Event operations, OS specific, & Other Specialized Linux descriptors
********************************************************************************/

// for legacy considerations we intercept epoll_create
int epoll_create(int size){
    // for older architectures that define __NR_epoll_create
    if(is_intercepted){
#ifdef __NR_epoll_create
        return (int)syscall(__NR_epoll_create, size);
#else
        return (int)syscall(__NR_epoll_create1, 0);
#endif
    }
    is_intercepted = 1;
    if(!interceptor_epoll_create)
        interceptor_epoll_create = (interceptor_epoll_create_t)dlsym(RTLD_NEXT, "epoll_create");
    int fd = interceptor_epoll_create(size);
    if (fd >= 0 )
        add_fd_to_tracker(fd, "epoll_create");
    is_intercepted = 0;
    return fd;
}

int epoll_create1(int flags){
    if(is_intercepted)
        return (int)syscall(__NR_epoll_create1, flags);
    is_intercepted = 1;
    if(!interceptor_epoll_create1)
        interceptor_epoll_create1 = (interceptor_epoll_create1_t)dlsym(RTLD_NEXT, "epoll_create1");
    int fd = interceptor_epoll_create1(flags);
    if (fd >= 0 )
        add_fd_to_tracker(fd, "epoll_create1");
    is_intercepted = 0;
    return fd;
}

int eventfd(unsigned int initval, int flags)
{
    if (is_intercepted) 
        return (int)syscall(__NR_eventfd2, initval, flags);
    is_intercepted = 1;
    if (!interceptor_eventfd) 
        interceptor_eventfd = (interceptor_eventfd_t)dlsym(RTLD_NEXT, "eventfd");
    int fd = interceptor_eventfd(initval, flags);
    if (fd >= 0) 
        add_fd_to_tracker(fd, "eventfd");
    is_intercepted = 0;
    return fd;
}

int timerfd_create(int clockid, int flags){
    if (is_intercepted)
        return (int)syscall(__NR_timerfd_create, clockid, flags);
    is_intercepted = 1;
    if (!interceptor_timerfd_create)
        interceptor_timerfd_create = (interceptor_timerfd_create_t)dlsym(RTLD_NEXT, "timerfd_create");
    int fd = interceptor_timerfd_create(clockid,flags);
    if (fd >= 0)
        add_fd_to_tracker(fd, "timerfd_create");
    is_intercepted = 0;
    return fd;
}

int signalfd(int fd, const sigset_t *mask, int flags){
    if (is_intercepted)
        return (int)syscall(__NR_signalfd4, fd, mask, _NSIG / 8, flags);
/* 1. signalfd4 instead of signalfd - signalfd4 accepts flags bitmask which original
    signalfd syscall doesn't support. And glibc wrapper function runs signalfd4 internally
   2. _NSIG / 8  - To convert the total signal bits to bytes to match exact size kernel needs */
   is_intercepted = 1;
    if(!interceptor_signalfd)
        interceptor_signalfd = (interceptor_signalfd_t)dlsym(RTLD_NEXT, "signalfd");
    int new_fd = interceptor_signalfd(fd, mask, flags);
    if ( new_fd >= 0)
        add_fd_to_tracker(new_fd, "signalfd");
    is_intercepted = 0;
    return new_fd;
}

int inotify_init(void){
    if(is_intercepted){
#ifdef __NR_inotify_init
        return (int)syscall(__NR_inotify_init);
#else
        return (int)syscall(__NR_inotify_init1, 0);
#endif
    }
    is_intercepted = 1;
    if(!interceptor_inotify_init)
        interceptor_inotify_init = (interceptor_inotify_init_t)dlsym(RTLD_NEXT, "inotify_init");
    int fd = interceptor_inotify_init();
    if (fd >= 0)
         add_fd_to_tracker(fd, "inotify_init");
    is_intercepted = 0;
    return fd;
}

int inotify_init1(int flags){
    if(is_intercepted)
        return (int)syscall(__NR_inotify_init1, flags);
    is_intercepted = 1;
    if(!interceptor_inotify_init1)
        interceptor_inotify_init1 = (interceptor_inotify_init1_t)dlsym(RTLD_NEXT, "inotify_init1");
    int fd = interceptor_inotify_init1(flags);
    if (fd >= 0)
         add_fd_to_tracker(fd, "inotify_init1");
    is_intercepted = 0;
    return fd;
}

int socketpair(int domain, int type, int protocol, int sv[2]){
    if(is_intercepted)
        return (int)syscall(__NR_socketpair, domain, type, protocol, sv);
    is_intercepted = 1;
    if(!interceptor_socketpair) 
        interceptor_socketpair = (interceptor_socketpair_t)dlsym(RTLD_NEXT, "socketpair");
    int rc = interceptor_socketpair(domain, type, protocol, sv);
    if (rc == 0){
        add_fd_to_tracker(sv[0], "socketpair:[0]-end");
        add_fd_to_tracker(sv[1], "socketpair:[1]-end");
    }
    is_intercepted = 0;
    return rc;
}

int memfd_create(const char *name, unsigned int flags){
    if(is_intercepted){
#ifdef __NR_memfd_create
        return (int)syscall(__NR_memfd_create, name, flags);
#else
        return -1;
#endif
    }
    is_intercepted = 1;
    if(!interceptor_memfd_create)
        interceptor_memfd_create = (interceptor_memfd_create_t)dlsym(RTLD_NEXT, "memfd_create");
    int fd;
    if(interceptor_memfd_create){
        fd = interceptor_memfd_create(name, flags);
    } else {
#ifdef __NR_memfd_create       
        fd = (int)syscall(__NR_memfd_create, name, flags);
#else
        fd = -1;
#endif
    }
    if(fd >= 0)
        add_fd_to_tracker(fd, "memfd_create");
    is_intercepted = 0;
    return fd;
}

int io_uring_setup(unsigned entries, void *params){
    if(is_intercepted){
// For compile compatibility on old systems, since io_uring_setup is relatively new
#ifdef __NR_io_uring_setup
        return (int)syscall(__NR_io_uring_setup, entries, params);
#else
        return -1;  // If kernel header is too old, syscall isn't available anyways
#endif
    }
    is_intercepted = 1;
    if(!interceptor_io_uring_setup)
        interceptor_io_uring_setup = (interceptor_io_uring_setup_t)dlsym(RTLD_NEXT, "io_uring_setup");
// For older glibc compatibility to avoid segfault on NULL pointer
    int fd;
    if(interceptor_io_uring_setup){
        fd = interceptor_io_uring_setup (entries, params);
    } else {
#ifdef __NR_io_uring_setup
        fd = (int)syscall(__NR_io_uring_setup, entries, params);
#else
        fd = -1;
#endif
    }
    if(fd >= 0)
        add_fd_to_tracker(fd, "io_uring_setup");
    is_intercepted = 0;
    return fd;
}

int pidfd_open(pid_t pid, unsigned int flags){
    if(is_intercepted){
// For compile compatibility on old systems, since pidfd_open is relatively new
#ifdef __NR_pidfd_open
        return (int)syscall(__NR_pidfd_open, pid, flags);
#else
        return -1; // If kernel header is too old, syscall isn't available anyways
#endif
    }
    is_intercepted = 1;
    if(!interceptor_pidfd_open)
        interceptor_pidfd_open = (interceptor_pidfd_open_t)dlsym(RTLD_NEXT, "pidfd_open");
// For very old glibc compatibility    
    int fd;
    if(interceptor_pidfd_open){
        fd = interceptor_pidfd_open(pid, flags);
    } else {
#ifdef __NR_pidfd_open
        fd =  (int)syscall(__NR_pidfd_open, pid, flags);
#else
        fd = -1;
#endif
    }
    if(fd >= 0)
        add_fd_to_tracker(fd, "pidfd_open");
    is_intercepted = 0;
    return fd;
}
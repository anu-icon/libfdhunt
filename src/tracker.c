/**************************************************
 * This program tracks active file descriptors 
 * obtained from intercepting syscalls of the 
 * program under scrutiny
 * 
 * When close() is invoked, the corresponding 
 * indexed FD entry is marked inactive
 * 
 * At program exit, table is scanned for active flags 
 * indicating leaked/unclosed FD's
 * It then reports those leaked FD's & their call sources 
 * 
 *****************************************************/
#include "tracker.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

#define DEFAULT_FD_MAX_FALLBACK 65536

typedef struct {
    int is_fd_active;
    char pathname[256];
} fd_entry_t;   // represents a single fd table component

static fd_entry_t *fd_table = NULL; 
static long max_fd_limit = 0;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

// This function configures & allocates the fd_table we're using for tracking
static void initialize_tracker_fd_table(void) {
    if(fd_table != NULL)
        return;
    
    // get the system's max fd limit and set accordingly 
    max_fd_limit =sysconf(_SC_OPEN_MAX);
    if(max_fd_limit <=0)
        max_fd_limit = DEFAULT_FD_MAX_FALLBACK;
    
    fd_table = (fd_entry_t *)calloc((size_t)max_fd_limit, sizeof(fd_entry_t));
    if(!fd_table){
        perror("libfdhunt [tracker]: Failed to allocate memory!");
        abort();
    }
}

// This function adds the FD & obtained path to our tracker table
// at same indexed postion as the FD 

void add_fd_to_tracker(int fd, const char *pathname){
    if(fd < 0)
        return;
    
    pthread_mutex_lock(&lock);
    if(!fd_table)
        initialize_tracker_fd_table();
    
    if(fd < max_fd_limit){
        fd_table[fd].is_fd_active = 1;
        if(pathname){
            strncpy(fd_table[fd].pathname, pathname, sizeof(fd_table[fd].pathname) - 1);
            fd_table[fd].pathname[sizeof(fd_table[fd].pathname) -1 ] = '\0';
        } 
        else {
            fd_table[fd].pathname[0] = '\0';
        }
    }
    pthread_mutex_unlock(&lock);
}

// This functions removes FD from tracker table when close() is invoked
void remove_fd_from_tracker(int fd){
    if(fd < 0)
        return;
    
    pthread_mutex_lock(&lock);

    if(fd_table && fd < max_fd_limit){
        fd_table[fd].is_fd_active = 0;
        fd_table[fd].pathname[0] = '\0';
    }

    pthread_mutex_unlock(&lock);
}

// This function dumps the unclosed FD's this tool found
void report_fd_leaks(void){
    pthread_mutex_lock(&lock);
    if(!fd_table){
        pthread_mutex_unlock(&lock);
        return;
    }

    int leak_counter = 0;

    for(long fd = 0; fd < max_fd_limit; fd++){  
        if(fd_table[fd].is_fd_active){
            leak_counter++;
            fprintf(stderr, "Leaked file descriptor: fd=%ld from source :\"%s\"\n",
                fd, fd_table[fd].pathname[0] ? fd_table[fd].pathname: "<unknown>");
        }
    }
    if(leak_counter == 0)
        fprintf(stderr, "No leaked file descriptors detected.\n");
    else
        fprintf(stderr, "Total leaked file descriptors found: %d\n", leak_counter);
    
    free(fd_table);
    fd_table = NULL;
    pthread_mutex_unlock(&lock);
}
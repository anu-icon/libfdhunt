/* Verify thread safety. Launch multiple worker threads simultaneously
opening & closing descriptors in loop to ensure tracker.c internal mutex
prevents race conditions */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>

#define THREAD_COUNT 8
#define ITERATIONS_PER_THREAD 100

static void *thread_worker(void *arg) {
    (void)arg;

    for (int i = 0; i < ITERATIONS_PER_THREAD; i++) {
        int fd = open("/dev/null", O_RDONLY);
        if (fd < 0) {
            perror("[test_mutex] open failed");
            pthread_exit((void *)(intptr_t)1);
        }

        if (close(fd) < 0) {
            perror("[test_mutex] close failed");
            pthread_exit((void *)(intptr_t)1);
        }
    }

    return NULL;
}

int main(void) {
    pthread_t threads[THREAD_COUNT];

    /* Launch worker threads */
    for (int i = 0; i < THREAD_COUNT; i++) {
        if (pthread_create(&threads[i], NULL, thread_worker, NULL) != 0) {
            perror("[test_mutex] pthread_create failed");
            return EXIT_FAILURE;
        }
    }

    /* Wait for all threads to finish */
    for (int i = 0; i < THREAD_COUNT; i++) {
        void *thread_ret = NULL;
        if (pthread_join(threads[i], &thread_ret) != 0 || thread_ret != NULL) {
            fprintf(stderr, "[test_mutex] Thread %d failed\n", i);
            return EXIT_FAILURE;
        }
    }

    printf("[test_mutex] All %d threads completed %d iterations cleanly.\n",
           THREAD_COUNT, ITERATIONS_PER_THREAD);
    return EXIT_SUCCESS;
}
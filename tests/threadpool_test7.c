/*
 * Fork/Join Framework 
 *
 * Test that pools can be fired up and shut down reliably.
 *
 * Written by G. Back for CS3214 Spring 2024.
 */
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <time.h>

#include "threadpool.h"
#include "threadpool_lib.h"
#define DEFAULT_THREADS 2
#define DEFAULT_REPETITIONS 40

static int
run_test(int nthreads, int nrepetitions)
{
    struct benchmark_data *bdata = start_benchmark();
   
    printf("starting %d pools...\n", nrepetitions);
    for (int i = 0; i < nrepetitions; i++) {
        struct thread_pool *threadpool = thread_pool_new(nthreads);
        thread_pool_shutdown_and_destroy(threadpool);
    }

    printf("starting %d simultaneous pools...\n", nrepetitions);
    struct thread_pool *threadpool[nrepetitions];
    for (int i = 0; i < nrepetitions; i++)
        threadpool[i] = thread_pool_new(nthreads);

    for (int i = 0; i < nrepetitions; i++)
        thread_pool_shutdown_and_destroy(threadpool[i]);

    stop_benchmark(bdata);

    report_benchmark_results(bdata);
    printf("Test successful.\n");
    free(bdata);
    return 0;
}

/**********************************************************************************/

static void
usage(char *av0, int exvalue)
{
    fprintf(stderr, "Usage: %s [-n <n>] [-t <t>]\n"
                    " -n number of threads in pool, default %d\n"
                    " -r number of repetitions, default %d\n"
                    , av0, DEFAULT_THREADS, DEFAULT_REPETITIONS);
    exit(exvalue);
}

int 
main(int ac, char *av[]) 
{
    int c, nthreads = DEFAULT_THREADS, repetitions = DEFAULT_REPETITIONS;
    while ((c = getopt(ac, av, "hn:r:")) != EOF) {
        switch (c) {
        case 'n':
            nthreads = atoi(optarg);
            break;
        case 'r':
            repetitions = atoi(optarg);
            break;
        case 'h':
            usage(av[0], EXIT_SUCCESS);
        }
    }

    return run_test(nthreads, repetitions);
}

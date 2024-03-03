/*
 * Fork/Join Framework 
 *
 * Test that idle pools do not continuously spin waiting for new tasks.
 * Although some opportunistic spinning may be ok, it needs to be adaptive
 * and cannot result in continous use of the CPU.
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
#define DEFAULT_UPTIME 1

static int
run_test(int nthreads)
{
    struct benchmark_data *bdata = start_benchmark();
   
    printf("starting thread pool...\n");
    struct thread_pool *threadpool = thread_pool_new(nthreads);
    
    struct timespec sleep_time = {
        .tv_sec = DEFAULT_UPTIME,
        .tv_nsec = 0
    };
    nanosleep(&sleep_time, NULL);
    thread_pool_shutdown_and_destroy(threadpool);
    printf("thread pool shut down...\n");

    stop_benchmark(bdata);
    uint64_t cpu = get_overall_cpu_consumption(bdata);
    double consumed_cpu_sec = cpu/1000000.0;
    
    double maxAllowed = 0.5 * nthreads * sleep_time.tv_sec;
    if (consumed_cpu_sec > maxAllowed) {
        fprintf(stderr, "Your threadpool of %d threads consumed %fs of CPU time in a %lds period while idle.\n", 
            nthreads, consumed_cpu_sec, sleep_time.tv_sec);
        fprintf(stderr, "Your maximum allowance while idle is %fs.\n", maxAllowed);
        abort();
    } else {
        printf("Consumed CPU time %f is within limit (%fs).\n", 
            consumed_cpu_sec, maxAllowed);
    }

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
                    , av0, DEFAULT_THREADS);
    exit(exvalue);
}

int 
main(int ac, char *av[]) 
{
    int c, nthreads = DEFAULT_THREADS;
    while ((c = getopt(ac, av, "hn:r:")) != EOF) {
        switch (c) {
        case 'n':
            nthreads = atoi(optarg);
            break;
        case 'h':
            usage(av[0], EXIT_SUCCESS);
        }
    }

    return run_test(nthreads);
}

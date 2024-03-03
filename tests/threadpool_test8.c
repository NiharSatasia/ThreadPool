/*
 * Fork/Join Framework 
 *
 * Test 8.
 *
 * Tests that multiple thread pools can coexist
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
#define DEFAULT_THREADS 8
#define DEFAULT_POOLS 8

/* Data to be passed to callable. */
struct arg2 {
    uintptr_t a;
    uintptr_t b;
};

/* 
 * A FJ task that multiplies 2 numbers. 
 */
static void *
multiplier_task(struct thread_pool *pool, struct arg2 * data)
{
    return (void *)(data->a * data->b);
}

/* 
 * A FJ task that adds 2 numbers. 
 */
static void *
adder_task(struct thread_pool *pool, struct arg2 * data)
{
    return (void *)(data->a + data->b);
}

static void *
test_task(struct thread_pool *pool, struct arg2 * data)
{
    struct future *f1 = thread_pool_submit(pool, (fork_join_task_t) adder_task, data);
    uintptr_t r1 = (uintptr_t) future_get(f1);
    future_free(f1);

    struct arg2 a2 = {
        .a = r1,
        .b = 7,
    };
    struct future *f2 = thread_pool_submit(pool, (fork_join_task_t) multiplier_task, &a2);
    uintptr_t r2 = (uintptr_t) future_get(f2);
    future_free(f2);

    return (void *)r2;
}

static int
run_test(int npools, int nthreads)
{
    struct benchmark_data *bdata = start_benchmark();
    struct thread_pool *pools[npools];
    for (int i = 0; i < npools; i++)
        pools[i] = thread_pool_new(nthreads);
   
#define N_TASK 11
    struct future *f[npools*N_TASK];
    for (int i = 0; i < npools; i++) {
        for (int j = 0; j < N_TASK; j++) {
            struct arg2 *args = malloc(sizeof *args);
            args->a = i;
            args->b = j;
            f[N_TASK*i+j] = thread_pool_submit(pools[i], (fork_join_task_t) test_task, args);
        }
    }

    uintptr_t ssum = 0;
    for (int i = 0; i < npools; i++) {
        for (int j = 0; j < N_TASK; j++) {

            ssum += (uintptr_t) future_get(f[N_TASK*i+j]);
            future_free(f[N_TASK*i+j]);
        }
    }

    for (int i = 0; i < npools; i++)
        thread_pool_shutdown_and_destroy(pools[i]);

    stop_benchmark(bdata);

    // consistency check
    int expect = 7 * npools * N_TASK * (npools + N_TASK - 2) / 2;
    if (ssum != expect) {
        fprintf(stderr, "Wrong result, expected %d, got %ld\n", expect, ssum);
        abort();
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
    fprintf(stderr, "Usage: %s [-n <n>]\n"
                    " -n number of threads in pool, default %d\n"
                    " -p number of pools, default %d\n"
                    , av0, DEFAULT_THREADS, DEFAULT_POOLS);
    exit(exvalue);
}

int 
main(int ac, char *av[]) 
{
    int c, nthreads = DEFAULT_THREADS, npools = DEFAULT_POOLS;
    while ((c = getopt(ac, av, "hn:p:")) != EOF) {
        switch (c) {
        case 'n':
            nthreads = atoi(optarg);
            break;
        case 'p':
            npools = atoi(optarg);
            break;
        case 'h':
            usage(av[0], EXIT_SUCCESS);
        }
    }

    return run_test(npools, nthreads);
}

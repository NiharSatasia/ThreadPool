/**
 * Adaptive quadrature via Simpson's method.
 *
 * Spring 2024
 *
 * @author Godmar Back
 */
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <math.h>
#include <assert.h>
#include "threadpool.h"
#include "threadpool_lib.h"

typedef double (*DoubleFunc)(double);
struct simpsonState {
    DoubleFunc f;
    double a, b, epsilon;
    int level_max;
};

/* 
 * @param epsilon - desired error for interval
 * @param level_max - maximum recursion depth
 */
static void *
simpson(struct thread_pool *pool, void *_state) {
    struct simpsonState *state = _state;
    // simpson(DoubleFunc f, double a, double b, double epsilon, int level_max) 
    DoubleFunc f = state->f;
    double a = state->a;
    double b = state->b;
    double epsilon = state->epsilon;
    int level_max = state->level_max;

    double m = (a + b)/2;
    double fa = f(a);   // make sure these are computed only once
    double fb = f(b);
    double fm = f(m);
    double one_simpson = (b - a) * (fa + 4 * fm + fb)/6;
    double two_simpson = (b - a) * (fa + 4 * f((a + m)/2) 
                        + 2 * fm + 4 * f((m + b)/2) + fb)/12;

    // printf("%f - %f %d\n", a, b, level_max);
    double result;
    if (level_max == 0) { 
        result = two_simpson; // too deep
    } else if (fabs(two_simpson - one_simpson) < 15*epsilon) {
        result = 16 * two_simpson / 15 - one_simpson / 15;
    } else {
        struct simpsonState leftTask = {
            .f = f,
            .a = a,
            .b = m,
            .epsilon = epsilon/2,
            .level_max = level_max - 1
        };

        struct future *leftFuture = thread_pool_submit(pool, simpson, &leftTask);
        struct simpsonState rightTask = {
            .f = f,
            .a = m,
            .b = b,
            .epsilon = epsilon/2,
            .level_max = level_max - 1
        };
        double left, right;
        void *_right = simpson(pool, &rightTask);
        void *_left = future_get(leftFuture);
        memcpy(&left, &_left, sizeof(double));
        memcpy(&right, &_right, sizeof(double));
        future_free(leftFuture);
        result = left + right;
    }
    void *_result;
    memcpy(&_result, &result, sizeof(double));
    return _result;
}

// two carefully chosen constants
static double A = 5;
static double B = 2000.0;

/*
 * A highly oscillatory function
 */
static double
func(double x)
{
    return exp(-x/A) * pow(sin(B*x), 2);
}

/* 
 * And its indefinite integral
 * (courtesy of Wolfram Alpha since GPT-4 can't do math)
 */
static double
antiderivative_of_func(double x)
{
    return -A * exp(-x/A) * (4*A*A*B*B + 2*A*B*sin(2*B*x) - cos(2*B*x) + 1)/(2*(4*A*A*B*B+1));
}

static void benchmark(double l, double r, double eps, int level_max, int threads) {
    printf("Integrating exp(-x/%g) sin(%g*x)^2 over [%f,%f] eps=%e\n", A, B, l, r, eps);
    struct thread_pool* pool = thread_pool_new(threads);

    struct benchmark_data* bdata = start_benchmark();
    
    struct simpsonState rootTask = {
        .f = func,
        .a = l,
        .b = r,
        .epsilon = eps,
        .level_max = level_max
    };

    struct future* fut = thread_pool_submit(pool, simpson, &rootTask);
    void *_answer = future_get(fut);
    double answer;
    memcpy(&answer, &_answer, sizeof(double));

    stop_benchmark(bdata);

    future_free(fut);
    thread_pool_shutdown_and_destroy(pool);

    double exact = antiderivative_of_func(r) - antiderivative_of_func(l);
    double abserror = fabs(answer - exact);
    printf("Solution: %.17f exact %.17f\n", answer, exact);

    // this bound is entirely empirical
    const double MAX_ABS_ERROR = 1e-11;
    if (abserror < MAX_ABS_ERROR) {
        printf("Solution ok.\n");
        report_benchmark_results(bdata);
    } else { 
        fprintf(stderr, "Absolute error is too large (%.17f >= %.17f).\n", 
            abserror, MAX_ABS_ERROR);
        abort();
    }
}

static void usage(char *av0, int depth, int nthreads) {
    fprintf(stderr, "Usage: %s [-d <n>] [-n <n>] [-e <eps>] [-l <l>] [-r <r>]\n"
                    " -d        maximum recursion depth, default %d\n"
                    " -n        number of threads in pool, default %d\n"
                    " <l>       left bound, default 0\n"
                    " <r>       right bound, default 300\n"
                    " <eps>     epsilon\n"
                    , av0, depth, nthreads);
    abort();
}

int main(int ac, char** av) 
{
    /* Note: this code assumes that a 64-bit double can be 
     * stored in the 64 bits of a pointer and be bitwise
     * copied.  This may not be legal as per ISO-C.
     * We do this to avoid memory allocation in `simpson`
     */
    static_assert(sizeof(void *) == sizeof(double));

    double eps = 1e-14;
    int threads = 4;
    int max_parallel_depth = 30;
    double l = 0.0; 
    double r = 300.0; 
    int c;
    while ((c = getopt(ac, av, "d:n:he:l:r:")) != EOF) {
        switch (c) {
        case 'd':
            max_parallel_depth = atoi(optarg);
            break;
        case 'n':
            threads = atoi(optarg);
            break;
        case 'e':
            eps = atof(optarg);
            break;
        case 'l':
            l = atof(optarg);
            break;
        case 'r':
            r = atof(optarg);
            break;
        case 'h':
            usage(av[0], max_parallel_depth, threads);
        }
    }

    benchmark(l, r, eps, max_parallel_depth, threads);
    return 0;
}

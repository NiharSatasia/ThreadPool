#include "threadpool.h"
#include "list.h"
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <threads.h>

/*
3 structs we should implement
*/

/*
● Should contain any state you need for a threadpool
● Ideas:
○ Locks (pthread_mutex_t)
■ To protect the global queue
○ Queues/Deques (provided list struct from previous project)
○ Semaphores (sem_t)
○ Conditional Variables (pthread_cond_t)
○ Shutdown flag
○ List of workers associated with this thread_pool
○ Etc.
*/
struct thread_pool
{
    pthread_mutex_t lock_global_queue;
    struct list global_queue;
    pthread_cond_t cond_thread_pool;
    int shutdownFlag;
    pthread_t *workers;
    int nthreads;
};

/*
● Should contain a worker struct as well
● Ideas:
○ Maintain which pool this worker is for
○ Queue of internal submissions
○ Lock for local queue
○ etc…
*/

// Not being used in work sharing approach right now
struct worker
{
    struct thread_pool *pool_worker;
    struct list local_queue;
    pthread_mutex_t lock_local_queue;
};

/*
How do we represent a task we need to do?
○ future
○ Threadpool: an instance of a task that you must execute
○ Client: a promise we will give them a reply when they ask for it
● You will invoke “task” as a method, it represents the method passed
through by thread_pool_submit, the return value gets stored into
the result: fut->result = fut->task(pool, fut->data);
*/
struct future
{
    fork_join_task_t task; // typedef of a function pointer type that you will execute
    void *args;            // the data from thread_pool_submit
    void *result;          // will store task result once it completes execution

    // may also need synchronization primitives (mutexes, semaphores, etc)
    pthread_mutex_t lock_future;
    pthread_cond_t cond_future;
    struct list_elem elem;
    int readyFlag;
    struct thread_pool *pool_future;
};

// thread_local variable to keep track of which thread is executing
static thread_local int internal_worker_thread = 0;

// Helper functions
static void *start_routine(void *arg);
static bool execute_task(struct thread_pool *pool);

/*
5 functions we need to implement
*/

/*
Advice from help session pdf for thread_pool_new:
● Create thread pool
● Initialize worker threads
● Call pthread_create: starts a new thread in the calling process. The new
thread starts execution by invoking start_routine(); arg is passed as the
argument of start_routine()

#include <pthread.h
int pthread_create(pthread_t *restrict thread,
                   const pthread_attr_t *restrict attr,
                   void *(*start_routine)(void *),
                   void *restrict arg);
*/

/* Create a new thread pool with no more than n threads. 
 * If any of the threads cannot be created, print
 * an error message and return NULL. */
struct thread_pool *thread_pool_new(int nthreads)
{
    struct thread_pool *pool = malloc(sizeof(struct thread_pool));

    // Initializations
    pthread_mutex_init(&pool->lock_global_queue, NULL);
    pthread_cond_init(&pool->cond_thread_pool, NULL);
    list_init(&pool->global_queue);
    pool->workers = malloc(sizeof(pthread_t) * nthreads);
    pool->nthreads = nthreads;
    pool->shutdownFlag = 0;

    pthread_mutex_lock(&pool->lock_global_queue);

    // Creating worker threads
    for (int i = 0; i < nthreads; i++)
    {
        if (pthread_create(&pool->workers[i], NULL, start_routine, pool) != 0)
        {
            fprintf(stderr, "failed to create a thread");
            return NULL;
        }
    }

    pthread_mutex_unlock(&pool->lock_global_queue);
    return pool;
}

/* 
 * Shutdown this thread pool in an orderly fashion.  
 * Tasks that have been submitted but not executed may or
 * may not be executed.
 *
 * Deallocate the thread pool object before returning. 
 */
void thread_pool_shutdown_and_destroy(struct thread_pool *pool)
{
    // Signal shutdown and broadcast to all worker threads
    pthread_mutex_lock(&pool->lock_global_queue);
    pool->shutdownFlag = 1;
    pthread_cond_broadcast(&pool->cond_thread_pool);
    pthread_mutex_unlock(&pool->lock_global_queue);

    // Join the worker threads
    for (int i = 0; i < pool->nthreads; i++)
    {
        pthread_join(pool->workers[i], NULL);
    }

    // Free 
    free(pool->workers);
    free(pool);
}

/* 
 * Submit a fork join task to the thread pool and return a
 * future.  The returned future can be used in future_get()
 * to obtain the result.
 * 'pool' - the pool to which to submit
 * 'task' - the task to be submitted.
 * 'data' - data to be passed to the task's function
 *
 * Returns a future representing this computation.
 */
struct future *thread_pool_submit(struct thread_pool *pool, fork_join_task_t task, void *data)
{
    pthread_mutex_lock(&pool->lock_global_queue);
    struct future *fut = malloc(sizeof(struct future));

    // Initializations
    pthread_mutex_init(&fut->lock_future, NULL);
    pthread_cond_init(&fut->cond_future, NULL);
    fut->pool_future = pool;
    fut->task = task;
    fut->args = data;
    fut->result = NULL;
    fut->readyFlag = 0;

    // Add task to global queue and signal worker thread
    list_push_back(&pool->global_queue, &fut->elem);
    pthread_cond_signal(&pool->cond_thread_pool);

    pthread_mutex_unlock(&pool->lock_global_queue);
    return fut;
}

/* Make sure that the thread pool has completed the execution
 * of the fork join task this future represents.
 *
 * Returns the value returned by this task.
 */
void *future_get(struct future *fut)
{
    pthread_mutex_lock(&fut->lock_future);
    while (fut->readyFlag == 0)
    {
        if (internal_worker_thread == 1)
        {
            // Unlock and execute task
            pthread_mutex_unlock(&fut->lock_future);
            if (execute_task(fut->pool_future) == false)
            {
                // If no task was executed lock and wait
                pthread_mutex_lock(&fut->lock_future);
                if (fut->readyFlag == 0)
                {
                    pthread_cond_wait(&fut->cond_future, &fut->lock_future);
                }
            }
        }
        else
        {
            // Wait for task to complete
            pthread_cond_wait(&fut->cond_future, &fut->lock_future);
        }
    }

    pthread_mutex_unlock(&fut->lock_future);\
    return fut->result;
}

/* Deallocate this future.  Must be called after future_get() */
void future_free(struct future *fut)
{
    free(fut);
}

// Static helper function for worker thread routine. Executes tasks from global queue, if empty then waits for tasks.
static void *start_routine(void *arg)
{
    struct thread_pool *pool = (struct thread_pool *)arg;
    internal_worker_thread = 1;
    while (1)
    {
        pthread_mutex_lock(&pool->lock_global_queue);

        // Wait for tasks or shutdown signal
        while (list_empty(&pool->global_queue) && !pool->shutdownFlag)
        {
            pthread_cond_wait(&pool->cond_thread_pool, &pool->lock_global_queue);
        }
        if (pool->shutdownFlag)
        {
            pthread_mutex_unlock(&pool->lock_global_queue);
            return NULL;
        }
        pthread_mutex_unlock(&pool->lock_global_queue);

        while (execute_task(pool) == true)
        {
            // Continue executing tasks until queue is empty
        }
    }
    return NULL;
}

// Another static helper function to execute tasks, returns true if a task was executed and false if queue is empty
static bool execute_task(struct thread_pool *pool)
{
    pthread_mutex_lock(&pool->lock_global_queue);

    // Check for tasks in the global queue
    if (!list_empty(&pool->global_queue))
    {
        // Pop first task from global queue and unlock
        struct list_elem *task_elem = list_pop_front(&pool->global_queue);
        struct future *fut = list_entry(task_elem, struct future, elem);
        pthread_mutex_unlock(&pool->lock_global_queue);

        // Execute task and broadcast
        fut->result = fut->task(pool, fut->args);
        pthread_mutex_lock(&fut->lock_future);
        fut->readyFlag = 1;
        pthread_cond_broadcast(&fut->cond_future);
        pthread_mutex_unlock(&fut->lock_future);
        return true;
    }

    pthread_mutex_unlock(&pool->lock_global_queue);
    return false;
}

/* Current Test Results
Starting test: Basic functionality testing (1)
================================================================================
Running: timeout 15 ./threadpool_test -n 1 [+]
Running: timeout 15 ./threadpool_test -n 2 [+]
Running: timeout 15 ./threadpool_test -n 4 [+]

Starting test: Basic functionality testing (2)
================================================================================
Running: timeout 15 ./threadpool_test2 -n 1 [+]
Running: timeout 15 ./threadpool_test2 -n 2 [+]
Running: timeout 15 ./threadpool_test2 -n 4 [+]

Starting test: Basic functionality testing (3)
================================================================================
Running: timeout 15 ./threadpool_test3 -n 1 [+]
Running: timeout 15 ./threadpool_test3 -n 2 [+]
Running: timeout 15 ./threadpool_test3 -n 4 [+]

Starting test: Basic functionality testing (4)
================================================================================
Running: timeout 15 ./threadpool_test4 -n 2 [+]
Running: timeout 15 ./threadpool_test4 -n 4 [+]

Starting test: Basic functionality testing (5)
================================================================================
Running: timeout 15 ./threadpool_test5 -n 2 [+]
Running: timeout 15 ./threadpool_test5 -n 4 [+]

Starting test: Basic functionality testing (6)
================================================================================
Running: timeout 15 ./threadpool_test6.py -n 1 [+]

Starting test: Basic functionality testing (7)
================================================================================
Running: timeout 15 ./threadpool_test7 -n 2 [+]
Running: timeout 15 ./threadpool_test7 -n 4 [+]
Running: timeout 15 ./threadpool_test7 -n 8 [+]

Starting test: Basic functionality testing (8)
================================================================================
Running: timeout 15 ./threadpool_test8 -n 2 -p 4 [+]
Running: timeout 15 ./threadpool_test8 -n 4 -p 4 [+]
Running: timeout 15 ./threadpool_test8 -n 8 -p 4 [+]
Running: timeout 15 ./threadpool_test8 -n 2 -p 8 [+]
Running: timeout 15 ./threadpool_test8 -n 4 -p 8 [+]
Running: timeout 15 ./threadpool_test8 -n 8 -p 8 [+]
Running: timeout 15 ./threadpool_test8 -n 2 -p 16 [+]
Running: timeout 15 ./threadpool_test8 -n 4 -p 16 [+]
Running: timeout 15 ./threadpool_test8 -n 8 -p 16 [+]

Starting test: Basic functionality testing (9)
================================================================================
Running: timeout 15 ./threadpool_test9 -n 2 [+]
Running: timeout 15 ./threadpool_test9 -n 4 [+]

Starting test: parallel mergesort
================================================================================
Running: timeout 15 ./mergesort -n 1 -s 44 3000000 [+]
Running: timeout 15 ./mergesort -n 2 -s 44 3000000 [+]
Running: timeout 15 ./mergesort -n 4 -s 44 3000000 [+]
Running: timeout 15 ./mergesort -n 8 -s 44 3000000 [+]
Running: timeout 15 ./mergesort -n 16 -s 44 3000000 [+]
Running: timeout 15 ./mergesort -n 1 -s 44 30000000 [+]
Running: timeout 15 ./mergesort -n 2 -s 44 30000000 [+]
Running: timeout 15 ./mergesort -n 4 -s 44 30000000 [+]
Running: timeout 15 ./mergesort -n 8 -s 44 30000000 [+]
Running: timeout 15 ./mergesort -n 16 -s 44 30000000 [+]
Running: timeout 60 ./mergesort -n 8 -s 44 300000000 [ ]
        Program terminated with signal 11 (SIGSEGV) 
        --------------------------------------------
        Program output:
        
        StdErr output:
        
        
Running: timeout 60 ./mergesort -n 16 -s 44 300000000 [+]
Running: timeout 60 ./mergesort -n 32 -s 44 300000000 [+]

Starting test: parallel quicksort
================================================================================
Running: timeout 15 ./quicksort -n 1 -s 44 -d 12 3000000 [+]
Running: timeout 15 ./quicksort -n 2 -s 44 -d 12 3000000 [+]
Running: timeout 15 ./quicksort -n 4 -s 44 -d 12 3000000 [+]
Running: timeout 15 ./quicksort -n 8 -s 44 -d 12 3000000 [+]
Running: timeout 15 ./quicksort -n 16 -s 44 -d 12 3000000 [+]
Running: timeout 15 ./quicksort -n 1 -s 44 -d 15 30000000 [+]
Running: timeout 15 ./quicksort -n 2 -s 44 -d 15 30000000 [+]
Running: timeout 15 ./quicksort -n 4 -s 44 -d 15 30000000 [ ]
        Program terminated with signal 6 (SIGABRT) 
        --------------------------------------------
        Program output:
        
        StdErr output:
        Fatal glibc error: pthread_mutex_lock.c:95 (___pthread_mutex_lock): assertion failed: mutex->__data.
        
Running: timeout 15 ./quicksort -n 8 -s 44 -d 15 30000000 [ ]
        Program terminated with signal 6 (SIGABRT) 
        --------------------------------------------
        Program output:
        
        StdErr output:
        Fatal glibc error: pthread_mutex_lock.c:95 (___pthread_mutex_lock): assertion failed: mutex->__data.
        
Running: timeout 15 ./quicksort -n 16 -s 44 -d 15 30000000 [+]
Running: timeout 60 ./quicksort -n 8 -s 44 -d 18 300000000 [+]
Running: timeout 60 ./quicksort -n 16 -s 44 -d 18 300000000 [+]
Running: timeout 60 ./quicksort -n 32 -s 44 -d 18 300000000 [ ]
        Program terminated with signal 6 (SIGABRT) 
        --------------------------------------------
        Program output:
        
        StdErr output:
        Fatal glibc error: pthread_mutex_lock.c:95 (___pthread_mutex_lock): assertion failed: mutex->__data.
        

Starting test: parallel sum using divide-and-conquer
================================================================================
Running: timeout 15 ./psum_test -n 1 10000000 [ ]
        Program terminated with signal 11 (SIGSEGV) 
        --------------------------------------------
        Program output:
        
        StdErr output:
        
        
Running: timeout 15 ./psum_test -n 2 10000000 [+]
Running: timeout 15 ./psum_test -n 4 10000000 [+]
Running: timeout 15 ./psum_test -n 8 10000000 [+]
Running: timeout 15 ./psum_test -n 16 10000000 [+]
Running: timeout 15 ./psum_test -n 1 100000000 [ ]
        Program terminated with signal 11 (SIGSEGV) 
        --------------------------------------------
        Program output:
        
        StdErr output:
        
        
Running: timeout 15 ./psum_test -n 2 100000000 [ ]
        Program terminated with signal 11 (SIGSEGV) 
        --------------------------------------------
        Program output:
        
        StdErr output:
        
        
Running: timeout 15 ./psum_test -n 4 100000000 [ ]
        Program terminated with signal 11 (SIGSEGV) 
        --------------------------------------------
        Program output:
        
        StdErr output:
        
        
Running: timeout 15 ./psum_test -n 8 100000000 [ ]
        Program terminated with signal 11 (SIGSEGV) 
        --------------------------------------------
        Program output:
        
        StdErr output:
        
        
Running: timeout 15 ./psum_test -n 16 100000000 [+]
Running: timeout 60 ./psum_test -n 8 1000000000 [ ]
        Program terminated with signal 11 (SIGSEGV) 
        --------------------------------------------
        Program output:
        
        StdErr output:
        
        
Running: timeout 60 ./psum_test -n 16 1000000000 [ ]
        Program terminated with signal 11 (SIGSEGV) 
        --------------------------------------------
        Program output:
        
        StdErr output:
        
        
Running: timeout 60 ./psum_test -n 32 1000000000 [ ]
        Program terminated with signal 11 (SIGSEGV) 
        --------------------------------------------
        Program output:
        
        StdErr output:
        
        

Starting test: parallel n-queens solver
================================================================================
Running: timeout 15 ./nqueens -n 1 11 [+]
Running: timeout 15 ./nqueens -n 2 11 [+]
Running: timeout 15 ./nqueens -n 4 11 [+]
Running: timeout 15 ./nqueens -n 8 11 [+]
Running: timeout 15 ./nqueens -n 16 11 [+]
Running: timeout 60 ./nqueens -n 1 12 [+]
Running: timeout 60 ./nqueens -n 2 12 [+]
Running: timeout 60 ./nqueens -n 4 12 [+]
Running: timeout 60 ./nqueens -n 8 12 [+]
Running: timeout 60 ./nqueens -n 16 12 [+]
Running: timeout 60 ./nqueens -n 8 13 [+]
Running: timeout 60 ./nqueens -n 16 13 [+]
Running: timeout 60 ./nqueens -n 32 13 [+]
Running: timeout 60 ./nqueens -n 16 14 [+]
Running: timeout 60 ./nqueens -n 32 14 [+]

Starting test: parallel Simpson integration
================================================================================
Running: timeout 60 ./simpson -n 8 [ ]
        Program terminated with signal 11 (SIGSEGV) 
        --------------------------------------------
        Program output:
        
        StdErr output:
        
        
Running: timeout 60 ./simpson -n 16 [ ]
        Program terminated with signal 11 (SIGSEGV) 
        --------------------------------------------
        Program output:
        
        StdErr output:
        
        
Running: timeout 60 ./simpson -n 32 [ ]
        Program terminated with signal 11 (SIGSEGV) 
        --------------------------------------------
        Program output:
        
        StdErr output:
        
        

Starting test: parallel fibonacci toy test
================================================================================
Running: timeout 60 ./fib_test -n 1 32 [ ]
        Program terminated with signal 11 (SIGSEGV) 
        --------------------------------------------
        Program output:
        
        StdErr output:
        
        
Running: timeout 60 ./fib_test -n 2 32 [ ]
        Program terminated with signal 11 (SIGSEGV) 
        --------------------------------------------
        Program output:
        
        StdErr output:
        
        
Running: timeout 60 ./fib_test -n 4 32 [ ]
        Program terminated with signal 11 (SIGSEGV) 
        --------------------------------------------
        Program output:
        
        StdErr output:
        
        
Running: timeout 60 ./fib_test -n 8 32 [ ]
        Program terminated with signal 11 (SIGSEGV) 
        --------------------------------------------
        Program output:
        
        StdErr output:
        
        
Running: timeout 60 ./fib_test -n 16 32 [ ]
        Program terminated with signal 11 (SIGSEGV) 
        --------------------------------------------
        Program output:
        
        StdErr output:
        
        
Running: timeout 60 ./fib_test -n 16 41 [ ]
        Program terminated with signal 11 (SIGSEGV) 
        --------------------------------------------
        Program output:
        
        StdErr output:
        
        
Running: timeout 60 ./fib_test -n 32 41 [ ]
        Program terminated with signal 11 (SIGSEGV) 
        --------------------------------------------
        Program output:
        
        StdErr output:
        
        

Test name:                1         2         4         8         16        32        
================================================================================
BASIC1:  Basic functionality testing (1)
  basic test 1           [X]       [X]       [X]                                     
BASIC2:  Basic functionality testing (2)
  basic test 2           [X]       [X]       [X]                                     
BASIC3:  Basic functionality testing (3)
  basic test 3           [X]       [X]       [X]                                     
BASIC4:  Basic functionality testing (4)
  basic test 4                     [X]       [X]                                     
BASIC5:  Basic functionality testing (5)
  basic test 5                     [X]       [X]                                     
BASIC6:  Basic functionality testing (6)
  basic test 6           [X]                                                         
BASIC7:  Basic functionality testing (7)
  basic test 7                     [X]       [X]       [X]                           
BASIC8:  Basic functionality testing (8)
  basic test 8 (4 pools)           [X]       [X]       [X]                           
  basic test 8 (8 pools)           [X]       [X]       [X]                           
  basic test 8 (16 pools)          [X]       [X]       [X]                           
BASIC9:  Basic functionality testing (9)
  basic test 9                     [X]       [X]                                     
MERGESORT:  parallel mergesort
  mergesort small        [X]       [X]       [X]       [X]       [X]                 
  mergesort medium       [X]       [X]       [X]       [X]       [X]                 
  mergesort large                                      [ ]       [8.411s]  [7.133s]  
QUICKSORT:  parallel quicksort
  quicksort small        [X]       [X]       [X]       [X]       [X]                 
  quicksort medium       [X]       [X]       [ ]       [ ]       [X]                 
  quicksort large                                      [6.973s]  [6.958s]  [ ]       
PSUM:  parallel sum using divide-and-conquer
  psum_test small        [ ]       [X]       [X]       [X]       [X]                 
  psum_test medium       [ ]       [ ]       [ ]       [ ]       [X]                 
  psum_test large                                      [ ]       [ ]       [ ]       
NQUEENS:  parallel n-queens solver
  nqueens 11             [X]       [X]       [X]       [X]       [X]                 
  nqueens 12             [X]       [X]       [X]       [X]       [X]                 
  nqueens 13                                           [X]       [X]       [X]       
  nqueens 14                                                     [10.330s] [5.942s]  
SIMPSON:  parallel Simpson integration
  simpson                                              [ ]       [ ]       [ ]       
FIBONACCI:  parallel fibonacci toy test
  fibonacci 32           [ ]       [ ]       [ ]       [ ]       [ ]                 
  fibonacci 41                                                   [ ]       [ ]       
================================================================================
You have met minimum requirements, your performance score will count.
Wrote full results to fj_testdir_2024-03-18_14:30:35.588040/full-results.json

*/
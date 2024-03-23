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
  pthread_t *worker_threads;
  int nthreads;
  struct list **workerQueues;
  pthread_mutex_t **workerLocks;
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
  //pthread_cond_t workerCond;
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
  bool executing;
  //bool waiting;
};


// thread_local variable to keep track of which thread is executing
static thread_local int internal_worker_thread = 0;
static thread_local struct worker *current_worker;


// Helper functions
static void *start_routine(void *arg);
static bool execute_task(struct thread_pool *pool);
static void executeSpecific(struct future *fut);


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
  pool->worker_threads = malloc(sizeof(pthread_t) * nthreads);
  pool->nthreads = nthreads;
  pool->shutdownFlag = 0;
  pool->workerQueues = malloc(sizeof(struct list) * nthreads);
  pool->workerLocks = malloc(sizeof(pthread_mutex_t) * nthreads);


  pthread_mutex_lock(&pool->lock_global_queue);


  // Creating worker threads
  for (int i = 0; i < nthreads; i++)
  {
    pool->workerQueues[i] = NULL;
    pool->workerLocks[i] = NULL;
    if (pthread_create(&pool->worker_threads[i], NULL, start_routine, pool) != 0)
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
    pthread_join(pool->worker_threads[i], NULL);
  }


  // Destroy thread_pool struct created in thread_pool_new
  pthread_mutex_destroy(&pool->lock_global_queue);
  pthread_cond_destroy(&pool->cond_thread_pool);


  // Free
  free(pool->worker_threads);
  free(pool->workerQueues);
  free(pool->workerLocks);
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
  struct future *fut = malloc(sizeof(struct future));


  // Initializations
  pthread_mutex_init(&fut->lock_future, NULL);
  pthread_cond_init(&fut->cond_future, NULL);
  fut->pool_future = pool;
  fut->task = task;
  fut->args = data;
  fut->result = NULL;
  fut->readyFlag = 0;
  fut->executing = false;


  // Add task to global queue and signal worker thread
  if (current_worker) {
    pthread_mutex_lock(&current_worker->lock_local_queue);
    list_push_front(&current_worker->local_queue, &fut->elem);
    pthread_mutex_unlock(&current_worker->lock_local_queue);
  }
  else {
    pthread_mutex_lock(&pool->lock_global_queue);
    list_push_back(&pool->global_queue, &fut->elem);
    pthread_cond_broadcast(&pool->cond_thread_pool);
    pthread_mutex_unlock(&pool->lock_global_queue);
  }


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
      //Unlock and execute task
      //pthread_mutex_unlock(&fut->lock_future);
      if (!fut->executing) {
        //execute yourself
        pthread_mutex_unlock(&fut->lock_future);
        pthread_mutex_lock(&current_worker->lock_local_queue);
        list_remove(&fut->elem);
        pthread_mutex_unlock(&current_worker->lock_local_queue);
        //pthread_mutex_unlock(&fut->lock_future);
        executeSpecific(fut);
        return fut->result;
      }
      else {
        while(fut->readyFlag == 0 && execute_task(fut->pool_future)) {
            //execute tasks while waiting
        }
        if (fut->readyFlag == 0) {
          pthread_cond_wait(&fut->cond_future, &fut->lock_future);
        }
      }
    }
    else
    {
      //Wait for task to complete
      pthread_cond_wait(&fut->cond_future, &fut->lock_future);
    }
  }


  pthread_mutex_unlock(&fut->lock_future);
  return fut->result;
}


/* Deallocate this future.  Must be called after future_get() */
void future_free(struct future *fut)
{
  // Destroy future struct created in thread_pool_submit
  pthread_mutex_destroy(&fut->lock_future);
  pthread_cond_destroy(&fut->cond_future);


  free(fut);
}


// Static helper function for worker thread routine. Executes tasks from global queue, if empty then waits for tasks.
static void *start_routine(void *arg)
{
  struct thread_pool *pool = (struct thread_pool *)arg;
  internal_worker_thread = 1;


  if (!current_worker) {
    current_worker = malloc(sizeof(struct worker));
    list_init(&current_worker->local_queue);
    pthread_mutex_init(&current_worker->lock_local_queue, NULL);
    current_worker->pool_worker = pool;
    //pthread_cond_init(&current_worker->workerCond, NULL);
    pthread_mutex_lock(&pool->lock_global_queue);
    for (int i = 0; i < pool->nthreads; i++) {
      if (!pool->workerQueues[i]) {
        pool->workerQueues[i] = &current_worker->local_queue;
        pool->workerLocks[i] = &current_worker->lock_local_queue;
      }
    }
    pthread_mutex_unlock(&pool->lock_global_queue);
  }


  while (1)
  {
    //pthread_mutex_lock(&pool->lock_global_queue);


    // Wait for tasks or shutdown signal
    //pthread_mutex_lock(&current_worker->lock_local_queue);
    while (!pool->shutdownFlag && execute_task(pool))
    {
      //execute tasks
    }
    //pthread_mutex_unlock(&current_worker->lock_local_queue);
    if (pool->shutdownFlag)
    {
      //pthread_mutex_unlock(&pool->lock_global_queue);
      free(current_worker);
      return NULL;
    }
    else {
      pthread_mutex_lock(&pool->lock_global_queue);
      pthread_cond_wait(&pool->cond_thread_pool, &pool->lock_global_queue);
      pthread_mutex_unlock(&pool->lock_global_queue);
    }


    // while (execute_task(pool) == true)
    // {
    //   // Continue executing tasks until queue is empty
    // }
  }
  return NULL;
}


static void executeSpecific(struct future *fut) {
    fut->executing = true;
    fut->result = fut->task(fut->pool_future, fut->args);
    pthread_mutex_lock(&fut->lock_future);
    fut->readyFlag = 1;
    pthread_cond_broadcast(&fut->cond_future);
    pthread_mutex_unlock(&fut->lock_future);
    //return true;
}


// Another static helper function to execute tasks, returns true if a task was executed and false if queue is empty
static bool execute_task(struct thread_pool *pool)
{
  pthread_mutex_lock(&current_worker->lock_local_queue);
  if (!list_empty(&current_worker->local_queue)) {
    struct list_elem *task_elem = list_pop_front(&current_worker->local_queue);
    struct future *fut = list_entry(task_elem, struct future, elem);
    pthread_mutex_unlock(&current_worker->lock_local_queue);


    // Execute task and broadcast
    fut->executing = true;
    fut->result = fut->task(pool, fut->args);
    pthread_mutex_lock(&fut->lock_future);
    fut->readyFlag = 1;
    pthread_cond_broadcast(&fut->cond_future);
    pthread_mutex_unlock(&fut->lock_future);
    return true;
  }
  pthread_mutex_unlock(&current_worker->lock_local_queue);


  pthread_mutex_lock(&pool->lock_global_queue);
  // Check for tasks in the global queue
  if (!list_empty(&pool->global_queue))
  {
    //fprintf(stdout, "%s\n", "global");
    // Pop first task from global queue and unlock
    struct list_elem *task_elem = list_pop_front(&pool->global_queue);
    struct future *fut = list_entry(task_elem, struct future, elem);
    pthread_mutex_unlock(&pool->lock_global_queue);


    // Execute task and broadcast
    fut->executing = true;
    fut->result = fut->task(pool, fut->args);
    pthread_mutex_lock(&fut->lock_future);
    fut->readyFlag = 1;
    pthread_cond_broadcast(&fut->cond_future);
    pthread_mutex_unlock(&fut->lock_future);
    return true;
  }
  pthread_mutex_unlock(&pool->lock_global_queue);


  for (int i = 0; i < pool->nthreads; i++) {
    if (pthread_mutex_trylock(pool->workerLocks[i]) == 0) {
      if (!list_empty(pool->workerQueues[i])) {
        fprintf(stdout, "%s\n", "enter");
        struct list_elem *task_elem = list_pop_back(pool->workerQueues[i]);
        struct future *fut = list_entry(task_elem, struct future, elem);
        pthread_mutex_unlock(pool->workerLocks[i]);


        // Execute task and broadcast
        fut->executing = true;
        fut->result = fut->task(pool, fut->args);
        pthread_mutex_lock(&fut->lock_future);
        fut->readyFlag = 1;
        pthread_cond_broadcast(&fut->cond_future);
        pthread_mutex_unlock(&fut->lock_future);
        //fprintf(stdout, "%s\n", "yep");
        return true;
    }
    pthread_mutex_unlock(pool->workerLocks[i]);
  }
  }
  return false;
}


#include "threadpool.h"
#include "list.h"
#include <pthread.h>


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
struct thread_pool {

};

/*
● Should contain a worker struct as well
● Ideas:
○ Maintain which pool this worker is for
○ Queue of internal submissions
○ Lock for local queue
○ etc…
*/
struct worker {

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
struct future {

fork_join_task_t task; // typedef of a function pointer type that you will execute
 void* args; // the data from thread_pool_submit
 void* result; // will store task result once it completes execution

 // may also need synchronization primitives (mutexes, semaphores, etc)

};


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
struct thread_pool * thread_pool_new(int nthreads);

void thread_pool_shutdown_and_destroy(struct thread_pool *);

struct future * thread_pool_submit(struct thread_pool *pool, fork_join_task_t task, void * data);

void * future_get(struct future *);

void future_free(struct future *);
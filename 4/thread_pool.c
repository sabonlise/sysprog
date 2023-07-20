#include "thread_pool.h"
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdatomic.h>
#include <stdbool.h>

enum status {
    TASK_RUNNING,
    TASK_WAITING,
    TASK_JOINED,
    TASK_COMPLETED
};

struct thread_task {
	thread_task_f function;
	void *arg;

    /* PUT HERE OTHER MEMBERS */
    pthread_mutex_t mutex;
    pthread_cond_t cond;

    _Atomic int status;

};

struct thread_pool {
	pthread_t *threads;

    /* PUT HERE OTHER MEMBERS */

    _Atomic int threads_count;
    size_t max_threads;
    _Atomic int task_count;

    pthread_mutex_t mutex;
    pthread_cond_t cond;
};

int thread_pool_new(int max_thread_count, struct thread_pool **pool) {
    if (max_thread_count > TPOOL_MAX_THREADS || max_thread_count < 0) {
        return TPOOL_ERR_INVALID_ARGUMENT;
    }

    *pool = malloc(sizeof(struct thread_pool));
    (*pool)->max_threads = (size_t) max_thread_count;
    (*pool)->threads_count = 0;
    (*pool)->task_count = 0;
    (*pool)->threads = malloc(max_thread_count * sizeof(pthread_t));

    pthread_mutex_init(&(*pool)->mutex, NULL);
    pthread_cond_init(&(*pool)->cond, NULL);

	return 0;
}

int thread_pool_thread_count(const struct thread_pool *pool) {
	return pool->threads_count;
}

int thread_pool_delete(struct thread_pool *pool) {
	/* IMPLEMENT THIS FUNCTION */
	(void)pool;
	return TPOOL_ERR_NOT_IMPLEMENTED;
}

void *pool_worker(void *_pool) {
    struct thread_pool *pool = (struct thread_pool *) _pool;

    return NULL;
}

int thread_pool_push_task(struct thread_pool *pool, struct thread_task *task) {
    if (pool->task_count > TPOOL_MAX_TASKS || pool->task_count < 0) {
        return TPOOL_ERR_INVALID_ARGUMENT;
    }

    _Atomic int cnt = atomic_fetch_add_explicit(&pool->task_count, 1, memory_order_acq_rel);

    return 0;
}

int
thread_task_new(struct thread_task **task, thread_task_f function, void *arg)
{
    *task = malloc(sizeof(struct thread_task));
    (*task)->function = function;
    (*task)->arg = arg;

    pthread_mutex_init(&(*task)->mutex, NULL);
    pthread_cond_init(&(*task)->cond, NULL);

	return 0;
}

bool
thread_task_is_finished(const struct thread_task *task)
{
	return task->status == TASK_COMPLETED;
}

bool
thread_task_is_running(const struct thread_task *task)
{
	return task->status == TASK_RUNNING;
}

int
thread_task_join(struct thread_task *task, void **result)
{
	/* IMPLEMENT THIS FUNCTION */
	(void)task;
	(void)result;
	return TPOOL_ERR_NOT_IMPLEMENTED;
}

#ifdef NEED_TIMED_JOIN

int
thread_task_timed_join(struct thread_task *task, double timeout, void **result)
{
	/* IMPLEMENT THIS FUNCTION */
	(void)task;
	(void)timeout;
	(void)result;
	return TPOOL_ERR_NOT_IMPLEMENTED;
}

#endif

int
thread_task_delete(struct thread_task *task)
{
	/* IMPLEMENT THIS FUNCTION */
	(void)task;
	return TPOOL_ERR_NOT_IMPLEMENTED;
}

#ifdef NEED_DETACH

int
thread_task_detach(struct thread_task *task)
{
	/* IMPLEMENT THIS FUNCTION */
	(void)task;
	return TPOOL_ERR_NOT_IMPLEMENTED;
}

#endif

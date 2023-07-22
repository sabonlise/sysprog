#include "thread_pool.h"
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdatomic.h>
#include <stdbool.h>

enum status {
    TASK_RUNNING = 11,
    TASK_WAITING,
    TASK_JOINED,
    TASK_COMPLETED,
    TASK_CREATED
};

struct thread_task {
    thread_task_f function;
    void *arg;
    
    /* PUT HERE OTHER MEMBERS */
    _Atomic int status;
    void *result;
    
    pthread_mutex_t mutex;
    pthread_cond_t is_completed;
};

struct thread_pool {
    pthread_t *threads;
    
    /* PUT HERE OTHER MEMBERS */
    
    _Atomic int threads_count;
    _Atomic int max_threads;
    _Atomic int task_count;
    
    _Atomic bool is_deleted;
    
    struct thread_task **tasks;
    
    pthread_mutex_t mutex;
    pthread_cond_t is_available_for_task;
};

int thread_pool_new(int max_thread_count, struct thread_pool **pool) {
    if (max_thread_count > TPOOL_MAX_THREADS || max_thread_count <= 0) {
        return TPOOL_ERR_INVALID_ARGUMENT;
    }
    
    *pool = malloc(sizeof(struct thread_pool));
    atomic_init(&(*pool)->max_threads, max_thread_count);
    
    atomic_init(&(*pool)->threads_count, 0);
    atomic_init(&(*pool)->task_count, 0);
    
    (*pool)->threads = malloc(max_thread_count * sizeof(pthread_t));
    (*pool)->tasks = malloc(sizeof(struct thread_task) * TPOOL_MAX_TASKS);
    atomic_init(&(*pool)->is_deleted, false);
    
    pthread_mutex_init(&(*pool)->mutex, NULL);
    pthread_cond_init(&(*pool)->is_available_for_task, NULL);
    
    return 0;
}

int thread_pool_thread_count(const struct thread_pool *pool) {
    return pool->threads_count;
}

int thread_pool_delete(struct thread_pool *pool) {
    printf("current task count: %d\n", pool->task_count);
    if (pool->task_count > 0) {
        return TPOOL_ERR_HAS_TASKS;
    }
    
    atomic_store(&pool->is_deleted, true);
    
    pthread_mutex_lock(&pool->mutex);
    pthread_cond_broadcast(&pool->is_available_for_task);
    pthread_mutex_unlock(&pool->mutex);
    
    for (int i = 0; i < pool->threads_count; i++) {
        printf("thread count %d. joining thread %d\n", thread_pool_thread_count(pool), i);
        pthread_join(pool->threads[i], NULL);
    }
    
    pthread_cond_destroy(&pool->is_available_for_task);
    pthread_mutex_destroy(&pool->mutex);
    
    free(pool->tasks);
    free(pool->threads);
    free(pool);
    
    return 0;
}

void *pool_worker(void *tpool) {
    struct thread_pool *pool = (struct thread_pool *) tpool;
    
    while (true) {
        pthread_mutex_lock(&pool->mutex);
    
        if (pool->is_deleted) {
            pthread_mutex_unlock(&pool->mutex);
            break;
        }
    
        while (pool->task_count == 0 && !pool->is_deleted) {
            pthread_cond_wait(&pool->is_available_for_task, &pool->mutex);
        }
    
        atomic_fetch_sub_explicit(&pool->task_count, 1, memory_order_acq_rel);
    
        struct thread_task *task = pool->tasks[pool->task_count];
        pthread_mutex_unlock(&pool->mutex);
    
        atomic_store_explicit(&task->status, TASK_RUNNING, memory_order_relaxed);
    
        void *result = task->function(task->arg);
    
        pthread_mutex_lock(&task->mutex);
        pthread_mutex_lock(&pool->mutex);
    
        atomic_store(&task->result, result);
        atomic_store_explicit(&task->status, TASK_COMPLETED, memory_order_relaxed);
    
        pthread_cond_signal(&task->is_completed);
        // atomic_fetch_sub_explicit(&pool->threads_count, 1, memory_order_acq_rel);
    
        pthread_mutex_unlock(&task->mutex);
        pthread_mutex_unlock(&pool->mutex);
    }
    
    return NULL;
}

int thread_pool_push_task(struct thread_pool *pool, struct thread_task *task) {
    if (pool->task_count > TPOOL_MAX_TASKS) {
        return TPOOL_ERR_TOO_MANY_TASKS;
    }
    
    pthread_mutex_lock(&pool->mutex);
    pool->tasks[pool->task_count] = task;
    atomic_fetch_add_explicit(&pool->task_count, 1, memory_order_acq_rel);
    
    atomic_store(&task->status, TASK_WAITING);
    
    printf("pushed new task. tasks in pool: %d. threads: %d\n", pool->task_count, pool->threads_count);
    
    if (pool->max_threads > pool->threads_count && pool->task_count > pool->threads_count) {
        pthread_create(&(pool->threads[pool->threads_count]), NULL, pool_worker, (void*) pool);
        atomic_fetch_add_explicit(&pool->threads_count, 1, memory_order_acq_rel);
    }
    
    pthread_cond_signal(&pool->is_available_for_task);
    pthread_mutex_unlock(&pool->mutex);
    
    return 0;
}

int thread_task_new(struct thread_task **task, thread_task_f function, void *arg) {
    *task = malloc(sizeof(struct thread_task));
    (*task)->function = function;
    (*task)->arg = arg;
    (*task)->status = TASK_CREATED;
    
    pthread_mutex_init(&(*task)->mutex, NULL);
    pthread_cond_init(&(*task)->is_completed, NULL);
    printf("created new task\n");
    
    return 0;
}

bool thread_task_is_finished(const struct thread_task *task) {
    return task->status == TASK_COMPLETED;
}

bool thread_task_is_running(const struct thread_task *task) {
	return task->status == TASK_RUNNING;
}

int thread_task_join(struct thread_task *task, void **result) {
    if (task->status == TASK_CREATED) {
        return TPOOL_ERR_TASK_NOT_PUSHED;
    }
    
    pthread_mutex_lock(&task->mutex);
    
    while (!thread_task_is_finished(task)) {
        pthread_cond_wait(&task->is_completed, &task->mutex);
    }
    
    *result = task->result;
    task->status = TASK_JOINED;
    pthread_mutex_unlock(&task->mutex);
    
    return 0;
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

int thread_task_delete(struct thread_task *task) {
    if (task->status != TASK_CREATED && task->status != TASK_JOINED) {
        return TPOOL_ERR_TASK_IN_POOL;
    }
    
    pthread_mutex_destroy(&task->mutex);
    pthread_cond_destroy(&task->is_completed);
    
    free(task);
    
    return 0;
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

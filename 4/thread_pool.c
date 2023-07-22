#include "thread_pool.h"
#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/time.h>
#include <asm-generic/errno.h>

const int SUCCESS = 0;
const int NONE = -1;

struct thread_task {
    thread_task_f function;
    void *arg;
    void *result;

    bool finished;
    bool assigned;
    bool scheduled_for_deletion;

    // lock not needed since now we use pool's mutex for everything
    // pthread_mutex_t lock;
    pthread_cond_t has_just_finished_cond;

    struct thread_pool *parent_pool;
    int index_in_parent_pool;
};

const int TASK_APPEARS_EVENT = 0, TASKS_ENDED_EVENT = 1;

struct thread_pool {
    pthread_t *threads;

    int max_thread_count;
    int thread_count;
    struct thread_task **tasks;
    int unassigned_tasks_count;
    int running_tasks_count;
    int pushed_tasks_count;
    pthread_mutex_t lock;
    pthread_cond_t thread_update_cond;
    int thread_update_event; // TASK_APPEARS_EVENT | TASKS_ENDED_EVENT | NONE
};

void swap_tasks_in_pool(struct thread_task *task1, struct thread_task *task2) {
    struct thread_pool *parent_pool = task1->parent_pool;
    int index1 = task1->index_in_parent_pool;
    int index2 = task2->index_in_parent_pool;

    parent_pool->tasks[index1] = task2;
    task2->index_in_parent_pool = index1;

    parent_pool->tasks[index2] = task1;
    task1->index_in_parent_pool = index2;
}

void thread_task_join_no_lock(struct thread_task *task);

void *execute_thread(void *arg) {
    struct thread_pool *pool = (struct thread_pool *) arg;

    while (true) {
        pthread_mutex_lock(&pool->lock);
        while (pool->unassigned_tasks_count == 0) {

            pthread_cond_wait(&pool->thread_update_cond, &pool->lock);

            if (pool->thread_update_event == TASKS_ENDED_EVENT) {
                pthread_mutex_unlock(&pool->lock);
                return NULL;
            }
        }


        struct thread_task *task_to_execute = pool->tasks[0];

        pool->unassigned_tasks_count--;
        swap_tasks_in_pool(task_to_execute, pool->tasks[pool->unassigned_tasks_count]);

        pool->running_tasks_count++;
        task_to_execute->assigned = true;
        pthread_mutex_unlock(&pool->lock);

        void *result = task_to_execute->function(task_to_execute->arg);

        pthread_mutex_lock(&pool->lock);
        task_to_execute->result = result;
        pool->running_tasks_count--;
        pthread_cond_broadcast(&task_to_execute->has_just_finished_cond);
        task_to_execute->finished = true;

        if (task_to_execute->scheduled_for_deletion) {
            thread_task_join_no_lock(task_to_execute);
            thread_task_delete(task_to_execute);
        }

        pthread_mutex_unlock(&pool->lock);
    }
}

int thread_pool_new(int max_thread_count, struct thread_pool **pool) {
    if (max_thread_count <= 0 || max_thread_count > TPOOL_MAX_THREADS)
        return TPOOL_ERR_INVALID_ARGUMENT;

    struct thread_pool *new_pool = malloc(sizeof(struct thread_pool));
    *new_pool = (struct thread_pool) {
            .threads = malloc(max_thread_count * sizeof(pthread_t)),
            .max_thread_count = max_thread_count,
            .thread_count = 0,

            .tasks = malloc(TPOOL_MAX_TASKS * sizeof(struct thread_task *)),
            .unassigned_tasks_count = 0,
            .running_tasks_count = 0,

            .thread_update_event = NONE
    };

    pthread_mutex_init(&new_pool->lock, NULL);
    pthread_cond_init(&new_pool->thread_update_cond, NULL);

    *pool = new_pool;
    return SUCCESS;
}

int thread_pool_thread_count(const struct thread_pool *pool) {
    pthread_mutex_lock(&((struct thread_pool *) pool)->lock);
    int count = pool->thread_count;
    pthread_mutex_unlock(&((struct thread_pool *) pool)->lock);
    return count;
}

int thread_pool_delete(struct thread_pool *pool) {
    pthread_mutex_lock(&pool->lock);
    if (pool->unassigned_tasks_count > 0 || pool->running_tasks_count > 0) {
        pthread_mutex_unlock(&pool->lock);
        return TPOOL_ERR_HAS_TASKS;
    }

    pool->thread_update_event = TASKS_ENDED_EVENT;
    pthread_cond_broadcast(&pool->thread_update_cond);

    pthread_mutex_unlock(&pool->lock);

    for (int i = 0; i < pool->thread_count; ++i) {
        pthread_join(pool->threads[i], NULL);
    }


    pthread_cond_destroy(&pool->thread_update_cond);
    pthread_mutex_destroy(&pool->lock);
    free(pool->tasks);
    free(pool->threads);
    free(pool);

    return SUCCESS;
}

int thread_pool_push_task(struct thread_pool *pool, struct thread_task *task) {
    pthread_mutex_lock(&pool->lock);

    if (pool->pushed_tasks_count >= TPOOL_MAX_TASKS) {
        pthread_mutex_unlock(&pool->lock);
        return TPOOL_ERR_TOO_MANY_TASKS;
    }

    task->finished = false;
    task->assigned = false;
    task->parent_pool = pool;

    pool->tasks[pool->pushed_tasks_count++] = task;
    task->index_in_parent_pool = pool->pushed_tasks_count - 1;
    pool->unassigned_tasks_count++;
    if (pool->running_tasks_count == pool->thread_count && pool->thread_count < pool->max_thread_count) {
        // Create new thread
        pthread_create(&pool->threads[pool->thread_count++], NULL, execute_thread, pool);
    } else {
        // Use existing thread
        if (pool->unassigned_tasks_count == 1) {
            pool->thread_update_event = TASK_APPEARS_EVENT;
            pthread_cond_signal(&pool->thread_update_cond);
        }
    }
    swap_tasks_in_pool(pool->tasks[pool->unassigned_tasks_count - 1], task);
    pthread_mutex_unlock(&pool->lock);

    return SUCCESS;
}

int thread_task_new(struct thread_task **task, thread_task_f function, void *arg) {
    struct thread_task *new_task = malloc(sizeof(struct thread_task));
    *new_task = (struct thread_task) {
            .function = function,
            .arg = arg,
            .finished = false,
            .scheduled_for_deletion = false,
            .assigned = false,
            .parent_pool = NULL,
            .index_in_parent_pool = NONE
    };
    pthread_cond_init(&new_task->has_just_finished_cond, NULL);

    *task = new_task;
    return SUCCESS;
}

bool thread_task_is_finished(const struct thread_task *task) {
    if (task->parent_pool == NULL)
        return task->finished; // NOTE: Returns that the task is finished after it was joined but before it was pushed

    pthread_mutex_lock(&task->parent_pool->lock);
    bool is_finished = task->finished;
    pthread_mutex_unlock(&task->parent_pool->lock);

    return is_finished;
}

bool thread_task_is_running(const struct thread_task *task) {
    if (task->parent_pool == NULL)
        return false;

    pthread_mutex_lock(&task->parent_pool->lock);
    bool is_running = task->assigned && !task->finished;
    pthread_mutex_unlock(&task->parent_pool->lock);

    return is_running;
}

int thread_task_join(struct thread_task *task, void **result) {
    struct thread_pool *parent_pool = task->parent_pool;
    if (parent_pool == NULL)
        return TPOOL_ERR_TASK_NOT_PUSHED;

    pthread_mutex_lock(&parent_pool->lock);

    thread_task_join_no_lock(task);
    *result = task->result;

    pthread_mutex_unlock(&parent_pool->lock);
    return SUCCESS;
}

void thread_task_join_no_lock(struct thread_task *task) {
    struct thread_pool *parent_pool = task->parent_pool;
    while (!task->finished) {
        pthread_cond_wait(&task->has_just_finished_cond, &parent_pool->lock);
    }

    swap_tasks_in_pool(task, parent_pool->tasks[parent_pool->pushed_tasks_count - 1]);
    parent_pool->pushed_tasks_count--;

    task->parent_pool = NULL;
    task->index_in_parent_pool = NONE;
    task->assigned = false;
}


#ifdef NEED_TIMED_JOIN

const long nsec_in_sec = 1000000000;

const long nsec_in_millisecond = 1000000;
const long milliseconds_in_sec = 1000;

const long nsec_in_microsecond = 1000;
const long microseconds_in_sec = 1000000;

struct timespec add_timespec(const struct timespec spec1,
                             const struct timespec spec2) {
    struct timespec addition = {.tv_sec = spec1.tv_sec + spec2.tv_sec,
            .tv_nsec = spec1.tv_nsec + spec2.tv_nsec};
    if (addition.tv_nsec >= nsec_in_sec) {
        addition.tv_nsec -= nsec_in_sec;
        addition.tv_sec++;
    }
    return addition;
}

struct timespec milliseconds_to_timespec(long long ms) {
    long long nsec = ms * nsec_in_millisecond;
    struct timespec spec = {.tv_sec = nsec / nsec_in_sec,
            .tv_nsec = nsec % nsec_in_sec};
    return spec;
}

struct timespec now_as_timespec() {
    struct timespec ans;

    struct timeval now;
    gettimeofday(&now, NULL);
    ans.tv_sec = now.tv_sec;
    ans.tv_nsec = now.tv_usec * nsec_in_microsecond;
    return ans;
}

int
thread_task_timed_join(struct thread_task *task, double timeout, void **result) {
    if (task->parent_pool == NULL)
        return TPOOL_ERR_TASK_NOT_PUSHED;
    struct thread_pool *parent_pool = task->parent_pool;
    pthread_mutex_lock(&parent_pool->lock);

    struct timespec timeout_spec = add_timespec(
            now_as_timespec(),
            milliseconds_to_timespec((long long) (timeout * (double) milliseconds_in_sec)));

    while (!task->finished) {
        if (ETIMEDOUT == pthread_cond_timedwait(&task->has_just_finished_cond, &parent_pool->lock, &timeout_spec)) {
            pthread_mutex_unlock(&parent_pool->lock);
            return TPOOL_ERR_TIMEOUT;
        }
    }

    swap_tasks_in_pool(task, parent_pool->tasks[parent_pool->pushed_tasks_count - 1]);
    parent_pool->pushed_tasks_count--;

    task->parent_pool = NULL;
    task->index_in_parent_pool = NONE;
    task->assigned = false;

    *result = task->result;
    pthread_mutex_unlock(&parent_pool->lock);

    return SUCCESS;
}

#endif


int thread_task_delete(struct thread_task *task) {
    if (task->parent_pool != NULL)
        return TPOOL_ERR_TASK_IN_POOL;

    pthread_cond_destroy(&task->has_just_finished_cond);
    free(task);
    return SUCCESS;
}

#ifdef NEED_DETACH

int
thread_task_detach(struct thread_task *task) {
    if (task->parent_pool == NULL)
        return TPOOL_ERR_TASK_NOT_PUSHED;
    struct thread_pool *parent_pool = task->parent_pool;

    pthread_mutex_lock(&parent_pool->lock);
    if (task->finished) {
        thread_task_join_no_lock(task);
        thread_task_delete(task);
    } else {
        task->scheduled_for_deletion = true;
    }
    pthread_mutex_unlock(&parent_pool->lock);

    return SUCCESS;
}

#endif

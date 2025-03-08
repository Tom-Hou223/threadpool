#include "atomic.h"
#include <pthread.h>
#include "spinlock.h"
#include "stdlib.h"
#include <stdatomic.h>
#include <stdint.h>
#include "thrdpool.h"


//api
//typedef void (*handler_pt)(void*);


typedef struct task_s {
    void* next;
    handler_pt func;
    void* arg;
}task_t;

typedef struct spinlock spinlock_t;

typedef struct task_queue_s {
    void* head;
    void** tail;
    int block;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    spinlock_t lock;
}task_queue_t;

//thrdpool=taskqueue+threads+thrd_cnt
typedef struct thrdpool_s {
    task_queue_t* queue;
    atomic_int quit;
    pthread_t* threads;
    int thrd_cnt;
}thrdpool_t;


static task_queue_t* __taskqueue_create();
static void* __taskqueue_destroy(task_queue_t* queue);
static void* __thrdpool_worker(void* arg);
static int __threads_create(thrdpool_t* pool, int thrd_cnt);
static void* __threads_destroy(thrdpool_t* pool);
static void _add_task(task_queue_t* queue, void* task);
static task_t* _get_task(task_queue_t* queue);
static task_t* _pop_task(task_queue_t* queue);
void __nonblock(task_queue_t* queue);


// 资源创建 采用回滚式代码
thrdpool_t*
thrdpool_create(int thrd_count)
{
    thrdpool_t* pool;
    pool = (thrdpool_t*)malloc(sizeof(thrdpool_t));
    if(pool)
    {

        task_queue_t* queue = __taskqueue_create();
        if(queue)
        {
            pool->queue = queue;
            atomic_init(&pool->quit, 0);
            if(__threads_create(pool, thrd_count) == 0)
            {
                return pool;
            }
            __taskqueue_destroy(queue);
        }
        free(pool);
    }
    return NULL;
}

void
thrdpool_terminate(thrdpool_t* pool)
{
    atomic_store(&pool->quit, 1);
    __nonblock(pool->queue);
}

static task_queue_t*
__taskqueue_create()
{
    int ret;
    task_queue_t* queue;
    queue = (task_queue_t*)malloc(sizeof(task_queue_t));
    if(queue)
    {
        ret = pthread_mutex_init(&queue->mutex, NULL);
        if(ret == 0)
        {
            ret = pthread_cond_init(&queue->cond, NULL);
            if(ret == 0)
            {
                spinlock_init(&queue->lock);
                
                queue->head = NULL;
                queue->tail = &queue->head; // ?
                queue->block = 1;
                return queue;
                //pthread_cond_destroy(&queue->mutex);
            }
            pthread_mutex_destroy(&queue->mutex);
        }
        free(queue);
    }
}

static void*
__taskqueue_destroy(task_queue_t* queue)
{
    task_t* task;
    while((task = _pop_task(queue)))
    {
        free(task);
    }
    spinlock_unlock(&queue->lock);
    pthread_mutex_destroy(&queue->mutex);
    pthread_cond_destroy(&queue->cond);
    free(queue);
}

void
__nonblock(task_queue_t* queue)
{
    pthread_mutex_lock(&queue->mutex);
    queue->block = 0;
    pthread_mutex_unlock(&queue->mutex);
    pthread_cond_broadcast(&queue->cond);//通知所有的线程
}


static task_t*
_pop_task(task_queue_t* queue)
{
    spinlock_lock(&queue->lock);
    if(queue->head == NULL)
    {
        spinlock_unlock(&queue->lock);
        return NULL;
    }
    task_t* task;
    task = queue->head;

    void** link = (void**)task;
    queue->head = *link;

    if(queue->head == NULL)
    {
        queue->tail = &queue->head;
    }

    spinlock_unlock(&queue->lock);
    return task;
}


static void
_add_task(task_queue_t* queue, void* task)
{
    void** link = (void**)task;
    *link = NULL;

    spinlock_lock(&queue->lock);
    *queue->tail = link; // queue->tail->next=link;
    queue->tail = link;
    spinlock_unlock(&queue->lock);
    pthread_cond_signal(&queue->cond);
}


static task_t*
_get_task(task_queue_t* queue)
{
    task_t* task;

    while((task = _pop_task(queue)) == NULL)
    {
        pthread_mutex_lock(&queue->mutex);
        if(queue->block == 0)
        {
            pthread_mutex_unlock(&queue->mutex);
            return NULL;
        }

        pthread_mutex_unlock(&queue->mutex);
    }
    
    return task;
}


static int
__threads_create(thrdpool_t* pool, int thrd_cnt)
{
    pthread_attr_t attr;

    int ret = pthread_attr_init(&attr);
    if(ret == 0)
    {
        pool->threads = (pthread_t*)malloc(sizeof(pthread_t) * thrd_cnt);
        if(pool->threads)
        {
            int i = 0;
            for(i; i < thrd_cnt; i++)
            {
                if(!pthread_create(&pool->threads[i], &attr, __thrdpool_worker, pool))
                {
                    break;
                }
            }
            pool->thrd_cnt = i;
            pthread_attr_destroy(&attr);
            if(pool->thrd_cnt == i)
                return 0;
            thrdpool_terminate(pool);
            free(pool->threads);
        }
        ret = -1;
    }
    return ret;
}

static void*
__thrdpool_worker(void* arg)
{
    thrdpool_t* pool = (thrdpool_t*)arg;
    task_t* task;

    while (atomic_load(&pool->quit) == 0) {
        task = _get_task(pool->queue);
        if (task) {
            handler_pt func = task->func;
            void* ctx = task->arg;
            free(task);
            func(ctx);
        } else {
            // 队列为空且非阻塞，退出循环
            break;
        }
    }
    return NULL;
}


static void*
__threads_destroy(thrdpool_t* pool)
{
    atomic_store(&pool->quit, 1);
    __nonblock(pool->queue);
    int i = 0;
    for(i=0; i < pool->thrd_cnt; i++)
    {
        pthread_join(pool->threads[i], NULL);
    }
}


void
_thrdpool_waitdone(thrdpool_t* pool)
{
    int i;
    for(i = 0; i < pool->thrd_cnt; i++)
    {
        pthread_join(pool->threads[i], NULL);
    }
    __taskqueue_destroy(pool->queue); // 在__taskqueue_destroy()中，已经free(queue)了
    //free(pool->queue);
    free(pool->threads);
    free(pool);
}


int
thrdpool_post(thrdpool_t* pool, handler_pt func, void* arg)
{
    if(pool->quit == 1)
    {
        return -1;
    }

    task_t* task = (task_t*)malloc(sizeof(task_t));
    if(!task) return -1;
    task->func = func;
    task->arg = arg;
    _add_task(pool->queue, task);
    return 0;
}
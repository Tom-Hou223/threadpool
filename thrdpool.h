#ifndef _THRDPOOL_H_
#define _THRDPOOL_H_

#include <stddef.h>

typedef struct thrdpool_s thrdpool_t;
typedef void (*handler_pt)(void*);

// 用户可见的接口
thrdpool_t* thrdpool_create(int thrd_count);
void thrdpool_terminate(thrdpool_t* pool);
int thrdpool_post(thrdpool_t* pool, handler_pt func, void* arg);
void _thrdpool_waitdone(thrdpool_t* pool);

#endif
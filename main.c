#include "thrdpool.h"
#include "pthread.h"
#include "stdio.h"
#include "stdlib.h"


pthread_mutex_t lock;
int x = 0;


void to_do(void* arg)
{
    thrdpool_t* pool = (thrdpool_t*)arg;
    pthread_mutex_lock(&lock);
    x++;
    printf("doing %d tasks\n", x);
    pthread_mutex_unlock(&lock);
    if(x > 1000)
    {
        thrdpool_terminate(pool);
    }
}


void test()
{
    int threads_count = 8;
    pthread_mutex_init(&lock, NULL);
   thrdpool_t* pool = thrdpool_create(threads_count);
   if(pool == NULL)
   {
       printf("thread pool create error\n");
       exit(-1);
   }
   while(thrdpool_post(pool, &to_do, pool) == 0)
   {
       
   }

   _thrdpool_waitdone(pool);
   pthread_mutex_destroy(&lock);
}




int main()
{
    test();
    return 0;
}
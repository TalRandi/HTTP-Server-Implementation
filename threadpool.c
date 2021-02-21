
#include "threadpool.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

threadpool* create_threadpool(int num_threads_in_pool)
{
    //Input validation check
    if(num_threads_in_pool < 0 || num_threads_in_pool > MAXT_IN_POOL)
    {
        fprintf(stderr,"Usage: threadpool <pool-size> <max-number-of-jobs>\n");
        return NULL;
    }
    threadpool* pool = (threadpool*)malloc(sizeof(threadpool));
    if(!pool)
    {
        perror("error: Malloc failure\n");
        return NULL;
    }
    pool->num_threads = num_threads_in_pool;
    pool->qsize = 0;
    pool->threads = (pthread_t*)malloc((pool->num_threads)*sizeof(pthread_t));
    if(!pool->threads)
    {
        perror("error: Malloc failure\n");
        return NULL;
    }
    pool->qhead = NULL;
    pool->qtail = NULL;
    pool->shutdown = 0;
    pool->dont_accept = 0;
    pthread_mutex_init( &(pool->qlock), NULL);
    pthread_cond_init( &(pool->q_not_empty), NULL);
    pthread_cond_init( &(pool->q_empty), NULL);

    for(int i = 0 ; i < pool->num_threads ; i++)
        pthread_create(&(pool->threads[i]), NULL, do_work, pool);
    
    return pool;
}

void* do_work(void* p)
{
    threadpool* temp = (threadpool*)p;
    while(1)
    {
        pthread_mutex_lock(&(temp->qlock));
        if(temp->shutdown == 1) //Destroy has begun
        {
            pthread_mutex_unlock(&(temp->qlock));
            return NULL;         
        }
        //Empty queue
        if(temp->qsize == 0)
            pthread_cond_wait(&(temp->q_not_empty),&(temp->qlock));

        if(temp->shutdown == 1) //Another check if destroy has begun and the thread signaled by broadcast function
        { 
            pthread_mutex_unlock(&(temp->qlock));
            return NULL; 
        }       
        work_t* work_to_do;
        work_to_do = temp->qhead;
        
        if(temp->qtail == temp->qhead) //One node queue
        {  
            temp->qtail = NULL;
        }
        if(work_to_do)
        {
            temp->qhead = temp->qhead->next;        
            temp->qsize--;
        }
        if(temp->dont_accept == 1 && temp->qsize == 0) //Last job
            pthread_cond_signal(&temp->q_empty);
        pthread_mutex_unlock(&(temp->qlock)); 
        if(work_to_do)
        {
            work_to_do->routine(work_to_do->arg); //Thread takes the job
            free(work_to_do);
        }
    }
}

void dispatch(threadpool* from_me, dispatch_fn dispatch_to_here, void *arg)
{
    //Destroy has begun
    if(from_me->dont_accept == 1)
        return;
       
    work_t* work = (work_t*)malloc(sizeof(work_t));
    if(!work)
    {
        perror("error: Malloc failure\n");
        return;        
    }
    work->next = NULL;
    work->routine = dispatch_to_here;
    work->arg = arg;

    pthread_mutex_lock(&(from_me->qlock));
    //Add the work to the queue
    if(!from_me->qtail && !from_me->qhead) //Empty queue
    {
        from_me->qhead = work;
        from_me->qtail = work;
    }
    else //Any other case
    {
        from_me->qtail->next = work;
        from_me->qtail = work;
    }
    from_me->qsize++;
    pthread_mutex_unlock(&(from_me->qlock)); 
    pthread_cond_signal(&(from_me->q_not_empty)); 
}

void destroy_threadpool(threadpool* destroyme)
{
    pthread_mutex_lock(&(destroyme->qlock));
    destroyme->dont_accept = 1;
    if(destroyme->qsize != 0)
        pthread_cond_wait(&(destroyme->q_empty),&(destroyme->qlock));

    destroyme->shutdown = 1;

    pthread_mutex_unlock(&(destroyme->qlock)); 
    pthread_cond_broadcast(&(destroyme->q_not_empty));

    for(int i = 0 ; i < destroyme->num_threads ; i++)
        pthread_join(destroyme->threads[i], NULL);

    free(destroyme->threads);
    free(destroyme);
}
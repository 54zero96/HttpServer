//创建一个线程池，并阻塞等待任务。当任务队列中加入新的任务任务时，通知阻塞的线程取出任务执行。

#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/timeb.h>

#define MAX_THREAD 10
#define MAX_WORK 50

#include "thread_pool.h"


void *thread_routine(void *arg){
	pool_t *pool = (pool_t*)arg;
	while(1){
		pthread_mutex_lock(&pool->pool_sync);
		while(pool->cur_queue_size == 0){
			pthread_cond_wait(&pool->pool_ready, &pool->pool_sync);
		}
		pool->work_queue[pool->first].thread_execute(pool->work_queue[pool->first].arg); //取出任务队列中的任务并执行
		pool->first = (pool->first+1)%MAX_WORK; //更新任务队列
		pool->cur_queue_size--;
		pthread_mutex_unlock(&pool->pool_sync);
	}
}

void pool_init(pool_t *pool){
	pool->cur_queue_size = 0;
	pool->first = pool->last = 0;
	pthread_mutex_init(&pool->pool_sync, 0);  //pthread_mutex_init(mtx, 0) 这个函数有两个参数
	pthread_cond_init(&pool->pool_ready, 0);  // 同上
	for(int i = 0; i < MAX_THREAD; ++i){
		if(pthread_create(&pool->workers[i].id, NULL, thread_routine, pool) != 0){
			printf("Create thread error\n");
		}
	}
}

void pool_add_work(pool_t *pool, void *(*task)(void *arg), void *arg){
	pthread_mutex_lock(&pool->pool_sync);
	if(pool->cur_queue_size == MAX_WORK){
		printf("Task queue FULL!\n");
		pthread_mutex_unlock(&pool->pool_sync);
		return;
	}
	pool->work_queue[pool->last].thread_execute = task;
	pool->work_queue[pool->last].arg = arg;
	pool->last = (pool->last+1)%MAX_WORK;
	pool->cur_queue_size++;
	pthread_cond_signal(&pool->pool_ready);
	pthread_mutex_unlock(&pool->pool_sync);
}


// 测试模块
//=================================================================================================
void *myprintf(void *n){
	printf("%d\t", *(int *)n);        //printf()不在std空间中
//	fflush(stdout);
}


int main(void){
	pool_t pool;

	struct timeb st1, st2;
	long long t1,t2;
	ftime(&st1);


	pool_init(&pool);
//	int num[10000];
	for(int i = 0; i < 10000; ++i){
//		num[i] = i;
		pool_add_work(&pool, myprintf, &i);  //由于传入的是指针，例程函数溯源找到了i的本源导致了错误
	}
//	sleep(1);
//	printf("\n");
//	for(int j = 0; j < 20; ++j){
//		printf("%d ", *(int *)(pool.work_queue[j].arg));
//	}
//	pthread_cond_broadcast(&pool.pool_ready);
//	pthread_join(pool.workers[0].id, NULL);

	while(pool.cur_queue_size != 0);

/*
	for(int i = 0; i < 10000; ++i){
		printf("%d\t", i);
	}
*/
	ftime(&st2);
	t1 = (long long)st1.time*1000 + st1.millitm;
	t2 = (long long)st2.time*1000 + st2.millitm;
	long long i = t2 - t1;
	printf("%lld", i);

	sleep(1);
//	fflush(stdout);
}

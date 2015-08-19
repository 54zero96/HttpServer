typedef struct worker_s worker_t;
typedef struct work_s work_t;
typedef struct pool_s pool_t;

struct worker_s{
	pthread_t id;
};

struct work_s{
	void *(*thread_execute)(void *arg);
	void *arg;
};

struct pool_s{
	worker_t workers[MAX_THREAD];
	work_t work_queue[MAX_WORK];   //使用环形缓冲区放置任务
	int cur_queue_size;
	int first, last;       //分别标示任务队列的队首，队尾
	pthread_mutex_t pool_sync;
	pthread_cond_t pool_ready;
};

void pool_init(pool_t *pool);
void pool_add_work(pool_t *pool, void *(*task)(void *arg), void *arg);

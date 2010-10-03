#ifndef WIN_PTHREADS_RWLOCK_H
#define WIN_PTHREADS_RWLOCK_H

#define LIFE_RWLOCK 0xBAB1F0ED
#define DEAD_RWLOCK 0xDEADB0EF

#define CHECK_RWLOCK(l)  { \
    if (!(l) || !*l \
		|| ( ((rwlock_t *)(*l))->valid != (unsigned int)LIFE_RWLOCK ) ) \
        return EINVAL; }

#ifndef SRWLOCK_INIT
#define RTL_SRWLOCK_INIT {0}   
typedef struct _RTL_SRWLOCK {                            
        PVOID Ptr;                                       
} RTL_SRWLOCK, *PRTL_SRWLOCK;                            
typedef RTL_SRWLOCK SRWLOCK, *PSRWLOCK;
#endif

typedef struct rwlock_t rwlock_t;
struct rwlock_t {
    unsigned int valid;
#ifdef USE_RWLOCK_pthread_cond
    int readers;
    int writers;
    int readers_count;	/* Number of waiting readers,  */
    int writers_count;	/* Number of waiting writers.  */
    pthread_mutex_t m;
    pthread_cond_t cr;
    pthread_cond_t cw;
#else
	SRWLOCK l;
#endif
};


#endif

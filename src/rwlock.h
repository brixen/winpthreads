#ifndef WIN_PTHREADS_RWLOCK_H
#define WIN_PTHREADS_RWLOCK_H

#define LIFE_RWLOCK 0xBAB1F0ED
#define DEAD_RWLOCK 0xDEADB0EF

#define CHECK_RWLOCK(l)  { \
    if (!(l) || !*l \
		|| ( ((rwlock_t *)(*l))->valid != (unsigned int)LIFE_RWLOCK ) ) \
        return EINVAL; }

#ifdef USE_RWLOCK_SRWLock
#ifndef SRWLOCK_INIT
#define RTL_SRWLOCK_INIT {0}   
#define SRWLOCK_INIT RTL_SRWLOCK_INIT

typedef struct _RTL_SRWLOCK {                            
        PVOID Ptr;                                       
} RTL_SRWLOCK, *PRTL_SRWLOCK;  

typedef RTL_SRWLOCK SRWLOCK, *PSRWLOCK;

WINBASEAPI VOID WINAPI InitializeSRWLock(PSRWLOCK SRWLock);
WINBASEAPI VOID WINAPI ReleaseSRWLockExclusive(PSRWLOCK SRWLock);
WINBASEAPI VOID WINAPI ReleaseSRWLockShared(PSRWLOCK SRWLock);
WINBASEAPI VOID WINAPI AcquireSRWLockExclusive(PSRWLOCK SRWLock);
WINBASEAPI VOID WINAPI AcquireSRWLockShared(PSRWLOCK SRWLock);
WINBASEAPI BOOLEAN WINAPI TryAcquireSRWLockExclusive(PSRWLOCK SRWLock);
WINBASEAPI BOOLEAN WINAPI TryAcquireSRWLockShared(PSRWLOCK SRWLock);
#endif /* SRWLOCK_INIT */
#endif /* USE_RWLOCK_SRWLock */

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

#ifndef WIN_PTHREADS_RWLOCK_H
#define WIN_PTHREADS_RWLOCK_H

#define LIFE_RWLOCK 0xBAB1F0ED
#define DEAD_RWLOCK 0xDEADB0EF

#ifdef USE_RWLOCK_SRWLock
#define COND_RWL_LOCKED(r)	 (*(void **) &r->l)
#else
#define COND_RWL_LOCKED(r)	(r->readers||r->writers||r->readers_count||r->writers_count)
#endif

#define INIT_RWLOCK(rwl)  { int r; \
    if (STATIC_RWL_INITIALIZER(*rwl)) { if ((r = rwlock_static_init(rwl))) return r; }}

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

#ifdef USE_RWLOCK_SRWLock
#define AT_MOST_1(x)				((x)?1:0)

#ifndef RTL_SRWLOCK_BITS
#define RTL_SRWLOCK_OWNED_BIT		0
#define RTL_SRWLOCK_CONTENDED_BIT   1
#define RTL_SRWLOCK_SHARED_BIT		2
#define RTL_SRWLOCK_CONTENTION_LOCK_BIT 3
#define RTL_SRWLOCK_OWNED			(1 << RTL_SRWLOCK_OWNED_BIT)
#define RTL_SRWLOCK_CONTENDED		(1 << RTL_SRWLOCK_CONTENDED_BIT)
#define RTL_SRWLOCK_SHARED			(1 << RTL_SRWLOCK_SHARED_BIT)
#define RTL_SRWLOCK_CONTENTION_LOCK (1 << RTL_SRWLOCK_CONTENTION_LOCK_BIT)
#define RTL_SRWLOCK_MASK			(RTL_SRWLOCK_OWNED | RTL_SRWLOCK_CONTENDED | \
                                    RTL_SRWLOCK_SHARED | RTL_SRWLOCK_CONTENTION_LOCK)
#define RTL_SRWLOCK_BITS    4
#endif /* RTL_SRWLOCK_BITS */
#define RTL_SRWLOCK_LOCKED   (RTL_SRWLOCK_CONTENDED|RTL_SRWLOCK_SHARED|RTL_SRWLOCK_CONTENTION_LOCK)

#endif /* USE_RWLOCK_SRWLock */

#define STATIC_RWL_INITIALIZER(x)		((pthread_rwlock_t)(x) == ((pthread_rwlock_t)PTHREAD_RWLOCK_INITIALIZER))

typedef struct rwlock_t rwlock_t;
struct rwlock_t {
    unsigned int valid;
    int busy;
#ifdef USE_RWLOCK_pthread_cond
    LONG readers_count;	/* Number of waiting readers,  */
    LONG writers_count;	/* Number of waiting writers.  */
    int readers;
    int writers;
    pthread_mutex_t m;
    pthread_cond_t cr;
    pthread_cond_t cw;
#else
    LONG treaders_count;	/* Number of waiting timed readers,  */
    LONG twriters_count;	/* Number of waiting timed writers.  */
    HANDLE semTimedR;
    HANDLE semTimedW;
    SRWLOCK l;
#endif
};

#define RWL_SET	0x01
#define RWL_TRY	0x02

void rwl_print(volatile pthread_rwlock_t *rwl, char *txt);
void rwl_print_set(int state);

inline int rwlock_static_init(volatile pthread_rwlock_t *r);
inline int rwl_check_owner(pthread_rwlock_t *rwl, int flags);
inline int rwl_unset_owner(pthread_rwlock_t *rwl, int flags);
#endif

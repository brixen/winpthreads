#ifndef WIN_PTHREADS_MUTEX_H
#define WIN_PTHREADS_MUTEX_H

#ifdef USE_MUTEX_CriticalSection
#define COND_LOCKED(m)	(((_tid_u)m->cs.rc.OwningThread).tid != 0)
#define COND_OWNER(m)	(((_tid_u)m->cs.rc.OwningThread).tid == GetCurrentThreadId())
#define COND_DEADLK(m)	COND_OWNER(m)
#define GET_OWNER(m)	(((_tid_u)m->cs.rc.OwningThread).tid)
#define GET_HANDLE(m)	(m->cs.rc.LockSemaphore)
#define GET_LOCKCNT(m)	(m->cs.rc.LockCount)
#define GET_RCNT(m)		(m->cs.rc.RecursionCount)
#define SET_OWNER(m)
#define UNSET_OWNER(m)
#define LOCK_UNDO(m)	_UndoWaitCriticalSection(&m->cs.rc)
#else
#define COND_LOCKED(m)	(m->owner != 0)
#define COND_OWNER(m)	(m->owner == GetCurrentThreadId())
#define COND_DEADLK(m)	COND_OWNER(m)
#define GET_OWNER(m)	(m->owner)
#define GET_HANDLE(m)	(m->h)
#define GET_LOCKCNT(m)	(m->count)
#define GET_RCNT(m)	(m->count) /* not accurate! */
#define SET_OWNER(m)	(m->owner = GetCurrentThreadId())
#define UNSET_OWNER(m)	{ m->owner = 0; }
#define LOCK_UNDO(m)
#endif
#define COND_DEADLK_NR(m)	((m->type != PTHREAD_MUTEX_RECURSIVE) && COND_DEADLK(m))
#define CHECK_DEADLK(m)		{ if (COND_DEADLK_NR(m)) return EDEADLK; }

#define STATIC_INITIALIZER(x)		((pthread_mutex_t)(x) >= ((pthread_mutex_t)PTHREAD_RECURSIVE_MUTEX_INITIALIZER))
#define MUTEX_INITIALIZER2TYPE(x)	((LONGBAG)PTHREAD_NORMAL_MUTEX_INITIALIZER - (LONGBAG)(x))

#define CHECK_MUTEX(m)  { \
    if (!(m) || !*m || (STATIC_INITIALIZER(*m)) \
        || ( ((mutex_t *)(*m))->valid != (unsigned int)LIFE_MUTEX ) ) \
        return EINVAL; }

#define INIT_MUTEX(m)  { int r; \
    if (STATIC_INITIALIZER(*m)) { if ((r = mutex_static_init(m))) return r; }}

#define LIFE_MUTEX 0xBAB1F00D
#define DEAD_MUTEX 0xDEADBEEF

#if defined USE_MUTEX_CriticalSection
typedef union _csu _csu;
union _csu {
    RTL_CRITICAL_SECTION rc;
    CRITICAL_SECTION cs;
};

typedef union _tid_u _tid_u;
union _tid_u {
    HANDLE	h;
    DWORD	tid;
};

#endif

typedef struct mutex_t mutex_t;
struct mutex_t
{
    LONG valid;   
    volatile LONG busy;   
    int type;
    volatile LONG count;
#if defined USE_MUTEX_Mutex
    LONG lockOwner;
    DWORD owner;
    HANDLE h;
#else /* USE_MUTEX_CriticalSection.  */
    _csu cs;
#endif
};

inline int mutex_static_init(volatile pthread_mutex_t *m);
int _mutex_trylock(pthread_mutex_t *m);
void mutex_print(volatile pthread_mutex_t *m, char *txt);
void mutex_print_set(int state);

#endif

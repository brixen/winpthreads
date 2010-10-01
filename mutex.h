#ifndef WIN_PTHREADS_MUTEX_H
#define WIN_PTHREADS_MUTEX_H

#ifdef USE_MUTEX_CriticalSection
#define CHECK_DEADLK(m)	if ((m->type != PTHREAD_MUTEX_RECURSIVE) && \
							(((_tid_u)m->cs.OwningThread).tid == GetCurrentThreadId()) ) \
							return EDEADLK
#else
#define CHECK_DEADLK(m)	if ((m->type != PTHREAD_MUTEX_RECURSIVE) && \
							(m->owner == GetCurrentThreadId()) ) \
							return EDEADLK
#endif
#define SET_OWNER(m)	if (m->type != PTHREAD_MUTEX_RECURSIVE) \
							m->owner = GetCurrentThreadId()

#define UNSET_OWNER(m)	m->owner = 0

#define CHECK_MUTEX(m)  { \
    if (!(m) || !*m || (*m == PTHREAD_MUTEX_INITIALIZER) \
		|| ( ((mutex_t *)(*m))->valid != (unsigned int)LIFE_MUTEX ) ) \
        return EINVAL; }

#define INIT_MUTEX(m)  { int r; \
    if (!m || !*m)	return EINVAL; \
	if (*m == PTHREAD_MUTEX_INITIALIZER) if ((r = mutex_static_init(m))) return r; \
	if ( ( ((mutex_t *)(*m))->valid != (unsigned int)LIFE_MUTEX ) ) return EINVAL; }

#define LIFE_MUTEX 0xBAB1F00D
#define DEAD_MUTEX 0xDEADBEEF

typedef struct mutex_t mutex_t;
struct mutex_t
{
    unsigned int valid;   
    int type;   
	DWORD owner;
#if defined USE_MUTEX_Mutex
    HANDLE h;
#else /* USE_MUTEX_CriticalSection.  */
    CRITICAL_SECTION cs;
#endif
};

#if defined USE_MUTEX_CriticalSection
struct _pthread_crit_t
{
	void *debug;
	LONG count;
	LONG r_count;
	HANDLE owner;
	HANDLE sem;
	ULONG_PTR spin;
};

typedef union _pthread_crit_u _pthread_crit_u;
union _pthread_crit_u {
	struct _pthread_crit_t *pc;
	CRITICAL_SECTION *cs;
};

typedef union _tid_u _tid_u;
union _tid_u {
	HANDLE	h;
	DWORD	tid;
};

#endif

inline int mutex_static_init(volatile pthread_mutex_t *m);

#endif

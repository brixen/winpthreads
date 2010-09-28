#include <stdio.h>
#include "pthreads.h"
#include "mutex.h"
#include "misc.h"


#if defined USE_MUTEX_Mutex
int pthread_mutex_lock(pthread_mutex_t *m)
{
    CHECK_MUTEX(m);
	CHECK_DEADLK(m);
    switch (WaitForSingleObject(m->h, INFINITE)) {
        case WAIT_ABANDONED:
			printf("pthread_mutex_unlock WAIT_ABANDONED GetLastError %d\n",GetLastError());
            return EINVAL;
            break;
        case WAIT_OBJECT_0:
			SET_OWNER(m);
            return 0;
            break;
        case WAIT_FAILED:
			printf("pthread_mutex_unlock WAIT_FAILED GetLastError %d\n",GetLastError());
            return EINVAL;
            break;
    }
	printf("pthread_mutex_unlock GetLastError %d\n",GetLastError());
    return EINVAL;
}

int pthread_mutex_timedlock(pthread_mutex_t *m, struct timespec *ts)
{
    unsigned long long t, ct;
	int r;

    CHECK_MUTEX(m);
    CHECK_PTR(ts);

    /* Try to lock it without waiting */
    if ( !(r=pthread_mutex_trylock(m)) ) return 0;
	if ( r != EBUSY ) return r;
    ct = _pthread_time_in_ms();
    t = _pthread_time_in_ms_from_timespec(ts);

    while (1)
    {
        /* Have we waited long enough? */
        if (ct >= t) return ETIMEDOUT;
        switch (WaitForSingleObject(m->h, dwMilliSecs(t - ct))) {
            case WAIT_TIMEOUT:
                break;
            case WAIT_ABANDONED:
                return EINVAL;
                break;
            case WAIT_OBJECT_0:
				SET_OWNER(m);
                return 0;
                break;
            case WAIT_FAILED:
                return EINVAL;
                break;
        }
        /* Get current time */
        ct = _pthread_time_in_ms();
    }
    return 0;
}

int pthread_mutex_unlock(pthread_mutex_t *m)
{
    CHECK_MUTEX(m);
    if (!ReleaseMutex(m->h)) {
		printf("pthread_mutex_unlock GetLastError %d\n",GetLastError());
        return EPERM;
    }
	UNSET_OWNER(m);
    return 0;
}

int pthread_mutex_trylock(pthread_mutex_t *m)
{
    CHECK_MUTEX(m);
	CHECK_DEADLK(m);
    switch (WaitForSingleObject(m->h, 0)) {
        case WAIT_TIMEOUT:
            return EBUSY;
            break;
        case WAIT_ABANDONED:
            return EINVAL;
            break;
        case WAIT_OBJECT_0:
			SET_OWNER(m);
            return 0;
            break;
        case WAIT_FAILED:
            return EINVAL;
            break;
    }
    return EINVAL;
}

int pthread_mutex_init(pthread_mutex_t *m, pthread_mutexattr_t *a)
{
 	int r = 0;
    (void) a;

    if (!m) return EINVAL;
    if (m->valid) return EBUSY;
   
	m->type = PTHREAD_MUTEX_DEFAULT;
	if (a) {
		r = pthread_mutexattr_gettype(a, &m->type);
	}
	if (!r) {
		if ( (m->h = CreateMutex(NULL, FALSE, NULL)) == NULL) {
			switch (GetLastError()) {
			case ERROR_ALREADY_EXISTS:
					r = EBUSY;
					break;
			case ERROR_ACCESS_DENIED:
					r = EPERM;
					break;
			default: /* OK, this might not be accurate, but .. */
					r = ENOMEM;
			}
		}
	}
	if (!r) {
		m->valid = 1;
		m->owner = NULL;
	}

    return r;
}

int pthread_mutex_destroy(pthread_mutex_t *m)
{
    CHECK_MUTEX(m);
	if (m->owner) {
		/* Works only in non-recursive case currently */
		return EBUSY;
	}
    CloseHandle(m->h);
    m->type = m->valid = 0;
    return 0;
}
#else /* USE_MUTEX_CriticalSection */
int pthread_mutex_lock(pthread_mutex_t *m)
{
	CHECK_DEADLK(m);
	EnterCriticalSection(&m->cs);
	SET_OWNER(m);
	return 0;
}

int pthread_mutex_timedlock(pthread_mutex_t *m, struct timespec *ts)
{
	int r;
	unsigned long long t, ct;
	
	struct _pthread_crit_t
	{
		void *debug;
		LONG count;
		LONG r_count;
		HANDLE owner;
		HANDLE sem;
		ULONG_PTR spin;
	};
	
	/* Try to lock it without waiting */
    if ( !(r=pthread_mutex_trylock(m)) ) return 0;
	if ( r != EBUSY ) return r;
	
	ct = _pthread_time_in_ms();
	t = _pthread_time_in_ms_from_timespec(ts);
	
	while (1)
	{
	  	union __cast {
		  struct _pthread_crit_t *pc;
		  CRITICAL_SECTION *cs;
		} c;
		/* Have we waited long enough? */
		if (ct > t) return ETIMEDOUT;
		
		c.cs = &m->cs;
		/* Wait on semaphore within critical section */
		WaitForSingleObject(c.pc->sem, t - ct);
		
		/* Try to grab lock */
		if (!pthread_mutex_trylock(m)) return 0;
		
		/* Get current time */
		ct = _pthread_time_in_ms();
	}
	SET_OWNER(m);
	return 0;
}

int pthread_mutex_unlock(pthread_mutex_t *m)
{
	LeaveCriticalSection(&m->cs);
	UNSET_OWNER(m);
	return 0;
}
	
int pthread_mutex_trylock(pthread_mutex_t *m)
{
	CHECK_DEADLK(m);
	int r = TryEnterCriticalSection(&m->cs) ? 0 : EBUSY;
	if (!r) SET_OWNER(m);
	return r;
}

int pthread_mutex_init(pthread_mutex_t *m, pthread_mutexattr_t *a)
{
	int r = 0;
	(void) a;
	m->type = PTHREAD_MUTEX_DEFAULT;
	if (a) {
		r = pthread_mutexattr_gettype(a, &m->type);
	}
	if (!r) {
		InitializeCriticalSection(&m->cs);
		m->valid = 1;
		m->owner = NULL;
	}
	
	return r;
}

int pthread_mutex_destroy(pthread_mutex_t *m)
{
	if (m->owner) {
		/* Works only in non-recursive case currently */
		return EBUSY;
	}
    m->type = m->valid = 0;
	DeleteCriticalSection(&m->cs);
	return 0;
}
#endif /* USE_MUTEX_Mutex */

int pthread_mutexattr_init(pthread_mutexattr_t *a)
{
    *a = 0;
    return 0;
}

int pthread_mutexattr_destroy(pthread_mutexattr_t *a)
{
    (void) a;
    return 0;
}

int pthread_mutexattr_gettype(pthread_mutexattr_t *a, int *type)
{
    *type = *a & 3;

    return 0;
}

int pthread_mutexattr_settype(pthread_mutexattr_t *a, int type)
{
    if ((unsigned) type > 3) return EINVAL;
	/* We support DEFAULT==ERRORCHECK semantics + RECURSIVE: */
    if ((unsigned) type == PTHREAD_MUTEX_NORMAL) return ENOTSUP;
    *a &= ~3;
    *a |= type;

    return 0;
}

int pthread_mutexattr_getpshared(pthread_mutexattr_t *a, int *type)
{
    *type = *a & 4;

    return 0;
}

int pthread_mutexattr_setpshared(pthread_mutexattr_t * a, int type)
{
    if ((type & 4) != type) return EINVAL;

    *a &= ~4;
    *a |= type;

    return 0;
}

int pthread_mutexattr_getprotocol(pthread_mutexattr_t *a, int *type)
{
    *type = *a & (8 + 16);

    return 0;
}

int pthread_mutexattr_setprotocol(pthread_mutexattr_t *a, int type)
{
    if ((type & (8 + 16)) != 8 + 16) return EINVAL;

    *a &= ~(8 + 16);
    *a |= type;

    return 0;
}

int pthread_mutexattr_getprioceiling(pthread_mutexattr_t *a, int * prio)
{
    *prio = *a / PTHREAD_PRIO_MULT;
    return 0;
}

int pthread_mutexattr_setprioceiling(pthread_mutexattr_t *a, int prio)
{
    *a &= (PTHREAD_PRIO_MULT - 1);
    *a += prio * PTHREAD_PRIO_MULT;

    return 0;
}

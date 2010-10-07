#include <windows.h>
#include <stdio.h>
#include "pthread.h"
#include "mutex.h"
#include "misc.h"

inline int mutex_static_init(volatile pthread_mutex_t *m )
{
    pthread_mutex_t m_tmp;
	mutex_t *mi, *m_replaced;

	if ( !STATIC_INITIALIZER(mi = (mutex_t *)*m) ) {
		/* Assume someone crept in between: */
		return 0;
	}

	int r = pthread_mutex_init(&m_tmp, NULL);
	((mutex_t *)m_tmp)->type = MUTEX_INITIALIZER2TYPE(mi);
	if (!r) {
		m_replaced = (mutex_t *)InterlockedCompareExchangePointer(
			(PVOID *)m, 
			m_tmp,
			mi);
		if (m_replaced != mi) {
			/* someone crept in between: */
			pthread_mutex_destroy(&m_tmp);
			/* it could even be destroyed: */
			if (!m_replaced) r = EINVAL;
		}
	}
	return r;
}

static int mutex_normal_handle_deadlk(mutex_t *_m)
{
	/* Now hang in the semaphore until released by another thread */
	/* The latter is undefined behaviour and thus NOT portable */
	InterlockedExchange(&_m->lockExt, 1);
	while(WaitForSingleObject(_m->semExt, INFINITE));
	if (!COND_OWNER(_m)) return EINVAL; /* or should this be an assert ? */
	return 0;
}

static int mutex_normal_handle_undefined(mutex_t *_m)
{
	/* Release the semaphore waited on by another thread */
	/* This is undefined behaviour and thus NOT portable */
	if (InterlockedExchange(&_m->lockExt, 0)) {
		/* only 1 unlock should actually release the semaphore */
		ReleaseSemaphore(_m->semExt, 1, NULL);
	}
	return 0;
}

int pthread_mutex_lock(pthread_mutex_t *m)
{
	INIT_MUTEX(m);
	mutex_t *_m = (mutex_t *)*m;

	if (_m->type == PTHREAD_MUTEX_NORMAL) {
		if (COND_DEADLK(_m)) {
			return mutex_normal_handle_deadlk(_m);
		}
	} else {
		CHECK_DEADLK(_m);
	}
#if defined USE_MUTEX_Mutex
    switch (WaitForSingleObject(_m->h, INFINITE)) {
        case WAIT_ABANDONED:
            return EINVAL;
            break;
        case WAIT_OBJECT_0:
			/* OK */
            break;
        case WAIT_FAILED:
            return EINVAL;
            break;
		default:
            return EINVAL;
            break;
    }
#else /* USE_MUTEX_CriticalSection */
	EnterCriticalSection(&_m->cs);
#endif
	SET_OWNER(_m);
	return 0;

}

int pthread_mutex_timedlock(pthread_mutex_t *m, struct timespec *ts)
{
	int r;
	unsigned long long t, ct;
	
	if (!ts) return EINVAL; 
	
	/* Try to lock it without waiting */
	/* And also call INIT_MUTEX() for us */
    if ( !(r=pthread_mutex_trylock(m)) ) return 0;
	if ( r != EBUSY ) return r;
	
	mutex_t *_m = (mutex_t *)*m;
	if (_m->type == PTHREAD_MUTEX_NORMAL) {
		if (COND_DEADLK(_m)) {
			return mutex_normal_handle_deadlk(_m);
		}
	} else {
		CHECK_DEADLK(_m);
	}
	ct = _pthread_time_in_ms();
	t = _pthread_time_in_ms_from_timespec(ts);
	
    while (1)
    {
 #if defined USE_MUTEX_Mutex
		/* Have we waited long enough? */
        if (ct >= t) return ETIMEDOUT;
        switch (WaitForSingleObject(_m->h, dwMilliSecs(t - ct))) {
            case WAIT_TIMEOUT:
                break;
            case WAIT_ABANDONED:
                return EINVAL;
                break;
            case WAIT_OBJECT_0:
				/* OK */
                break;
            case WAIT_FAILED:
                return EINVAL;
                break;
        }
#else /* USE_MUTEX_CriticalSection */
		_pthread_crit_u cu;

		/* Have we waited long enough? */
		if (ct > t) return ETIMEDOUT;
		
		cu.cs = &_m->cs;
		/* Wait on semaphore within critical section */
		WaitForSingleObject(cu.pc->sem, t - ct);
		
		/* Try to grab lock */
		if (!pthread_mutex_trylock(m)) return 0;
#endif
		/* Get current time */
        ct = _pthread_time_in_ms();
    }
	SET_OWNER(_m);
	return 0;
}

int pthread_mutex_unlock(pthread_mutex_t *m)
{
	CHECK_MUTEX(m); 
	mutex_t *_m = (mutex_t *)*m;
	if (_m->type == PTHREAD_MUTEX_NORMAL) {
		if (!COND_LOCKED(_m)) {
			return EPERM;
		} else if (!COND_OWNER(_m)){
			return mutex_normal_handle_undefined(_m);
		}
	} else if (!COND_OWNER(_m)){
		return EPERM;
	}

	UNSET_OWNER(_m);
#if defined USE_MUTEX_Mutex
	if (!ReleaseMutex(_m->h)) {
		printf("pthread_mutex_unlock GetLastError %d\n",GetLastError());
        return EPERM;
    }
#else /* USE_MUTEX_CriticalSection */
#ifdef WINPTHREAD_DBG
	_pthread_crit_u cu;
	cu.cs = &_m->cs;
	printf("owner before LeaveCriticalSection: %ld\n",cu.cs->OwningThread);
#endif
	LeaveCriticalSection(&_m->cs);
#ifdef WINPTHREAD_DBG
	printf("owner after LeaveCriticalSection: %ld\n",cu.cs->OwningThread);
#endif
#endif
    return 0;
}

int pthread_mutex_trylock(pthread_mutex_t *m)
{
	int r=0;
	INIT_MUTEX(m); 
	mutex_t *_m = (mutex_t *)*m;
	if ((_m->type != PTHREAD_MUTEX_RECURSIVE) && COND_DEADLK(_m)) {
		return EBUSY;
	}
#if defined USE_MUTEX_Mutex
	switch (WaitForSingleObject(_m->h, 0)) {
        case WAIT_TIMEOUT:
            r = EBUSY;
            break;
        case WAIT_ABANDONED:
            r = EINVAL;
            break;
        case WAIT_OBJECT_0:
			/* OK */
            break;
        case WAIT_FAILED:
            r = EINVAL;
            break;
    }
    return EINVAL;
#else /* USE_MUTEX_CriticalSection */
	r = TryEnterCriticalSection(&_m->cs) ? 0 : EBUSY;
#endif
	if (!r) SET_OWNER(_m);
	return r;
}

int pthread_mutex_init(pthread_mutex_t *m, pthread_mutexattr_t *a)
{
	mutex_t *_m;

	int r = 0;
	(void) a;

	if (!m)	return EINVAL; 
	if ( !(_m = (pthread_mutex_t)malloc(sizeof(*_m))) ) {
		return ENOMEM; 
	}
	_m->valid		= DEAD_MUTEX;
#if defined USE_MUTEX_Mutex
	_m->owner		= 0;
	_m->lockOwner	= 0;
#endif
	_m->semExt		= NULL;
	_m->lockExt		= 0;

	_m->type = PTHREAD_MUTEX_DEFAULT; 
	if (a) {
		r = pthread_mutexattr_gettype(a, &_m->type);
	}
	if (!r) {
#if defined USE_MUTEX_Mutex
		if ( (_m->h = CreateMutex(NULL, FALSE, NULL)) != NULL) {
#else /* USE_MUTEX_CriticalSection */
		if (InitializeCriticalSectionAndSpinCount(&_m->cs, USE_MUTEX_CriticalSection_SpinCount)) {
#endif
		if (_m->type == PTHREAD_MUTEX_NORMAL) {
				/* Prevent multiple (external) unlocks from messing up the semaphore signal state.
				 * Setting lMaximumCount to 1 is not enough, we also use _m->lockExt.
				 */
				_m->semExt = CreateSemaphore (NULL, 0, 1, NULL);
				if (!_m->semExt) {
#if defined USE_MUTEX_Mutex
					CloseHandle(_m->h);
#else /* USE_MUTEX_CriticalSection */
					DeleteCriticalSection(&_m->cs);
#endif
					r = ENOMEM;
				}
			}
		} else {
#if defined USE_MUTEX_Mutex
			switch (GetLastError()) {
			case ERROR_ACCESS_DENIED:
					r = EPERM;
					break;
			default: /* We assume this, to keep it simple: */
					r = ENOMEM;
			}
#else /* USE_MUTEX_CriticalSection */
			r = ENOMEM;
#endif
		}
	} 
	if (r) {
		free(_m);
	} else {
		_m->valid		= LIFE_MUTEX;
		*m = _m;
	}

    return r;
}

int pthread_mutex_destroy(pthread_mutex_t *m)
{
	mutex_t *_m = (mutex_t *)*m;
	int r = 0;

	if (!m || !*m)	return EINVAL; 
	if (STATIC_INITIALIZER(*m)) {
		*m = NULL;
		return 0;
	}
	if (_m->valid != LIFE_MUTEX) {
		return EINVAL;
	}
	if ((r = pthread_mutex_trylock(m))) {
		return r; /* EBUSY, likely */
	}
	pthread_mutex_t *m2 = m;
	*m = NULL; /* dereference first, free later */
	_ReadWriteBarrier();
	pthread_mutex_unlock(m2);
    _m->type  = 0;
    _m->valid  = DEAD_MUTEX;
	if (_m->semExt) {
		CloseHandle(_m->semExt);
	}
#if defined USE_MUTEX_Mutex
	CloseHandle(_m->h);
#else /* USE_MUTEX_CriticalSection */
	DeleteCriticalSection(&_m->cs);
#endif
	free(*m2);
	return 0;
}

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

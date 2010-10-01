#include <windows.h>
#include <stdio.h>
#include "pthread.h"
#include "mutex.h"
#include "misc.h"

inline int mutex_static_init(volatile pthread_mutex_t *m)
{
    pthread_mutex_t m_tmp, *m_replaced;
	int r = pthread_mutex_init(&m_tmp, NULL);

	if (!r) {
		m_replaced = (pthread_mutex_t *)InterlockedCompareExchangePointer(
			(PVOID *)m, 
			m_tmp,
			PTHREAD_MUTEX_INITIALIZER);
		if (m_replaced != (pthread_mutex_t *)PTHREAD_MUTEX_INITIALIZER) {
			printf("mutex_static_init race detected: mutex %p\n",  m_replaced);
			/* someone crept in between: */
			pthread_mutex_destroy(&m_tmp);
			/* it could even be destroyed: */
			if (!m_replaced) r = EINVAL;
		}
	}
	return r;
}

#if defined USE_MUTEX_Mutex
int pthread_mutex_lock(pthread_mutex_t *m)
{
	INIT_MUTEX(m);
	mutex_t *_m = (mutex_t *)*m;
	CHECK_DEADLK(_m);
    switch (WaitForSingleObject(_m->h, INFINITE)) {
        case WAIT_ABANDONED:
            return EINVAL;
            break;
        case WAIT_OBJECT_0:
			SET_OWNER(_m);
            return 0;
            break;
        case WAIT_FAILED:
            return EINVAL;
            break;
    }
    return EINVAL;
}

int pthread_mutex_timedlock(pthread_mutex_t *m, struct timespec *ts)
{
	int r;
	unsigned long long t, ct;
	
	if (!m)	return EINVAL; 
	if (!ts) return EINVAL; 
	mutex_t *_m = (mutex_t *)*m;
	
	/* Try to lock it without waiting */
	/* And also call INIT_MUTEX() for us */
    if ( !(r=pthread_mutex_trylock(m)) ) return 0;
	if ( r != EBUSY ) return r;
	
	ct = _pthread_time_in_ms();
	t = _pthread_time_in_ms_from_timespec(ts);
	
    while (1)
    {
        /* Have we waited long enough? */
        if (ct >= t) return ETIMEDOUT;
        switch (WaitForSingleObject(_m->h, dwMilliSecs(t - ct))) {
            case WAIT_TIMEOUT:
                break;
            case WAIT_ABANDONED:
                return EINVAL;
                break;
            case WAIT_OBJECT_0:
				SET_OWNER(_m);
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
	mutex_t *_m = (mutex_t *)*m;
	UNSET_OWNER(_m);
    if (!ReleaseMutex(_m->h)) {
		printf("pthread_mutex_unlock GetLastError %d\n",GetLastError());
        return EPERM;
    }
    return 0;
}

int pthread_mutex_trylock(pthread_mutex_t *m)
{
	INIT_MUTEX(m); 
	mutex_t *_m = (mutex_t *)*m;
	CHECK_DEADLK(_m);
    switch (WaitForSingleObject(_m->h, 0)) {
        case WAIT_TIMEOUT:
            return EBUSY;
            break;
        case WAIT_ABANDONED:
            return EINVAL;
            break;
        case WAIT_OBJECT_0:
			SET_OWNER(_m);
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
	mutex_t *_m;

	int r = 0;
	(void) a;

	if (!m)	return EINVAL; 
	if ( !(_m = (pthread_mutex_t)malloc(sizeof(*_m))) ) {
		return ENOMEM; 
	}

	_m->type = PTHREAD_MUTEX_DEFAULT; 
	if (a) {
		r = pthread_mutexattr_gettype(a, &_m->type);
	}
	if (!r) {
		if ( (_m->h = CreateMutex(NULL, FALSE, NULL)) == NULL) {
			switch (GetLastError()) {
			case ERROR_ACCESS_DENIED:
					r = EPERM;
					break;
			default: /* We assume this, to keep it simple: */
					r = ENOMEM;
			}
		} else {
			_m->owner = 0;
			_m->valid = LIFE_MUTEX;
			*m = _m;
		}
	} 
	if (r) {
		_m->valid = DEAD_MUTEX;
		free(_m);
	}

    return r;
}

int pthread_mutex_destroy(pthread_mutex_t *m)
{
	mutex_t *_m = (mutex_t *)*m;

	if (!m || !*m)	return EINVAL; 
	if (*m == PTHREAD_MUTEX_INITIALIZER) {
		*m = NULL;
		return 0;
	}
	if (_m->owner) {
		/* Works only in non-recursive case currently */
		return EBUSY;
	}
	pthread_mutex_t *m2 = m;
	*m = NULL; /* dereference first, free later */
	_ReadWriteBarrier();
    _m->valid  = DEAD_MUTEX;
    _m->type  = 0;
    CloseHandle(_m->h);
	free(*m2);
	return 0;
}
#else /* USE_MUTEX_CriticalSection */
int pthread_mutex_lock(pthread_mutex_t *m)
{
	INIT_MUTEX(m);
	mutex_t *_m = (mutex_t *)*m;

	CHECK_DEADLK(_m);
	EnterCriticalSection(&_m->cs);
	SET_OWNER(_m);
	return 0;
}

int pthread_mutex_timedlock(pthread_mutex_t *m, struct timespec *ts)
{
	int r;
	unsigned long long t, ct;
	
	if (!m)	return EINVAL; 
	if (!ts) return EINVAL; 
	mutex_t *_m = (mutex_t *)*m;
	
	/* Try to lock it without waiting */
	/* And also call INIT_MUTEX() for us */
    if ( !(r=pthread_mutex_trylock(m)) ) return 0;
	if ( r != EBUSY ) return r;
	
	ct = _pthread_time_in_ms();
	t = _pthread_time_in_ms_from_timespec(ts);
	
	while (1)
	{
		_pthread_crit_u cu;

		/* Have we waited long enough? */
		if (ct > t) return ETIMEDOUT;
		
		cu.cs = &_m->cs;
		/* Wait on semaphore within critical section */
		WaitForSingleObject(cu.pc->sem, t - ct);
		
		/* Try to grab lock */
		if (!pthread_mutex_trylock(m)) return 0;
		
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
	UNSET_OWNER(_m);
#ifdef WINPTHREAD_DBG
	_pthread_crit_u cu;
	cu.cs = &_m->cs;
	printf("owner before LeaveCriticalSection: %ld\n",cu.cs->OwningThread);
#endif
	LeaveCriticalSection(&_m->cs);
#ifdef WINPTHREAD_DBG
	printf("owner after LeaveCriticalSection: %ld\n",cu.cs->OwningThread);
#endif
	return 0;
}
	
int pthread_mutex_trylock(pthread_mutex_t *m)
{
	INIT_MUTEX(m); 
	mutex_t *_m = (mutex_t *)*m;
	CHECK_DEADLK(_m);
	int r = TryEnterCriticalSection(&_m->cs) ? 0 : EBUSY;
	if (!r) SET_OWNER(_m);
	return r;
}

int pthread_mutex_init(pthread_mutex_t *m, pthread_mutexattr_t *a)
{
	mutex_t *_m;

	int r = 0;
	(void) a;

	if (!m) return EINVAL; 
	if ( !(_m = (pthread_mutex_t)malloc(sizeof(*_m))) ) {
		return ENOMEM; 
	}

	_m->type = PTHREAD_MUTEX_DEFAULT; 
	if (a) {
		r = pthread_mutexattr_gettype(a, &_m->type);
	}
	if (!r) {
		InitializeCriticalSection(&_m->cs);

		_m->owner = 0;
		_m->valid = LIFE_MUTEX;
		*m = _m;
	} else {
		_m->valid = DEAD_MUTEX;
		free(_m);
	}
	
	return r;
}

int pthread_mutex_destroy(pthread_mutex_t *m)
{
	mutex_t *_m = (mutex_t *)*m;

	if (!m || !*m)	return EINVAL; 
	if (*m == PTHREAD_MUTEX_INITIALIZER) {
		*m = NULL;
		return 0;
	}
	if (_m->valid != LIFE_MUTEX) {
		return EINVAL;
	}
	if (_m->owner) {
		/* Works only in non-recursive case currently */
		return EBUSY;
	}
	pthread_mutex_t *m2 = m;
	*m = NULL; /* dereference first, free later */
	_ReadWriteBarrier();
    _m->type  = 0;
    _m->valid  = DEAD_MUTEX;
	DeleteCriticalSection(&_m->cs);
	free(*m2);
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

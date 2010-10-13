#include <windows.h>
#include <stdio.h>
#include "pthread.h"
#include "spinlock.h"
#include "mutex.h"
#include "misc.h"

static spin_t mutex_global = {0,LIFE_SPINLOCK,0};

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
	if (_m && _m->semExt && InterlockedExchange(&_m->lockExt, 0)) {
		/* only 1 unlock should actually release the semaphore */
		printf("mutex ext unlock: from thread %d of thread %d\n",GetCurrentThreadId(), GET_OWNER(_m));
		ReleaseSemaphore(_m->semExt, 1, NULL);
	}
	return 0;
}

static int print_state = 1;
void mutex_print_set(int state)
{
	print_state = state;
}

void mutex_print(volatile pthread_mutex_t *m, char *txt)
{
	if (!print_state) return;
	mutex_t *m_ = (mutex_t *)*m;
	if (m_ == NULL) {
		printf("M%p %d %s\n",*m,GetCurrentThreadId(),txt);
	} else {
		printf("M%p %d V=%0X B=%d t=%d o=%d %s\n",*m, GetCurrentThreadId(), m_->valid, m_->busy,m_->type,GET_OWNER(m_),txt);
	}
}

inline int mutex_unref(volatile pthread_mutex_t *m, int r)
{
	mutex_t *m_ = (mutex_t *)*m;
	_spin_lite_lock(&mutex_global);
	m_->busy --;
	_spin_lite_unlock(&mutex_global);
	return r;
}

/* External: must be called by owner of a locked mutex: */
inline int mutex_ref_ext(volatile pthread_mutex_t *m)
{
	mutex_t *m_ = (mutex_t *)*m;
	_spin_lite_lock(&mutex_global);
	m_->busy ++;
	_spin_lite_unlock(&mutex_global);
	return 0;
}

/* Set the mutex to busy in a thread-safe way */
/* A busy mutex can't be destroyed */
static inline int mutex_ref(volatile pthread_mutex_t *m )
{
	int r = 0;

	INIT_MUTEX(m);
	_spin_lite_lock(&mutex_global);

	if (!m || !*m) r = EINVAL;
	else {
		((mutex_t *)*m)->busy ++;
	}

	_spin_lite_unlock(&mutex_global);

	/* because mutex_ref_destroy should have dereferenced the mutex, 
	 * this check can live outside the lock as an assert: */
	if (!r)assert(((mutex_t *)*m)->valid == LIFE_MUTEX);

	return r;
}

/* An unlock can simply fail with EPERM instead of auto-init (can't be owned) */
static inline int mutex_ref_unlock(volatile pthread_mutex_t *m )
{
	int r = 0;

	_spin_lite_lock(&mutex_global);

	if (!m || !*m) r = EINVAL;
	else if (STATIC_INITIALIZER(*m)) r= EPERM;
	else {
		((mutex_t *)*m)->busy ++;
	}

	_spin_lite_unlock(&mutex_global);

	/* because mutex_ref_destroy should have dereferenced the mutex, 
	 * this check can live outside the lock as an assert: */
	if (!r)assert(((mutex_t *)*m)->valid == LIFE_MUTEX);

	return r;
}

/* doesn't lock the mutex but set it to invalid in a thread-safe way */
/* A busy mutex can't be destroyed -> EBUSY */
static inline int mutex_ref_destroy(volatile pthread_mutex_t *m, pthread_mutex_t *mDestroy )
{
	int r = 0;

	*mDestroy = NULL;
	/* also considered as busy, any concurrent access prevents destruction: */
	if (_spin_lite_trylock(&mutex_global)) return EBUSY;
	
	if (!m || !*m)  r = EINVAL;
	else {
		mutex_t *m_ = (mutex_t *)*m;
		if (STATIC_INITIALIZER(*m)) { *m = NULL; r= -1; }
		else if (m_->valid != LIFE_MUTEX) r = EINVAL;
		else if (m_->busy || COND_LOCKED(m_)) r = EBUSY;
		else {
			*mDestroy = *m;
			*m = NULL;
		}
	}

	_spin_lite_unlock(&mutex_global);
	return r;
}

/* A valid mutex can't be re-initialized -> EBUSY */
static inline int mutex_ref_init(volatile pthread_mutex_t *m )
{
	int r = 0;

	_spin_lite_lock(&mutex_global);
	
	if (!m)  r = EINVAL;
	else if (*m) {
		mutex_t *m_ = (mutex_t *)*m;
		if (m_->valid == LIFE_MUTEX) r = EBUSY;
	}

	_spin_lite_unlock(&mutex_global);
	return r;
}



inline int mutex_static_init(volatile pthread_mutex_t *m )
{
    pthread_mutex_t m_tmp;
	mutex_t *mi, *m_replaced;

	if ( !STATIC_INITIALIZER(mi = (mutex_t *)*m) ) {
		/* Assume someone crept in between: */
		return 0;
	}

	int r = pthread_mutex_init(&m_tmp, NULL);
	if (!r) {
		((mutex_t *)m_tmp)->type = MUTEX_INITIALIZER2TYPE(mi);
		m_replaced = (mutex_t *)InterlockedCompareExchangePointer(
			(PVOID *)m, 
			m_tmp,
			mi);
		if (m_replaced != mi) {
			/* someone crept in between: */
			pthread_mutex_destroy(&m_tmp);
			/* But it could also be destroyed already: */
			if (!m_replaced) r = EINVAL;
		}
	}
	return r;
}

int pthread_mutex_lock(pthread_mutex_t *m)
{
	int r = mutex_ref(m);
	if(r) return r;

	mutex_t *_m = (mutex_t *)*m;

	if (_m->type == PTHREAD_MUTEX_NORMAL) {
		if (COND_DEADLK(_m)) {
			return mutex_unref(m,mutex_normal_handle_deadlk(_m));
		}
	} else {
		if(COND_DEADLK_NR(_m)) {
			return mutex_unref(m,EDEADLK);
		}
	}
#if defined USE_MUTEX_Mutex
    switch (WaitForSingleObject(_m->h, INFINITE)) {
        case WAIT_OBJECT_0:
			/* OK */
            break;
		default:
           return mutex_unref(m,EINVAL);
    }
#else /* USE_MUTEX_CriticalSection */
	EnterCriticalSection(&_m->cs);
#endif
	SET_OWNER(_m);
	return mutex_unref(m,r);

}

int pthread_mutex_timedlock(pthread_mutex_t *m, struct timespec *ts)
{
	if (!ts) return EINVAL; 
	int r = mutex_ref(m);
	if(r) return r;

	unsigned long long t, ct;
	
	
	/* Try to lock it without waiting */
	if ( !(r=_mutex_trylock(m)) ) return mutex_unref(m,0);
	if ( r != EBUSY ) return mutex_unref(m,r);
	
	mutex_t *_m = (mutex_t *)*m;
	if (_m->type == PTHREAD_MUTEX_NORMAL) {
		if (COND_DEADLK(_m)) {
			return mutex_unref(m,mutex_normal_handle_deadlk(_m));
		}
	} else {
		if(COND_DEADLK_NR(_m)) {
			return mutex_unref(m,EDEADLK);
		}
	}
	ct = _pthread_time_in_ms();
	t = _pthread_time_in_ms_from_timespec(ts);
	
    while (1)
    {
 #if defined USE_MUTEX_Mutex
		/* Have we waited long enough? */
        if (ct >= t) return mutex_unref(m,ETIMEDOUT);
        switch (WaitForSingleObject(_m->h, dwMilliSecs(t - ct))) {
            case WAIT_TIMEOUT:
                break;
            case WAIT_OBJECT_0:
				SET_OWNER(_m);
				return mutex_unref(m,0);
			default:
               return mutex_unref(m,EINVAL);
        }
#else /* USE_MUTEX_CriticalSection */
		_pthread_crit_u cu;

		/* Have we waited long enough? */
		if (ct > t) return mutex_unref(m,ETIMEDOUT);
		
		cu.cs = &_m->cs;
		/* Wait on semaphore within critical section */
		WaitForSingleObject(cu.pc->sem, t - ct);
		
		/* Try to grab lock */
		if (!pthread_mutex_trylock(m)) break;
#endif
		/* Get current time */
        ct = _pthread_time_in_ms();
    }
	SET_OWNER(_m);
	return  mutex_unref(m,0);
}

int pthread_mutex_unlock(pthread_mutex_t *m)
{
	int r = mutex_ref_unlock(m);
	if(r) return r;

	mutex_t *_m = (mutex_t *)*m;
	if (_m->type == PTHREAD_MUTEX_NORMAL) {
		if (!COND_LOCKED(_m)) {
			return mutex_unref(m,EPERM);
		} else if (!COND_OWNER(_m)){
			return mutex_unref(m,mutex_normal_handle_undefined(_m));
		}
	} else if (!COND_OWNER(_m)){
		return mutex_unref(m,EPERM);
	}

	UNSET_OWNER(_m);
#if defined USE_MUTEX_Mutex
	if (!ReleaseMutex(_m->h)) {
        return mutex_unref(m,EPERM);
    }
#else /* USE_MUTEX_CriticalSection */
	LeaveCriticalSection(&_m->cs);
#endif
    return mutex_unref(m,0);
}

int _mutex_trylock(pthread_mutex_t *m)
{
	int r = 0;
	mutex_t *_m = (mutex_t *)*m;
	if ((_m->type != PTHREAD_MUTEX_RECURSIVE) && COND_DEADLK(_m)) {
		return EBUSY;
	}
#if defined USE_MUTEX_Mutex
	switch (WaitForSingleObject(_m->h, 0)) {
        case WAIT_TIMEOUT:
            r = EBUSY;
            break;
        case WAIT_OBJECT_0:
			/* OK */
            break;
        default:
            r = EINVAL;
     }
#else /* USE_MUTEX_CriticalSection */
	r = TryEnterCriticalSection(&_m->cs) ? 0 : EBUSY;
#endif
	if (!r) SET_OWNER(_m);
	return r;
}

int pthread_mutex_trylock(pthread_mutex_t *m)
{
	int r = mutex_ref(m);
	if(r) return r;

	return mutex_unref(m,_mutex_trylock(m));
}

int pthread_mutex_init(pthread_mutex_t *m, pthread_mutexattr_t *a)
{
	mutex_t *_m;

	int r = mutex_ref_init(m);
	if(r) return r;
	(void) a;

	if ( !(_m = (pthread_mutex_t)calloc(1,sizeof(*_m))) ) {
		return ENOMEM; 
	}

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
	pthread_mutex_t mDestroy;
	int r = mutex_ref_destroy(m,&mDestroy);
	if(r) return r;
	if(r<0) return 0; /* destroyed a (still) static initialized mutex */

	/* now the mutex is invalid, and no one can touch it */
	mutex_t *_m = (mutex_t *)mDestroy;


	if (_m->semExt) {
		CloseHandle(_m->semExt);
	}
#if defined USE_MUTEX_Mutex
	CloseHandle(_m->h);
#else /* USE_MUTEX_CriticalSection */
	DeleteCriticalSection(&_m->cs);
#endif
	_m->valid = DEAD_MUTEX;
    _m->type  = 0;
	free(mDestroy);
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

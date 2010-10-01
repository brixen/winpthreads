/*
 * Posix Condition Variables for Microsoft Windows.
 * 22-9-2010 Partly based on the ACE framework implementation.
 */
#include <windows.h>
#include <stdio.h>
#include "pthread.h"
#include "cond.h"
#include "mutex.h"

#include "misc.h"

static inline int cond_static_init(volatile pthread_cond_t *c)
{
    pthread_cond_t c_tmp, *c_replaced;
	int r = pthread_cond_init(&c_tmp, NULL);

	if (!r) {
		c_replaced = (pthread_cond_t *)InterlockedCompareExchangePointerAcquire(
			(PVOID *)c, 
			c_tmp,
			PTHREAD_COND_INITIALIZER);
		if (c_replaced != (pthread_cond_t *)PTHREAD_COND_INITIALIZER) {
			printf("cond_static_init race detected: cond %p\n",  c_replaced);
			/* someone crept in between: */
			pthread_cond_destroy(&c_tmp);
			/* it could even be destroyed: */
			if (!c_replaced) r = EINVAL;
		}
	}
	return r;
}


#if defined  USE_COND_ConditionVariable
int pthread_cond_init(pthread_cond_t *c, pthread_condattr_t *a)
{
	cond_t *_c;

	int r = 0;
	(void) a;

	if (!c)	return EINVAL; 
	if ( !(_c = (pthread_cond_t)malloc(sizeof(*_c))) ) {
		return ENOMEM; 
	}
	
	InitializeConditionVariable(&_c->CV);
	_c->valid = LIFE_COND;
	*c = _c;
	return 0;
}

int pthread_cond_destroy(pthread_cond_t *c)
{
	cond_t *_c = (cond_t *)*c;

	if (!c || !*c)	return EINVAL; 
	if (*c == PTHREAD_COND_INITIALIZER) {
		*c = NULL;
		return 0;
	}

	pthread_cond_t *c_del = c;
	*c = NULL; /* dereference first, free later */
	_ReadWriteBarrier();
    _c->valid  = DEAD_COND;
 	/* There is indeed no DeleteConditionVariable */
 	free(*c_del);
    return 0;
}
#else /* USE_COND_SignalObjectAndWait USE_COND_Semaphore */
int pthread_cond_init(pthread_cond_t *c, 
                             pthread_condattr_t *a)
{
	cond_t *_c;

	int r = 0;
	(void) a;

	if (!c)	return EINVAL; 
	if ( !(_c = (pthread_cond_t)malloc(sizeof(*_c))) ) {
		return ENOMEM; 
	}

	_c->waiters_count_ = 0;
    _c->was_broadcast_ = 0;
    _c->sema_ = CreateSemaphore (NULL,       /* no security */
        0,          /* initially 0 */
        0x7fffffff, /* max count */
        NULL);      /* unnamed  */
    if (_c->sema_ == NULL) return EAGAIN;
    InitializeCriticalSection(&_c->waiters_count_lock_);
    _c->waiters_done_ = CreateEvent (NULL,  /* no security */
        FALSE, /* auto-reset */
        FALSE, /* non-signaled initially */
        NULL); /* unnamed */
    if (_c->waiters_done_ == NULL) { /*assume EAGAIN (but could be ENOMEM) */
        DeleteCriticalSection(&_c->waiters_count_lock_);
        CloseHandle(_c->sema_);
		_c->valid  = DEAD_COND;
		free(_c);
        return EAGAIN;
    } else {
		_c->valid = LIFE_COND;
		*c = _c;
	}
    return 0;
}

int pthread_cond_destroy(pthread_cond_t *c)
{
	cond_t *_c = (cond_t *)*c;

	if (!c || !*c)	return EINVAL; 
	if (*c == PTHREAD_COND_INITIALIZER) {
		*c = NULL;
		return 0;
	}

	pthread_cond_t *c_del = c;
	*c = NULL; /* dereference first, free later */
	_ReadWriteBarrier();
    _c->valid  = DEAD_COND;
    CloseHandle(_c->waiters_done_);
    DeleteCriticalSection(&_c->waiters_count_lock_);
    CloseHandle(_c->sema_);
 	free(*c_del);
    return 0;
}
#endif /*USE_COND_ConditionVariable */

#if defined USE_COND_SignalObjectAndWait
int pthread_cond_signal (pthread_cond_t *c)
{
    CHECK_COND(c);	
	cond_t *_c = (cond_t *)*c;

    EnterCriticalSection (&_c->waiters_count_lock_);
    int have_waiters = _c->waiters_count_ > 0;
    LeaveCriticalSection (&_c->waiters_count_lock_);

    /* If there aren't any waiters, then this is a no-op.   */
    if (have_waiters)
        ReleaseSemaphore (_c->sema_, 1, 0);
    return 0;
}


int pthread_cond_broadcast (pthread_cond_t *c)
{
    CHECK_COND(c);	
	cond_t *_c = (cond_t *)*c;

    /* This is needed to ensure that <waiters_count_> and <was_broadcast_> are */
    /* consistent relative to each other. */
    EnterCriticalSection (&_c->waiters_count_lock_);
    int have_waiters = 0;

    if (_c->waiters_count_ > 0) {
        /* We are broadcasting, even if there is just one waiter... */
        /* Record that we are broadcasting, which helps optimize */
        /* <pthread_cond_wait> for the non-broadcast case. */
        _c->was_broadcast_ = 1;
        have_waiters = 1;
    }

    if (have_waiters) {
        /* Wake up all the waiters atomically. */
        ReleaseSemaphore (_c->sema_, _c->waiters_count_, 0);

        LeaveCriticalSection (&_c->waiters_count_lock_);

        /* Wait for all the awakened threads to acquire the counting */
        /* semaphore.  */
        WaitForSingleObject (_c->waiters_done_, INFINITE);
        /* This assignment is okay, even without the <waiters_count_lock_> held  */
        /* because no other waiter threads can wake up to access it. */
        _c->was_broadcast_ = 0;
    } else
        LeaveCriticalSection (&_c->waiters_count_lock_);
    return 0;
}


int pthread_cond_wait (pthread_cond_t *c, 
                              pthread_mutex_t *external_mutex)
{
	INIT_COND(c);
    INIT_MUTEX(external_mutex);	
	cond_t *_c = (cond_t *)*c;
	mutex_t *_m = (mutex_t *)*external_mutex;

	pthread_testcancel();
     /* Avoid race conditions. */
    EnterCriticalSection (&_c->waiters_count_lock_);
    _c->waiters_count_++;
    LeaveCriticalSection (&_c->waiters_count_lock_);

    /* This call atomically releases the mutex and waits on the */
    /* semaphore until <pthread_cond_signal> or <pthread_cond_broadcast> */
    /* are called by another thread. */
    SignalObjectAndWait (_m->h, _c->sema_, INFINITE, FALSE);

    /* Reacquire lock to avoid race conditions. */
    EnterCriticalSection (&_c->waiters_count_lock_);

    /* We're no longer waiting... */
    _c->waiters_count_--;

    /* Check to see if we're the last waiter after <pthread_cond_broadcast>. */
    int last_waiter = _c->was_broadcast_ && _c->waiters_count_ == 0;

    LeaveCriticalSection (&_c->waiters_count_lock_);

    /* If we're the last waiter thread during this particular broadcast */
    /* then let all the other threads proceed. */
    if (last_waiter)
        /* This call atomically signals the <waiters_done_> event and waits until */
        /* it can acquire the <external_mutex>.  This is required to ensure fairness.  */
        SignalObjectAndWait (_c->waiters_done_, _m->h, INFINITE, FALSE);
    else
        /* Always regain the external mutex since that's the guarantee we */
        /* give to our callers.  */
        WaitForSingleObject (_m->h, INFINITE);

    return 0;
}

int pthread_cond_timedwait(pthread_cond_t *c, pthread_mutex_t *external_mutex, struct timespec *t)
{
    int r = 0;
	DWORD dwr;
	INIT_COND(c);
    INIT_MUTEX(external_mutex);	
	cond_t *_c = (cond_t *)*c;
	mutex_t *_m = (mutex_t *)*external_mutex;

	pthread_testcancel();
    /* Avoid race conditions. */
    EnterCriticalSection (&_c->waiters_count_lock_);
    _c->waiters_count_++;
    LeaveCriticalSection (&_c->waiters_count_lock_);

    /* This call atomically releases the mutex and waits on the */
    /* semaphore until <pthread_cond_signal> or <pthread_cond_broadcast> */
    /* are called by another thread. */
    /*dwr = SignalObjectAndWait (external_mutex->h, cv->sema_, dwMilliSecs(_pthread_time_in_ms_from_timespec(t)), FALSE); */
	dwr = dwMilliSecs(_pthread_time_in_ms_from_timespec(t));
	printf("pthread_cond_timedwait wait %d ms (3000 hardcoded)\n", (int) dwr);
    dwr = SignalObjectAndWait (_m->h, _c->sema_, 3000, FALSE);
	switch (dwr) {
	case WAIT_TIMEOUT:
		r = ETIMEDOUT;
		/*Not done by SignalObjectAndWait in this case */
		/*BUG? check race condition (not atomic anymore), but it seems to work */
		WaitForSingleObject (_m->h, INFINITE);
		break;
	case WAIT_ABANDONED:
		r = EPERM;
		break;
	case WAIT_OBJECT_0:
		r = 0;
		break;
	default:
		/*We can only return EINVAL though it might not be posix compliant  */
		r = EINVAL;
	}
	if (r) {
		EnterCriticalSection (&_c->waiters_count_lock_);
		_c->waiters_count_--;
		LeaveCriticalSection (&_c->waiters_count_lock_);
		return r;
	}

    /* Reacquire lock to avoid race conditions. */
    EnterCriticalSection (&_c->waiters_count_lock_);

    /* We're no longer waiting... */
    _c->waiters_count_--;

    /* Check to see if we're the last waiter after <pthread_cond_broadcast>. */
    int last_waiter = _c->was_broadcast_ && _c->waiters_count_ == 0;

    LeaveCriticalSection (&_c->waiters_count_lock_);

    /* If we're the last waiter thread during this particular broadcast */
    /* then let all the other threads proceed. */
    if (last_waiter)
        /* This call atomically signals the <waiters_done_> event and waits until */
        /* it can acquire the <external_mutex>.  This is required to ensure fairness.  */
        SignalObjectAndWait (_c->waiters_done_, _m->h, INFINITE, FALSE);
    else
        /* Always regain the external mutex since that's the guarantee we */
        /* give to our callers.  */
        WaitForSingleObject (_m->h, INFINITE);

    return r;
}
#elif defined  USE_COND_ConditionVariable
int pthread_cond_signal(pthread_cond_t *c)
{
    CHECK_COND(c);	
	cond_t *_c = (cond_t *)*c;

	WakeConditionVariable(&_c->CV);
	return 0;
}

int pthread_cond_broadcast(pthread_cond_t *c)
{
    CHECK_COND(c);	
	cond_t *_c = (cond_t *)*c;

	WakeAllConditionVariable(&_c->CV);
	return 0;
}

int pthread_cond_wait(pthread_cond_t *c, pthread_mutex_t *m)
{
	INIT_COND(c);
    INIT_MUTEX(m);	
	cond_t *_c = (cond_t *)*c;
	mutex_t *_m = (mutex_t *)*m;

	pthread_testcancel();
	SleepConditionVariableCS(&_c->CV, &_m->cs, INFINITE);
	return 0;
}

int pthread_cond_timedwait(pthread_cond_t *c, pthread_mutex_t *m, struct timespec *t)
{
	unsigned long long tm = _pthread_rel_time_in_ms(t);
	DWORD dwr;
	INIT_COND(c);
    INIT_MUTEX(m);	
	cond_t *_c = (cond_t *)*c;
	mutex_t *_m = (mutex_t *)*m;
	
	pthread_testcancel();
	
	dwr = dwMilliSecs(_pthread_time_in_ms_from_timespec(t));
	printf("pthread_cond_timedwait wait %d ms (3000 hardcoded for now)\n", (int) dwr); dwr = 3000;
	if (!SleepConditionVariableCS(&_c->CV,  &_m->cs, dwr)) return ETIMEDOUT;
	
	/* We can have a spurious wakeup after the timeout */
	if (!_pthread_rel_time_in_ms(t)) return ETIMEDOUT;
	
	return 0;
}


#else /*default USE_COND_Semaphore */
int pthread_cond_signal (pthread_cond_t *c)
{
    CHECK_COND(c);	
	cond_t *_c = (cond_t *)*c;
    int have_waiters = _c->waiters_count_ > 0;

    /* If there aren't any waiters, then this is a no-op.   */
    if (have_waiters)
        ReleaseSemaphore (_c->sema_, 1, 0);
    return 0;
}

int pthread_cond_broadcast (pthread_cond_t *c)
{
    CHECK_COND(c);	
	cond_t *_c = (cond_t *)*c;
    int have_waiters = _c->waiters_count_ > 0;

	/* If there aren't any waiters, then this is a no-op.   */
    if (have_waiters)
        ReleaseSemaphore (_c->sema_, _c->waiters_count_, 0);
    return 0;
}

int pthread_cond_wait (pthread_cond_t *c, 
                              pthread_mutex_t *external_mutex)
{
	INIT_COND(c);
    INIT_MUTEX(external_mutex);	
	cond_t *_c = (cond_t *)*c;

	int r = 0;
	DWORD dwr;

	pthread_testcancel();
	InterlockedIncrement(&_c->waiters_count_);

    pthread_mutex_unlock(external_mutex);
    dwr = WaitForSingleObject(_c->sema_, INFINITE);
	switch (dwr) {
	case WAIT_TIMEOUT:
		r = ETIMEDOUT;
		break;
	case WAIT_ABANDONED:
		r = EPERM;
		break;
	case WAIT_OBJECT_0:
		r = 0;
		break;
	default:
		/*We can only return EINVAL though it might not be posix compliant  */
		r = EINVAL;
	}
    pthread_mutex_lock(external_mutex);
	InterlockedDecrement(&_c->waiters_count_);

    return r;
}

int pthread_cond_timedwait(pthread_cond_t *c, pthread_mutex_t *external_mutex, struct timespec *t)
{
	INIT_COND(c);
    INIT_MUTEX(external_mutex);	
	cond_t *_c = (cond_t *)*c;

	int r = 0;
	DWORD dwr;

    CHECK_MUTEX(external_mutex);
	InterlockedIncrement(&_c->waiters_count_);
    pthread_mutex_unlock(external_mutex);

    /*dwr = SignalObjectAndWait (external_mutex->h, cv->sema_, dwMilliSecs(_pthread_time_in_ms_from_timespec(t)), FALSE); */
	dwr = dwMilliSecs(_pthread_time_in_ms_from_timespec(t));
	printf("pthread_cond_timedwait wait %d ms (3000 hardcoded for now)\n", (int) dwr); dwr = 3000;

    dwr = WaitForSingleObject(_c->sema_, dwr);
	switch (dwr) {
	case WAIT_TIMEOUT:
		r = ETIMEDOUT;
		break;
	case WAIT_ABANDONED:
		r = EPERM;
		break;
	case WAIT_OBJECT_0:
		r = 0;
		break;
	default:
		/*We can only return EINVAL though it might not be posix compliant  */
		r = EINVAL;
	}
    printf("pthread_cond_timedwait: WaitForSingleObject() rc=%d\n",r);
    pthread_mutex_lock(external_mutex);
	InterlockedDecrement(&_c->waiters_count_);

	return r;
}
#endif /* USE_COND_SignalObjectAndWait */

int pthread_condattr_destroy(pthread_condattr_t *a)
{
    (void) a;
    return 0;
}

int pthread_condattr_init(pthread_condattr_t *a)
{
    *a = 0;
    return 0;
}

int pthread_condattr_getpshared(pthread_condattr_t *a, int *s)
{
    *s = *a;
    return 0;
}

int pthread_condattr_setpshared(pthread_condattr_t *a, int s)
{
    *a = s;
    return 0;
}


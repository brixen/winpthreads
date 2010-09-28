/*
 * Posix Condition Variables for Microsoft Windows.
 * 22-9-2010 Partly based on the ACE framework implementation.
 */
#include <stdio.h>
#include "pthread.h"
#include "cond.h"

#include "misc.h"

#if defined  USE_COND_ConditionVariable
int pthread_cond_init(pthread_cond_t *c, pthread_condattr_t *a)
{
	(void) a;
	
	c->waiters_count_ = 0;
	InitializeCriticalSection(&c->waiters_count_lock_);
	InitializeConditionVariable(&c->CV);
	return 0;
}

int pthread_cond_destroy(pthread_cond_t *c)
{
	c->waiters_count_ = 0;
	return 0;
}
#else
int pthread_cond_init(pthread_cond_t *cv, 
                             pthread_condattr_t *a)
{
    CHECK_PTR(cv);

    cv->waiters_count_ = 0;
    cv->was_broadcast_ = 0;
    cv->sema_ = CreateSemaphore (NULL,       /* no security */
        0,          /* initially 0 */
        0x7fffffff, /* max count */
        NULL);      /* unnamed  */
    if (cv->sema_ == NULL) return EAGAIN;
    InitializeCriticalSection(&cv->waiters_count_lock_);
    cv->waiters_done_ = CreateEvent (NULL,  /* no security */
        FALSE, /* auto-reset */
        FALSE, /* non-signaled initially */
        NULL); /* unnamed */
    if (cv->waiters_done_ == NULL) { /*assume EAGAIN (but could be ENOMEM) */
        DeleteCriticalSection(&cv->waiters_count_lock_);
        CloseHandle(cv->sema_);
        return EAGAIN;
    }
    return 0;
}

int pthread_cond_destroy(pthread_cond_t *cv)
{

    CHECK_HANDLE(cv->waiters_done_);	
    CloseHandle(cv->waiters_done_);
    DeleteCriticalSection(&cv->waiters_count_lock_);
    CloseHandle(cv->sema_);
    return 0;
}
#endif /*USE_COND_ConditionVariable */

#if defined USE_COND_SignalObjectAndWait
int pthread_cond_signal (pthread_cond_t *cv)
{
    CHECK_HANDLE(cv->waiters_done_);	
    EnterCriticalSection (&cv->waiters_count_lock_);
    int have_waiters = cv->waiters_count_ > 0;
    LeaveCriticalSection (&cv->waiters_count_lock_);

    /* If there aren't any waiters, then this is a no-op.   */
    if (have_waiters)
        ReleaseSemaphore (cv->sema_, 1, 0);
    return 0;
}


int pthread_cond_broadcast (pthread_cond_t *cv)
{
    CHECK_HANDLE(cv->waiters_done_);	
    /* This is needed to ensure that <waiters_count_> and <was_broadcast_> are */
    /* consistent relative to each other. */
    EnterCriticalSection (&cv->waiters_count_lock_);
    int have_waiters = 0;

    if (cv->waiters_count_ > 0) {
        /* We are broadcasting, even if there is just one waiter... */
        /* Record that we are broadcasting, which helps optimize */
        /* <pthread_cond_wait> for the non-broadcast case. */
        cv->was_broadcast_ = 1;
        have_waiters = 1;
    }

    if (have_waiters) {
        /* Wake up all the waiters atomically. */
        ReleaseSemaphore (cv->sema_, cv->waiters_count_, 0);

        LeaveCriticalSection (&cv->waiters_count_lock_);

        /* Wait for all the awakened threads to acquire the counting */
        /* semaphore.  */
        WaitForSingleObject (cv->waiters_done_, INFINITE);
        /* This assignment is okay, even without the <waiters_count_lock_> held  */
        /* because no other waiter threads can wake up to access it. */
        cv->was_broadcast_ = 0;
    } else
        LeaveCriticalSection (&cv->waiters_count_lock_);
    return 0;
}


int pthread_cond_wait (pthread_cond_t *cv, 
                              pthread_mutex_t *external_mutex)
{
    CHECK_HANDLE(cv->waiters_done_);
    CHECK_MUTEX(external_mutex);

    /* Avoid race conditions. */
    EnterCriticalSection (&cv->waiters_count_lock_);
    cv->waiters_count_++;
    LeaveCriticalSection (&cv->waiters_count_lock_);

    /* This call atomically releases the mutex and waits on the */
    /* semaphore until <pthread_cond_signal> or <pthread_cond_broadcast> */
    /* are called by another thread. */
    SignalObjectAndWait (external_mutex->h, cv->sema_, INFINITE, FALSE);

    /* Reacquire lock to avoid race conditions. */
    EnterCriticalSection (&cv->waiters_count_lock_);

    /* We're no longer waiting... */
    cv->waiters_count_--;

    /* Check to see if we're the last waiter after <pthread_cond_broadcast>. */
    int last_waiter = cv->was_broadcast_ && cv->waiters_count_ == 0;

    LeaveCriticalSection (&cv->waiters_count_lock_);

    /* If we're the last waiter thread during this particular broadcast */
    /* then let all the other threads proceed. */
    if (last_waiter)
        /* This call atomically signals the <waiters_done_> event and waits until */
        /* it can acquire the <external_mutex>.  This is required to ensure fairness.  */
        SignalObjectAndWait (cv->waiters_done_, external_mutex->h, INFINITE, FALSE);
    else
        /* Always regain the external mutex since that's the guarantee we */
        /* give to our callers.  */
        WaitForSingleObject (external_mutex->h, INFINITE);

    return 0;
}

int pthread_cond_timedwait(pthread_cond_t *cv, pthread_mutex_t *external_mutex, struct timespec *t)
{
    int r = 0;
	DWORD dwr;

    CHECK_HANDLE(cv->waiters_done_);
    CHECK_MUTEX(external_mutex);
    CHECK_PTR(t);

    /* Avoid race conditions. */
    EnterCriticalSection (&cv->waiters_count_lock_);
    cv->waiters_count_++;
    LeaveCriticalSection (&cv->waiters_count_lock_);

    /* This call atomically releases the mutex and waits on the */
    /* semaphore until <pthread_cond_signal> or <pthread_cond_broadcast> */
    /* are called by another thread. */
    /*dwr = SignalObjectAndWait (external_mutex->h, cv->sema_, dwMilliSecs(_pthread_time_in_ms_from_timespec(t)), FALSE); */
	dwr = dwMilliSecs(_pthread_time_in_ms_from_timespec(t));
	printf("pthread_cond_timedwait wait %d ms (3000 hardcoded)\n", (int) dwr);
    dwr = SignalObjectAndWait (external_mutex->h, cv->sema_, 3000, FALSE);
	switch (dwr) {
	case WAIT_TIMEOUT:
		r = ETIMEDOUT;
		/*Not done by SignalObjectAndWait in this case */
		/*BUG? check race condition (not atomic anymore), but it seems to work */
		WaitForSingleObject (external_mutex->h, INFINITE);
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
		EnterCriticalSection (&cv->waiters_count_lock_);
		cv->waiters_count_--;
		LeaveCriticalSection (&cv->waiters_count_lock_);
		return r;
	}

    /* Reacquire lock to avoid race conditions. */
    EnterCriticalSection (&cv->waiters_count_lock_);

    /* We're no longer waiting... */
    cv->waiters_count_--;

    /* Check to see if we're the last waiter after <pthread_cond_broadcast>. */
    int last_waiter = cv->was_broadcast_ && cv->waiters_count_ == 0;

    LeaveCriticalSection (&cv->waiters_count_lock_);

    /* If we're the last waiter thread during this particular broadcast */
    /* then let all the other threads proceed. */
    if (last_waiter)
        /* This call atomically signals the <waiters_done_> event and waits until */
        /* it can acquire the <external_mutex>.  This is required to ensure fairness.  */
        SignalObjectAndWait (cv->waiters_done_, external_mutex->h, INFINITE, FALSE);
    else
        /* Always regain the external mutex since that's the guarantee we */
        /* give to our callers.  */
        WaitForSingleObject (external_mutex->h, INFINITE);

    return r;
}
#elif defined  USE_COND_ConditionVariable
int pthread_cond_signal(pthread_cond_t *c)
{
	WakeConditionVariable(&c->CV);
	return 0;
}

int pthread_cond_broadcast(pthread_cond_t *c)
{
	WakeAllConditionVariable(&c->CV);
	return 0;
}

int pthread_cond_wait(pthread_cond_t *c, pthread_mutex_t *m)
{
	pthread_testcancel();
	SleepConditionVariableCS(&c->CV, &m->cs, INFINITE);
	return 0;
}

int pthread_cond_timedwait(pthread_cond_t *c, pthread_mutex_t *m, struct timespec *t)
{
	unsigned long long tm = _pthread_rel_time_in_ms(t);
	DWORD dwr;
	
	pthread_testcancel();
	
	dwr = dwMilliSecs(_pthread_time_in_ms_from_timespec(t));
	printf("pthread_cond_timedwait wait %d ms (3000 hardcoded for now)\n", (int) dwr); dwr = 3000;
	if (!SleepConditionVariableCS(&c->CV,  &m->cs, dwr)) return ETIMEDOUT;
	
	/* We can have a spurious wakeup after the timeout */
	if (!_pthread_rel_time_in_ms(t)) return ETIMEDOUT;
	
	return 0;
}


#else /*default USE_COND_Semaphore */
int pthread_cond_signal (pthread_cond_t *cv)
{
    CHECK_HANDLE(cv->waiters_done_);	
    EnterCriticalSection (&cv->waiters_count_lock_);
    int have_waiters = cv->waiters_count_ > 0;
    LeaveCriticalSection (&cv->waiters_count_lock_);

    /* If there aren't any waiters, then this is a no-op.   */
	printf("have_waiters cv->waiters_count_ = %d %d\n", (int) have_waiters,cv->waiters_count_);
    if (have_waiters)
        ReleaseSemaphore (cv->sema_, 1, 0);
    return 0;
}

int pthread_cond_broadcast (pthread_cond_t *cv)
{
    CHECK_HANDLE(cv->waiters_done_);	

	EnterCriticalSection (&cv->waiters_count_lock_);
    int have_waiters = cv->waiters_count_ > 0;
    LeaveCriticalSection (&cv->waiters_count_lock_);

	/* If there aren't any waiters, then this is a no-op.   */
    if (have_waiters)
        ReleaseSemaphore (cv->sema_, cv->waiters_count_, 0);
    return 0;
}

int pthread_cond_wait (pthread_cond_t *cv, 
                              pthread_mutex_t *external_mutex)
{
    int r = 0;
	DWORD dwr;

	CHECK_HANDLE(cv->waiters_done_);
    CHECK_MUTEX(external_mutex);

	pthread_testcancel();
    EnterCriticalSection (&cv->waiters_count_lock_);
    cv->waiters_count_++;
    LeaveCriticalSection (&cv->waiters_count_lock_);

    pthread_mutex_unlock(external_mutex);
    dwr = WaitForSingleObject(cv->sema_, INFINITE);
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
    EnterCriticalSection (&cv->waiters_count_lock_);
    cv->waiters_count_--;
    LeaveCriticalSection (&cv->waiters_count_lock_);

    return r;
}

int pthread_cond_timedwait(pthread_cond_t *cv, pthread_mutex_t *external_mutex, struct timespec *t)
{
    int r = 0;
	DWORD dwr;

    CHECK_HANDLE(cv->waiters_done_);
    CHECK_MUTEX(external_mutex);
    CHECK_PTR(t);

 	pthread_testcancel();
    EnterCriticalSection (&cv->waiters_count_lock_);
    cv->waiters_count_++;
    LeaveCriticalSection (&cv->waiters_count_lock_);
    pthread_mutex_unlock(external_mutex);

    /*dwr = SignalObjectAndWait (external_mutex->h, cv->sema_, dwMilliSecs(_pthread_time_in_ms_from_timespec(t)), FALSE); */
	dwr = dwMilliSecs(_pthread_time_in_ms_from_timespec(t));
	printf("pthread_cond_timedwait wait %d ms (3000 hardcoded for now)\n", (int) dwr); dwr = 3000;

    dwr = WaitForSingleObject(cv->sema_, dwr);
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
    EnterCriticalSection (&cv->waiters_count_lock_);
    cv->waiters_count_--;
    LeaveCriticalSection (&cv->waiters_count_lock_);

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


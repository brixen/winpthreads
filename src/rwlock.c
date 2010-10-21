#include <windows.h>
#include "pthread.h"
#include "thread.h"
#include "rwlock.h"
#include "misc.h"

#ifdef USE_RWLOCK_pthread_cond
int pthread_rwlock_destroy (pthread_rwlock_t *rwlock_)
{
    int result=0;
    
    CHECK_RWLOCK(rwlock_);
	rwlock_t *rwlock = (rwlock_t *)*rwlock_;
    if ( (result = pthread_mutex_lock (&rwlock->m)) )
        return result;

    if (rwlock->readers > 0 || rwlock->writers) {
        pthread_mutex_unlock (&rwlock->m);
        return EBUSY;
    }

     if (rwlock->readers_count != 0 || rwlock->writers_count != 0) {
        pthread_mutex_unlock (&rwlock->m);
        return EBUSY;
    }

 	pthread_rwlock_t *rwlock2 = rwlock_;
	*rwlock_= NULL; /* dereference first, free later */
	_ReadWriteBarrier();
    rwlock->valid  = DEAD_RWLOCK;

	UPD_RESULT(pthread_mutex_unlock (&rwlock->m), result);
    UPD_RESULT(pthread_mutex_destroy(&rwlock->m), result);
    UPD_RESULT(pthread_cond_destroy (&rwlock->cw), result);
	free(*rwlock2);

    return result;
} 

int pthread_rwlock_init (pthread_rwlock_t *rwlock_, const pthread_rwlockattr_t *attr)
{
    int result=0;
	rwlock_t *rwlock;

	if ( !(rwlock = (pthread_rwlock_t)malloc(sizeof(*rwlock))) ) {
		return ENOMEM; 
	}

	memset(rwlock, 0,sizeof(*rwlock));

    if ( (result = pthread_mutex_init (&rwlock->m, NULL)) )
        return result;

    if ( (result = pthread_cond_init (&rwlock->cr, NULL)) ) {
        pthread_mutex_destroy (&rwlock->m);
        return result;
    }

    if ( (result = pthread_cond_init (&rwlock->cw, NULL)) ) {
        pthread_cond_destroy (&rwlock->cr);
        pthread_mutex_destroy (&rwlock->m);
        return result;
    }

	rwlock->valid = LIFE_RWLOCK;
	*rwlock_ = rwlock;
	return 0;
} 

static void _pthread_once_rwlock_readcleanup (pthread_once_t *arg)
{
    rwlock_t   *rwlock = (rwlock_t *)arg;

    rwlock->readers_count--;
    pthread_mutex_unlock (&rwlock->m);
}

int pthread_rwlock_rdlock (pthread_rwlock_t *rwlock_)
{
    int result=0;
    
    CHECK_RWLOCK(rwlock_);
	rwlock_t *rwlock = (rwlock_t *)*rwlock_;

	pthread_testcancel();
    if ( (result = pthread_mutex_lock (&rwlock->m)) )
        return result;

	if (rwlock->writers) {
        rwlock->readers_count++;
        pthread_cleanup_push (_pthread_once_rwlock_readcleanup, (void*)rwlock);
        while (rwlock->writers) {
			if ( (result = pthread_cond_wait (&rwlock->cr, &rwlock->m)) )
				break;
        }
        pthread_cleanup_pop (0);
        rwlock->readers_count--;
    }

	if (result == 0)
        rwlock->readers++;

    UPD_RESULT(pthread_mutex_unlock (&rwlock->m), result);
    return result;
}

int pthread_rwlock_timedrdlock (pthread_rwlock_t *rwlock_, struct timespec *ts)
{
    int result=0;
    
    CHECK_RWLOCK(rwlock_);
	rwlock_t *rwlock = (rwlock_t *)*rwlock_;
    
	pthread_testcancel();
    if ( (result = pthread_mutex_lock (&rwlock->m)) )
        return result;

	if (rwlock->writers) {
        rwlock->readers_count++;
        pthread_cleanup_push (_pthread_once_rwlock_readcleanup, (void*)rwlock);
        while (rwlock->writers) {
			result = pthread_cond_timedwait (&rwlock->cr, &rwlock->m, ts);
			if (result) break;
        }
        pthread_cleanup_pop (0);
        rwlock->readers_count--;
    }

	if (result == 0)
        rwlock->readers++;

    UPD_RESULT(pthread_mutex_unlock (&rwlock->m), result);
    return result;
}

int pthread_rwlock_tryrdlock (pthread_rwlock_t *rwlock_)
{
    int result=0;
    
    CHECK_RWLOCK(rwlock_);
	rwlock_t *rwlock = (rwlock_t *)*rwlock_;

    if ( (result = pthread_mutex_lock (&rwlock->m)) )
        return result;

	if (rwlock->writers)
        result = EBUSY;
    else
        rwlock->readers++;
 
	UPD_RESULT(pthread_mutex_unlock (&rwlock->m), result);
    return result;
} 

int pthread_rwlock_trywrlock (pthread_rwlock_t *rwlock_)
{
    int result=0;
    
    CHECK_RWLOCK(rwlock_);
	rwlock_t *rwlock = (rwlock_t *)*rwlock_;

    if ( (result = pthread_mutex_lock (&rwlock->m)) )
        return result;

	if (rwlock->writers > 0 || rwlock->readers > 0)
        result = EBUSY;
    else
        rwlock->writers = 1;

	UPD_RESULT(pthread_mutex_unlock (&rwlock->m), result);
    return result;
} 

int pthread_rwlock_unlock (pthread_rwlock_t *rwlock_)
{
    int result=0;
    
    CHECK_RWLOCK(rwlock_);
	rwlock_t *rwlock = (rwlock_t *)*rwlock_;

    if ( (result = pthread_mutex_lock (&rwlock->m)) )
        return result;

	if (rwlock->writers) {
		rwlock->writers = 0;
		if (rwlock->readers_count == 1) {
			/* optimize with just a signal for the most common case:  */
			result = pthread_cond_signal (&rwlock->cr);
		} else if (rwlock->readers_count > 0) {
			result = pthread_cond_broadcast (&rwlock->cr);
		} else if (rwlock->writers_count > 0) {
			result = pthread_cond_signal (&rwlock->cw);
		}
	} else if (rwlock->readers > 0) {
		rwlock->readers--;
		if (rwlock->readers == 0 && rwlock->writers_count > 0)
			result = pthread_cond_signal (&rwlock->cw);
	}

	UPD_RESULT(pthread_mutex_unlock (&rwlock->m), result);
    return result;
} 

static void _pthread_once_rwlock_writecleanup (pthread_once_t *arg)
{
    rwlock_t *rwlock = (rwlock_t *)arg;

    rwlock->writers_count--;
    pthread_mutex_unlock (&rwlock->m);
}

int pthread_rwlock_wrlock (pthread_rwlock_t *rwlock_)
{
    int result=0;
    
    CHECK_RWLOCK(rwlock_);
	rwlock_t *rwlock = (rwlock_t *)*rwlock_;

	pthread_testcancel();
    if ( (result = pthread_mutex_lock (&rwlock->m)) )
        return result;

	if (rwlock->writers || rwlock->readers > 0) {
        rwlock->writers_count++;
        pthread_cleanup_push (_pthread_once_rwlock_writecleanup, (void*)rwlock);
        while (rwlock->writers || rwlock->readers > 0) {
            if ( (result = pthread_cond_wait (&rwlock->cw, &rwlock->m)) != 0)
                break;
        }
        pthread_cleanup_pop (0);
        rwlock->writers_count--;
    }
    if (result == 0)
        rwlock->writers = 1;

    UPD_RESULT(pthread_mutex_unlock (&rwlock->m), result);
    return result;
}

int pthread_rwlock_timedwrlock (pthread_rwlock_t *rwlock_, struct timespec *ts)
{
    int result=0;
    
    CHECK_RWLOCK(rwlock_);
	rwlock_t *rwlock = (rwlock_t *)*rwlock_;

	pthread_testcancel();
	if ( (result = pthread_mutex_lock (&rwlock->m)) )
        return result;

	if (rwlock->writers || rwlock->readers > 0) {
        rwlock->writers_count++;
        pthread_cleanup_push (_pthread_once_rwlock_writecleanup, (void*)rwlock);
        while (rwlock->writers || rwlock->readers > 0) {
			result = pthread_cond_timedwait (&rwlock->cw, &rwlock->m, ts);
			if (result) break;
        }
        pthread_cleanup_pop (0);
        rwlock->writers_count--;
    }
    if (result == 0)
        rwlock->writers = 1;

    UPD_RESULT(pthread_mutex_unlock (&rwlock->m), result);
    return result;
}
#else
int pthread_rwlock_init(pthread_rwlock_t *rwlock_, pthread_rwlockattr_t *a)
{
    int result=0;
	rwlock_t *rwlock;

	if ( !(rwlock = (pthread_rwlock_t)malloc(sizeof(*rwlock))) ) {
		return ENOMEM; 
	}

	memset(rwlock, 0,sizeof(*rwlock));
	InitializeSRWLock(&rwlock->l);
	
	rwlock->valid = LIFE_RWLOCK;
	*rwlock_ = rwlock;
	return 0;
}

int pthread_rwlock_destroy(pthread_rwlock_t *rwlock_)
{
    int result=0;
    
    CHECK_RWLOCK(rwlock_);
	rwlock_t *rwlock = (rwlock_t *)*rwlock_;

 	pthread_rwlock_t *rwlock2 = rwlock_;
	*rwlock_= NULL; /* dereference first, free later */
	_ReadWriteBarrier();
    rwlock->valid  = DEAD_RWLOCK;
	free(*rwlock2);
	return 0;
}

int pthread_rwlock_rdlock(pthread_rwlock_t *rwlock_)
{
    CHECK_RWLOCK(rwlock_);
	rwlock_t *rwlock = (rwlock_t *)*rwlock_;

	pthread_testcancel();
	AcquireSRWLockShared(&rwlock->l);
	
	return 0;
}

int pthread_rwlock_wrlock(pthread_rwlock_t *rwlock_)
{
    CHECK_RWLOCK(rwlock_);
	rwlock_t *rwlock = (rwlock_t *)*rwlock_;

	pthread_testcancel();
	AcquireSRWLockExclusive(&rwlock->l);
	
	return 0;
}

int pthread_rwlock_tryrdlock(pthread_rwlock_t *rwlock_)
{
    CHECK_RWLOCK(rwlock_);
	rwlock_t *rwlock = (rwlock_t *)*rwlock_;

	/* Get the current state of the lock */
	void *state = *(void **) &rwlock->l;
	
	if (!state)
	{
		/* Unlocked to locked */
		if (!InterlockedCompareExchangePointer((PVOID *)(&rwlock->l), (void *)0x11, NULL)) return 0;
		return EBUSY;
	}
	
	/* A single writer exists */
	if (state == (void *) 1) return EBUSY;
	
	/* Multiple writers exist? */
	if ((uintptr_t) state & 14) return EBUSY;
	
	if (InterlockedCompareExchangePointer((PVOID *)(&rwlock->l), (void *) ((uintptr_t)state + 16), state) == state) return 0;
	
	return EBUSY;
}

int pthread_rwlock_trywrlock(pthread_rwlock_t *rwlock_)
{
    CHECK_RWLOCK(rwlock_);
	rwlock_t *rwlock = (rwlock_t *)*rwlock_;

	/* Try to grab lock if it has no users */
	if (!InterlockedCompareExchangePointer((PVOID *)(&rwlock->l), (void *)1, NULL)) return 0;
	
	return EBUSY;
}

int pthread_rwlock_unlock(pthread_rwlock_t *rwlock_)
{
    CHECK_RWLOCK(rwlock_);
	rwlock_t *rwlock = (rwlock_t *)*rwlock_;
	void *state = *(void **) &rwlock->l;
	
	if (state == (void *) 1)
	{
		/* Known to be an exclusive lock */
		ReleaseSRWLockExclusive(&rwlock->l);
	}
	else
	{
		/* A shared unlock will work */
		ReleaseSRWLockShared(&rwlock->l);
	}
	
	return 0;
}

int pthread_rwlock_timedrdlock(pthread_rwlock_t *rwlock_, struct timespec *ts)
{
    CHECK_RWLOCK(rwlock_);
	rwlock_t *rwlock = (rwlock_t *)*rwlock_;

	unsigned long long ct = _pthread_time_in_ms();
	unsigned long long t = _pthread_time_in_ms_from_timespec(ts);

	pthread_testcancel();
	
	/* Use a busy-loop */
	while (1)
	{
		/* Try to grab lock */
		if (!pthread_rwlock_tryrdlock(rwlock_)) return 0;
		
		/* Get current time */
		ct = _pthread_time_in_ms();
		
		/* Have we waited long enough? */
		if (ct > t) return ETIMEDOUT;
	}
}

int pthread_rwlock_timedwrlock(pthread_rwlock_t *rwlock_, struct timespec *ts)
{
    CHECK_RWLOCK(rwlock_);
	rwlock_t *rwlock = (rwlock_t *)*rwlock_;

	unsigned long long ct = _pthread_time_in_ms();
	unsigned long long t = _pthread_time_in_ms_from_timespec(ts);

	pthread_testcancel();
	
	/* Use a busy-loop */
	while (1)
	{
		/* Try to grab lock */
		if (!pthread_rwlock_trywrlock(rwlock_)) return 0;
		
		/* Get current time */
		ct = _pthread_time_in_ms();
		
		/* Have we waited long enough? */
		if (ct > t) return ETIMEDOUT;
	}
}
#endif

int pthread_rwlockattr_destroy(pthread_rwlockattr_t *a)
{
	(void) a;
	return 0;
}

int pthread_rwlockattr_init(pthread_rwlockattr_t *a)
{
	*a = 0;
	return 0;
}

int pthread_rwlockattr_getpshared(pthread_rwlockattr_t *a, int *s)
{
	*s = *a;
	return 0;
}

int pthread_rwlockattr_setpshared(pthread_rwlockattr_t *a, int s)
{
	*a = s;
	return 0;
}


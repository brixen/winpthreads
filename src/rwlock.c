#include <windows.h>
#include <stdio.h>
#include "pthread.h"
#include "thread.h"
#include "ref.h"
#include "rwlock.h"
#include "spinlock.h"
#include "misc.h"

static int print_state = 1;
void rwl_print_set(int state)
{
    print_state = state;
}

void rwl_print(volatile pthread_rwlock_t *rwl, char *txt)
{
    if (!print_state) return;
    rwlock_t *r = (rwlock_t *)*rwl;
    if (r == NULL) {
        printf("RWL%p %d %s\n",*rwl,(int)GetCurrentThreadId(),txt);
    } else {
        printf("RWL%p %d V=%0X B=%d r=%ld w=%ld L=%p %s\n",
            *rwl, 
            (int)GetCurrentThreadId(), 
            (int)r->valid, 
            (int)r->busy,
#ifdef USE_RWLOCK_pthread_cond
            (LONG)r->readers,(LONG)r->writers,NULL,txt);
#else
            r->treaders_count,r->twriters_count,r->l.Ptr,txt);
#endif
    }
}

inline int rwlock_static_init(volatile pthread_rwlock_t *r)
{
    pthread_rwlock_t r_tmp=NULL, *r_replaced;
    int res = pthread_rwlock_init(&r_tmp, NULL);

    if (!res) {
        r_replaced = (pthread_rwlock_t *)InterlockedCompareExchangePointer(
            (PVOID *)r, 
            r_tmp,
            PTHREAD_RWLOCK_INITIALIZER);
        if (r_replaced != (pthread_rwlock_t *)PTHREAD_RWLOCK_INITIALIZER) {
            printf("rwlock_static_init race detected: rwlock %p\n",  r_replaced);
            /* someone crept in between: */
            pthread_rwlock_destroy(&r_tmp);
            /* it could even be destroyed: */
            if (!r_replaced) res = EINVAL;
        }
    }
    return res;
}

inline int rwl_check_owner(pthread_rwlock_t *rwl, int flags)
{
    int r=0;

    pthread_t t = pthread_self();
    if (t->rwlc >= RWLS_PER_THREAD) {
        r = EAGAIN;
    } else if (t->rwlc && (t->rwlq[t->rwlc-1] == rwl)) {
        r = (flags&RWL_TRY) ? EBUSY : EDEADLK;
    } else if (flags&RWL_SET) {
        t->rwlq[t->rwlc++] = rwl;
    }

    return r;
}

inline int rwl_unset_owner(pthread_rwlock_t *rwl, int flags)
{
    int r=0;

    pthread_t t = pthread_self();
    if (t->rwlc && t->rwlq[t->rwlc-1] == rwl) {
        if ((flags&RWL_SET)) {
            t->rwlc --;
        }
    } else {
        r = EPERM;
    }

    return r;
}

int pthread_rwlock_destroy (pthread_rwlock_t *rwlock_)
{
    pthread_rwlock_t rDestroy;
    int r = rwl_ref_destroy(rwlock_,&rDestroy);
    if(r) return r;
    if(!rDestroy) return 0; /* destroyed a (still) static initialized rwl */

    rwlock_t *rwlock = (rwlock_t *)rDestroy;
#ifdef USE_RWLOCK_pthread_cond
    pthread_mutex_lock (&rwlock->m);
 
    if (rwlock->readers_count != 0 || rwlock->writers_count != 0) {
        /* Could this happen? */
        *rwlock_ = rDestroy;
        pthread_mutex_unlock (&rwlock->m);
        return rwl_unref(rwlock_,EBUSY);
    }

    UPD_RESULT(pthread_mutex_unlock (&rwlock->m), r);
    UPD_RESULT(pthread_mutex_destroy(&rwlock->m), r);
    UPD_RESULT(pthread_cond_destroy (&rwlock->cw), r);

#else /* USE_RWLOCK_SRWLock */
    CloseHandle(rwlock->semTimedR);
    CloseHandle(rwlock->semTimedW);

#endif
    rwlock->valid  = DEAD_RWLOCK;
    free(rDestroy);
    return 0;
} 

int pthread_rwlock_init (pthread_rwlock_t *rwlock_, const pthread_rwlockattr_t *attr)
{
    int r = rwl_ref_init(rwlock_);
    if(r) return r;

    rwlock_t *rwlock;

    if ( !(rwlock = (pthread_rwlock_t)calloc(1, sizeof(*rwlock))) ) {
        return ENOMEM; 
    }
    rwlock->valid = DEAD_RWLOCK;

#ifdef USE_RWLOCK_pthread_cond
    if ( (r = pthread_mutex_init (&rwlock->m, NULL)) ) {
        free(rwlock);
        return r;
    }

    if ( (r = pthread_cond_init (&rwlock->cr, NULL)) ) {
        pthread_mutex_destroy (&rwlock->m);
        free(rwlock);
        return r;
    }

    if ( (r = pthread_cond_init (&rwlock->cw, NULL)) ) {
        pthread_cond_destroy (&rwlock->cr);
        pthread_mutex_destroy (&rwlock->m);
        free(rwlock);
        return r;
    }

#else /* USE_RWLOCK_SRWLock */
    InitializeSRWLock(&rwlock->l);
    rwlock->semTimedR = CreateSemaphore (NULL, 0, 1, NULL);
    if (!rwlock->semTimedR) {
        free(rwlock);
        return ENOMEM;
    }
    rwlock->semTimedW = CreateSemaphore (NULL, 0, 1, NULL);
    if (!rwlock->semTimedW) {
        CloseHandle(rwlock->semTimedR);
        free(rwlock);
        return ENOMEM;
    }
    
#endif
    rwlock->valid = LIFE_RWLOCK;
    *rwlock_ = rwlock;
    return r;
} 

#ifdef USE_RWLOCK_pthread_cond
static void _pthread_once_rwlock_readcleanup (pthread_once_t *arg)
{
    rwlock_t   *rwlock = (rwlock_t *)arg;

    rwlock->readers_count--;
    pthread_mutex_unlock (&rwlock->m);
}
#endif

int pthread_rwlock_rdlock (pthread_rwlock_t *rwlock_)
{
    int result = rwl_ref(rwlock_,0);
    if(result) return result;
    
    rwlock_t *rwlock = (rwlock_t *)*rwlock_;

    pthread_testcancel();
#ifdef USE_RWLOCK_pthread_cond
    if ( (result = pthread_mutex_lock (&rwlock->m)) ) {
        return rwl_unref(rwlock_, result);
    }

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

#else /* USE_RWLOCK_SRWLock */
    AcquireSRWLockShared(&rwlock->l);
    
#endif
    if (!result) result = rwl_check_owner(rwlock_,RWL_SET);
    return rwl_unref(rwlock_, result);
}

int pthread_rwlock_timedrdlock (pthread_rwlock_t *rwlock_, struct timespec *ts)
{
    if (!ts) return EINVAL;
    int result = rwl_ref(rwlock_,0);
    if(result) return result;

    rwlock_t *rwlock = (rwlock_t *)*rwlock_;

    pthread_testcancel();
#ifdef USE_RWLOCK_pthread_cond
    if ( (result = pthread_mutex_lock (&rwlock->m)) )
        return rwl_unref(rwlock_, result);

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

    if (!result) {
          result = rwl_check_owner(rwlock_,RWL_SET);
          if (!result) rwlock->readers++;
    }
    UPD_RESULT(pthread_mutex_unlock (&rwlock->m), result);

#else /* USE_RWLOCK_SRWLock */
    unsigned long long ct = _pthread_time_in_ms();
    unsigned long long t = _pthread_time_in_ms_from_timespec(ts);

#ifdef USE_RWLOCK_SRWLock_Sync
/* fix the busy-wait problem with 2 semaphores */
    uintptr_t s;

    if (!pthread_rwlock_tryrdlock(rwlock_)) {
        printf("%ld: pthread_rwlock_timedrdlock try success:  %ld ms\n",GetCurrentThreadId(), dwMilliSecs(t - ct));
        return rwl_unref(rwlock_, 0);
    }
    InterlockedIncrement(&rwlock->treaders_count);
    while (1)
    {
        /* Wait for the semaphore: */
        printf("%ld: pthread_rwlock_timedrdlock wait:  %ld ms c=%ld\n",GetCurrentThreadId(), dwMilliSecs(t - ct), rwlock->treaders_count);
        switch (WaitForSingleObject(rwlock->semTimedR, dwMilliSecs(t - ct))) {
        case WAIT_OBJECT_0:
            /* Try to grab lock && check if it is a rdlock: */
            printf("%ld: pthread_rwlock_timedrdlock Try to grab lock\n",GetCurrentThreadId());
            if (!(result=pthread_rwlock_tryrdlock(rwlock_))) {
                InterlockedDecrement(&rwlock->treaders_count);
                printf("%ld: pthread_rwlock_timedrdlock grabbed lock c=%ld\n",GetCurrentThreadId(), rwlock->treaders_count);
                return rwl_unref(rwlock_, 0);
            }
            if( result!=EBUSY) return rwl_unref(rwlock_, result);
            printf("%ld: pthread_rwlock_timedrdlock Try to grab lock failed\n",GetCurrentThreadId());

            /* Not a rdlock for us, redo the signal and wait again. */
            s = (uintptr_t)(*(void **) &rwlock->l);
            if (!(s&(RTL_SRWLOCK_OWNED|RTL_SRWLOCK_CONTENDED))) {
                printf("%ld: pthread_rwlock_timedrdlock re-release\n",GetCurrentThreadId());
                ReleaseSemaphore(rwlock->semTimedR, 1, NULL);
                Sleep(10);
            } 
            /* else have to wait on an unlock anyway */
            break;

        case WAIT_TIMEOUT:
            printf("%ld: pthread_rwlock_timedrdlock WAIT_TIMEOUT\n",GetCurrentThreadId());
            break;

        default:
            printf("%ld: pthread_rwlock_timedrdlock GLE=%ld\n",GetCurrentThreadId(), GetLastError());
            InterlockedDecrement(&rwlock->treaders_count);
            return rwl_unref(rwlock_, EINVAL);
        }

        /* Get current time */
        ct = _pthread_time_in_ms();
        
        /* Have we waited long enough? */
        if (ct > t) {
            InterlockedDecrement(&rwlock->treaders_count);
            return rwl_unref(rwlock_, ETIMEDOUT);
        }
    }

#else /* Busy wait solution */
    (void)rwlock; /* Awaiting fix below */
    /* SRWLock are most sophisticated but still must use a busy-loop here.*/ 
    /* Unfortunately, NtWaitForKeyedEvent is needed to fix this eventually */
    while (1)
    {
        /* Try to grab lock */
        if (!(result=pthread_rwlock_tryrdlock(rwlock_))) {
            return rwl_unref(rwlock_, 0);
        }
        if( result!=EBUSY) return rwl_unref(rwlock_, result);
        
        /* Get current time */
        ct = _pthread_time_in_ms();
        
        /* Have we waited long enough? */
        if (ct > t) return rwl_unref(rwlock_, ETIMEDOUT);
    }

#endif /* Busy wait solution */
#endif /* USE_RWLOCK_SRWLock */
    return rwl_unref(rwlock_, result);
}

int pthread_rwlock_tryrdlock (pthread_rwlock_t *rwlock_)
{
    int result = rwl_ref(rwlock_,RWL_TRY);
    if(result) return result;

    rwlock_t *rwlock = (rwlock_t *)*rwlock_;
#ifdef USE_RWLOCK_pthread_cond
    if ( (result = pthread_mutex_lock (&rwlock->m)) )
        return rwl_unref(rwlock_, result);

    if (rwlock->writers)
        result = EBUSY;
    else
        rwlock->readers++;
 
    UPD_RESULT(pthread_mutex_unlock (&rwlock->m), result);
 
#else /* USE_RWLOCK_SRWLock */
    /* Get the current state of the lock */
    void *state = *(void **) &rwlock->l;
    
    if (!state)
    {
        /* Unlocked to locked */
        if (!InterlockedCompareExchangePointer((PVOID *)(&rwlock->l), (void *)(16+RTL_SRWLOCK_OWNED), NULL))
            return rwl_unref(rwlock_,rwl_check_owner(rwlock_,RWL_SET));
        return rwl_unref(rwlock_,EBUSY);
    }
    
    /* A single writer exists */
    if (state == (void *) RTL_SRWLOCK_OWNED) return rwl_unref(rwlock_,EBUSY);
    
    /* Multiple writers exist? */
    if ((uintptr_t) state & RTL_SRWLOCK_LOCKED) return rwl_unref(rwlock_,EBUSY);
    
    if (InterlockedCompareExchangePointer((PVOID *)(&rwlock->l), (void *) ((uintptr_t)state + 16), state) == state)
            return rwl_unref(rwlock_,rwl_check_owner(rwlock_,RWL_SET));
    
    result = EBUSY;

#endif
    if (!result) result = rwl_check_owner(rwlock_,RWL_SET);
    return rwl_unref(rwlock_,result);
} 

int pthread_rwlock_trywrlock (pthread_rwlock_t *rwlock_)
{
    int result = rwl_ref(rwlock_,RWL_TRY);
    if(result) return result;

    rwlock_t *rwlock = (rwlock_t *)*rwlock_;
#ifdef USE_RWLOCK_pthread_cond
    if ( (result = pthread_mutex_lock (&rwlock->m)) )
        return rwl_unref(rwlock_, result);

    if (rwlock->writers || rwlock->readers)
        result = EBUSY;
    else {
        rwlock->writers = 1;
    }

    UPD_RESULT(pthread_mutex_unlock (&rwlock->m), result);

#else /* USE_RWLOCK_SRWLock */
    /* Try to grab lock if it has no users */
    if (!InterlockedCompareExchangePointer((PVOID *)(&rwlock->l), (void *)RTL_SRWLOCK_OWNED, NULL)) {
        return rwl_unref(rwlock_, rwl_check_owner(rwlock_,RWL_SET));
    }
    
    result = EBUSY;

#endif
    if (!result) result = rwl_check_owner(rwlock_,RWL_SET);
    return rwl_unref(rwlock_, result);
} 

int pthread_rwlock_unlock (pthread_rwlock_t *rwlock_)
{
    int result = rwl_ref_unlock(rwlock_);
    if(result) return result;

    /* Check ownership here because the ReleaseSRW* functions
     * would throw an exception instead of return an error.
     * Still need SEH for other errors it could throw.
     */
    rwlock_t *rwlock = (rwlock_t *)*rwlock_;
#ifdef USE_RWLOCK_pthread_cond
    if ( (result = pthread_mutex_lock (&rwlock->m)) )
        return rwl_unref(rwlock_, result);

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

#else /* USE_RWLOCK_SRWLock */
    void *state = *(void **) &rwlock->l;
    uintptr_t s = (uintptr_t)state;

    
    if (s == RTL_SRWLOCK_OWNED){
        /* Known to be an exclusive lock */
        printf("A ReleaseSRWLockExclusive rwlock->treaders_count=%ld\n",rwlock->treaders_count);
        ReleaseSRWLockExclusive(&rwlock->l);
#ifdef USE_RWLOCK_SRWLock_Sync
        printf("B ReleaseSRWLockExclusive rwlock->treaders_count=%ld\n",rwlock->treaders_count);
        if (rwlock->treaders_count) {
            /* favour readers */
            ReleaseSemaphore(rwlock->semTimedR, rwlock->treaders_count, NULL);
        } else {
            ReleaseSemaphore(rwlock->semTimedW, AT_MOST_1(rwlock->twriters_count), NULL);
        }
#endif
    } else {
        /* A shared unlock will work */
        printf("A ReleaseSRWLockShared rwlock->treaders_count=%ld\n",rwlock->treaders_count);
        ReleaseSRWLockShared(&rwlock->l);
#ifdef USE_RWLOCK_SRWLock_Sync
        printf("B ReleaseSRWLockShared rwlock->treaders_count=%ld\n",rwlock->treaders_count);
        if ( (s>16) && !((s-RTL_SRWLOCK_OWNED)%16) ) {
            /* unlock by shared reader: */
            if (s == 16+RTL_SRWLOCK_OWNED) {
                /* the last one: */
                if (rwlock->twriters_count) {
                    /* favour writers this time */
                    ReleaseSemaphore(rwlock->semTimedW, AT_MOST_1(rwlock->twriters_count), NULL);
                } else {
                    ReleaseSemaphore(rwlock->semTimedR, rwlock->treaders_count, NULL);
                }
            }
        } else {
            /* anything else (contention), wake readers only (contenders are never timed)
             * Writers would have to wait anyway for one of the contenders.
             */
            ReleaseSemaphore(rwlock->semTimedR, rwlock->treaders_count, NULL);
        }
#endif /* USE_RWLOCK_SRWLock_Sync */	
    }
#endif
    if (!result) result = rwl_unset_owner(rwlock_,RWL_SET);
    return rwl_unref(rwlock_, result);
} 

#ifdef USE_RWLOCK_pthread_cond
static void _pthread_once_rwlock_writecleanup (pthread_once_t *arg)
{
    rwlock_t *rwlock = (rwlock_t *)arg;

    rwlock->writers_count--;
    pthread_mutex_unlock (&rwlock->m);
}
#endif

int pthread_rwlock_wrlock (pthread_rwlock_t *rwlock_)
{
    int result = rwl_ref(rwlock_,0);
    if(result) return result;
  
    rwlock_t *rwlock = (rwlock_t *)*rwlock_;
    pthread_testcancel();
#ifdef USE_RWLOCK_pthread_cond
    if ( (result = pthread_mutex_lock (&rwlock->m)) )
        return rwl_unref(rwlock_,result);

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

#else /* USE_RWLOCK_SRWLock */
    AcquireSRWLockExclusive(&rwlock->l);

#endif
    if (!result) result = rwl_check_owner(rwlock_,RWL_SET);
    return rwl_unref(rwlock_,result);
}

int pthread_rwlock_timedwrlock (pthread_rwlock_t *rwlock_, struct timespec *ts)
{
    if (!ts) return EINVAL;
    int result = rwl_ref(rwlock_,0);
    if(result) return result;
    
    rwlock_t *rwlock = (rwlock_t *)*rwlock_;

    pthread_testcancel();
#ifdef USE_RWLOCK_pthread_cond
    if ( (result = pthread_mutex_lock (&rwlock->m)) )
        return rwl_unref(rwlock_,result);

    if (rwlock->writers || rwlock->readers) {
        rwlock->writers_count++;
        pthread_cleanup_push (_pthread_once_rwlock_writecleanup, (void*)rwlock);
        while (rwlock->writers || rwlock->readers > 0) {
            result = pthread_cond_timedwait (&rwlock->cw, &rwlock->m, ts);
            if (result) break;
        }
        pthread_cleanup_pop (0);
        rwlock->writers_count--;
    }
    if (!result) {
          result = rwl_check_owner(rwlock_,RWL_SET);
          if (!result) rwlock->writers = 1;
    }

    UPD_RESULT(pthread_mutex_unlock (&rwlock->m), result);

#else /* USE_RWLOCK_SRWLock */
    unsigned long long ct = _pthread_time_in_ms();
    unsigned long long t = _pthread_time_in_ms_from_timespec(ts);

#ifdef USE_RWLOCK_SRWLock_Sync
    uintptr_t s;

    if (!pthread_rwlock_trywrlock(rwlock_)) {
        return rwl_unref(rwlock_,0);
    }
    InterlockedIncrement(&rwlock->twriters_count);
    while (1)
    {
        /* Wait for the semaphore: */
        switch (WaitForSingleObject(rwlock->semTimedW, dwMilliSecs(t - ct))) {
        case WAIT_OBJECT_0:
            /* Try to grab lock && check if it is a wrlock: */
            printf("%ld: pthread_rwlock_timedwrlock Try to grab lock\n",GetCurrentThreadId());
            if (!( result=pthread_rwlock_trywrlock(rwlock_) )) {
                InterlockedDecrement(&rwlock->twriters_count);
                return rwl_unref(rwlock_,0);
            }
            if( result!=EBUSY) return rwl_unref(rwlock_, result);
            printf("%ld: pthread_rwlock_timedwrlock Try to grab lock failed\n",GetCurrentThreadId());

            /* Not a wrlock for us, redo the signal and wait again. */
            s = (uintptr_t)(*(void **) &rwlock->l);
            if (!(s&(RTL_SRWLOCK_OWNED|RTL_SRWLOCK_CONTENDED))) {
                printf("%ld: pthread_rwlock_timedwrlock re-release\n",GetCurrentThreadId());
                ReleaseSemaphore(rwlock->semTimedW, 1, NULL);
                Sleep(10);
            }
            break;

        case WAIT_TIMEOUT:
            printf("%ld: pthread_rwlock_timedwrlock WAIT_TIMEOUT\n",GetCurrentThreadId());
            break;

        default:
            printf("%ld: pthread_rwlock_timedwrlock GLE=%ld\n",GetCurrentThreadId(), GetLastError());
            InterlockedDecrement(&rwlock->twriters_count);
            return rwl_unref(rwlock_,EINVAL);
        }

        /* Get current time */
        ct = _pthread_time_in_ms();
        
        /* Have we waited long enough? */
        if (ct > t) {
            InterlockedDecrement(&rwlock->twriters_count);
            return rwl_unref(rwlock_,ETIMEDOUT);
        }
    }

#else /* Busy wait solution */
    (void)rwlock; /* Awaiting fix below */
    /* Use a busy-loop here too. Ha ha. */
    while (1)
    {
        /* Try to grab lock */
        if (!( result=pthread_rwlock_trywrlock(rwlock_) )) {
            return rwl_unref(rwlock_,0);
        }
        if( result!=EBUSY) return rwl_unref(rwlock_, result);
        
        /* Get current time */
        ct = _pthread_time_in_ms();
        
        /* Have we waited long enough? */
        if (ct > t) return rwl_unref(rwlock_,ETIMEDOUT);
    }

#endif /* Busy wait solution */
#endif /* USE_RWLOCK_SRWLock */
    return rwl_unref(rwlock_,result);
}

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


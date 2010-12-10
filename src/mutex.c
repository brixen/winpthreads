#include <windows.h>
#include <winternl.h>
#include <stdio.h>
#include "pthread.h"
#include "spinlock.h"
#include "ref.h"
#include "mutex.h"
#include "misc.h"

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
        printf("M%p %d %s\n",*m,(int)GetCurrentThreadId(),txt);
    } else {
        printf("M%p %d V=%0X B=%d t=%d o=%d C=%d R=%d H=%p %s\n",
            *m, 
            (int)GetCurrentThreadId(), 
            (int)m_->valid, 
            (int)m_->busy,
            m_->type,
            (int)GET_OWNER(m_),(int)(m_->count),(int)GET_RCNT(m_),GET_HANDLE(m_),txt);
    }
}

inline int mutex_static_init(volatile pthread_mutex_t *m )
{
    pthread_mutex_t m_tmp=NULL;
    mutex_t *mi, *m_replaced;

    if (!STATIC_INITIALIZER(mi = (mutex_t *)*m) ) {
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
    if (_m->type != PTHREAD_MUTEX_NORMAL)
    {
      if (COND_LOCKED(_m))
      {
	if (COND_OWNER(_m))
	{
	  if (_m->type == PTHREAD_MUTEX_RECURSIVE)
	  {
	    InterlockedIncrement(&_m->count);
	    return mutex_unref(m,0);
	  }
	  return mutex_unref(m, EDEADLK);
	}
      }
    } else {
      if (COND_OWNER(_m))
      {
	do {
	  Sleep(0);
	} while (COND_OWNER(_m));
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
    EnterCriticalSection(&_m->cs.cs);
#endif
    _m->count = 1;
    SET_OWNER(_m);
    return mutex_unref(m,0);

}

/*	
    See this article: http://msdn.microsoft.com/nl-nl/magazine/cc164040(en-us).aspx#S8
    Unfortunately, behaviour has changed in Win 7 and maybe Vista (untested). Who said
    "undocumented"? Some testing and tracing revealed the following:
    On Win 7:
    - LockCount is DECREMENTED with 4. The low bit(0) is set when the cs is unlocked. 
      Also bit(1) is set, so lock sequence goes like -2,-6, -10 ... etc.
    - The LockSemaphore event is only auto-initialized at contention. 
      Although pthread_mutex_timedlock() seemed to work, it just busy-waits, because
      WaitForSingleObject() always returns WAIT_FAILED+INVALID_HANDLE. 
    Solution on Win 7:
    - The LockSemaphore member MUST be auto-initialized and only at contention. 
      Pre-initializing messes up the cs state (causes EnterCriticalSection() to hang)
    - Before the lock (wait): 
      - auto-init LockSemaphore
      - LockCount is DECREMENTED with 4
    - Data members must be also correctly updated when the lock is obtained, after
      waiting on the event:
      - Increment LockCount, setting the low bit
      - set RecursionCount to 1
      - set OwningThread to CurrentThreadId()
    - When the wait failed or timed out, reset LockCount (INCREMENT with 4)
    On Win XP (2000): (untested)
    - LockCount is updated with +1 as documented in the article,
      so lock sequence goes like -1, 0, 1, 2 ... etc.
    - The event is obviously also auto-initialized in TryEnterCriticalSection. That is, 
      we hope so, otherwise the original implementation never worked correctly in the
      first place (just busy-waits, munching precious CPU time).
    - Data members are also correctly updated in TryEnterCriticalSection. Same
      hope / assumption here.
    We do an one-time test lock and draw conclusions based on the resulting value
    of LockCount: 0=XP behaviour, -2=Win 7 behaviour. Also crossing fingers helps.
*/
static LONG LockDelta	= -4; /* Win 7 default */

static inline int _InitWaitCriticalSection(volatile RTL_CRITICAL_SECTION *prc)
{
    int r = 0;
    HANDLE evt;
    LONG LockCount = prc->LockCount;

    r = 0;
    if (!prc->OwningThread || !prc->RecursionCount || (LockCount & 1)) {
        /* not locked (anymore), caller should redo trylock sequence: */
        return EAGAIN;
    } else {
        _ReadWriteBarrier();
        if( LockCount != InterlockedCompareExchange(&prc->LockCount, LockCount+LockDelta, LockCount) ) {
            /* recheck here too: */
            return EAGAIN;
        }
    }

    if ( !prc->LockSemaphore) {
        if (!(evt =  CreateEvent(NULL,FALSE,FALSE,NULL)) ) {
            InterlockedExchangeAdd(&prc->LockCount, -LockDelta);
            return ENOMEM;
        }
        if(InterlockedCompareExchangePointer(&prc->LockSemaphore,evt,NULL)) {
            /* someone sneaked in between, keep the original: */
            CloseHandle(evt);
        }
    }

    return r;
}

/* the wait failed, so we have to restore the LockCount member */
static inline void _UndoWaitCriticalSection(volatile RTL_CRITICAL_SECTION *prc)
{
        InterlockedExchangeAdd(&prc->LockCount, -LockDelta);
}

int pthread_mutex_timedlock(pthread_mutex_t *m, struct timespec *ts)
{
    unsigned long long t, ct;
    int rwait = 0, i = 0, r;

    if (!ts) return pthread_mutex_lock(m);
    r = mutex_ref(m);
    if (r) return r;

    /* Try to lock it without waiting */
    r=_mutex_trylock(m);
    if (r != EBUSY ) return mutex_unref(m,r);
    
    mutex_t *_m = (mutex_t *)*m;
    if (_m->type != PTHREAD_MUTEX_NORMAL && COND_LOCKED(_m) && COND_OWNER(_m))
      return mutex_unref(m,EDEADLK);
    mutex_unref(m,EDEADLK);
    ct = _pthread_time_in_ms();
    t = _pthread_time_in_ms_from_timespec(ts);
    
    while (1)
    {
        /* Have we waited long enough? A high count means we busy-waited probably.*/
        if (ct >= t) {
            printf("%d: Timeout after %d times\n",(int)GetCurrentThreadId(), i);
            return ETIMEDOUT;
        }
        Sleep(0);
        /* Try to grab lock */
	r = pthread_mutex_trylock(m);
	if (r != EBUSY) break;
        /* Get current time */
        ct = _pthread_time_in_ms();
        i ++;
    }
    return  r;
}

int pthread_mutex_unlock(pthread_mutex_t *m)
{
    int r = mutex_ref_unlock(m);
    if(r) return r;

    mutex_t *_m = (mutex_t *)*m;
    if (_m->type == PTHREAD_MUTEX_NORMAL)
    {
        if (!COND_LOCKED(_m))
	  return mutex_unref(m,EPERM);
    }
    else if (!COND_LOCKED(_m) || !COND_OWNER(_m))
        return mutex_unref(m,EPERM);
    if (_m->type == PTHREAD_MUTEX_RECURSIVE)
    {
      if(InterlockedDecrement(&_m->count))
	return mutex_unref(m,0);
    }
#if defined USE_MUTEX_Mutex
    UNSET_OWNER(_m);
    if (_m->h != NULL && !ReleaseMutex(_m->h)) {
    	SET_OWNER(_m);
        /* restore our own bookkeeping */
        return mutex_unref(m,EPERM);
    }
#else /* USE_MUTEX_CriticalSection */
    UNSET_OWNER(_m);
    LeaveCriticalSection(&_m->cs.cs);
#endif
    return mutex_unref(m,0);
}

int _mutex_trylock(pthread_mutex_t *m)
{
    int r = 0;
    mutex_t *_m = (mutex_t *)*m;
    if (_m->type != PTHREAD_MUTEX_NORMAL)
    {
      if (COND_LOCKED(_m))
      {
	if (_m->type == PTHREAD_MUTEX_RECURSIVE && COND_OWNER(_m))
	{
	  InterlockedIncrement(&_m->count);
	  return 0;
	}
	return EBUSY;
      }
    } else if (COND_LOCKED(_m))
      return EBUSY;
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
    r = TryEnterCriticalSection(&_m->cs.cs) ? 0 : EBUSY;
#endif
    if (!r)
    {
      _m->count = 1;
      SET_OWNER(_m);
    }
    return r;
}

int pthread_mutex_trylock(pthread_mutex_t *m)
{
    int r = mutex_ref(m);
    if(r) return r;

    return mutex_unref(m,_mutex_trylock(m));
}

static LONG InitOnce	= 1;
static void _mutex_init_once(mutex_t *m)
{
#if defined USE_MUTEX_CriticalSection
    LONG lc = 0;
    EnterCriticalSection(&m->cs.cs);
    lc = m->cs.rc.LockCount;
    LeaveCriticalSection(&m->cs.cs);
    switch (lc) {
    case 0: /* Win XP + 2k(?) */
        LockDelta = 1;
        break;
    case -2:  /* Win 7 + Vista(?) */
        LockDelta = -4;
        break;
    default:
        /* give up */
        assert(FALSE);
    }
#endif
}

int pthread_mutex_init(pthread_mutex_t *m, pthread_mutexattr_t *a)
{
    mutex_t *_m;

    int r = mutex_ref_init(m);
    if(r) return r;

    if (!(_m = (pthread_mutex_t)calloc(1,sizeof(*_m))))
      return ENOMEM; 

    _m->type = PTHREAD_MUTEX_DEFAULT;
    _m->count = 0;

    if (a) {
        int share = PTHREAD_PROCESS_SHARED;
        r = pthread_mutexattr_gettype(a, &_m->type);
        if (!r) r = pthread_mutexattr_getpshared(a, &share);
        if (!r && share == PTHREAD_PROCESS_SHARED) r = ENOSYS;
    }
    if (!r) {
#if defined USE_MUTEX_Mutex
        if ( (_m->h = CreateMutex(NULL, FALSE, NULL)) != NULL) {
#else /* USE_MUTEX_CriticalSection */
        if (InitializeCriticalSectionAndSpinCount(&_m->cs.cs, USE_MUTEX_CriticalSection_SpinCount)) {
#endif
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
    if (r)
    {
        _m->valid = DEAD_MUTEX;
        free(_m);
        *m = NULL;
        return r;
    }
    if (InterlockedExchange(&InitOnce, 0))
	    _mutex_init_once(_m);
    _m->valid = LIFE_MUTEX;
    *m = _m;

    return 0;
}

int pthread_mutex_destroy(pthread_mutex_t *m)
{
    pthread_mutex_t mDestroy;
    int r = mutex_ref_destroy(m,&mDestroy);
    if(r) return r;
    if(!mDestroy) return 0; /* destroyed a (still) static initialized mutex */

    /* now the mutex is invalid, and no one can touch it */
    mutex_t *_m = (mutex_t *)mDestroy;


#if defined USE_MUTEX_Mutex
    CloseHandle(_m->h);
#else /* USE_MUTEX_CriticalSection */
    DeleteCriticalSection(&_m->cs.cs);
#endif
    _m->valid = DEAD_MUTEX;
    _m->type  = 0;
    _m->count = 0;
    free(mDestroy);
    return 0;
}

int pthread_mutexattr_init(pthread_mutexattr_t *a)
{
    *a = PTHREAD_MUTEX_NORMAL | (PTHREAD_PROCESS_PRIVATE << 3);
    return 0;
}

int pthread_mutexattr_destroy(pthread_mutexattr_t *a)
{
    if (!a)
      return EINVAL;
    return 0;
}

int pthread_mutexattr_gettype(pthread_mutexattr_t *a, int *type)
{
    if (!a || !type)
      return EINVAL;
    *type = *a & 3;

    return 0;
}

int pthread_mutexattr_settype(pthread_mutexattr_t *a, int type)
{
    if (!a || (type != PTHREAD_MUTEX_NORMAL && type != PTHREAD_MUTEX_RECURSIVE && type != PTHREAD_MUTEX_ERRORCHECK))
      return EINVAL;
    *a &= ~3;
    *a |= type;

    return 0;
}

int pthread_mutexattr_getpshared(pthread_mutexattr_t *a, int *type)
{
    if (!a || !type)
      return EINVAL;
    *type = (*a & 4 ? PTHREAD_PROCESS_SHARED : PTHREAD_PROCESS_PRIVATE);

    return 0;
}

int pthread_mutexattr_setpshared(pthread_mutexattr_t * a, int type)
{
    int r = 0;
    if (!a || (type != PTHREAD_PROCESS_SHARED && type != PTHREAD_PROCESS_PRIVATE))
      return EINVAL;
    if (type == PTHREAD_PROCESS_SHARED)
    {
      type = PTHREAD_PROCESS_PRIVATE;
      r = ENOSYS;
    }
    type = (type == PTHREAD_PROCESS_SHARED ? 4 : 0);

    *a &= ~4;
    *a |= type;

    return r;
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

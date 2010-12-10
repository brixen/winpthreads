/*
 * Posix Condition Variables for Microsoft Windows.
 * 22-9-2010 Partly based on the ACE framework implementation.
 */
#include <windows.h>
#include <stdio.h>
#include "pthread.h"
#include "ref.h"
#include "cond.h"
#include "mutex.h"
#include "spinlock.h"

#include "misc.h"

static int print_state = 0;
static FILE *fo = NULL;

void cond_print_set(int state, FILE *f)
{
    if (f) fo = f;
    if (!fo) fo = stdout;
    print_state = state;
}

void cond_print(volatile pthread_cond_t *c, char *txt)
{
    if (!print_state) return;
    cond_t *c_ = (cond_t *)*c;
    if (c_ == NULL) {
        fprintf(fo,"C%p %d %s\n",*c,(int)GetCurrentThreadId(),txt);
    } else {
        fprintf(fo,"C%p %d V=%0X B=%d b=%p w=%ld %s\n",
            *c, 
            (int)GetCurrentThreadId(), 
            (int)c_->valid, 
            (int)c_->busy,
            NULL,
            c_->waiters_count_,
            txt
            );
    }
}

static spin_t cond_locked = {0,LIFE_SPINLOCK,0};

static int cond_static_init(pthread_cond_t *c)
{
  int r = 0;
  
  _spin_lite_lock(&cond_locked);
  if (*c == NULL)
    r = EINVAL;
  else if (*c == PTHREAD_COND_INITIALIZER)
    r = pthread_cond_init (c, NULL);
  else
    /* We assume someone was faster ... */
    r = 0;
  _spin_lite_unlock(&cond_locked);
  return r;
}

int pthread_condattr_destroy(pthread_condattr_t *a)
{
  if (!a)
    return EINVAL;
   *a = 0;
   return 0;
}

int pthread_condattr_init(pthread_condattr_t *a)
{
  if (!a)
    return EINVAL;
  *a = 0;
  return 0;
}

int pthread_condattr_getpshared(pthread_condattr_t *a, int *s)
{
  if (!a || !s)
    return EINVAL;
  *s = *a;
  return 0;
}

int pthread_condattr_setpshared(pthread_condattr_t *a, int s)
{
  if (!a || (s != PTHREAD_PROCESS_SHARED && s != PTHREAD_PROCESS_PRIVATE))
    return EINVAL;
  if (s == PTHREAD_PROCESS_SHARED)
  {
     *a = PTHREAD_PROCESS_PRIVATE;
     return ENOSYS;
  }
  *a = s;
  return 0;
}

int pthread_cond_init(pthread_cond_t *c, pthread_condattr_t *a)
{
    cond_t *_c;
    int r = 0;
    
    if (!c)
      return EINVAL;
    *c = NULL;
    if (a && *a == PTHREAD_PROCESS_SHARED)
      return ENOSYS;

    if ( !(_c = (pthread_cond_t)calloc(1,sizeof(*_c))) ) {
        return ENOMEM; 
    }
    _c->valid  = DEAD_COND;

#if defined  USE_COND_ConditionVariable
    InitializeConditionVariable(&_c->CV);

#else /* USE_COND_Semaphore */
    _c->waiters_count_ = 0;

    _c->sema_q = CreateSemaphore (NULL,       /* no security */
        0,          /* initially 0 */
        0x7fffffff, /* max count */
        NULL);      /* unnamed  */
    _c->sema_b =  CreateSemaphore (NULL,       /* no security */
        0,          /* initially 0 */
        0x7fffffff, /* max count */
        NULL);  
    if (_c->sema_q == NULL || _c->sema_b == NULL) {
        if (_c->sema_q != NULL)
          CloseHandle (_c->sema_q);
        if (_c->sema_b != NULL)
          CloseHandle (_c->sema_b);
        free (_c);
        r = EAGAIN;
    } else {
        InitializeCriticalSection(&_c->waiters_count_lock_);
    }
#endif /*USE_COND_ConditionVariable */
    if (!r) {
        _c->valid = LIFE_COND;
        *c = _c;
    }
    return r;
}

int pthread_cond_destroy(pthread_cond_t *c)
{
    pthread_cond_t cDestroy;
    cond_t *_c;
    int r;
    if (!c || !*c)
      return EINVAL;
    if (*c == PTHREAD_COND_INITIALIZER)
    {
        _spin_lite_lock(&cond_locked);
        if (*c == PTHREAD_COND_INITIALIZER)
        {
          *c = NULL;
          r = 0;
	}
        else
          r = EBUSY;
        _spin_lite_unlock(&cond_locked);
        return r;
    }
    r = cond_ref_destroy(c,&cDestroy);
    if(r) return r;
    if(!cDestroy) return 0; /* destroyed a (still) static initialized cond */

    _c = (cond_t *)cDestroy;
#if defined  USE_COND_ConditionVariable
    if (_c->waiters_count_ != 0)
    {
      *c = cDestroy;
      return EBUSY;
    }
    /* There is indeed no DeleteConditionVariable */
#else /* USE_COND_Semaphore */
    EnterCriticalSection (&_c->waiters_count_lock_);
    if (_c->waiters_count_ != 0)
    {
      *c = cDestroy;
      LeaveCriticalSection (&_c->waiters_count_lock_);
      return EBUSY;
    }
    CloseHandle(_c->sema_q);
    CloseHandle(_c->sema_b);
    LeaveCriticalSection (&_c->waiters_count_lock_);
    DeleteCriticalSection(&_c->waiters_count_lock_);

#endif /*USE_COND_ConditionVariable */
    _c->valid  = DEAD_COND;
    free(cDestroy);
    return 0;
}

int pthread_cond_signal (pthread_cond_t *c)
{
    cond_t *_c;
    int r;
    
    if (!c || !*c)
      return EINVAL;
    _c = (cond_t *)*c;
    if (_c == (cond_t *)PTHREAD_COND_INITIALIZER)
      return 0;
    else if (_c->valid != (unsigned int)LIFE_COND)
      return EINVAL;

    r = cond_ref(c);
    if(r) return r;

#if defined  USE_COND_ConditionVariable
    WakeConditionVariable(&_c->CV);

#else /*default USE_COND_Semaphore */
    EnterCriticalSection (&_c->waiters_count_lock_);
    /* If there aren't any waiters, then this is a no-op.   */
    if (_c->waiters_count_ > 0) {
        ReleaseSemaphore (_c->sema_q, 1, 0);
    }
    LeaveCriticalSection (&_c->waiters_count_lock_);

#endif
    return cond_unref(c,0);
}

int pthread_cond_broadcast (pthread_cond_t *c)
{
    cond_t *_c;
    int r;
    
    if (!c || !*c)
      return EINVAL;
    _c = (cond_t *)*c;
    if (_c == (cond_t*)PTHREAD_COND_INITIALIZER)
      return 0;
    else if (_c->valid != (unsigned int)LIFE_COND)
      return EINVAL;
    r = cond_ref(c);
    if(r) return r;

#if defined  USE_COND_ConditionVariable
    WakeAllConditionVariable(&_c->CV);

#else /*default USE_COND_Semaphore */
    EnterCriticalSection (&_c->waiters_count_lock_);
    /* If there aren't any waiters, then this is a no-op.   */
    if (_c->waiters_count_ > 0) {
        ReleaseSemaphore (_c->sema_q, _c->waiters_count_, NULL);
    }
    LeaveCriticalSection (&_c->waiters_count_lock_);

#endif
    return cond_unref(c,0);
}


int pthread_cond_wait (pthread_cond_t *c, 
                              pthread_mutex_t *external_mutex)
{
    cond_t *_c;
    int r;

    if (!c || *c == NULL)
      return EINVAL;
    _c = (cond_t *)*c;
    if (_c == (cond_t *)PTHREAD_COND_INITIALIZER)
    {
      r = cond_static_init(c);
      if (r != 0 && r != EBUSY)
        return r;
    } else if (_c->valid != (unsigned int)LIFE_COND)
      return EINVAL;
    
    r = cond_ref_wait(c);
    if(r) return r;


    if ((r=mutex_ref_ext(external_mutex)))return cond_unref(c,r);

    pthread_testcancel();
#if defined  USE_COND_ConditionVariable
    mutex_t *_m = (mutex_t *)*external_mutex;

    SleepConditionVariableCS(&_c->CV, &_m->cs.cs, INFINITE);

#else /*default USE_COND_Semaphore */
    DWORD dwr;

    EnterCriticalSection (&_c->waiters_count_lock_);
    InterlockedIncrement((long *)&_c->waiters_count_);
    LeaveCriticalSection (&_c->waiters_count_lock_);
    pthread_mutex_unlock(external_mutex);
    dwr = WaitForSingleObject(_c->sema_q, INFINITE);
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
    EnterCriticalSection (&_c->waiters_count_lock_);
    InterlockedDecrement((long *)&_c->waiters_count_);
    LeaveCriticalSection (&_c->waiters_count_lock_);

#endif
    return cond_unref(c,mutex_unref(external_mutex,r));
}

int pthread_cond_timedwait(pthread_cond_t *c, pthread_mutex_t *external_mutex, struct timespec *t)
{
    DWORD dwr;
    int r;
    
    cond_t *_c;

    if (!c || !*c)
      return EINVAL;
    _c = (cond_t *)*c;
    if (_c == (cond_t *)PTHREAD_COND_INITIALIZER)
    {
      r = cond_static_init(c);
      if (r && r != EBUSY)
        return r;
    } else if ((_c)->valid != (unsigned int)LIFE_COND)
      return EINVAL;
    r = cond_ref_wait(c);
    if(r) return r;

    if ((r=mutex_ref_ext(external_mutex)) != 0)
    	return cond_unref(c,r);

    pthread_testcancel();
    dwr = dwMilliSecs(_pthread_rel_time_in_ms(t));
#if defined  USE_COND_ConditionVariable
    mutex_t *_m = (mutex_t *)*external_mutex;

    if (!SleepConditionVariableCS(&_c->CV,  &_m->cs.cs, dwr)) {
        r = ETIMEDOUT;
    } else {
        /* We can have a spurious wakeup after the timeout */
        if (!_pthread_rel_time_in_ms(t)) r = ETIMEDOUT;
    }

#else /*default USE_COND_Semaphore */
    EnterCriticalSection (&_c->waiters_count_lock_);
    InterlockedIncrement((long *)&_c->waiters_count_);
    LeaveCriticalSection (&_c->waiters_count_lock_);
    pthread_mutex_unlock(external_mutex);

    dwr = WaitForSingleObject(_c->sema_q, dwr);
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
    EnterCriticalSection (&_c->waiters_count_lock_);
    InterlockedDecrement((long *)&_c->waiters_count_);
    LeaveCriticalSection (&_c->waiters_count_lock_);
#endif
    return cond_unref(c,mutex_unref(external_mutex,r));
}

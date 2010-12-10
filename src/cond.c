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

int __pthread_shallcancel (void);

static int print_state = 0;
static FILE *fo = NULL;

int do_sema_b_wait (HANDLE sema, int nointerrupt, DWORD timeout,CRITICAL_SECTION *cs, LONG *val);
int do_sema_b_wait_intern (HANDLE sema, int nointerrupt, DWORD timeout);

int do_sema_b_release(HANDLE sema, LONG count,CRITICAL_SECTION *cs, LONG *val);

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
    _c->waiters_count_gone_ = 0;
    _c->waiters_count_unblock_ = 0;

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
        InitializeCriticalSection(&_c->waiters_b_lock_);
        InitializeCriticalSection(&_c->waiters_q_lock_);
        _c->value_q = 0;
        _c->value_b = 1;
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
    _c = (cond_t *) *c;
#if !defined  USE_COND_ConditionVariable
    r = do_sema_b_wait(_c->sema_b, 0, INFINITE,&_c->waiters_b_lock_,&_c->value_b);
    if (r != 0)
      return r;
    if (!TryEnterCriticalSection(&_c->waiters_count_lock_))
    {
       do_sema_b_release (_c->sema_b, 1,&_c->waiters_b_lock_,&_c->value_b);
       return EBUSY;
    }
#endif
#if defined  USE_COND_ConditionVariable
    if (_c->waiters_count_ != 0)
    {
      return EBUSY;
    }
#else
    if (_c->waiters_count_ > _c->waiters_count_gone_)
    {
      r = do_sema_b_release (_c->sema_b, 1,&_c->waiters_b_lock_,&_c->value_b);
      if (!r) r = EBUSY;
      LeaveCriticalSection(&_c->waiters_count_lock_);
      return r;
    }
#endif
    *c = NULL;
#if defined  USE_COND_ConditionVariable
    /* There is indeed no DeleteConditionVariable */
#else /* USE_COND_Semaphore */
      do_sema_b_release (_c->sema_b, 1,&_c->waiters_b_lock_,&_c->value_b);
      if (!CloseHandle(_c->sema_q))
      if (!r) r = EINVAL;
    if (!CloseHandle(_c->sema_b))
      if (!r) r = EINVAL;
    LeaveCriticalSection (&_c->waiters_count_lock_);
    DeleteCriticalSection(&_c->waiters_count_lock_);
    DeleteCriticalSection(&_c->waiters_b_lock_);
    DeleteCriticalSection(&_c->waiters_q_lock_);
#endif /*USE_COND_ConditionVariable */
    _c->valid  = DEAD_COND;
    free(_c);
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
    if (_c->waiters_count_unblock_ == 0 && _c->waiters_count_ <= _c->waiters_count_gone_)
    {
      LeaveCriticalSection (&_c->waiters_count_lock_);
      return cond_unref(c,0);
    }
    else if (_c->waiters_count_unblock_ != 0)
    {
      if (_c->waiters_count_ == 0)
      {
	LeaveCriticalSection (&_c->waiters_count_lock_);
	return cond_unref(c,0);
      }
      _c->waiters_count_ -= 1;
      _c->waiters_count_unblock_ += 1;
    }
    else if (_c->waiters_count_ > _c->waiters_count_gone_)
    {
    	r = do_sema_b_wait (_c->sema_b, 1, INFINITE,&_c->waiters_b_lock_,&_c->value_b);
    	if (r != 0)
    	{
    	  LeaveCriticalSection (&_c->waiters_count_lock_);
    	  return cond_unref(c,r);
    	}
    	if (_c->waiters_count_gone_ != 0)
    	{
    	  _c->waiters_count_ -= _c->waiters_count_gone_;
    	  _c->waiters_count_gone_ = 0;
    	}
    	_c->waiters_count_ -= 1;
    }
    LeaveCriticalSection (&_c->waiters_count_lock_);
    return cond_unref(c, do_sema_b_release(_c->sema_q, 1,&_c->waiters_q_lock_,&_c->value_q));
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
    int relCnt = 0;
    EnterCriticalSection (&_c->waiters_count_lock_);
    /* If there aren't any waiters, then this is a no-op.   */
    if (_c->waiters_count_unblock_ == 0 && _c->waiters_count_ <= _c->waiters_count_gone_)
    {
      LeaveCriticalSection (&_c->waiters_count_lock_);
      return cond_unref(c,0);
    }
    else if (_c->waiters_count_unblock_ != 0)
    {
      if (_c->waiters_count_ == 0)
      {
	LeaveCriticalSection (&_c->waiters_count_lock_);
	return cond_unref(c,0);
      }
      relCnt = _c->waiters_count_;
      _c->waiters_count_ = 0;
      _c->waiters_count_unblock_ += relCnt;
    }
    else if (_c->waiters_count_ > _c->waiters_count_gone_)
    {
    	r = do_sema_b_wait (_c->sema_b, 1, INFINITE,&_c->waiters_b_lock_,&_c->value_b);
    	if (r != 0)
    	{
    	  LeaveCriticalSection (&_c->waiters_count_lock_);
    	  return cond_unref(c,r);
    	}
    	if (_c->waiters_count_gone_ != 0)
    	{
    	  _c->waiters_count_ -= _c->waiters_count_gone_;
    	  _c->waiters_count_gone_ = 0;
    	}
    	relCnt = _c->waiters_count_;
    	_c->waiters_count_unblock_ = relCnt;
    	_c->waiters_count_ = 0;
    }
    LeaveCriticalSection (&_c->waiters_count_lock_);
    return cond_unref(c,do_sema_b_release(_c->sema_q, relCnt,&_c->waiters_q_lock_,&_c->value_q));
#endif
    return cond_unref(c,0);
}


int pthread_cond_wait (pthread_cond_t *c, pthread_mutex_t *external_mutex)
{
    cond_t *_c;
    int r, r2, n;

    pthread_testcancel();

    if (!c || *c == NULL)
      return EINVAL;
    _c = (cond_t *)*c;
    if (_c == (cond_t *)PTHREAD_COND_INITIALIZER)
    {
      r = cond_static_init(c);
      if (r != 0 && r != EBUSY)
        return r;
      _c = (cond_t *) *c;
    } else if (_c->valid != (unsigned int)LIFE_COND)
      return EINVAL;
    
    r = cond_ref_wait(c);
    if(r) return r;


#if defined  USE_COND_ConditionVariable
    mutex_t *_m = (mutex_t *)*external_mutex;

    SleepConditionVariableCS(&_c->CV, &_m->cs.cs, INFINITE);

#else /*default USE_COND_Semaphore */
    r = do_sema_b_wait (_c->sema_b, 0, INFINITE,&_c->waiters_b_lock_,&_c->value_b);
    if (r != 0)
      return cond_unref(c, r);
    _c->waiters_count_++;
    r = do_sema_b_release (_c->sema_b, 1,&_c->waiters_b_lock_,&_c->value_b);
    if (r != 0)
      return cond_unref(c, r);

    r = pthread_mutex_unlock(external_mutex);
    if (!r)
      r = do_sema_b_wait (_c->sema_q, 0, INFINITE,&_c->waiters_q_lock_,&_c->value_q);
    
    EnterCriticalSection (&_c->waiters_count_lock_);
    n = _c->waiters_count_unblock_;
    if (n != 0)
      _c->waiters_count_unblock_ -= 1;
    else if ((INT_MAX/2) - 1 == _c->waiters_count_gone_)
    {
      _c->waiters_count_gone_ += 1;
      r2 = do_sema_b_wait (_c->sema_b, 1, INFINITE,&_c->waiters_b_lock_,&_c->value_b);
      if (r2 != 0)
      {
        LeaveCriticalSection(&_c->waiters_count_lock_);
        return cond_unref(c, r2);
      }
      _c->waiters_count_ -= _c->waiters_count_gone_;
      r2 = do_sema_b_release (_c->sema_b, 1,&_c->waiters_b_lock_,&_c->value_b);
      if (r2 != 0)
      {
        LeaveCriticalSection(&_c->waiters_count_lock_);
	return cond_unref(c, r2);
      }
      _c->waiters_count_gone_ = 0;
    }
    else
      _c->waiters_count_gone_ += 1;
    LeaveCriticalSection (&_c->waiters_count_lock_);
     
    if (n == 1)
    {
      r2 = do_sema_b_release (_c->sema_b, 1,&_c->waiters_b_lock_,&_c->value_b);
      if (r2 != 0) return cond_unref(c,r2);
    }
    r2 = pthread_mutex_lock(external_mutex);
    if (r2 != 0)
      r = r2;
#endif
    return cond_unref(c,r);
}

int pthread_cond_timedwait(pthread_cond_t *c, pthread_mutex_t *external_mutex, struct timespec *t)
{
    DWORD dwr;
    int r, n, r2;
    cond_t *_c;

    pthread_testcancel();

    if (!c || !*c)
      return EINVAL;
    _c = (cond_t *)*c;
    if (_c == (cond_t *)PTHREAD_COND_INITIALIZER)
    {
      r = cond_static_init(c);
      if (r && r != EBUSY)
        return r;
      _c = (cond_t *) *c;
    } else if ((_c)->valid != (unsigned int)LIFE_COND)
      return EINVAL;
    r = cond_ref_wait(c);
    if(r) return r;

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
    r = do_sema_b_wait (_c->sema_b, 0, INFINITE,&_c->waiters_b_lock_,&_c->value_b);
    if (r != 0)
      return cond_unref(c, r);
    _c->waiters_count_++;
    r = do_sema_b_release (_c->sema_b, 1,&_c->waiters_b_lock_,&_c->value_b);
    if (r != 0)
      return cond_unref(c, r);

    r = pthread_mutex_unlock(external_mutex);
    if (!r)
      r = do_sema_b_wait (_c->sema_q, 0, dwr,&_c->waiters_q_lock_,&_c->value_q);
    
    EnterCriticalSection (&_c->waiters_count_lock_);
    n = _c->waiters_count_unblock_;
    if (n != 0)
      _c->waiters_count_unblock_ -= 1;
    else if ((INT_MAX/2) - 1 == _c->waiters_count_gone_)
    {
      _c->waiters_count_gone_ += 1;
      r2 = do_sema_b_wait (_c->sema_b, 1, INFINITE,&_c->waiters_b_lock_,&_c->value_b);
      if (r2 != 0)
      {
        LeaveCriticalSection(&_c->waiters_count_lock_);
        return cond_unref(c, r2);
      }
      _c->waiters_count_ -= _c->waiters_count_gone_;
      r2 = do_sema_b_release (_c->sema_b, 1,&_c->waiters_b_lock_,&_c->value_b);
      if (r2 != 0)
      {
        LeaveCriticalSection(&_c->waiters_count_lock_);
	return cond_unref(c, r2);
      }
      _c->waiters_count_gone_ = 0;
    }
    else
      _c->waiters_count_gone_ += 1;
    LeaveCriticalSection (&_c->waiters_count_lock_);
     
    if (n == 1)
    {
      r2 = do_sema_b_release (_c->sema_b, 1,&_c->waiters_b_lock_,&_c->value_b);
      if (r2 != 0) return cond_unref(c,r2);
    }
    r2 = pthread_mutex_lock(external_mutex);
    if (r2 != 0)
      r = r2;
#endif
    return cond_unref(c,r);
}

int
do_sema_b_wait (HANDLE sema, int nointerrupt, DWORD timeout,CRITICAL_SECTION *cs, LONG *val)
{
  int r;
  LONG v;
  EnterCriticalSection(cs);
  InterlockedDecrement(val);
  v = val[0];
  LeaveCriticalSection(cs);
  if (v >= 0)
    return 0;
  r = do_sema_b_wait_intern (sema, nointerrupt, timeout);
  EnterCriticalSection(cs);
  if (r != 0)
    InterlockedIncrement(val);
  LeaveCriticalSection(cs);
  return r;
}

int
do_sema_b_wait_intern (HANDLE sema, int nointerrupt, DWORD timeout)
{
  int r = 0;
  DWORD res, dt;
  if (nointerrupt)
  {
    res = WaitForSingleObject(sema, timeout);
    switch (res) {
    case WAIT_TIMEOUT:
	r = ETIMEDOUT;
	break;
    case WAIT_ABANDONED:
	r = EPERM;
	break;
    case WAIT_OBJECT_0:
	break;
    default:
	/*We can only return EINVAL though it might not be posix compliant  */
	r = EINVAL;
    }
    if (r != EINVAL && WaitForSingleObject(sema, 0) == WAIT_OBJECT_0)
      r = 0;
    return r;
  }
  if (timeout == INFINITE)
  {
    do {
      res = WaitForSingleObject(sema, 40);
      switch (res) {
      case WAIT_TIMEOUT:
	  r = ETIMEDOUT;
	  if (__pthread_shallcancel ())
	    r = EINVAL;
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
    } while (r == ETIMEDOUT);
    if (r != EINVAL && WaitForSingleObject(sema, 0) == WAIT_OBJECT_0)
      r = 0;
    return r;
  }
  dt = 20;
  do {
    if (dt > timeout) dt = timeout;
    res = WaitForSingleObject(sema, dt);
    switch (res) {
    case WAIT_TIMEOUT:
	r = ETIMEDOUT;
	if (__pthread_shallcancel ())
	  r = EINVAL;
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
    timeout -= dt;
  } while (r == ETIMEDOUT && timeout != 0);
  if (r == ETIMEDOUT && WaitForSingleObject(sema, 0) == WAIT_OBJECT_0)
    r = 0;
  return r;
}

int do_sema_b_release(HANDLE sema, LONG count,CRITICAL_SECTION *cs, LONG *val)
{
  int wc;
  EnterCriticalSection(cs);
  if (((long long) val[0] + (long long) count) > (long long) 0x7fffffffLL)
  {
    LeaveCriticalSection(cs);
    return ERANGE;
  }
  wc = -val[0];
  InterlockedAdd(val, count);
  if (wc <= 0 || ReleaseSemaphore(sema, (wc < count ? wc : count), NULL))
  {
    LeaveCriticalSection(cs);
    return 0;
  }
  InterlockedAdd(val, -count);
  LeaveCriticalSection(cs);
  return EINVAL;  
}

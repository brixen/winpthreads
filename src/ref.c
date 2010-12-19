#include <windows.h>
#include <winternl.h>
#include <stdio.h>
#include "pthread.h"
#include "semaphore.h"
#include "spinlock.h"
#include "mutex.h"
#include "rwlock.h"
#include "cond.h"
#include "barrier.h"
#include "sem.h"
#include "ref.h"
#include "misc.h"

static spin_t mutex_global = {0,LIFE_SPINLOCK,0};
static spin_t barrier_global = {0,LIFE_SPINLOCK,0};

inline int mutex_unref(volatile pthread_mutex_t *m, int r)
{
    mutex_t *m_ = (mutex_t *)*m;
    _spin_lite_lock(&mutex_global);
#ifdef WINPTHREAD_DBG
    assert((m_->valid == LIFE_MUTEX) && (m_->busy > 0));
#endif
    m_->busy --;
    _spin_lite_unlock(&mutex_global);
    return r;
}

/* External: must be called by owner of a locked mutex: */
inline int mutex_ref_ext(volatile pthread_mutex_t *m)
{
    int r = 0;
    mutex_t *m_ = (mutex_t *)*m;
    _spin_lite_lock(&mutex_global);

    if (!m || !*m ) r = EINVAL;
    else if (STATIC_INITIALIZER(m) || !COND_OWNER(m_)) r = EPERM;
    else m_->busy ++;

    _spin_lite_unlock(&mutex_global);
    return r;
}

/* Set the mutex to busy in a thread-safe way */
/* A busy mutex can't be destroyed */
inline int mutex_ref(volatile pthread_mutex_t *m )
{
    int r = 0;

    INIT_MUTEX(m);
    _spin_lite_lock(&mutex_global);

    if (!m || !*m || ((mutex_t *)*m)->valid != LIFE_MUTEX) r = EINVAL;
    else {
        ((mutex_t *)*m)->busy ++;
    }

    _spin_lite_unlock(&mutex_global);

    return r;
}

/* An unlock can simply fail with EPERM instead of auto-init (can't be owned) */
inline int mutex_ref_unlock(volatile pthread_mutex_t *m)
{
    int r = 0;
    mutex_t *m_ = (mutex_t *)*m;

    _spin_lite_lock(&mutex_global);

    if (!m || !*m || ((mutex_t *)*m)->valid != LIFE_MUTEX) r = EINVAL;
    else if (STATIC_INITIALIZER(*m) && !COND_LOCKED(m_)) {
      r= EPERM;
    }
    else {
        ((mutex_t *)*m)->busy ++;
    }

    _spin_lite_unlock(&mutex_global);

    return r;
}

/* doesn't lock the mutex but set it to invalid in a thread-safe way */
/* A busy mutex can't be destroyed -> EBUSY */
inline int mutex_ref_destroy(volatile pthread_mutex_t *m, pthread_mutex_t *mDestroy )
{
    int r = 0;

    *mDestroy = NULL;
    /* also considered as busy, any concurrent access prevents destruction: */
    if (_spin_lite_trylock(&mutex_global)) return EBUSY;
    
    if (!m || !*m) r = EINVAL;
    else {
        mutex_t *m_ = (mutex_t *)*m;
        if (STATIC_INITIALIZER(*m)) *m = NULL;
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

inline int mutex_ref_init(volatile pthread_mutex_t *m )
{
    int r = 0;

    _spin_lite_lock(&mutex_global);
    
    if (!m)  r = EINVAL;
 
    _spin_lite_unlock(&mutex_global);
    return r;
}

inline int barrier_unref(volatile pthread_barrier_t *barrier, int res)
{
    _spin_lite_lock(&barrier_global);
#ifdef WINPTHREAD_DBG
    assert((((barrier_t *)*barrier)->valid == LIFE_BARRIER) && (((barrier_t *)*barrier)->busy > 0));
#endif
     ((barrier_t *)*barrier)->busy--;
    _spin_lite_unlock(&barrier_global);
    return res;
}

inline int barrier_ref(volatile pthread_barrier_t *barrier)
{
    int r = 0;
    _spin_lite_lock(&barrier_global);

    if (!barrier || !*barrier || ((barrier_t *)*barrier)->valid != LIFE_BARRIER) r = EINVAL;
    else {
        ((barrier_t *)*barrier)->busy ++;
    }

    _spin_lite_unlock(&barrier_global);

    return r;
}

inline int
barrier_ref_destroy(volatile pthread_barrier_t *barrier, pthread_barrier_t *bDestroy)
{
    int r = 0;

    *bDestroy = NULL;
    if (_spin_lite_trylock(&barrier_global)) return EBUSY;
    
    if (!barrier || !*barrier || ((barrier_t *)*barrier)->valid != LIFE_BARRIER) r = EINVAL;
    else {
        barrier_t *b_ = (barrier_t *)*barrier;
        if (b_->busy) r = EBUSY;
        else {
            *bDestroy = *barrier;
            *barrier = NULL;
        }
    }

    _spin_lite_unlock(&barrier_global);
    return r;
}

inline void barrier_ref_set (volatile pthread_barrier_t *barrier, void *v)
{
  _spin_lite_lock(&barrier_global);
  *barrier = v;
  _spin_lite_unlock(&barrier_global);
}

inline int barrier_ref_init(volatile pthread_barrier_t *barrier)
{
    int r = 0;

    _spin_lite_lock(&barrier_global);
    
    if (!barrier)  r = EINVAL;

    _spin_lite_unlock(&barrier_global);
    return r;
}


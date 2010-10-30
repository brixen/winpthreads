#include <windows.h>
#include <winternl.h>
#include <stdio.h>
#include "pthread.h"
#include "spinlock.h"
#include "mutex.h"
#include "rwlock.h"
#include "cond.h"
#include "barrier.h"
#include "ref.h"
#include "misc.h"

static spin_t mutex_global = {0,LIFE_SPINLOCK,0};
static spin_t rwl_global = {0,LIFE_SPINLOCK,0};
static spin_t cond_global = {0,LIFE_SPINLOCK,0};
static spin_t barrier_global = {0,LIFE_SPINLOCK,0};

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
inline int mutex_ref_unlock(volatile pthread_mutex_t *m )
{
    int r = 0;

    _spin_lite_lock(&mutex_global);

    if (!m || !*m || ((mutex_t *)*m)->valid != LIFE_MUTEX) r = EINVAL;
    else if (STATIC_INITIALIZER(*m)) r= EPERM;
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
    
    if (!m || !*m || ((mutex_t *)*m)->valid != LIFE_MUTEX) r = EINVAL;
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

/* A valid mutex can't be re-initialized -> EBUSY */
inline int mutex_ref_init(volatile pthread_mutex_t *m )
{
    int r = 0;

    _spin_lite_lock(&mutex_global);
    
    if (!m)  r = EINVAL;
    else if (*m && !STATIC_INITIALIZER(*m)) {
        mutex_t *m_ = (mutex_t *)*m;
        if (m_->valid == LIFE_MUTEX) r = EBUSY;
    }

    _spin_lite_unlock(&mutex_global);
    return r;
}

inline int rwl_unref(volatile pthread_rwlock_t *rwl, int res)
{
    _spin_lite_lock(&rwl_global);
     ((rwlock_t *)*rwl)->busy--;
    _spin_lite_unlock(&rwl_global);
    return res;
}

inline int rwl_ref(volatile pthread_rwlock_t *rwl, int f )
{
    int r = 0;
    INIT_RWLOCK(rwl);
    _spin_lite_lock(&rwl_global);

    if (!rwl || !*rwl || ((rwlock_t *)*rwl)->valid != LIFE_RWLOCK) r = EINVAL;
    else {
        ((rwlock_t *)*rwl)->busy ++;
    }

    _spin_lite_unlock(&rwl_global);
    if (!r) r=rwl_check_owner((pthread_rwlock_t *)rwl,f);

    return r;
}

inline int rwl_ref_unlock(volatile pthread_rwlock_t *rwl )
{
    int r = 0;

    _spin_lite_lock(&rwl_global);

    if (!rwl || !*rwl || ((rwlock_t *)*rwl)->valid != LIFE_RWLOCK) r = EINVAL;
    else if (STATIC_RWL_INITIALIZER(*rwl)) r= EPERM;
    else {
        ((rwlock_t *)*rwl)->busy ++;
    }

    _spin_lite_unlock(&rwl_global);
    if (!r) r=rwl_unset_owner((pthread_rwlock_t *)rwl,0);
 

    return r;
}

inline int rwl_ref_destroy(volatile pthread_rwlock_t *rwl, pthread_rwlock_t *rDestroy )
{
    int r = 0;

    *rDestroy = NULL;
    if (_spin_lite_trylock(&rwl_global)) return EBUSY;
    
    if (!rwl || !*rwl || ((rwlock_t *)*rwl)->valid != LIFE_RWLOCK) r = EINVAL;
    else {
        rwlock_t *r_ = (rwlock_t *)*rwl;
        if (STATIC_RWL_INITIALIZER(*rwl)) *rwl = NULL;
        else if (r_->valid != LIFE_RWLOCK) r = EINVAL;
        else if (r_->busy || COND_RWL_LOCKED(r_)) r = EBUSY;
        else {
            *rDestroy = *rwl;
            *rwl = NULL;
        }
    }

    _spin_lite_unlock(&rwl_global);
    return r;
}

inline int rwl_ref_init(volatile pthread_rwlock_t *rwl )
{
    int r = 0;

    _spin_lite_lock(&rwl_global);
    
    if (!rwl)  r = EINVAL;
    else if (*rwl && !STATIC_RWL_INITIALIZER(*rwl)) {
        rwlock_t *r_ = (rwlock_t *)*rwl;
        if (r_->valid == LIFE_RWLOCK) r = EBUSY;
    }

    _spin_lite_unlock(&rwl_global);
    return r;
}


inline int cond_unref(volatile pthread_cond_t *cond, int res)
{
    _spin_lite_lock(&cond_global);
    cond_t *c_ = (cond_t *)*cond;

    c_->busy--;
    if (!c_->busy) {
        c_->bound = NULL;
    }
    _spin_lite_unlock(&cond_global);
    return res;
}

inline int cond_ref(volatile pthread_cond_t *cond)
{
    int r = 0;
    INIT_COND(cond);
    _spin_lite_lock(&cond_global);

    if (!cond || !*cond || ((cond_t *)*cond)->valid != LIFE_COND) r = EINVAL;
    else {
        ((cond_t *)*cond)->busy ++;
    }

    _spin_lite_unlock(&cond_global);

    return r;
}

inline int cond_ref_wait(volatile pthread_cond_t *cond, pthread_mutex_t *m)
{
    int r = 0;
    INIT_COND(cond);
    _spin_lite_lock(&cond_global);

    if (!cond || !*cond || ((cond_t *)*cond)->valid != LIFE_COND) r = EINVAL;
    else {
        cond_t *c_ = (cond_t *)*cond;
        /* Different mutexes were supplied for concurrent pthread_cond_wait() or pthread_cond_timedwait() operations on the same condition variable: */
        if (c_->bound && c_->bound != m) {
            r = EINVAL;
        } else {
            c_->bound = m;
            c_->busy ++;
        }
    }

    _spin_lite_unlock(&cond_global);
 

    return r;
}

inline int cond_ref_destroy(volatile pthread_cond_t *cond, pthread_cond_t *cDestroy )
{
    int r = 0;

    *cDestroy = NULL;
    if (_spin_lite_trylock(&cond_global)) return EBUSY;
    
    if (!cond || !*cond || ((cond_t *)*cond)->valid != LIFE_COND) r = EINVAL;
    else {
        cond_t *c_ = (cond_t *)*cond;
        if (STATIC_COND_INITIALIZER(*cond)) *cond = NULL;
        else if (c_->valid != LIFE_COND) r = EINVAL;
        else if (c_->busy) r = EBUSY;
        else {
            *cDestroy = *cond;
            *cond = NULL;
        }
    }

    _spin_lite_unlock(&cond_global);
    return r;
}

inline int cond_ref_init(volatile pthread_cond_t *cond )
{
    int r = 0;

    _spin_lite_lock(&cond_global);
    
    if (!cond)  r = EINVAL;
    else if (*cond && !STATIC_COND_INITIALIZER(*cond)) {
        cond_t *r_ = (cond_t *)*cond;
        if (r_->valid == LIFE_COND) r = EBUSY;
    }

    _spin_lite_unlock(&cond_global);
    return r;
}


inline int barrier_unref(volatile pthread_barrier_t *barrier, int res)
{
    _spin_lite_lock(&barrier_global);
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

inline int barrier_ref_destroy(volatile pthread_barrier_t *barrier, pthread_barrier_t *bDestroy )
{
    int r = 0;

    *bDestroy = NULL;
    if (_spin_lite_trylock(&barrier_global)) return EBUSY;
    
    if (!barrier || !*barrier || ((barrier_t *)*barrier)->valid != LIFE_BARRIER) r = EINVAL;
    else {
        barrier_t *b_ = (barrier_t *)*barrier;
        if (b_->valid != LIFE_BARRIER) r = EINVAL;
        else if (b_->busy) r = EBUSY;
        else {
            *bDestroy = *barrier;
            *barrier = NULL;
        }
    }

    _spin_lite_unlock(&barrier_global);
    return r;
}

inline int barrier_ref_init(volatile pthread_barrier_t *barrier )
{
    int r = 0;

    _spin_lite_lock(&barrier_global);
    
    if (!barrier)  r = EINVAL;
    else if (*barrier) {
        barrier_t *b_ = (barrier_t *)*barrier;
        if (b_->valid == LIFE_BARRIER) r = EBUSY;
    }

    _spin_lite_unlock(&barrier_global);
    return r;
}


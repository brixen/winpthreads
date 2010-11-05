#include <windows.h>
#include <stdio.h>
#include "pthread.h"
#include "spinlock.h"
#include "misc.h"
      
static int scnt = 0;
static LONG bscnt = 0;
static int scntMax = 0;

static inline int
spinlock_static_init (volatile pthread_spinlock_t *s )
{
    pthread_spinlock_t s_tmp=NULL;
    spin_t *si, *s_replaced;

    if ( PTHREAD_SPINLOCK_INITIALIZER != (pthread_spinlock_t)(si = (spin_t *)*s))
    {
        /* Assume someone crept in between: */
        return 0;
    }

    int r = pthread_spin_init(&s_tmp, 0);
    if (!r) {
        s_replaced = (spin_t *)InterlockedCompareExchangePointer(
            (PVOID *)s, 
            s_tmp,
            si);
        if (s_replaced != si) {
            /* someone crept in between: */
            pthread_spin_destroy(&s_tmp);
            /* But it could also be destroyed already: */
            if (!s_replaced) r = EINVAL;
        }
    }
    return r;
}

int pthread_spin_init(pthread_spinlock_t *l, int pshared)
{
    spin_t *_l;
    (void) pshared;

    if (!l) return EINVAL; 
    if (pshared != PTHREAD_PROCESS_SHARED && pshared != PTHREAD_PROCESS_PRIVATE) return ENOTSUP; 
    if (!(_l = (pthread_spinlock_t)calloc(1, sizeof(*_l))))
        return ENOMEM;

    _l->valid = LIFE_SPINLOCK;
    *l = _l;
    return 0;
}

int pthread_spin_destroy(pthread_spinlock_t *l)
{
    CHECK_SPINLOCK(l);
    spin_t *_l = (spin_t *)*l;

    if (pthread_spin_trylock(l))
      return EBUSY;
    pthread_spinlock_t l2 = *l;
    *l= NULL; /* dereference first, free later */
    _l->valid  = DEAD_SPINLOCK;

   _ReadWriteBarrier();
    UNSET_OWNER_SL(_l);
    _l->l = 0;

    free(l2);
    return 0;
 }

/* No-fair spinlock due to lack of knowledge of thread number.  */
int pthread_spin_lock(pthread_spinlock_t *l)
{
    INIT_SPINLOCK(l);
    CHECK_SPINLOCK(l);
    spin_t *_l = (spin_t *)*l;
    CHECK_DEADLK_SL(_l);

    _vol_spinlock v;
    v.l = (LONG *)&_l->l;
    while (InterlockedExchange(v.lv, EBUSY))
    {
        /* Don't lock the bus whilst waiting */
        while (*v.lv)
        {
            YieldProcessor();

            /* Compiler barrier.  Prevent caching of *l */
            _ReadWriteBarrier();
        }
    }

    SET_OWNER_SL(_l);
    return 0;
}

int pthread_spin_trylock(pthread_spinlock_t *l)
{
    int r = 0;

    INIT_SPINLOCK(l);
    CHECK_SPINLOCK(l);
    spin_t *_l = (spin_t *)*l;

    r = InterlockedExchange(&_l->l, EBUSY);
    SET_OWNER_SLIF(_l, r);
    return r;
}

int _spin_lite_getsc(int reset)
{
    int r = scnt;
    if (reset) scnt = 0;
    return r;
}

int _spin_lite_getbsc(int reset)
{
    int r = bscnt;
    if (reset) bscnt = 0;
    return r;
}

int _spin_lite_getscMax(int reset)
{
    int r = scntMax;
    if (reset) scntMax = 0;
    return r;
}

int _spin_lite_trylock(spin_t *l)
{
    CHECK_SPINLOCK_LITE(l);
    return InterlockedExchange(&l->l, EBUSY);
}
int _spin_lite_unlock(spin_t *l)
{
    CHECK_SPINLOCK_LITE(l);
    /* Compiler barrier.  The store below acts with release symmantics.  */
    _ReadWriteBarrier();
    l->l = 0;

    return 0;
}
int _spin_lite_lock(spin_t *l)
{
    CHECK_SPINLOCK_LITE(l);
    int lscnt = 0;

    _vol_spinlock v;
    v.l = (LONG *)&l->l;
    _spin_lite_lock_inc(bscnt);
    while (InterlockedExchange(v.lv, EBUSY))
    {
        _spin_lite_lock_cnt(lscnt);
        /* Don't lock the bus whilst waiting */
        while (*v.lv)
        {
            _spin_lite_lock_cnt(lscnt);
            YieldProcessor();

            /* Compiler barrier.  Prevent caching of *l */
            _ReadWriteBarrier();
        }
    }
    _spin_lite_lock_dec(bscnt);
    _spin_lite_lock_stat(lscnt);
    return 0;
}


int
pthread_spin_unlock (pthread_spinlock_t *l)
{
    CHECK_SPINLOCK(l);
    spin_t *_l = (spin_t *)*l;
    CHECK_PERM_SL(_l);

    /* Compiler barrier.  The store below acts with release symmantics.  */
   _ReadWriteBarrier();
    UNSET_OWNER_SL(_l);
    _l->l = 0;

    return 0;
}

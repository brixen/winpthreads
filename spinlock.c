#include "pthreads.h"
#include "spinlock.h"
#include "misc.h"
      
int pthread_spin_init(pthread_spinlock_t *l, int pshared)
{
    (void) pshared;

    *l = 0;
    return 0;
}

int pthread_spin_destroy(pthread_spinlock_t *l)
{
    (void) l;
    return 0;
}

/* No-fair spinlock due to lack of knowledge of thread number */
int pthread_spin_lock(pthread_spinlock_t *l)
{
  union doVol {
    pthread_spinlock_t *l;
    volatile pthread_spinlock_t *lv;
  } v;
  v.l = l;
    while (InterlockedExchange(v.l, EBUSY))
    {
        /* Don't lock the bus whilst waiting */
        while (*v.l)
        {
            YieldProcessor();

            /* Compiler barrier.  Prevent caching of *l */
            _ReadWriteBarrier();
        }
    }

    return 0;
}

int pthread_spin_trylock(pthread_spinlock_t *l)
{
    return InterlockedExchange(l, EBUSY);
}

int pthread_spin_unlock(pthread_spinlock_t *l)
{
    /* Compiler barrier.  The store below acts with release symmantics */
    _ReadWriteBarrier();

    *l = 0;

    return 0;
}

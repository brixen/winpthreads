#include "pthreads.h"
#include "mutex.h"
#include "misc.h"

 
int pthread_mutex_lock(pthread_mutex_t *m)
{
    CHECK_MUTEX(m);
    switch (WaitForSingleObject(m->h, INFINITE)) {
        case WAIT_ABANDONED:
            return EINVAL;
            break;
        case WAIT_OBJECT_0:
            return 0;
            break;
        case WAIT_FAILED:
            return EINVAL;
            break;
    }
    return EINVAL;
}

int pthread_mutex_unlock(pthread_mutex_t *m)
{
    CHECK_MUTEX(m);
    if (!ReleaseMutex(m->h)) {
        return EPERM;
    }
    return 0;
}

int pthread_mutex_trylock(pthread_mutex_t *m)
{
    CHECK_MUTEX(m);
    switch (WaitForSingleObject(m->h, 0)) {
        case WAIT_TIMEOUT:
            return EBUSY;
            break;
        case WAIT_ABANDONED:
            return EINVAL;
            break;
        case WAIT_OBJECT_0:
            return 0;
            break;
        case WAIT_FAILED:
            return EINVAL;
            break;
    }
    return EINVAL;
}

int pthread_mutex_init(pthread_mutex_t *m, pthread_mutexattr_t *a)
{
    (void) a;

    if (!m) return EINVAL;
    if (m->valid) return EBUSY;
    m->h = CreateMutex(NULL, FALSE, NULL);
    m->valid = 1;

    CHECK_MUTEX(m);

    return 0;
}

int pthread_mutex_destroy(pthread_mutex_t *m)
{
    CHECK_MUTEX(m);

    CloseHandle(m->h);
    m->valid = 0;
    return 0;
}

int pthread_mutexattr_init(pthread_mutexattr_t *a)
{
    *a = 0;
    return 0;
}

int pthread_mutexattr_destroy(pthread_mutexattr_t *a)
{
    (void) a;
    return 0;
}

int pthread_mutexattr_gettype(pthread_mutexattr_t *a, int *type)
{
    *type = *a & 3;

    return 0;
}

int pthread_mutexattr_settype(pthread_mutexattr_t *a, int type)
{
    if ((unsigned) type > 3) return EINVAL;
    *a &= ~3;
    *a |= type;

    return 0;
}

int pthread_mutexattr_getpshared(pthread_mutexattr_t *a, int *type)
{
    *type = *a & 4;

    return 0;
}

int pthread_mutexattr_setpshared(pthread_mutexattr_t * a, int type)
{
    if ((type & 4) != type) return EINVAL;

    *a &= ~4;
    *a |= type;

    return 0;
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

int pthread_mutex_timedlock(pthread_mutex_t *m, struct timespec *ts)
{
    unsigned long long t, ct;

    CHECK_MUTEX(m);
    CHECK_PTR(ts);

    /* Try to lock it without waiting */
    if (!pthread_mutex_trylock(m)) return 0;

    ct = _pthread_time_in_ms();
    t = _pthread_time_in_ms_from_timespec(ts);

    while (1)
    {
        /* Have we waited long enough? */
        if (ct >= t) return ETIMEDOUT;
        switch (WaitForSingleObject(m->h, dwMilliSecs(t - ct))) {
            case WAIT_TIMEOUT:
                break;
            case WAIT_ABANDONED:
                return EINVAL;
                break;
            case WAIT_OBJECT_0:
                return 0;
                break;
            case WAIT_FAILED:
                return EINVAL;
                break;
        }
        /* Get current time */
        ct = _pthread_time_in_ms();
    }
    return 0;
}

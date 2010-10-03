#include <windows.h>
#include "pthread.h"
#include "misc.h"
#include "barrier.h" 

int pthread_barrier_destroy(pthread_barrier_t *b)
{
    int r = 0;

    CHECK_PTR(b);

    r = pthread_cond_destroy(&b->c);
    if (!r) {
        b->count = 0;
        b->total = 0;
        pthread_mutex_destroy(&b->m);
    }
    return r;

}

int pthread_barrier_init(pthread_barrier_t *b, void *attr, int count)
{
    int r = 0;

    /* Ignore attr */
    (void) attr;

    CHECK_PTR(b);

    r = pthread_mutex_init(&b->m, NULL);
    if (r) return r;

    r = pthread_cond_init(&b->c, NULL);
    if (r) {
       pthread_mutex_destroy(&b->m);
    } else {
        b->count = count;
        b->total = 0;
    }
    return r;
}

int pthread_barrier_wait(pthread_barrier_t *b)
{
    int r;

    r = pthread_mutex_lock(&b->m);
    if (r) return EINVAL;

    while (b->total > _PTHREAD_BARRIER_FLAG)
    {
        /* Wait until everyone exits the barrier */
        r = pthread_cond_wait(&b->c, &b->m);
        if (r) {
            pthread_mutex_unlock(&b->m);
            return EINVAL;
        }

    }

    /* Are we the first to enter? */
    if (b->total == _PTHREAD_BARRIER_FLAG) b->total = 0;

    b->total++;

    if (b->total == b->count)
    {
        b->total += _PTHREAD_BARRIER_FLAG - 1;
        r = pthread_cond_broadcast(&b->c);
        pthread_mutex_unlock(&b->m);
        if (r) {
            return EINVAL;
        }

        return 1;
    }
    else
    {
        while (b->total < _PTHREAD_BARRIER_FLAG)
        {
            /* Wait until enough threads enter the barrier */
            r = pthread_cond_wait(&b->c, &b->m);
            if (r) {
                pthread_mutex_unlock(&b->m);
                return EINVAL;
            }
        }

        b->total--;

        /* Get entering threads to wake up */
        if (b->total == _PTHREAD_BARRIER_FLAG) {
            r = pthread_cond_broadcast(&b->c);
        }

        if (pthread_mutex_unlock(&b->m) || r) {
            return EINVAL;
        }
    }
    return 0;
}

int pthread_barrierattr_init(void **attr)
{
    *attr = NULL;
    return 0;
}

int pthread_barrierattr_destroy(void **attr)
{
    /* Ignore attr */
    (void) attr;

    return 0;
}

int pthread_barrierattr_setpshared(void **attr, int s)
{
    *attr = (void *) (size_t) s;
    return 0;
}

int pthread_barrierattr_getpshared(void **attr, int *s)
{
    *s = (int) (size_t) *attr;

    return 0;
}


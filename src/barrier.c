#include <windows.h>
#include "pthread.h"
#include "barrier.h" 
#include "misc.h"

int pthread_barrier_destroy(pthread_barrier_t *b_)
{
    int r = 0;
 	
	CHECK_BARRIER(b_);
	barrier_t *b = (barrier_t *)*b_;
	
	if ((r = pthread_mutex_lock(&b->m))) return EINVAL;

	r = pthread_cond_destroy(&b->c);
    if (!r) {
		pthread_barrier_t *b2_ = b_;
		*b_= NULL; /* dereference first, free later */
		_ReadWriteBarrier();
		b->valid = DEAD_BARRIER;
        b->count = 0;
        b->total = 0;
		pthread_mutex_unlock(&b->m);
        pthread_mutex_destroy(&b->m);
		free(*b2_);
    } else {
		return EBUSY;
	}
    return r;

}

int pthread_barrier_init(pthread_barrier_t *b_, void *attr, unsigned int count)
{
	barrier_t *b;

	int r = 0;
	(void) attr;

	if (!b_)	return EINVAL; 
	if (!count)	return EINVAL; 
	if ( !(b = (pthread_barrier_t)malloc(sizeof(*b))) ) {
		return ENOMEM; 
	}

    if ((r = pthread_mutex_init(&b->m, NULL))) return r;

    if (( r = pthread_cond_init(&b->c, NULL))) {
       pthread_mutex_destroy(&b->m);
    } else {
        b->count = count;
        b->total = 0;
		b->valid = LIFE_BARRIER;
		*b_ = b;
    }
    return r;
}

int pthread_barrier_wait(pthread_barrier_t *b_)
{
    int r;
	CHECK_BARRIER(b_);
	barrier_t *b = (barrier_t *)*b_;

    if ((r = pthread_mutex_lock(&b->m))) return EINVAL;

    while (b->total > _PTHREAD_BARRIER_FLAG) {
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

    if (b->total == b->count) {
        b->total += _PTHREAD_BARRIER_FLAG - 1;
        r = pthread_cond_broadcast(&b->c);
        pthread_mutex_unlock(&b->m);
        if (r) {
            return EINVAL;
        }

        return 1;
    } else {
        while (b->total < _PTHREAD_BARRIER_FLAG) {
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


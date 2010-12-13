#include <windows.h>
#include "pthread.h"
#include "barrier.h"
#include "ref.h" 
#include "misc.h"

int pthread_barrier_destroy(pthread_barrier_t *b_)
{
    pthread_barrier_t bDestroy;
    int r = barrier_ref_destroy(b_,&bDestroy);
    
    if (r)
      return r;

    barrier_t *b = (barrier_t *)bDestroy;
    
    pthread_mutex_lock(&b->m);

    if ((r = pthread_cond_destroy(&b->c)) != 0)
    {
        /* Could this happen? */
        *b_ = bDestroy;
        pthread_mutex_unlock (&b->m);
        return EBUSY;
    }
    b->valid = DEAD_BARRIER;
    pthread_mutex_unlock(&b->m);
    pthread_mutex_destroy(&b->m);
    free(bDestroy);
    return 0;

}

int
pthread_barrier_init (pthread_barrier_t *b_, void *attr, unsigned int count)
{
    barrier_t *b;

    if (!count || !b_)
      return EINVAL;

    if (!(b = (pthread_barrier_t)calloc(1,sizeof(*b))))
       return ENOMEM;
    if (!attr || *((int **)attr) == NULL)
      b->share = PTHREAD_PROCESS_PRIVATE;
    else
      memcpy (&b->share, *((void **) attr), sizeof (int));
    b->total = 0;
    b->count = count;
    b->valid = LIFE_BARRIER;

    if (pthread_mutex_init(&b->m, NULL) != 0)
    {
      free (b);
      return ENOMEM;
    }

    if (pthread_cond_init(&b->c, NULL) != 0)
    {
       pthread_mutex_destroy(&b->m);
       free (b);
       return ENOMEM;
    }
    barrier_ref_set (b_,b);
    /* *b_ = b; */

    return 0;
}

int pthread_barrier_wait(pthread_barrier_t *b_)
{
    int r = barrier_ref(b_);
    if(r) return r;
    
    barrier_t *b = (barrier_t *)*b_;

    if ((r = pthread_mutex_lock(&b->m))) return  barrier_unref(b_,EINVAL);

    while (b->total > _PTHREAD_BARRIER_FLAG) {
        /* Wait until everyone exits the barrier */
        r = pthread_cond_wait(&b->c, &b->m);
        if (r) {
            pthread_mutex_unlock(&b->m);
            return barrier_unref(b_,EINVAL);
        }
    }

    /* Are we the first to enter? */
    if (b->total == _PTHREAD_BARRIER_FLAG) b->total = 0;
    InterlockedIncrement ((long *) &b->total);
    if (b->total == b->count)
    {
        InterlockedAdd ((long *)&b->total, _PTHREAD_BARRIER_FLAG - 1);
        r = pthread_cond_broadcast(&b->c);
        pthread_mutex_unlock(&b->m);
        if (r && r != EBUSY) {
            return barrier_unref(b_,EINVAL);
        }

        return barrier_unref(b_,PTHREAD_BARRIER_SERIAL_THREAD);
    }
    else
    {
        while (b->total < _PTHREAD_BARRIER_FLAG)
        {
            /* Wait until enough threads enter the barrier */
            r = pthread_cond_wait(&b->c, &b->m);
            if (r && r != EBUSY)
            {
                pthread_mutex_unlock(&b->m);
                return barrier_unref(b_,EINVAL);
            }
        }
        InterlockedDecrement ((long *) &b->total);

        if (b->total == _PTHREAD_BARRIER_FLAG)
        {
            /* Get entering threads to wake up */
            r = pthread_cond_broadcast(&b->c);
        }

        if (pthread_mutex_unlock(&b->m) || r) {
            return barrier_unref(b_,EINVAL);
        }
   }
   return barrier_unref(b_,0);
}

int pthread_barrierattr_init(void **attr)
{
  int *p;

  if (!(p = (int *) calloc (1, sizeof (int))))
    return ENOMEM;

  *p = PTHREAD_PROCESS_PRIVATE;
  *attr = p;

  return 0;
}

int pthread_barrierattr_destroy(void **attr)
{
  void *p;
  if (!attr || (p = *attr) == NULL)
    return EINVAL;
  *attr = NULL;
  free (p);
  return 0;
}

int pthread_barrierattr_setpshared(void **attr, int s)
{
  if (!attr || *attr == NULL
      || (s != PTHREAD_PROCESS_SHARED && s != PTHREAD_PROCESS_PRIVATE))
    return EINVAL;
  memcpy (*attr, &s, sizeof (int));
  return 0;
}

int pthread_barrierattr_getpshared(void **attr, int *s)
{
  if (!attr || !s || *attr == NULL)
    return EINVAL;
  memcpy (s, *attr, sizeof (int));
  return 0;
}

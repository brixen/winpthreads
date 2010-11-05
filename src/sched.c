#include <windows.h>
#include <stdio.h>
#include "pthread.h"
#include "thread.h"

#include "misc.h"

int sched_get_priority_min(int pol)
{
  if (pol < SCHED_MIN || pol > SCHED_MAX) {
      errno = EINVAL;
      return -1;
  }

  return THREAD_PRIORITY_IDLE;
}

int sched_get_priority_max(int pol)
{
  if (pol < SCHED_MIN || pol > SCHED_MAX) {
      errno = EINVAL;
      return -1;
  }

  return THREAD_PRIORITY_TIME_CRITICAL;
}

int pthread_attr_setschedparam(pthread_attr_t *attr, const struct sched_param *p)
{
    int r = 0;

    if (attr == NULL || p == NULL) {
        return EINVAL;
    }
    memcpy(&attr->param, p, sizeof (*p));
    return r;
}

int pthread_attr_getschedparam(const pthread_attr_t *attr, struct sched_param *p)
{
    int r = 0;

    if (attr == NULL || p == NULL) {
        return EINVAL;
    }
    memcpy(p, &attr->param, sizeof (*p));
    return r;
}

int pthread_getschedparam(pthread_t t, int *pol, struct sched_param *p)
{
    int r = 0;

    if (pol == NULL || p == NULL) {
        return EINVAL;
    }
    if ((r = pthread_kill (t, 0) )) {
        return r;
    }

    *pol = SCHED_OTHER;
    p->sched_priority = t->sched_priority;

    return r;

}

int pthread_setschedparam(pthread_t t, int pol,  const struct sched_param *p)
{
    int r = 0;

    if ((pol < SCHED_MIN || pol > SCHED_MAX) || p == NULL) {
        return EINVAL;
    }
    if (pol != SCHED_OTHER) {
        return ENOTSUP;
    }
    if ((r = pthread_kill (t, 0) )) {
        return r;
    }
    t->sched_pol = pol;
    t->sched_priority = p->sched_priority;

    return r;
}


#include <windows.h>
#include <stdio.h>
#include "pthread.h"
#include "thread.h"
#include "misc.h"
#include "semaphore.h"
#include "sem.h"
#include "mutex.h"
#include "ref.h"

int sem_init(sem_t *sem, int pshared, unsigned int value)
{
    _sem_t *s;
    mode_t r;

    if (value > (unsigned int)SEM_VALUE_MAX) {
        return EINVAL;
    }
    if (pshared == PTHREAD_PROCESS_SHARED) {
        return EPERM;
    }

    r = sem_ref_init(sem);
    if (r != 0)
        return r;

    if (!(s = (sem_t)calloc(1,sizeof(*s))))
        return ENOMEM; 

    if ((s->s = CreateSemaphore (NULL, value, SEM_VALUE_MAX, NULL)) == NULL) {
        s->valid = DEAD_SEM;
        free(s); 
        return ENOSPC;
    }
    InitializeCriticalSectionAndSpinCount(&s->value_lock,USE_SEM_CriticalSection_SpinCount);

    s->initial = s->value = value;
    s->valid = LIFE_SEM;
    *sem = s;

    return 0;
}

int sem_destroy(sem_t *sem)
{
    sem_t sDestroy;
    int r = sem_ref_destroy(sem,&sDestroy);
    
    if (r)
      return r;

    _sem_t *s = (_sem_t *)sDestroy;
    
    CloseHandle(s->s); /* no way back if this fails */
    DeleteCriticalSection(&s->value_lock);
    s->valid = DEAD_SEM;
    free(sDestroy);
    return r;

}

int sem_trywait(sem_t *sem)
{
    int r = sem_ref(sem);
    if(r) return r;
     
    _sem_t *s = (_sem_t *)*sem;
    EnterCriticalSection (&s->value_lock);
    if (s->value > 0) {
        s->value--;
    } else {
        r = EAGAIN;
    }
    LeaveCriticalSection (&s->value_lock);

    return sem_unref(sem,r);
}

static void _sem_cleanup (void *arg)
{
     _sem_t *s = (_sem_t *)arg;
    EnterCriticalSection (&s->value_lock);
    if (WaitForSingleObject(s->s, 0) != WAIT_OBJECT_0) {
        /* cleanup the wait state */
        s->value ++;
    }
    LeaveCriticalSection (&s->value_lock);
}

int sem_wait(sem_t *sem)
{
    int r = sem_ref(sem);
    if(r) return r;
    DWORD dwr;
    unsigned int n;
    
    _sem_t *s = (_sem_t *)*sem;
    pthread_testcancel();
    EnterCriticalSection (&s->value_lock);
    n = --s->value;
    LeaveCriticalSection (&s->value_lock);
    if (n<0) {
        dwr = WaitForSingleObject(s->s, INFINITE);
        switch (dwr) {
        case WAIT_ABANDONED:
            r = EINTR;
            break;
        case WAIT_OBJECT_0:
            r = 0;
            break;
        default:
            /*We can only return EINVAL though it might not be posix compliant  */
            r = EINVAL;
        }
     }
    if (r) {
        EnterCriticalSection (&s->value_lock);
        s->value++;
        LeaveCriticalSection (&s->value_lock);
    }

    return sem_unref(sem,r);
}

int sem_timedwait(sem_t *sem, const struct timespec *t)
{
    int r = sem_ref(sem);
    if(r) return r;
    DWORD dwr;
    unsigned int n;
    
    _sem_t *s = (_sem_t *)*sem;
    pthread_testcancel();
    EnterCriticalSection (&s->value_lock);
    n = --s->value;
    LeaveCriticalSection (&s->value_lock);
    dwr = (t==NULL) ? INFINITE :_pthread_rel_time_in_ms(t);
    if (n<0) {
        dwr = WaitForSingleObject(s->s, dwr);
        switch (dwr) {
        case WAIT_TIMEOUT:
            r = ETIMEDOUT;
            break;
        case WAIT_ABANDONED:
            r = EINTR;
            break;
        case WAIT_OBJECT_0:
            r = 0;
            break;
        default:
            /*We can only return EINVAL though it might not be posix compliant  */
            r = EINVAL;
        }
    }
    if (r) {
        EnterCriticalSection (&s->value_lock);
        s->value++;
        LeaveCriticalSection (&s->value_lock);
    }

    return sem_unref(sem,r);
}

int sem_post(sem_t * sem)
{
    int r = sem_ref(sem);
    if(r) return r;
    
    _sem_t *s = (_sem_t *)*sem;
    EnterCriticalSection (&s->value_lock);
    if (s->value < SEM_VALUE_MAX) {
        if (++s->value <= 0) {
            if (!ReleaseSemaphore (s->s, 1, NULL)) {
                s->value--;
                r = EINVAL;
            }
        }
    } else {
        r = ERANGE;
    }
    LeaveCriticalSection (&s->value_lock);

    return sem_unref(sem,r);
}

int sem_post_multiple(sem_t *sem, int count)
{
    int r = sem_ref(sem);
    if(r) return r;
    
    _sem_t *s = (_sem_t *)*sem;

    EnterCriticalSection (&s->value_lock);
    if (s->value > (SEM_VALUE_MAX-count)) {
        r = ERANGE;
    } else {
        int waiters_count = -s->value;
        s->value += count;
        waiters_count = (waiters_count > count) ? count : waiters_count;
        if (waiters_count > 0) {
            if (!ReleaseSemaphore (s->s, waiters_count, NULL)) {
                s->value -= count;
                r = EINVAL;
            }
        }
    }
    LeaveCriticalSection (&s->value_lock);

    return sem_unref(sem,r);
}

sem_t *sem_open(const char *name, int oflag, mode_t mode, unsigned int value)
{
  errno = ENOSYS;
  return SEM_FAILED;
}

int sem_close(sem_t *sem)
{
  errno = ENOSYS;
  return -1;
}

int sem_unlink(const char *name)
{
  errno = ENOSYS;
  return -1;
}

int sem_getvalue(sem_t *sem, int *sval)
{
    int r = sem_ref(sem);
    if(r) return r;
    
    _sem_t *s = (_sem_t *)*sem;
    *sval = s->value;
    return sem_unref(sem,r);
}

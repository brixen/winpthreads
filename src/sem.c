#include <windows.h>
#include <stdio.h>
#include "pthread.h"
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

    if ((s->s = CreateSemaphore (NULL, 0, SEM_VALUE_MAX, NULL)) == NULL) {
        s->valid = DEAD_SEM;
        free(s); 
        return ENOSPC;
    }

    s->value = value;
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
    s->valid = DEAD_SEM;
    free(sDestroy);
    return r;

}

int sem_trywait(sem_t *sem)
{
    int r = sem_ref(sem);
    if(r) return r;
    
    _sem_t *s = (_sem_t *)*sem;
    (void)s;

    return sem_unref(sem,r);
}

int sem_wait(sem_t *sem)
{
    int r = sem_ref(sem);
    if(r) return r;
    
    _sem_t *s = (_sem_t *)*sem;
    (void)s;

    return sem_unref(sem,r);
}

int sem_timedwait(sem_t *sem, const struct timespec *abstime)
{
    int r = sem_ref(sem);
    if(r) return r;
    
    _sem_t *s = (_sem_t *)*sem;
    (void)s;

    return sem_unref(sem,r);
}

int sem_post(sem_t * sem)
{
    int r = sem_ref(sem);
    if(r) return r;
    
    _sem_t *s = (_sem_t *)*sem;
    (void)s;

    return sem_unref(sem,r);
}

int sem_post_multiple(sem_t *sem, int count)
{
    int r = sem_ref(sem);
    if(r) return r;
    
    _sem_t *s = (_sem_t *)*sem;
    (void)s;

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

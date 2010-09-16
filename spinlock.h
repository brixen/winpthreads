#ifndef WIN_PTHREADS_SPINLOCK_H
#define WIN_PTHREADS_SPINLOCK_H

extern int pthread_spin_init(pthread_spinlock_t *l, int pshared);
extern int pthread_spin_destroy(pthread_spinlock_t *l);
/* No-fair spinlock due to lack of knowledge of thread number */
extern int pthread_spin_lock(pthread_spinlock_t *l);
extern int pthread_spin_trylock(pthread_spinlock_t *l);
extern int pthread_spin_unlock(pthread_spinlock_t *l);

#endif

#ifndef WIN_PTHREADS_COND_H
#define WIN_PTHREADS_COND_H

extern int pthread_cond_init(pthread_cond_t *cv, 
                             pthread_condattr_t *a);
extern int pthread_cond_destroy(pthread_cond_t *cv);
extern int pthread_cond_signal (pthread_cond_t *cv);
extern int pthread_cond_broadcast (pthread_cond_t *cv);
extern int pthread_cond_wait (pthread_cond_t *cv, 
                              pthread_mutex_t *external_mutex);
extern int pthread_cond_timedwait(pthread_cond_t *cv, pthread_mutex_t *external_mutex, struct timespec *t);
extern int pthread_condattr_destroy(pthread_condattr_t *a);
extern int pthread_condattr_init(pthread_condattr_t *a);
extern int pthread_condattr_getpshared(pthread_condattr_t *a, int *s);
extern int pthread_condattr_setpshared(pthread_condattr_t *a, int s);

#endif

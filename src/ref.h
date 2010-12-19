#ifndef WIN_PTHREADS_REF_H
#define WIN_PTHREADS_REF_H
#include "pthread.h"
#include "semaphore.h"

inline int mutex_unref(volatile pthread_mutex_t *m, int r);

inline int mutex_ref(volatile pthread_mutex_t *m );
inline int mutex_ref_unlock(volatile pthread_mutex_t *m );
inline int mutex_ref_destroy(volatile pthread_mutex_t *m, pthread_mutex_t *mDestroy );
inline int mutex_ref_init(volatile pthread_mutex_t *m );
inline int mutex_unref(volatile pthread_mutex_t *m, int r);
/* External: must be called by owner of a locked mutex: */
inline int mutex_ref_ext(volatile pthread_mutex_t *m);

inline int barrier_unref(volatile pthread_barrier_t *barrier, int res);
inline int barrier_ref(volatile pthread_barrier_t *barrier);
inline int barrier_ref_destroy(volatile pthread_barrier_t *barrier, pthread_barrier_t *bDestroy );
inline int barrier_ref_init(volatile pthread_barrier_t *barrier );
inline void barrier_ref_set (volatile pthread_barrier_t *barrier, void *v);

#endif


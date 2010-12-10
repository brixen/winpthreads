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

inline int rwl_unref(volatile pthread_rwlock_t *rwl, int res);
inline int rwl_ref(volatile pthread_rwlock_t *rwl, int f );
inline int rwl_ref_unlock(volatile pthread_rwlock_t *rwl );
inline int rwl_ref_destroy(volatile pthread_rwlock_t *rwl, pthread_rwlock_t *rDestroy );
inline int rwl_ref_init(volatile pthread_rwlock_t *rwl );

inline int cond_unref(pthread_cond_t *cond, int res);
inline int cond_ref(pthread_cond_t *cond);
inline int cond_ref_wait(pthread_cond_t *cond);
inline int cond_ref_destroy(pthread_cond_t *cond, pthread_cond_t *cDestroy );
inline int cond_ref_init(pthread_cond_t *cond );
/* to be used in barriers */
#define cond_ref_ext    cond_ref

inline int barrier_unref(volatile pthread_barrier_t *barrier, int res);
inline int barrier_ref(volatile pthread_barrier_t *barrier);
inline int barrier_ref_destroy(volatile pthread_barrier_t *barrier, pthread_barrier_t *bDestroy );
inline int barrier_ref_init(volatile pthread_barrier_t *barrier );
inline void barrier_ref_set (volatile pthread_barrier_t *barrier, void *v);

#endif


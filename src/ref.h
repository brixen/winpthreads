#ifndef WIN_PTHREADS_REF_H
#define WIN_PTHREADS_REF_H

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

#endif


#ifndef WIN_PTHREADS_MUTEX_H
#define WIN_PTHREADS_MUTEX_H

#define CHECK_DEADLK(m)	if ((m->type != PTHREAD_MUTEX_RECURSIVE) && (m->owner == pthread_self())) \
							return EDEADLK

#define SET_OWNER(m)	if (m->type != PTHREAD_MUTEX_RECURSIVE) \
							m->owner = pthread_self()

#define UNSET_OWNER(m)	m->owner = NULL

#endif

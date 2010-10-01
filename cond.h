#ifndef WIN_PTHREADS_COND_H
#define WIN_PTHREADS_COND_H

#define CHECK_COND(c)  { \
    if (!(c) || !*c || (*c == PTHREAD_COND_INITIALIZER) \
		|| ( ((cond_t *)(*c))->valid != (unsigned int)LIFE_COND ) ) \
        return EINVAL; }

#define INIT_COND(c)  { int r; \
    if (!c || !*c)	return EINVAL; \
	if (*c == PTHREAD_COND_INITIALIZER) if ((r = cond_static_init(c))) return r; \
	if ( ( ((cond_t *)(*c))->valid != (unsigned int)LIFE_COND ) ) return EINVAL; }

#define LIFE_COND 0xC0BAB1FD
#define DEAD_COND 0xC0DEADBF

#if defined USE_COND_ConditionVariable
#include "compat.h"
typedef struct cond_t cond_t;
struct cond_t
{
    unsigned int valid;   
    LONG waiters_count_; /* Number of waiting threads.  */
    CRITICAL_SECTION waiters_count_lock_; /* Serialize access to <waiters_count_>.  */
    CONDITION_VARIABLE CV;
};
#else /* USE_COND_Semaphore default */
typedef struct cond_t cond_t;
struct cond_t
{
    unsigned int valid;   
    LONG waiters_count_; /* Number of waiting threads.  */
    CRITICAL_SECTION waiters_count_lock_; /* Serialize access to <waiters_count_>.  */
    HANDLE sema_; /* Semaphore used to queue up threads waiting for the condition to
    		     become signaled.  */
    HANDLE waiters_done_; /* An auto-reset event used by the broadcast/signal thread to wait
    			     for all the waiting thread(s) to wake up and be released from the
    			     semaphore.  */
    size_t was_broadcast_; /* Keeps track of whether we were broadcasting or signaling.  This
    			      allows us to optimize the code if we're just signaling.  */
};
#endif

#endif

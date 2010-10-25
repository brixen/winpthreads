#ifndef WIN_PTHREADS_COND_H
#define WIN_PTHREADS_COND_H

#define CHECK_COND(c)  { \
    if (!(c) || !*c || (*c == PTHREAD_COND_INITIALIZER) \
        || ( ((cond_t *)(*c))->valid != (unsigned int)LIFE_COND ) ) \
        return EINVAL; }

#define INIT_COND(c)  { int r; \
    if (!c || !*c)	return EINVAL; \
    if (*c == PTHREAD_COND_INITIALIZER) { if ((r = cond_static_init(c))) return r; } \
    else if ( ( ((cond_t *)(*c))->valid != (unsigned int)LIFE_COND ) ) return EINVAL; }

#define LIFE_COND 0xC0BAB1FD
#define DEAD_COND 0xC0DEADBF

#if defined USE_COND_ConditionVariable
#include "rwlock.h"
#ifndef CONDITION_VARIABLE_INIT
typedef struct _RTL_CONDITION_VARIABLE {                    
        PVOID Ptr;                                       
} RTL_CONDITION_VARIABLE, *PRTL_CONDITION_VARIABLE;      
#define RTL_CONDITION_VARIABLE_INIT {0}                 
#define RTL_CONDITION_VARIABLE_LOCKMODE_SHARED  0x1     

#define CONDITION_VARIABLE_INIT RTL_CONDITION_VARIABLE_INIT
#define CONDITION_VARIABLE_LOCKMODE_SHARED RTL_CONDITION_VARIABLE_LOCKMODE_SHARED

typedef RTL_CONDITION_VARIABLE CONDITION_VARIABLE, *PCONDITION_VARIABLE;

WINBASEAPI VOID WINAPI InitializeConditionVariable(PCONDITION_VARIABLE ConditionVariable);
WINBASEAPI VOID WINAPI WakeConditionVariable(PCONDITION_VARIABLE ConditionVariable);
WINBASEAPI VOID WINAPI WakeAllConditionVariable(PCONDITION_VARIABLE ConditionVariable);
WINBASEAPI BOOL WINAPI SleepConditionVariableCS(PCONDITION_VARIABLE ConditionVariable,PCRITICAL_SECTION CriticalSection,DWORD dwMilliseconds);
WINBASEAPI BOOL WINAPI SleepConditionVariableSRW(PCONDITION_VARIABLE ConditionVariable,PSRWLOCK SRWLock,DWORD dwMilliseconds,ULONG Flags);
#endif /* CONDITION_VARIABLE_INIT */

#endif /* USE_COND_ConditionVariable */

typedef struct cond_t cond_t;
struct cond_t
{
    unsigned int valid;   
    LONG waiters_count_; /* Number of waiting threads.  */
#if defined USE_COND_ConditionVariable
    CRITICAL_SECTION waiters_count_lock_; /* Serialize access to <waiters_count_>.  */
    CONDITION_VARIABLE CV;

#else /* USE_COND_Semaphore USE_COND_SignalObjectAndWait */
    CRITICAL_SECTION waiters_count_lock_; /* Serialize access to <waiters_count_>.  */
    HANDLE sema_; /* Semaphore used to queue up threads waiting for the condition to
                 become signaled.  */
    HANDLE waiters_done_; /* An auto-reset event used by the broadcast/signal thread to wait
                     for all the waiting thread(s) to wake up and be released from the
                     semaphore.  */
    size_t was_broadcast_; /* Keeps track of whether we were broadcasting or signaling.  This
                      allows us to optimize the code if we're just signaling.  */

#endif
};

#endif

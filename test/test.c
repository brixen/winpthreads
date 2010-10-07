#include <stdio.h>
#include <time.h>
#include "../pthread.h"
#include "../thread.h"
#include "../mutex.h"
#include "../cond.h"
#include "../misc.h"

#define MAX_THREAD 1000
#define N_THREAD 40

#define checkResults(string, val) {             \
 if (val) {                                     \
   printf("Failed with %d at %s", val, string); \
   exit(1);                                     \
 }                                              \
}

typedef struct {
	int id;
} parm;

struct timespec *starttimer(struct timespec *ts, DWORD ms)
{
	struct timeval    tp;
    /* Convert from timeval to timespec */
	gettimeofday(&tp, NULL);
    ts->tv_sec  = tp.tv_sec;
    ts->tv_nsec = tp.tv_usec * 1000;
    ts->tv_sec += ms  / 1000;
	return ts;
}


void *hello(void *arg)
{
	parm *p=(parm *)arg;
	printf("Hello from node %d\n", p->id);
	return (NULL);
}

pthread_rwlock_t       rwlock;
char testType[100];

/*================================================================================*/
#define SPINLOCK_NTHREADS                50
pthread_spinlock_t      spinlock;
int                     SLsharedData=0;
int                     SLsharedData2=0;

void *spinlock_threadfunc(void *parm)
{
   int   rc, d;
   int tid = pthread_self()->tid;
   printf("Thread %d: Entered\n", tid);
   rc = pthread_spin_lock(&spinlock);
   d= SLsharedData;
   checkResults("pthread_spin_lock()\n", rc);
   /********** Critical Section *******************/
   printf("Thread %d: Start critical section, holding lock\n",
          tid);
   /* Access to shared data goes here */
   ++SLsharedData; --SLsharedData2;
   Sleep(50);
   printf("Thread %d: End critical section, release lock\n",
          tid);
   d -= (SLsharedData-1);
   /********** Critical Section *******************/
   rc = pthread_spin_unlock(&spinlock);
   checkResults("pthread_spinlock_unlock()\n", rc);
   if (d) {
		printf("FAILED Thread %d: check SLsharedData=%d instead of 0\n", tid, d);
		exit(1);
   }
   return NULL;
}
 
int spinlock_main(void)
{
	pthread_t             thread[SPINLOCK_NTHREADS];
	int                   rc=0;
	int                   i;

	printf("Enter Testcase - spinlock_main %s\n",testType);
	rc = pthread_spin_init(&spinlock, PTHREAD_PROCESS_SHARED);
	printf("Spinlock inited\n");
	checkResults("pthread_spin_init()\n", rc);

	printf("Hold Spinlock to prevent access to shared data\n");
	rc = pthread_spin_lock(&spinlock);
	printf("Unlock Spinlock to prevent access to shared data\n");
	checkResults("pthread_spin_lock()\n", rc);
	rc = pthread_spin_unlock(&spinlock);
	checkResults("pthread_spin_unlock()\n",rc);
	printf("Hold Spinlock to prevent access to shared data 2\n");
	rc = pthread_spin_lock(&spinlock);
	checkResults("pthread_spin_lock() 2\n", rc);

	printf("Create/start threads\n");
	for (i=0; i<SPINLOCK_NTHREADS; ++i) {
		rc = pthread_create(&thread[i], NULL, spinlock_threadfunc, NULL);
		checkResults("pthread_create()\n", rc);
	}

	printf("Wait a bit until we are 'done' with the shared data\n");
	Sleep(3000);
	printf("Unlock shared data\n");
	rc = pthread_spin_unlock(&spinlock);
	checkResults("pthread_spin_lock()\n",rc);

	printf("Wait for the threads to complete, and release their resources\n");
	for (i=0; i <SPINLOCK_NTHREADS; ++i) {
		rc = pthread_join(thread[i], NULL);
		checkResults("pthread_join()\n", rc);
	}

	printf("Clean up, data: %d %d\n",SLsharedData,SLsharedData2);
	rc = pthread_spin_destroy(&spinlock);
	printf("Main completed\n");
	return 0;
}
/*================================================================================*/
#define MUTEX_NTHREADS                50
pthread_mutex_t         mutex;
int                     sharedData=0;
int                     sharedData2=0;
int						mwait=30000;

void *mutex_threadfunc(void *parm)
{
   int   rc;
   int d;
   int tid = pthread_self()->tid;
   printf("Thread %d: Entered\n", tid);
   rc = pthread_mutex_lock(&mutex);
   d= sharedData;
   checkResults("pthread_mutex_lock()\n", rc);
   /********** Critical Section *******************/
   printf("Thread %d: Start critical section, holding lock\n",
          tid);
   /* Access to shared data goes here */
   ++sharedData; --sharedData2;
   Sleep(50);
   printf("Thread %d: End critical section, release lock\n",
          tid);
   d -= (sharedData-1);
   /********** Critical Section *******************/
   rc = pthread_mutex_unlock(&mutex);
   checkResults("pthread_mutex_unlock()\n", rc);
   if (d) {
		printf("FAILED Thread %d: check sharedData=%d instead of 0\n", tid, d);
		exit(1);
   }
   return NULL;
}
 
void *mutex_threadfunc_timed(void *parm)
{
   int   rc;
   int d;
   int tid = pthread_self()->tid;
   struct timespec   ts;
   printf("Thread %d: Entered\n", tid);
   rc = pthread_mutex_timedlock(&mutex,starttimer(&ts, 8000 ));
   d= sharedData;
   checkResults("pthread_mutex_lock()\n", rc);
   /********** Critical Section *******************/
   printf("Thread %d: Start critical section, holding lock\n",
          tid);
   /* Access to shared data goes here */
   ++sharedData; --sharedData2;
   Sleep(50);
   printf("Thread %d: End critical section, release lock\n",
          tid);
   d -= (sharedData-1);
   /********** Critical Section *******************/
   rc = pthread_mutex_unlock(&mutex);
   checkResults("pthread_mutex_unlock()\n", rc);
   if (d) {
		printf("FAILED Thread %d: check sharedData=%d instead of 0\n", tid, d);
		exit(1);
   }
   return NULL;
}
 
/* PTHREAD_NORMAL_MUTEX_INITIALIZER deadlock handling thread 1 */
void *mutex_threadfuncDL1(void *parm) 
{
   int   rc;
   int d;
   int tid = pthread_self()->tid;
   printf("Thread %d: Entered, locking n1\n", tid);
   rc = pthread_mutex_lock(&mutex);
   checkResults("pthread_mutex_lock() 1\n", rc);
   printf("Thread %d: locked nr1\n", tid);
   Sleep(1000);
   printf("Thread %d: locking nr2 for a deadlock\n", tid);
   rc = pthread_mutex_lock(&mutex);
   checkResults("pthread_mutex_lock() 2\n", rc);
   printf("Thread %d: locked nr2 and externally unlocked\n", tid);
   /********** Critical Section *******************/
   Sleep(2000);
   printf("Thread %d: Doing job\n", tid);
   Sleep(2000);
   printf("Thread %d: End critical section, release lock\n", tid);
   /********** Critical Section *******************/
   rc = pthread_mutex_unlock(&mutex);
   checkResults("pthread_mutex_unlock()\n", rc);
   printf("Thread %d: leaving\n", tid);
   return NULL;
}
 
/* PTHREAD_NORMAL_MUTEX_INITIALIZER deadlock handling main thread */
int mutex_main_DL(void)
{
   int   rc;
   int d;
   pthread_t thread;
   int tid = pthread_self()->tid;
	rc = pthread_create(&thread, NULL, mutex_threadfuncDL1, NULL);
	checkResults("mutex_main_DL: pthread_create()\n", rc);
   printf("mutex_main_DL: wait for thread\n");
   Sleep(3000);
   printf("mutex_main_DL: try ext release deadlock\n");

   rc = pthread_mutex_unlock(&mutex);
   checkResults("pthread_mutex_unlock() ext\n", rc);
   printf("mutex_main_DL: unlocked 1\n");
   Sleep(2000);
   //rc = pthread_mutex_unlock(&mutex);
   checkResults("pthread_mutex_unlock() ext 2\n", rc);
   printf("mutex_main_DL: unlocked 2\n");
   Sleep(10000);
   printf("mutex_main_DL: bye!\n");

   return 0;
}
 
int mutex_main_timed(void)
{
	pthread_t             thread[MUTEX_NTHREADS];
	int                   rc=0;
	int                   i;
	struct timespec   ts;

	printf("Enter Testcase - mutex_main_timed %s\n",testType);
	mutex = PTHREAD_NORMAL_MUTEX_INITIALIZER;
	printf("Mutex inited\n");

	printf("Hold Mutex to prevent access to shared data\n");
	rc = pthread_mutex_timedlock(&mutex,starttimer(&ts, 30000 ));
	printf("Mutex inited type=%d\n", ((mutex_t *)mutex)->type);
	printf("Unlock Mutex to prevent access to shared data\n");
	checkResults("pthread_mutex_lock()\n", rc);
	rc = pthread_mutex_unlock(&mutex);
	checkResults("pthread_mutex_unlock()\n",rc);
	printf("Hold Mutex to prevent access to shared data 2\n");
	rc = pthread_mutex_timedlock(&mutex,starttimer(&ts, 30000 ));
	checkResults("pthread_mutex_lock() 2\n", rc);

	printf("Create/start threads\n");
	for (i=0; i<MUTEX_NTHREADS; ++i) {
		rc = pthread_create(&thread[i], NULL, mutex_threadfunc_timed, NULL);
		checkResults("pthread_create()\n", rc);
	}

	printf("Wait a bit until we are 'done' with the shared data\n");
	Sleep(1000);
	printf("Unlock shared data\n");
	rc = pthread_mutex_unlock(&mutex);
	checkResults("pthread_mutex_lock()\n",rc);

	printf("Wait for the threads to complete, and release their resources\n");
	for (i=0; i <MUTEX_NTHREADS; ++i) {
		rc = pthread_join(thread[i], NULL);
		checkResults("pthread_join()\n", rc);
	}

	printf("Clean up, data: %d %d\n",sharedData,sharedData2);
	rc = pthread_mutex_destroy(&mutex);
	printf("Main completed\n");
	return 0;
}

int mutex_main(void)
{
	pthread_t             thread[MUTEX_NTHREADS];
	int                   rc=0;
	int                   i;

    int tid = pthread_self()->tid;
    printf("Main thread %d: Entered\n", tid);
	printf("Enter Testcase - mutex_main %s\n",testType);
	if (strcmp(testType,"static") == 0) {
		mutex = PTHREAD_DEFAULT_MUTEX_INITIALIZER;
	} else if (strcmp(testType,"staticN") == 0) {
		mutex = PTHREAD_NORMAL_MUTEX_INITIALIZER;
	} else if (strcmp(testType,"staticND") == 0) {
		mutex = PTHREAD_NORMAL_MUTEX_INITIALIZER;
		return mutex_main_DL();
	} else if (strcmp(testType,"staticE") == 0) {
		mutex = PTHREAD_ERRORCHECK_MUTEX_INITIALIZER;
	} else if (strcmp(testType,"staticR") == 0) {
		mutex = PTHREAD_RECURSIVE_MUTEX_INITIALIZER;
	} else if (strcmp(testType,"timed") == 0) {
		mutex = PTHREAD_NORMAL_MUTEX_INITIALIZER;
		return mutex_main_timed();
	} else {
		rc = pthread_mutex_init(&mutex,NULL);
		printf("Mutex inited\n");
		checkResults("pthread_mutex_init()\n", rc);
	}
	printf("Mutex inited\n");

	printf("Hold Mutex to prevent access to shared data o=%d\n",0);
	rc = pthread_mutex_lock(&mutex);
	printf("Mutex inited type=%d o=%d\n", ((mutex_t *)mutex)->type,GET_OWNER(((mutex_t *)mutex)));
	printf("Unlock Mutex to prevent access to shared data\n");
	checkResults("pthread_mutex_lock()\n", rc);
	rc = pthread_mutex_unlock(&mutex);
	printf("Unlocked Mutex to prevent access to shared data o=%d\n",GET_OWNER(((mutex_t *)mutex)));
	checkResults("pthread_mutex_unlock()\n",rc);
	printf("Hold Mutex to prevent access to shared data 2\n");
	rc = pthread_mutex_lock(&mutex);
	checkResults("pthread_mutex_lock() 2\n", rc);

	printf("Create/start threads\n");
	for (i=0; i<MUTEX_NTHREADS; ++i) {
		rc = pthread_create(&thread[i], NULL, mutex_threadfunc, NULL);
		checkResults("pthread_create()\n", rc);
	}

	printf("Wait a bit until we are 'done' with the shared data\n");
	Sleep(3000);
	printf("Unlock shared data\n");
	rc = pthread_mutex_unlock(&mutex);
	checkResults("pthread_mutex_lock()\n",rc);

	printf("Wait for the threads to complete, and release their resources\n");
	for (i=0; i <MUTEX_NTHREADS; ++i) {
		rc = pthread_join(thread[i], NULL);
		checkResults("pthread_join()\n", rc);
	}

	printf("Clean up, data: %d %d\n",sharedData,sharedData2);
	rc = pthread_mutex_destroy(&mutex);
	printf("Main completed\n");
	return 0;
}
#if 1
/*================================================================================*/
#define COND_NTHREADS                3
#define COND_WAIT_TIME_SECONDS       10

int                 workToDo = 0;
int                 workLeave = 0;
pthread_cond_t      cond;
pthread_mutex_t     mutex;

void *condTimed_threadfunc(void *parm)
{
  int               tid;
  int               rc;
  struct timespec   ts;
  struct timeval    tp;

  rc = pthread_mutex_lock(&mutex);
  checkResults("pthread_mutex_lock()\n", rc);
  tid = pthread_self()->tid;

  /* Usually worker threads will loop on these operations */
  while (!workLeave) {
    rc =  gettimeofday(&tp, NULL);
    checkResults("gettimeofday()\n", rc);

    /* Convert from timeval to timespec */
    ts.tv_sec  = tp.tv_sec;
    ts.tv_nsec = tp.tv_usec * 1000;
    ts.tv_sec += COND_WAIT_TIME_SECONDS;

    do {
	  if (strcmp(testType,"notTimed") == 0) {
		printf("Thread %d blocked, notTimed\n", tid);
		rc = pthread_cond_wait(&cond, &mutex);
	  } else {
		printf("Thread %d blocked and waiting\n", tid);
		rc = pthread_cond_timedwait(&cond, &mutex, starttimer(&ts, 6000 ));
	  }
      /* If the wait timed out, in this example, the work is complete, and   */
      /* the thread will end.                                                */
      /* In reality, a timeout must be accompanied by some sort of checking  */
      /* to see if the work is REALLY all complete. In the simple example    */
      /* we'll just go belly up when we time out.                            */
      printf("Thread %d unblocked\n", tid);
      if (rc == ETIMEDOUT) {
        printf("Wait %d timed out!\n", tid);
        rc = pthread_mutex_unlock(&mutex);
        checkResults("pthread_mutex_unlock() A\n", rc);
		printf("Exit %d\n", tid);
        pthread_exit(NULL);
      }
      checkResults("pthread_cond_timedwait()\n", rc);
    } while (!workLeave && !workToDo); 
	if (workToDo) {
		printf("Thread %d consumes work here\n", tid);
		Sleep(2000);
		workToDo = 0;
	}
  } 
  printf("Thread %d leaves here\n", tid);
  rc = pthread_mutex_unlock(&mutex);
  checkResults("pthread_mutex_unlock() B\n", rc);
  return NULL;
}

int condTimed_main()
{
  int                   rc=0;
  int                   i;
  pthread_t             threadid[COND_NTHREADS];
  struct timespec   ts;

  printf("Enter Testcase - condTimed_main %s\n",testType);

  rc = pthread_mutex_init (&mutex, NULL);
  checkResults("pthread_mutex_init()\n", rc);
	
  rc = pthread_cond_init (&cond, NULL);
  checkResults("pthread_cond_init()\n", rc);
	
  rc = pthread_mutex_lock(&mutex);
  checkResults("pthread_mutex_lock()\n", rc);

  printf("Try steal a signal 1 (wait on own signal), should timeout\n");
  rc = pthread_cond_broadcast(&cond);
  checkResults("pthread_cond_signal()\n", rc);

  rc = pthread_cond_timedwait(&cond, &mutex, starttimer(&ts, 3000 ));
  printf("rc=%d\n",rc);
  checkResults("pthread_cond_timedwait() Steal 1\n", rc - ETIMEDOUT);
  printf("Done, rc=%d\n",rc);
  rc = pthread_mutex_unlock(&mutex);

  printf("Create %d threads\n", COND_NTHREADS);
  for(i=0; i<COND_NTHREADS; ++i) {
    rc = pthread_create(&threadid[i], NULL, condTimed_threadfunc, NULL);
    checkResults("pthread_create()\n", rc);
  }
  Sleep(2000);
  printf("One work item to give to a thread\n");
  workToDo = 1;
  rc = pthread_mutex_lock(&mutex);
  rc = pthread_cond_signal(&cond);
  checkResults("pthread_cond_signal()\n", rc);
  rc = pthread_mutex_unlock(&mutex);
  checkResults("pthread_mutex_unlock()\n", rc);

  Sleep(3001);
  rc = pthread_mutex_lock(&mutex);
  checkResults("pthread_mutex_lock() 2\n", rc);
  printf("One another work item to give to a thread\n");
  workToDo = 1;
  rc = pthread_cond_signal(&cond);
  checkResults("pthread_cond_signal()\n", rc);

  rc = pthread_mutex_unlock(&mutex);
  checkResults("pthread_mutex_unlock()\n", rc);

  //Sleep(10000);
  printf("Broadcast leave to all threads, waiters=%d\n",((cond_t *)cond)->waiters_count_);
  workLeave = 1;
  rc = pthread_cond_broadcast(&cond);
   printf("Broadcast done, waiters=%d\n",((cond_t *)cond)->waiters_count_);
  checkResults("pthread_cond_broadcast()\n", rc);
  printf("Try steal a signal 2, should timeout\n");
  rc = pthread_mutex_lock(&mutex);
  printf("pthread_mutex_lock, waiters=%d\n",((cond_t *)cond)->waiters_count_);
  rc = pthread_cond_timedwait(&cond, &mutex, starttimer(&ts, 3000 ));
  printf("pthread_cond_timedwait, waiters=%d\n",((cond_t *)cond)->waiters_count_);
  checkResults("pthread_cond_timedwait() Steal 2\n", rc - ETIMEDOUT);
  printf("Done, rc=%d\n",rc);
  rc = pthread_mutex_unlock(&mutex);

  printf("Wait for threads and cleanup\n");
  for (i=0; i<COND_NTHREADS; ++i) {
    rc = pthread_join(threadid[i], NULL);
    checkResults("pthread_join()\n", rc);
  }

  printf("Exit, waiters=%d\n",((cond_t *)cond)->waiters_count_);
  pthread_cond_destroy(&cond);
  pthread_mutex_destroy(&mutex);
  printf("Main completed\n");
  return 0;
}
#else
/*================================================================================*/

#define COND_NTHREADS                3
#define COND_WAIT_TIME_SECONDS       10

int                 workToDo = 0;
int                 workLeave = 0;
pthread_cond_t      cond=PTHREAD_COND_INITIALIZER;
pthread_mutex_t     mutex=PTHREAD_MUTEX_INITIALIZER;

void *condTimed_threadfunc(void *parm)
{
  int               tid;
  int               rc;
  struct timespec   ts;
  struct timeval    tp;

  rc = pthread_mutex_lock(&mutex);
  checkResults("pthread_mutex_lock()\n", rc);
  tid = pthread_self()->tid;

  /* Usually worker threads will loop on these operations */
  printf("condTimed_threadfunc, wait %d secs\n", COND_WAIT_TIME_SECONDS);
  while (!workLeave) {
    rc =  gettimeofday(&tp, NULL);
    checkResults("gettimeofday()\n", rc);

    /* Convert from timeval to timespec */
    ts.tv_sec  = tp.tv_sec;
    ts.tv_nsec = tp.tv_usec * 1000;
    ts.tv_sec += COND_WAIT_TIME_SECONDS;

    do {
	  if (strcmp(testType,"notTimed") == 0) {
		printf("Thread %d blocked, notTimed\n", tid);
		rc = pthread_cond_wait(&cond, &mutex);
	  } else {
		printf("Thread %d blocked\n", tid);
		rc = pthread_cond_timedwait(&cond, &mutex, starttimer(&ts, 3000 ));
	  }
      /* If the wait timed out, in this example, the work is complete, and   */
      /* the thread will end.                                                */
      /* In reality, a timeout must be accompanied by some sort of checking  */
      /* to see if the work is REALLY all complete. In the simple example    */
      /* we'll just go belly up when we time out.                            */
      printf("Thread %d unblocked\n", tid);
      if (rc == ETIMEDOUT) {
        printf("Wait %d timed out!\n", tid);
        rc = pthread_mutex_unlock(&mutex);
        checkResults("pthread_mutex_unlock() A\n", rc);
		printf("Exit %d\n", tid);
        pthread_exit(NULL);
      }
      checkResults("pthread_cond_timedwait()\n", rc);
    } while (!workLeave && !workToDo); 
	if (workToDo) {
		printf("Thread %d consumes work here\n", tid);
		Sleep(2000);
		workToDo = 0;
	}
  } 
  printf("Thread %d leaves here\n", tid);
  rc = pthread_mutex_unlock(&mutex);
  checkResults("pthread_mutex_unlock() B\n", rc);
  return NULL;
}

int condTimed_main()
{
  int                   rc=0;
  int                   i;
  pthread_t             threadid[COND_NTHREADS];
  struct timespec   ts;

	printf("Enter Testcase - condTimed_main %s\n",testType);

	if (strcmp(testType,"static") == 0) {
		printf("cond + mutex static initialized\n");
		strcpy(testType, "notTimed");
	} else {
	  rc = pthread_mutex_init (&mutex, NULL);
	  checkResults("pthread_mutex_init()\n", rc);
	
	  rc = pthread_cond_init (&cond, NULL);
	  checkResults("pthread_cond_init()\n", rc);
		printf("cond + mutex normal initialized\n");
	}

	
  rc = pthread_mutex_lock(&mutex);
  checkResults("pthread_mutex_lock()\n", rc);
    printf("Mutex locked \n");

  printf("Try steal a signal 1, should timeout\n");
  
  printf("Timed wait 1, waiters=%d\n",cond->waiters_count_);
  rc = pthread_cond_timedwait(&cond, &mutex, starttimer(&ts, 3000 ));
  printf("rc=%d\n",rc);
  checkResults("pthread_cond_timedwait() Steal 1\n", rc - ETIMEDOUT);
  printf("Done, rc=%d\n",rc);
  rc = pthread_mutex_unlock(&mutex);

  printf("Create %d threads\n", COND_NTHREADS);
  for(i=0; i<COND_NTHREADS; ++i) {
    rc = pthread_create(&threadid[i], NULL, condTimed_threadfunc, NULL);
    checkResults("pthread_create()\n", rc);
  }
  Sleep(2000);
  printf("One work item to give to a thread\n");
  workToDo = 1;
  rc = pthread_mutex_lock(&mutex);
  printf("Mutex locked 2\n");
  rc = pthread_cond_signal(&cond);
  checkResults("pthread_cond_signal()\n", rc);
  rc = pthread_mutex_unlock(&mutex);
  checkResults("pthread_mutex_unlock()\n", rc);

  //Sleep(1000);
  rc = pthread_mutex_lock(&mutex);
  checkResults("pthread_mutex_lock() 2\n", rc);
  printf("One another work item to give to a thread\n");
  workToDo = 1;
  rc = pthread_cond_signal(&cond);
  checkResults("pthread_cond_signal()\n", rc);

  rc = pthread_mutex_unlock(&mutex);
  checkResults("pthread_mutex_unlock()\n", rc);

  //Sleep(1000);
  printf("Broadcast leave to all threads, waiters=%d\n",cond->waiters_count_);
  workLeave = 1;
  rc = pthread_cond_broadcast(&cond);
  //Sleep(1000);
  printf("Broadcast done, waiters=%d\n",cond->waiters_count_);
  checkResults("pthread_cond_broadcast()\n", rc);
  printf("Try steal a signal 2, should timeout\n");
  rc = pthread_mutex_lock(&mutex);
  printf("Timed wait 2, waiters=%d\n",cond->waiters_count_);
  rc = pthread_cond_timedwait(&cond, &mutex, starttimer(&ts, 2000 ));
  checkResults("pthread_cond_timedwait() Steal 2\n", rc - ETIMEDOUT);
  printf("Done, rc=%d\n",rc);
  rc = pthread_mutex_unlock(&mutex);

  printf("Wait for threads and cleanup\n");
  for (i=0; i<COND_NTHREADS; ++i) {
    rc = pthread_join(threadid[i], NULL);
    checkResults("pthread_join()\n", rc);
  }

  printf("Exit, waiters=%d\n",cond->waiters_count_);
  pthread_cond_destroy(&cond);
  pthread_mutex_destroy(&mutex);
  printf("Main completed\n");
  return 0;
}

int cond_main()
{
  strcpy(testType, "notTimed");
  return condTimed_main();
}

int condStatic_main()
{
  strcpy(testType, "static");
  return condTimed_main();
}
#endif
/*================================================================================*/
void *rwlockTimed_rdlockThread(void *arg)
{
  int             rc;
  int             count=0;
  struct timespec ts;

  printf("Entered thread, getting read lock with timeout\n");
  Retry:
  rc = pthread_rwlock_timedrdlock(&rwlock, starttimer(&ts, 3000 ));
  if (rc == EBUSY) {
    if (count >= 10) {
      printf("Retried too many times, failure!\n");
      exit(EXIT_FAILURE);
    }
    ++count;
    printf("RETRY...\n");
    goto Retry;
  }
  checkResults("pthread_rwlock_rdlock() 1\n", rc);

  Sleep(2000);

  printf("unlock the read lock\n");
  rc = pthread_rwlock_unlock(&rwlock);
  checkResults("pthread_rwlock_unlock()\n", rc);

  printf("Secondary thread complete\n");
  return NULL;
}

int rwlockTimed_main(void)
{
  int                   rc=0;
  pthread_t             thread;

  printf("Enter Testcase rwlockTimed_main\n");

  printf("Main, initialize the read write lock\n");
  rc = pthread_rwlock_init(&rwlock, NULL);
  checkResults("pthread_rwlock_init()\n", rc);

  printf("Main, get the write lock\n");
  rc = pthread_rwlock_wrlock(&rwlock);
  checkResults("pthread_rwlock_wrlock()\n", rc);

  printf("Main, create the timed rd lock thread\n");
  rc = pthread_create(&thread, NULL, rwlockTimed_rdlockThread, NULL);
  checkResults("pthread_create\n", rc);

  printf("Main, wait a bit holding the write lock\n");
  Sleep(2000);

  printf("Main, Now unlock the write lock\n");
  rc = pthread_rwlock_unlock(&rwlock);
  checkResults("pthread_rwlock_unlock()\n", rc);

  printf("Main, wait for the thread to end\n");
  rc = pthread_join(thread, NULL);
  checkResults("pthread_join\n", rc);

  rc = pthread_rwlock_destroy(&rwlock);
  checkResults("pthread_rwlock_destroy()\n", rc);
  printf("Main completed\n");
  return 0;
}


/*================================================================================*/

void *rwlock_rdlockThread(void *arg)
{
  int rc;

  printf("Entered thread, getting read lock\n");
  rc = pthread_rwlock_rdlock(&rwlock);
  checkResults("pthread_rwlock_rdlock()\n", rc);
  printf("got the rwlock read lock\n");

  Sleep(5);

  printf("unlock the read lock\n");
  rc = pthread_rwlock_unlock(&rwlock);
  checkResults("pthread_rwlock_unlock()\n", rc);
  printf("Secondary thread unlocked\n");
  return NULL;
}

void *rwlock_wrlockThread(void *arg)
{
  int rc;

  printf("Entered thread, getting write lock\n");
  rc = pthread_rwlock_wrlock(&rwlock);
  checkResults("pthread_rwlock_wrlock()\n", rc);

  printf("Got the rwlock write lock, now unlock\n");
  rc = pthread_rwlock_unlock(&rwlock);
  checkResults("pthread_rwlock_unlock()\n", rc);
  printf("Secondary thread unlocked\n");
  return NULL;
}

int rwlock_main(void)
{
  int                   rc=0;
  pthread_t             thread, thread1;

  printf("Enter Testcase rwlock_main\n");

  printf("Main, initialize the read write lock\n");
  rc = pthread_rwlock_init(&rwlock, NULL);
  checkResults("pthread_rwlock_init()\n", rc);

  printf("Main, grab a read lock\n");
  rc = pthread_rwlock_rdlock(&rwlock);
  checkResults("pthread_rwlock_rdlock()\n",rc);

  printf("Main, grab the same read lock again\n");
  rc = pthread_rwlock_rdlock(&rwlock);
  checkResults("pthread_rwlock_rdlock() second\n", rc);

  printf("Main, create the read lock thread\n");
  rc = pthread_create(&thread, NULL, rwlock_rdlockThread, NULL);
  checkResults("pthread_create\n", rc);

  printf("Main - unlock the first read lock\n");
  rc = pthread_rwlock_unlock(&rwlock);
  checkResults("pthread_rwlock_unlock()\n", rc);

  printf("Main, create the write lock thread\n");
  rc = pthread_create(&thread1, NULL, rwlock_wrlockThread, NULL);
  checkResults("pthread_create\n", rc);

  printf("Main - unlock the second read lock\n");
  rc = pthread_rwlock_unlock(&rwlock);
  checkResults("pthread_rwlock_unlock()\n", rc);

  printf("Main, wait for the threads\n");
  rc = pthread_join(thread, NULL);
  checkResults("pthread_join\n", rc);

  rc = pthread_join(thread1, NULL);
  checkResults("pthread_join\n", rc);

  rc = pthread_rwlock_destroy(&rwlock);
  checkResults("pthread_rwlock_destroy()\n", rc);

  printf("Main completed\n");
  return 0;
}

/*================================================================================*/
#define BARRIER_NTHREADS 20

#define BARRIER_ROUNDS 20

static pthread_barrier_t barriers[BARRIER_NTHREADS];

static pthread_mutex_t lock;
static int counters[BARRIER_NTHREADS];
static int serial[BARRIER_NTHREADS];



void *barrier_Thread(void *arg)
{
    void *result = NULL;
    int nr = (int)(uintptr_t)arg;
    int i;

    for (i = 0; i < BARRIER_ROUNDS; ++i)
    {
        int j;
        int retval;

        if (nr == 0)
        {
            memset (counters, '\0', sizeof (counters));
            memset (serial, '\0', sizeof (serial));
        }

        retval = pthread_barrier_wait (&barriers[BARRIER_NTHREADS - 1]);
        if (retval != 0 && retval != PTHREAD_BARRIER_SERIAL_THREAD)
        {
            printf ("thread %d failed to wait for all the others\n", nr);
            result = (void *) 1;
        }

        for (j = nr; j < BARRIER_NTHREADS; ++j)
        {
            /* Increment the counter for this round.  */
			//printf ("Increment the counter for this round %d\n", j);
            pthread_mutex_lock (&lock);
            ++counters[j];
            pthread_mutex_unlock (&lock);
			//printf ("Incremented the counter for this round %d\n", j);

            /* Wait for the rest.  */
            retval = pthread_barrier_wait (&barriers[j]);

            /* Test the result.  */
            if (nr == 0 && counters[j] != j + 1)
            {
                printf ("barrier in round %d released but count is %d\n",
                        j, counters[j]);
                result = (void *) 1;
            }

            if (retval != 0)
            {
                if (retval != PTHREAD_BARRIER_SERIAL_THREAD)
                {
                    printf ("thread %d in round %d has nonzero return value != PTHREAD_BARRIER_SERIAL_THREAD\n",
                            nr, j);
                    result = (void *) 1;
                }
                else
                {
 					//printf ("pthread_mutex_lock %d\n", nr);
					pthread_mutex_lock (&lock);
                    ++serial[j];
                    pthread_mutex_unlock (&lock);
                }
            }

            /* Wait for the rest again.  */
			printf ("Wait for the rest again %d\n",j);
            retval = pthread_barrier_wait (&barriers[j]);
			/* the following printf can make bugs go away - timing dependend */
			/* without this printf the test hangs here */
			/* try USE_MUTEX_CriticalSection + USE_COND_Semaphore */
			/* printf ("Wait for the rest again continue %d\n",j); */
	
            /* Now we can check whether exactly one thread was serializing.  */
            if (nr == 0 && serial[j] != 1)
            {
                printf ("not exactly one serial thread in round %d\n", j);
                result = (void *) 1;
				exit(1);
            }
        }
    }

    return result;
}

int barrier_main(void)
{
	int                   rc=0;
	pthread_t threads[BARRIER_NTHREADS];
	int i;
	void *res;
	int result = 0;


	printf("Enter Testcase - barrier_main %s\n",testType);

	rc = pthread_mutex_init (&lock, NULL);
	checkResults("pthread_mutex_init()\n", rc);

	/* Initialized the barrier variables.  */
	for (i = 0; i < BARRIER_NTHREADS; ++i) {
		if (pthread_barrier_init (&barriers[i], NULL, i + 1) != 0)
		{
			printf ("Failed to initialize barrier %d\n", i);
			exit (1);
		}
		printf ("initialized barrier %d\n", i);
	}

	/* Start the threads.  */
	for (i = 0; i < BARRIER_NTHREADS; ++i) {
		if (pthread_create (&threads[i], NULL, barrier_Thread, (void *)(uintptr_t)i) != 0)
		{
			printf ("Failed to start thread %d\n", i);
			exit (1);
		}
		printf ("started thread %d\n", i);
	}

	/* And wait for them.  */
	printf ("Wait for %d threads\n", i);
	for (i = 0; i < BARRIER_NTHREADS; ++i) {
		if (pthread_join (threads[i], &res) != 0 || res != NULL)
		{
			printf ("thread %d returned a failure\n", i);
			result = 1;
		}
	}

	printf ("Result: %d\n", result);
	if (result == 0)
		puts ("all OK");

	return result;

}
/*================================================================================*/
void thread(void)
{
	int i;
	parm *p;
	pthread_t *threads;
	pthread_attr_t pthread_custom_attr;
	int n = N_THREAD;
	n = N_THREAD;
	if ((n < 1) || (n > MAX_THREAD))
	{
		printf ("The no of thread should between 1 and %d.\n",MAX_THREAD);
		exit(1);
	}

	threads=(pthread_t *)malloc(n*sizeof(*threads));
	pthread_attr_init(&pthread_custom_attr);

	p=(parm *)malloc(sizeof(parm)*n);
	/* Start up thread */

	for (i=0; i<n; i++)
	{
		p[i].id=i;
		pthread_create(&threads[i], &pthread_custom_attr, hello, (void *)(p+i));
	}

	/* Synchronize the completion of each thread. */

	for (i=0; i<n; i++)
	{
		pthread_join(threads[i],NULL);
	}
	free(p);
}

int main(int argc, char * argv[]) {
    char name[100];

	if (argc < 2)
	{
		printf ("Usage: %s <name> [type]\nwhere <name> is test name\n",argv[0]);
		printf ("test names are: thread, rwlock, rwlockTimed, cond, condTimed, condStatic, spinlock, barrier, mutex [static[N|R|E|ND]].\n");
		exit(1);
	}
	strcpy(testType, "default");
 	if (argc == 3)
	{
		strcpy(testType, argv[2]);
	}
	strcpy(name, argv[1]); 
    printf ("Threads test: %s\n",name);
    printf ("P size: %d %d\n",SIZE_MAX>UINT_MAX,sizeof(struct timespec ));
	if (strcmp(name, "thread") == 0) thread();
	else if (strcmp(name, "rwlock") == 0) rwlock_main();
	else if (strcmp(name, "rwlockTimed") == 0) rwlockTimed_main();
	//else if (strcmp(name, "cond") == 0) cond_main();
	else if (strcmp(name, "condTimed") == 0) condTimed_main();
	//else if (strcmp(name, "condStatic") == 0) condStatic_main();
	else if (strcmp(name, "barrier") == 0) barrier_main();
	else if (strcmp(name, "mutex") == 0) mutex_main();
	else if (strcmp(name, "spinlock") == 0) spinlock_main();
 	else printf ("Unknown test name '%s'\n",name);
    return 0;
}

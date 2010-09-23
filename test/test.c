#include <stdio.h>
#include <time.h>
#include "../pthreads.h"

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

void *hello(void *arg)
{
	parm *p=(parm *)arg;
	printf("Hello from node %d\n", p->id);
	return (NULL);
}

pthread_rwlock_t       rwlock;

/*================================================================================*/
#define COND_NTHREADS                3
#define COND_WAIT_TIME_SECONDS       0

int                 workToDo = 0;
pthread_cond_t      cond;
pthread_mutex_t     mutex;

void *condTimed_threadfunc(void *parm)
{
  int               rc;
  struct timespec   ts;
  struct timeval    tp;

  rc = pthread_mutex_lock(&mutex);
  checkResults("pthread_mutex_lock()\n", rc);

  /* Usually worker threads will loop on these operations */
  while (1) {
    rc =  gettimeofday(&tp, NULL);
    checkResults("gettimeofday()\n", rc);

    /* Convert from timeval to timespec */
    ts.tv_sec  = tp.tv_sec;
    ts.tv_nsec = tp.tv_usec * 1000;
    ts.tv_sec += COND_WAIT_TIME_SECONDS;

    while (!workToDo) {
      printf("Thread blocked\n");
      rc = pthread_cond_timedwait(&cond, &mutex, &ts);
      /* If the wait timed out, in this example, the work is complete, and   */
      /* the thread will end.                                                */
      /* In reality, a timeout must be accompanied by some sort of checking  */
      /* to see if the work is REALLY all complete. In the simple example    */
      /* we'll just go belly up when we time out.                            */
      printf("Thread unblocked\n");
      if (rc == ETIMEDOUT) {
        printf("Wait timed out!\n");
        rc = pthread_mutex_unlock(&mutex);
        checkResults("pthread_mutex_lock()\n", rc);
        pthread_exit(NULL);
      }
      checkResults("pthread_cond_timedwait()\n", rc);
    }

    printf("Thread consumes work here\n");
    workToDo = 0;
  }

  rc = pthread_mutex_unlock(&mutex);
  checkResults("pthread_mutex_lock()\n", rc);
  return NULL;
}

int condTimed_main(void)
{
  int                   rc=0;
  int                   i;
  pthread_t             threadid[COND_NTHREADS];

  printf("Enter Testcase - condTimed_main\n");

  rc = pthread_mutex_init (&mutex, NULL);
  checkResults("pthread_mutex_init()\n", rc);
	
  rc = pthread_cond_init (&cond, NULL);
  checkResults("pthread_cond_init()\n", rc);
	
  printf("Create %d threads\n", COND_NTHREADS);
  for(i=0; i<COND_NTHREADS; ++i) {
    rc = pthread_create(&threadid[i], NULL, condTimed_threadfunc, NULL);
    checkResults("pthread_create()\n", rc);
  }

  rc = pthread_mutex_lock(&mutex);
  checkResults("pthread_mutex_lock()\n", rc);

  printf("One work item to give to a thread\n");
  workToDo = 1;
  rc = pthread_cond_signal(&cond);
  checkResults("pthread_cond_signal()\n", rc);

  rc = pthread_mutex_unlock(&mutex);
  checkResults("pthread_mutex_unlock()\n", rc);

  printf("Wait for threads and cleanup\n");
  for (i=0; i<COND_NTHREADS; ++i) {
    rc = pthread_join(threadid[i], NULL);
    checkResults("pthread_join()\n", rc);
  }

  pthread_cond_destroy(&cond);
  pthread_mutex_destroy(&mutex);
  printf("Main completed\n");
  return 0;
}


/*================================================================================*/
void *rwlockTimed_rdlockThread(void *arg)
{
  int             rc;
  int             count=0;
  struct timespec ts;

  /* 1.5 seconds */
  ts.tv_sec = 1;
  ts.tv_nsec = 500000000;

  printf("Entered thread, getting read lock with timeout\n");
  Retry:
  rc = pthread_rwlock_timedrdlock(&rwlock, &ts);
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

  Sleep(2);

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
  Sleep(5);

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

  Sleep(5);
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
    char name[1000];

	if (argc != 2)
	{
		printf ("Usage: %s <name>\nwhere <name> is test name\n",argv[0]);
		exit(1);
	}
	strcpy(name, argv[1]);
    printf ("Threads test: %s\n",name);
	if (strcmp(name, "thread") == 0) thread();
	else if (strcmp(name, "rwlock") == 0) rwlock_main();
	else if (strcmp(name, "rwlockTimed") == 0) rwlockTimed_main();
	else if (strcmp(name, "condTimed") == 0) condTimed_main();
 	else printf ("Unknown test name '%s'\n",name);
    return 0;
}

pthread.o: pthread.c
	gcc -c pthread.c

mutex.o: mutex.c
	gcc -c mutex.c

cond.o: cond.c
	gcc -c cond.c

rwlock.o: rwlock.c
	gcc -c rwlock.c

barrier.o: barrier.c
	gcc -c barrier.c

spinlock.o: spinlock.c
	gcc -c spinlock.c

misc.o: misc.c
	gcc -c misc.c

libpthreads.a: pthread.o mutex.o cond.o rwlock.o barrier.o spinlock.o misc.o
	ar rcs libpthreads.a  misc.o pthread.o mutex.o cond.o rwlock.o barrier.o spinlock.o

lib: libpthreads.a	

clean:
	rm -rf *.o *.a



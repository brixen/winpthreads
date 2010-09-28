thread.o: thread.c
	gcc -c thread.c

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

libpthread.a: thread.o mutex.o cond.o rwlock.o barrier.o spinlock.o misc.o
	ar rcs libpthread.a  misc.o thread.o mutex.o cond.o rwlock.o barrier.o spinlock.o

lib: libpthread.a	

clean:
	rm -rf *.o *.a



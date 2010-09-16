#ifndef WIN_PTHREAD_H
#define WIN_PTHREAD_H

#define pthread_cleanup_push(F, A)\
{\
    const _pthread_cleanup _pthread_cup = {(F), (A), pthread_self()->clean};\
    _ReadWriteBarrier();\
    pthread_self()->clean = (_pthread_cleanup *) &_pthread_cup;\
    _ReadWriteBarrier()

/* Note that if async cancelling is used, then there is a race here */
#define pthread_cleanup_pop(E)\
    (pthread_self()->clean = _pthread_cup.next, (E?_pthread_cup.func((pthread_once_t *)_pthread_cup.arg):0));}

#endif

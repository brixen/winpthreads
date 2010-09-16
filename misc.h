#ifndef WIN_PTHREADS_MISC_H
#define WIN_PTHREADS_MISC_H


#define CHECK_HANDLE(h) { DWORD dwFlags; \
    if (!(h) || ((h) == INVALID_HANDLE_VALUE) || !GetHandleInformation((h), &dwFlags)) \
    return EINVAL; }

#define CHECK_PTR(p)    if (!(p)) return EINVAL;

#define CHECK_MUTEX(m)  { \
    if (!(m) || (!(m)->valid)) \
        return EINVAL; }

#define CHECK_THREAD(t)  { \
    CHECK_PTR(t); \
    CHECK_HANDLE(t->h); }

#define CHECK_OBJECT(o, e)  { DWORD dwFlags; \
    if (!(o)) return e; \
    if (!((o)->h) || (((o)->h) == INVALID_HANDLE_VALUE) || !GetHandleInformation(((o)->h), &dwFlags)) \
        return e; }

/* ms can be 64 bit, solve wrap-around issues: */
#define dwMilliSecs(ms) ((ms) >= INFINITE ? INFINITE : (DWORD)(ms))

inline void _mm_pause(void)
{
    __asm__ __volatile__("pause");
}
#define _ReadWriteBarrier   __sync_synchronize
#define YieldProcessor      _mm_pause

unsigned long long _pthread_time_in_ms(void);
unsigned long long _pthread_time_in_ms_from_timespec(const struct timespec *ts);
unsigned long long _pthread_rel_time_in_ms(const struct timespec *ts);

#endif

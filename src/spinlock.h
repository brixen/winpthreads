#ifndef WIN_PTHREADS_SPINLOCK_H
#define WIN_PTHREADS_SPINLOCK_H

#define CHECK_SPINLOCK(l)  { \
    if (!(l) || !*l \
        || ( ((spin_t *)(*l))->valid != (unsigned int)LIFE_SPINLOCK ) ) \
        return EINVAL; }

#define CHECK_SPINLOCK_LITE(l) if (!(l)) return EINVAL;

#ifdef USE_SPINLOCK_DEADLK

#define CHECK_DEADLK_SL(l)	if (l->owner == GetCurrentThreadId()) \
                                return EDEADLK
#define SET_OWNER_SL(l)		l->owner = GetCurrentThreadId()
#define SET_OWNER_SLIF(l,r)	if(!(r))SET_OWNER_SL(l)
#define UNSET_OWNER_SL(l)	l->owner = 0

#else /* NOP's */

#define CHECK_DEADLK_SL(l)
#define SET_OWNER_SL(l)	
#define SET_OWNER_SLIF(l,r)	
#define UNSET_OWNER_SL(l)

#endif

#ifdef USE_SPINLOCK_DBG
#define _spin_lite_lock_cnt(c)	c++
#define _spin_lite_lock_inc(c)	InterlockedIncrement(&c)
#define _spin_lite_lock_dec(c)	InterlockedDecrement(&c)
#define _spin_lite_lock_stat(c)	{scntMax = (c > scntMax) ? c : scntMax;	scnt += lscnt;}
#else
#define _spin_lite_lock_cnt(c)
#define _spin_lite_lock_inc(c)
#define _spin_lite_lock_stat(c)
#define _spin_lite_lock_dec(c)
#endif

#ifdef USE_SPINLOCK_EPERM
#define CHECK_PERM_SL(l)	if (l->owner != GetCurrentThreadId()) \
                                return EPERM
#else /* NOP's */
#define CHECK_PERM_SL(l)
#endif

typedef struct spin_t spin_t;
struct spin_t
{
    DWORD owner;
    unsigned int valid;   
    LONG l;   
};

#define LIFE_SPINLOCK 0xFEEDBAB1
#define DEAD_SPINLOCK 0xB00FDEAD

typedef union _vol_spinlock _vol_spinlock;
union _vol_spinlock {
    LONG *l;
    volatile LONG *lv;
};

int _spin_lite_trylock(spin_t *l);

int _spin_lite_unlock(spin_t *l);

int _spin_lite_lock(spin_t *l);

int _spin_lite_getsc(int reset);
int _spin_lite_getbsc(int reset);

int _spin_lite_getscMax(int reset);

#endif

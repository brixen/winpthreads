#ifndef WIN_SEM
#define WIN_SEM

#define LIFE_SEM 0xBAB1F00D
#define DEAD_SEM 0xDEADBEEF

typedef struct _sem_t _sem_t;
struct _sem_t
{
    unsigned int valid;
    volatile LONG busy;
    HANDLE s;
    unsigned int value;
};

#endif /* WIN_SEM */
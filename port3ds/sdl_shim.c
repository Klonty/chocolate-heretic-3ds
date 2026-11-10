//
// libctru implementation of the tiny SDL2 subset declared in
// port3ds/include/SDL.h.
//

#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "SDL.h"
#include "port3ds.h"

// --- Init / quit -----------------------------------------------------------

int SDL_Init(Uint32 flags)
{
    (void) flags;
    return 0;
}

void SDL_QuitSubSystem(Uint32 flags)
{
    (void) flags;
}

void SDL_Quit(void)
{
}

int SDL_ShowSimpleMessageBox(Uint32 flags, const char *title,
                             const char *message, void *window)
{
    (void) flags;
    (void) window;
    P3DS_FatalScreen(title, message);
    return 0;
}

// --- Timer -----------------------------------------------------------------

static u64 start_ms;

Uint32 SDL_GetTicks(void)
{
    if (start_ms == 0)
    {
        start_ms = osGetTime();
    }

    return (Uint32) (osGetTime() - start_ms);
}

void SDL_Delay(Uint32 ms)
{
    svcSleepThread((s64) ms * 1000000LL);
}

// --- Mutexes ---------------------------------------------------------------
// SDL mutexes are recursive, so use libctru's RecursiveLock.

struct SDL_mutex
{
    RecursiveLock lock;
};

SDL_mutex *SDL_CreateMutex(void)
{
    SDL_mutex *m = malloc(sizeof(*m));

    if (m != NULL)
    {
        RecursiveLock_Init(&m->lock);
    }

    return m;
}

void SDL_DestroyMutex(SDL_mutex *mutex)
{
    free(mutex);
}

int SDL_LockMutex(SDL_mutex *mutex)
{
    RecursiveLock_Lock(&mutex->lock);
    return 0;
}

int SDL_UnlockMutex(SDL_mutex *mutex)
{
    RecursiveLock_Unlock(&mutex->lock);
    return 0;
}

// --- Condition variables ---------------------------------------------------
// Callers always re-check their predicate in a loop, so a one-shot event
// (which stays signalled until consumed, so no wakeup is lost) is enough.

struct SDL_cond
{
    LightEvent event;
};

SDL_cond *SDL_CreateCond(void)
{
    SDL_cond *c = malloc(sizeof(*c));

    if (c != NULL)
    {
        LightEvent_Init(&c->event, RESET_ONESHOT);
    }

    return c;
}

void SDL_DestroyCond(SDL_cond *cond)
{
    free(cond);
}

int SDL_CondSignal(SDL_cond *cond)
{
    LightEvent_Signal(&cond->event);
    return 0;
}

int SDL_CondWait(SDL_cond *cond, SDL_mutex *mutex)
{
    SDL_UnlockMutex(mutex);
    LightEvent_Wait(&cond->event);
    SDL_LockMutex(mutex);
    return 0;
}

// --- Threads ---------------------------------------------------------------

struct SDL_Thread
{
    Thread thread;
    SDL_ThreadFunction fn;
    void *data;
    int status;
};

static void ThreadTrampoline(void *arg)
{
    SDL_Thread *t = arg;
    t->status = t->fn(t->data);
}

SDL_Thread *SDL_CreateThread(SDL_ThreadFunction fn, const char *name,
                             void *data)
{
    SDL_Thread *t;

    t = calloc(1, sizeof(*t));

    if (t == NULL)
    {
        return NULL;
    }

    t->fn = fn;
    t->data = data;
    t->thread = P3DS_CreateThread(ThreadTrampoline, t, 32 * 1024, -1,
                                  name != NULL ? name : "SDL");

    if (t->thread == NULL)
    {
        free(t);
        return NULL;
    }

    return t;
}

void SDL_WaitThread(SDL_Thread *thread, int *status)
{
    if (thread == NULL)
    {
        return;
    }

    threadJoin(thread->thread, U64_MAX);
    threadFree(thread->thread);

    if (status != NULL)
    {
        *status = thread->status;
    }

    free(thread);
}

// --- Filesystem ------------------------------------------------------------

char *SDL_GetPrefPath(const char *org, const char *app)
{
    (void) org;
    (void) app;

    mkdir("sdmc:/3ds", 0777);
    mkdir(P3DS_DATA_DIR, 0777);

    return strdup(P3DS_DATA_DIR "/");
}

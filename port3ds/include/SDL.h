//
// Minimal SDL2 compatibility shim for the Nintendo 3DS port.
//
// Chocolate Doom's platform-independent code only touches a small slice
// of SDL (timers, mutexes, byte swapping, a few constants).  The real
// platform layer (video, input, audio) is replaced by native libctru code
// in port3ds/, so this header just provides enough to compile the rest.
//

#ifndef SDL_SHIM_3DS_H
#define SDL_SHIM_3DS_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef uint8_t  Uint8;
typedef int8_t   Sint8;
typedef uint16_t Uint16;
typedef int16_t  Sint16;
typedef uint32_t Uint32;
typedef int32_t  Sint32;
typedef uint64_t Uint64;
typedef int64_t  Sint64;

// --- Init / quit -----------------------------------------------------------

#define SDL_INIT_TIMER  0x0001u
#define SDL_INIT_AUDIO  0x0010u
#define SDL_INIT_VIDEO  0x0020u

int  SDL_Init(Uint32 flags);
void SDL_QuitSubSystem(Uint32 flags);
void SDL_Quit(void);

#define SDL_HINT_NO_SIGNAL_HANDLERS             "SDL_NO_SIGNAL_HANDLERS"
#define SDL_HINT_WINDOWS_DISABLE_THREAD_NAMING  "SDL_WINDOWS_DISABLE_THREAD_NAMING"
#define SDL_SetHint(name, value) ((void) (name), (void) (value), 1)

#define SDL_MESSAGEBOX_ERROR 0x10
int SDL_ShowSimpleMessageBox(Uint32 flags, const char *title,
                             const char *message, void *window);

// --- Timer -----------------------------------------------------------------

Uint32 SDL_GetTicks(void);
void   SDL_Delay(Uint32 ms);

// --- Threads and synchronisation ------------------------------------------

typedef struct SDL_mutex SDL_mutex;
typedef struct SDL_cond SDL_cond;
typedef struct SDL_Thread SDL_Thread;
typedef int (*SDL_ThreadFunction)(void *data);

SDL_mutex *SDL_CreateMutex(void);
void SDL_DestroyMutex(SDL_mutex *mutex);
int  SDL_LockMutex(SDL_mutex *mutex);
int  SDL_UnlockMutex(SDL_mutex *mutex);

SDL_cond *SDL_CreateCond(void);
void SDL_DestroyCond(SDL_cond *cond);
int  SDL_CondSignal(SDL_cond *cond);
int  SDL_CondWait(SDL_cond *cond, SDL_mutex *mutex);

SDL_Thread *SDL_CreateThread(SDL_ThreadFunction fn, const char *name,
                             void *data);
void SDL_WaitThread(SDL_Thread *thread, int *status);

// --- Byte order (the 3DS ARM11 is little-endian) ---------------------------

#define SDL_SwapLE16(x) ((Uint16) (x))
#define SDL_SwapLE32(x) ((Uint32) (x))
#define SDL_SwapBE16(x) ((Uint16) __builtin_bswap16((Uint16) (x)))
#define SDL_SwapBE32(x) ((Uint32) __builtin_bswap32((Uint32) (x)))

// --- stdinc / filesystem ---------------------------------------------------

#define SDL_qsort qsort
#define SDL_free  free

// Returns a malloc()ed path ending in '/', like the real SDL.
char *SDL_GetPrefPath(const char *org, const char *app);

// --- Types referenced by headers only -------------------------------------

typedef union SDL_Event { Uint32 type; Uint8 padding[56]; } SDL_Event;

typedef enum
{
    SDL_CONTROLLER_BUTTON_INVALID = -1,
    SDL_CONTROLLER_BUTTON_A,
    SDL_CONTROLLER_BUTTON_B,
    SDL_CONTROLLER_BUTTON_X,
    SDL_CONTROLLER_BUTTON_Y,
    SDL_CONTROLLER_BUTTON_BACK,
    SDL_CONTROLLER_BUTTON_GUIDE,
    SDL_CONTROLLER_BUTTON_START,
    SDL_CONTROLLER_BUTTON_LEFTSTICK,
    SDL_CONTROLLER_BUTTON_RIGHTSTICK,
    SDL_CONTROLLER_BUTTON_LEFTSHOULDER,
    SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,
    SDL_CONTROLLER_BUTTON_DPAD_UP,
    SDL_CONTROLLER_BUTTON_DPAD_DOWN,
    SDL_CONTROLLER_BUTTON_DPAD_LEFT,
    SDL_CONTROLLER_BUTTON_DPAD_RIGHT,
    SDL_CONTROLLER_BUTTON_MISC1,
    SDL_CONTROLLER_BUTTON_PADDLE1,
    SDL_CONTROLLER_BUTTON_PADDLE2,
    SDL_CONTROLLER_BUTTON_PADDLE3,
    SDL_CONTROLLER_BUTTON_PADDLE4,
    SDL_CONTROLLER_BUTTON_TOUCHPAD,
    SDL_CONTROLLER_BUTTON_MAX
} SDL_GameControllerButton;

#endif

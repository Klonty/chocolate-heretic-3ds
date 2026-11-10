//
// OPL (AdLib) music driver for the Nintendo 3DS: replaces opl/opl_sdl.c.
//
// Uses the DOSBox OPL emulator (DBOPL, taken from Chocolate Doom 2.2.1)
// rather than Nuked OPL3: Nuked is cycle-accurate and far too heavy for
// an ARM11, while DBOPL is several times cheaper at a given sample rate.
//
// Audio is generated in a dedicated thread, which streams it to NDSP
// channel 0 through a small ring of wave buffers.  Everything else
// (timers, callback queue, register writes) is the same scheme as the SDL
// driver.
//

#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "SDL.h"

#include "dbopl.h"
#include "opl.h"
#include "opl_internal.h"
#include "opl_queue.h"

#include "port3ds.h"

#define MUSIC_CHANNEL   0
#define NUM_BUFFERS     4
#define BUFFER_FRAMES   512     // stereo frames per buffer (~23 ms at 22 kHz)

typedef struct
{
    unsigned int rate;        // Number of times the timer is advanced per sec.
    unsigned int enabled;     // Non-zero if timer is enabled.
    unsigned int value;       // Last value that was set.
    uint64_t expire_time;     // Calculated time that timer will expire.
} opl_timer_t;

// When callback_mutex is locked using OPL_Lock, callbacks are not invoked.
static SDL_mutex *callback_mutex = NULL;

// Queue of callbacks waiting to be invoked, and its mutex.
static opl_callback_queue_t *callback_queue;
static SDL_mutex *callback_queue_mutex = NULL;

// Current time, in us since startup:
static uint64_t current_time;

// If non-zero, playback is currently paused.
static int opl_paused;

// Time offset (in us) due to the fact that callbacks were paused.
static uint64_t pause_offset;

// OPL software emulator structure.
static Chip opl_chip;
static int opl_opl3mode;

// Temporary mixing buffer used when generating samples (32-bit output
// from DBOPL, converted to 16-bit for NDSP).
static int32_t *mix_buffer = NULL;

// Register number that was written.
static int register_num = 0;

// Timers; the emulator does not do timer stuff itself.
static opl_timer_t timer1 = { 12500, 0, 0, 0 };
static opl_timer_t timer2 = { 3125, 0, 0, 0 };

// Streaming state.
static int mixing_freq;
static s16 *stream_mem;                     // linearAlloc()ed ring
static ndspWaveBuf stream_bufs[NUM_BUFFERS];
static Thread mix_thread;
static LightEvent mix_event;
static volatile int mix_thread_quit;
static int initialized;
static unsigned int underruns;

// --- Emulation --------------------------------------------------------------

// Advance time by the specified number of samples, invoking any
// callback functions as appropriate.
static void AdvanceTime(unsigned int nsamples)
{
    opl_callback_t callback;
    void *callback_data;
    uint64_t us;

    SDL_LockMutex(callback_queue_mutex);

    us = ((uint64_t) nsamples * OPL_SECOND) / mixing_freq;
    current_time += us;

    if (opl_paused)
    {
        pause_offset += us;
    }

    while (!OPL_Queue_IsEmpty(callback_queue)
        && current_time >= OPL_Queue_Peek(callback_queue) + pause_offset)
    {
        if (!OPL_Queue_Pop(callback_queue, &callback, &callback_data))
        {
            break;
        }

        // Hold callback_mutex while invoking (so OPL_Lock can block
        // callbacks), but not callback_queue_mutex, since the callback
        // may call OPL_SetCallback.
        SDL_UnlockMutex(callback_queue_mutex);

        SDL_LockMutex(callback_mutex);
        callback(callback_data);
        SDL_UnlockMutex(callback_mutex);

        SDL_LockMutex(callback_queue_mutex);
    }

    SDL_UnlockMutex(callback_queue_mutex);
}

// Generate nsamples stereo frames of OPL output into buffer.
static void GenerateSamples(int16_t *buffer, unsigned int nsamples)
{
    unsigned int i;

    // mix_buffer is one second long, far more than any slice.
    assert(nsamples < (unsigned int) mixing_freq);

    if (opl_opl3mode)
    {
        Chip__GenerateBlock3(&opl_chip, nsamples, mix_buffer);

        for (i = 0; i < nsamples; ++i)
        {
            buffer[i * 2] = (int16_t) mix_buffer[i * 2];
            buffer[i * 2 + 1] = (int16_t) mix_buffer[i * 2 + 1];
        }
    }
    else
    {
        Chip__GenerateBlock2(&opl_chip, nsamples, mix_buffer);

        for (i = 0; i < nsamples; ++i)
        {
            buffer[i * 2] = (int16_t) mix_buffer[i];
            buffer[i * 2 + 1] = (int16_t) mix_buffer[i];
        }
    }
}

// Fill one buffer of stereo frames, invoking callbacks at the right
// points in time.
static void FillStream(s16 *buffer, unsigned int buffer_samples)
{
    unsigned int filled = 0;

    while (filled < buffer_samples)
    {
        uint64_t next_callback_time;
        uint64_t nsamples;

        SDL_LockMutex(callback_queue_mutex);

        if (opl_paused || OPL_Queue_IsEmpty(callback_queue))
        {
            nsamples = buffer_samples - filled;
        }
        else
        {
            next_callback_time = OPL_Queue_Peek(callback_queue) + pause_offset;

            nsamples = (next_callback_time - current_time) * mixing_freq;
            nsamples = (nsamples + OPL_SECOND - 1) / OPL_SECOND;

            if (nsamples > buffer_samples - filled)
            {
                nsamples = buffer_samples - filled;
            }
        }

        SDL_UnlockMutex(callback_queue_mutex);

        GenerateSamples(buffer + filled * 2, nsamples);
        filled += nsamples;

        AdvanceTime(nsamples);
    }
}

static void NdspFrameCallback(void *data)
{
    (void) data;
    LightEvent_Signal(&mix_event);
}

static void MixThread(void *arg)
{
    int first_pass = 1;

    (void) arg;

    while (!mix_thread_quit)
    {
        int i, pending = 0;

        for (i = 0; i < NUM_BUFFERS; ++i)
        {
            ndspWaveBuf *buf = &stream_bufs[i];

            if (buf->status == NDSP_WBUF_DONE || buf->status == NDSP_WBUF_FREE)
            {
                ++pending;
            }
        }

        // Every buffer consumed before we got back: the DSP ran dry.
        if (!first_pass && pending == NUM_BUFFERS)
        {
            ++underruns;
        }

        first_pass = 0;

        for (i = 0; i < NUM_BUFFERS; ++i)
        {
            ndspWaveBuf *buf = &stream_bufs[i];

            if (buf->status == NDSP_WBUF_DONE || buf->status == NDSP_WBUF_FREE)
            {
                s16 *samples = (s16 *) buf->data_vaddr;

                FillStream(samples, BUFFER_FRAMES);
                DSP_FlushDataCache(samples, BUFFER_FRAMES * 2 * sizeof(s16));
                ndspChnWaveBufAdd(MUSIC_CHANNEL, buf);
            }
        }

        LightEvent_Wait(&mix_event);
    }
}

// --- Driver -----------------------------------------------------------------

static void OPL_3DS_Shutdown(void)
{
    if (initialized)
    {
        mix_thread_quit = 1;
        LightEvent_Signal(&mix_event);
        threadJoin(mix_thread, U64_MAX);
        threadFree(mix_thread);
        mix_thread = NULL;

        ndspSetCallback(NULL, NULL);
        ndspChnWaveBufClear(MUSIC_CHANNEL);

        printf("OPL_3DS: %u music underruns.\n", underruns);

        OPL_Queue_Destroy(callback_queue);
        linearFree(stream_mem);
        stream_mem = NULL;
        free(mix_buffer);
        mix_buffer = NULL;

        P3DS_AudioRelease();
        initialized = 0;
    }

    if (callback_mutex != NULL)
    {
        SDL_DestroyMutex(callback_mutex);
        callback_mutex = NULL;
    }

    if (callback_queue_mutex != NULL)
    {
        SDL_DestroyMutex(callback_queue_mutex);
        callback_queue_mutex = NULL;
    }
}

static int OPL_3DS_Init(unsigned int port_base)
{
    float mix[12];
    int i;

    (void) port_base;

    if (!P3DS_AudioInit())
    {
        fprintf(stderr, "OPL_3DS_Init: unable to set up sound.\n");
        return 0;
    }

    opl_paused = 0;
    pause_offset = 0;
    callback_queue = OPL_Queue_Create();
    current_time = 0;
    mixing_freq = opl_sample_rate;
    underruns = 0;

    // Mix buffer: one second of stereo 32-bit samples.
    mix_buffer = malloc(mixing_freq * sizeof(int32_t) * 2);

    DBOPL_InitTables();
    Chip__Chip(&opl_chip);
    Chip__Setup(&opl_chip, mixing_freq);
    opl_opl3mode = 0;

    callback_mutex = SDL_CreateMutex();
    callback_queue_mutex = SDL_CreateMutex();

    // Stream buffers.
    stream_mem = linearAlloc(NUM_BUFFERS * BUFFER_FRAMES * 2 * sizeof(s16));

    if (stream_mem == NULL || mix_buffer == NULL)
    {
        OPL_3DS_Shutdown();
        free(mix_buffer);
        mix_buffer = NULL;
        P3DS_AudioRelease();
        return 0;
    }

    memset(stream_mem, 0, NUM_BUFFERS * BUFFER_FRAMES * 2 * sizeof(s16));
    memset(stream_bufs, 0, sizeof(stream_bufs));

    for (i = 0; i < NUM_BUFFERS; ++i)
    {
        stream_bufs[i].data_vaddr = stream_mem + i * BUFFER_FRAMES * 2;
        stream_bufs[i].nsamples = BUFFER_FRAMES;
        stream_bufs[i].status = NDSP_WBUF_FREE;
    }

    ndspChnReset(MUSIC_CHANNEL);
    ndspChnSetInterp(MUSIC_CHANNEL, NDSP_INTERP_LINEAR);
    ndspChnSetRate(MUSIC_CHANNEL, (float) mixing_freq);
    ndspChnSetFormat(MUSIC_CHANNEL, NDSP_FORMAT_STEREO_PCM16);

    memset(mix, 0, sizeof(mix));
    mix[0] = 1.0f;
    mix[1] = 1.0f;
    ndspChnSetMix(MUSIC_CHANNEL, mix);

    LightEvent_Init(&mix_event, RESET_ONESHOT);
    mix_thread_quit = 0;
    ndspSetCallback(NdspFrameCallback, NULL);

    mix_thread = P3DS_CreateThread(MixThread, NULL, 64 * 1024, -2, "OPL mixer");

    if (mix_thread == NULL)
    {
        fprintf(stderr, "OPL_3DS_Init: unable to create the mixing thread.\n");
        ndspSetCallback(NULL, NULL);
        linearFree(stream_mem);
        stream_mem = NULL;
        free(mix_buffer);
        mix_buffer = NULL;
        OPL_Queue_Destroy(callback_queue);
        P3DS_AudioRelease();
        return 0;
    }

    printf("OPL_3DS_Init: DBOPL at %i Hz.\n", mixing_freq);

    initialized = 1;
    return 1;
}

static unsigned int OPL_3DS_PortRead(opl_port_t port)
{
    unsigned int result = 0;

    if (port == OPL_REGISTER_PORT_OPL3)
    {
        return 0xff;
    }

    if (timer1.enabled && current_time > timer1.expire_time)
    {
        result |= 0x80;   // Either have expired
        result |= 0x40;   // Timer 1 has expired
    }

    if (timer2.enabled && current_time > timer2.expire_time)
    {
        result |= 0x80;   // Either have expired
        result |= 0x20;   // Timer 2 has expired
    }

    return result;
}

static void OPLTimer_CalculateEndTime(opl_timer_t *timer)
{
    int tics;

    if (timer->enabled)
    {
        tics = 0x100 - timer->value;
        timer->expire_time = current_time
                           + ((uint64_t) tics * OPL_SECOND) / timer->rate;
    }
}

static void WriteRegister(unsigned int reg_num, unsigned int value)
{
    switch (reg_num)
    {
        case OPL_REG_TIMER1:
            timer1.value = value;
            OPLTimer_CalculateEndTime(&timer1);
            break;

        case OPL_REG_TIMER2:
            timer2.value = value;
            OPLTimer_CalculateEndTime(&timer2);
            break;

        case OPL_REG_TIMER_CTRL:
            if (value & 0x80)
            {
                timer1.enabled = 0;
                timer2.enabled = 0;
            }
            else
            {
                if ((value & 0x40) == 0)
                {
                    timer1.enabled = (value & 0x01) != 0;
                    OPLTimer_CalculateEndTime(&timer1);
                }

                if ((value & 0x20) == 0)
                {
                    timer2.enabled = (value & 0x02) != 0;
                    OPLTimer_CalculateEndTime(&timer2);
                }
            }
            break;

        case OPL_REG_NEW:
            opl_opl3mode = value & 0x01;
            // fall through

        default:
            Chip__WriteReg(&opl_chip, reg_num, value);
            break;
    }
}

static void OPL_3DS_PortWrite(opl_port_t port, unsigned int value)
{
    if (port == OPL_REGISTER_PORT)
    {
        register_num = value;
    }
    else if (port == OPL_REGISTER_PORT_OPL3)
    {
        register_num = value | 0x100;
    }
    else if (port == OPL_DATA_PORT)
    {
        WriteRegister(register_num, value);
    }
}

static void OPL_3DS_SetCallback(uint64_t us, opl_callback_t callback,
                                void *data)
{
    SDL_LockMutex(callback_queue_mutex);
    OPL_Queue_Push(callback_queue, callback, data,
                   current_time - pause_offset + us);
    SDL_UnlockMutex(callback_queue_mutex);
}

static void OPL_3DS_ClearCallbacks(void)
{
    SDL_LockMutex(callback_queue_mutex);
    OPL_Queue_Clear(callback_queue);
    SDL_UnlockMutex(callback_queue_mutex);
}

static void OPL_3DS_Lock(void)
{
    SDL_LockMutex(callback_mutex);
}

static void OPL_3DS_Unlock(void)
{
    SDL_UnlockMutex(callback_mutex);
}

static void OPL_3DS_SetPaused(int paused)
{
    opl_paused = paused;
}

static void OPL_3DS_AdjustCallbacks(float factor)
{
    SDL_LockMutex(callback_queue_mutex);
    OPL_Queue_AdjustCallbacks(callback_queue, current_time, factor);
    SDL_UnlockMutex(callback_queue_mutex);
}

opl_driver_t opl_3ds_driver =
{
    "3DS",
    OPL_3DS_Init,
    OPL_3DS_Shutdown,
    OPL_3DS_PortRead,
    OPL_3DS_PortWrite,
    OPL_3DS_SetCallback,
    OPL_3DS_ClearCallbacks,
    OPL_3DS_Lock,
    OPL_3DS_Unlock,
    OPL_3DS_SetPaused,
    OPL_3DS_AdjustCallbacks,
};

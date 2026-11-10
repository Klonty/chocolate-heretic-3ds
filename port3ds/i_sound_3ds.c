//
// Nintendo 3DS sound effects through NDSP: replaces src/i_sdlsound.c and
// src/i_pcsound.c.
//
// Each game sound channel maps to one NDSP hardware channel, so there is
// no software mixing at all.  DMX sound lumps are 8-bit unsigned mono;
// NDSP wants signed 8-bit in linear memory, so sounds are converted once
// when cached.  NDSP channel 0 is reserved for music (see opl_3ds.c).
//

#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "doomtype.h"
#include "deh_str.h"
#include "i_sound.h"
#include "i_system.h"
#include "m_misc.h"
#include "w_wad.h"
#include "z_zone.h"

#include "port3ds.h"

boolean P3DS_AudioInit(void);
void P3DS_AudioRelease(void);

#define NUM_CHANNELS    16
#define FIRST_CHANNEL   1       // NDSP channel 0 is music

typedef struct
{
    s8 *data;               // linearAlloc()ed signed samples
    u32 len;
    int rate;
} sample_t;

static boolean sound_initialized = false;
static boolean use_sfx_prefix;
static ndspWaveBuf wavebufs[NUM_CHANNELS];
static sfxinfo_t *channel_sfx[NUM_CHANNELS];

// --- Loading ---------------------------------------------------------------

static void GetSfxLumpName(sfxinfo_t *sfx, char *buf, size_t buf_len)
{
    // Linked sfx lumps? Get the lump number for the sound linked to.
    if (sfx->link != NULL)
    {
        sfx = sfx->link;
    }

    if (use_sfx_prefix)
    {
        M_snprintf(buf, buf_len, "ds%s", DEH_String(sfx->name));
    }
    else
    {
        M_StringCopy(buf, DEH_String(sfx->name), buf_len);
    }
}

static boolean CacheSFX(sfxinfo_t *sfxinfo)
{
    byte *data;
    unsigned int lumplen, length, samplerate, i;
    sample_t *sample;

    if (sfxinfo->driver_data != NULL)
    {
        return true;
    }

    lumplen = W_LumpLength(sfxinfo->lumpnum);
    data = W_CacheLumpNum(sfxinfo->lumpnum, PU_STATIC);

    // DMX header: format (2 bytes), sample rate (2), length (4).
    if (lumplen < 8 || data[0] != 0x03 || data[1] != 0x00)
    {
        W_ReleaseLumpNum(sfxinfo->lumpnum);
        return false;
    }

    samplerate = (data[3] << 8) | data[2];
    length = (data[7] << 24) | (data[6] << 16) | (data[5] << 8) | data[4];

    if (length > lumplen - 8 || length <= 48)
    {
        W_ReleaseLumpNum(sfxinfo->lumpnum);
        return false;
    }

    // DMX skips the first and last 16 bytes of the sample data.
    data += 16;
    length -= 32;

    sample = malloc(sizeof(*sample));
    sample->data = linearAlloc((length + 3) & ~3u);

    if (sample == NULL || sample->data == NULL)
    {
        free(sample);
        W_ReleaseLumpNum(sfxinfo->lumpnum);
        return false;
    }

    for (i = 0; i < length; ++i)
    {
        sample->data[i] = (s8) (data[8 + i] ^ 0x80);
    }

    sample->len = length;
    sample->rate = samplerate;
    DSP_FlushDataCache(sample->data, length);

    sfxinfo->driver_data = sample;
    W_ReleaseLumpNum(sfxinfo->lumpnum);

    return true;
}

// --- sound_module_t --------------------------------------------------------

static void SetMix(int channel, int vol, int sep)
{
    float mix[12];
    float left = (float) ((254 - sep) * vol) / (127.0f * 255.0f);
    float right = (float) (sep * vol) / (127.0f * 255.0f);

    if (left > 1.0f) left = 1.0f;
    if (right > 1.0f) right = 1.0f;

    memset(mix, 0, sizeof(mix));
    mix[0] = left;
    mix[1] = right;
    ndspChnSetMix(FIRST_CHANNEL + channel, mix);
}

static boolean I_3DS_InitSound(GameMission_t mission)
{
    int i;

    use_sfx_prefix = (mission == doom || mission == strife);

    if (!P3DS_AudioInit())
    {
        return false;
    }

    for (i = 0; i < NUM_CHANNELS; ++i)
    {
        ndspChnReset(FIRST_CHANNEL + i);
        ndspChnSetInterp(FIRST_CHANNEL + i, NDSP_INTERP_LINEAR);
        ndspChnSetFormat(FIRST_CHANNEL + i, NDSP_FORMAT_MONO_PCM8);
    }

    sound_initialized = true;
    return true;
}

static void I_3DS_ShutdownSound(void)
{
    int i;

    if (!sound_initialized)
    {
        return;
    }

    for (i = 0; i < NUM_CHANNELS; ++i)
    {
        ndspChnWaveBufClear(FIRST_CHANNEL + i);
    }

    P3DS_AudioRelease();
    sound_initialized = false;
}

static int I_3DS_GetSfxLumpNum(sfxinfo_t *sfx)
{
    char namebuf[9];

    GetSfxLumpName(sfx, namebuf, sizeof(namebuf));

    return W_GetNumForName(namebuf);
}

static void I_3DS_UpdateSound(void)
{
}

static void I_3DS_UpdateSoundParams(int channel, int vol, int sep)
{
    if (!sound_initialized || channel < 0 || channel >= NUM_CHANNELS)
    {
        return;
    }

    SetMix(channel, vol, sep);
}

static int I_3DS_StartSound(sfxinfo_t *sfxinfo, int channel, int vol,
                            int sep, int pitch)
{
    sample_t *sample;
    ndspWaveBuf *buf;
    float rate;

    if (!sound_initialized || channel < 0 || channel >= NUM_CHANNELS)
    {
        return -1;
    }

    if (!CacheSFX(sfxinfo))
    {
        return -1;
    }

    sample = sfxinfo->driver_data;

    ndspChnWaveBufClear(FIRST_CHANNEL + channel);

    rate = (float) sample->rate;

    if (snd_pitchshift && pitch != NORM_PITCH)
    {
        rate = rate * pitch / NORM_PITCH;
    }

    ndspChnSetRate(FIRST_CHANNEL + channel, rate);
    SetMix(channel, vol, sep);

    buf = &wavebufs[channel];
    memset(buf, 0, sizeof(*buf));
    buf->data_vaddr = sample->data;
    buf->nsamples = sample->len;
    buf->looping = false;
    ndspChnWaveBufAdd(FIRST_CHANNEL + channel, buf);

    channel_sfx[channel] = sfxinfo;

    return channel;
}

static void I_3DS_StopSound(int channel)
{
    if (!sound_initialized || channel < 0 || channel >= NUM_CHANNELS)
    {
        return;
    }

    ndspChnWaveBufClear(FIRST_CHANNEL + channel);
    channel_sfx[channel] = NULL;
}

static boolean I_3DS_SoundIsPlaying(int channel)
{
    if (!sound_initialized || channel < 0 || channel >= NUM_CHANNELS)
    {
        return false;
    }

    return ndspChnIsPlaying(FIRST_CHANNEL + channel);
}

static void I_3DS_PrecacheSounds(sfxinfo_t *sounds, int num_sounds)
{
    int i;

    printf("I_3DS_PrecacheSounds: Precaching %i sounds...", num_sounds);

    for (i = 0; i < num_sounds; ++i)
    {
        if (sounds[i].lumpnum != -1 && sounds[i].link == NULL)
        {
            CacheSFX(&sounds[i]);
        }
    }

    printf(" done.\n");
}

static const snddevice_t sound_3ds_devices[] =
{
    SNDDEVICE_SB,
    SNDDEVICE_PAS,
    SNDDEVICE_GUS,
    SNDDEVICE_WAVEBLASTER,
    SNDDEVICE_SOUNDCANVAS,
    SNDDEVICE_AWE32,
};

const sound_module_t sound_3ds_module =
{
    sound_3ds_devices,
    arrlen(sound_3ds_devices),
    I_3DS_InitSound,
    I_3DS_ShutdownSound,
    I_3DS_GetSfxLumpNum,
    I_3DS_UpdateSound,
    I_3DS_UpdateSoundParams,
    I_3DS_StartSound,
    I_3DS_StopSound,
    I_3DS_SoundIsPlaying,
    I_3DS_PrecacheSounds,
};

// PC speaker emulation is not built on the 3DS; keep the symbol so
// i_sound.c links, but never initialise successfully.

static boolean I_PCS_InitSound(GameMission_t mission)
{
    (void) mission;
    return false;
}

static const snddevice_t pcsound_devices[] = { SNDDEVICE_PCSPEAKER };

const sound_module_t sound_pcsound_module =
{
    pcsound_devices,
    arrlen(pcsound_devices),
    I_PCS_InitSound,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
};

// Variables and hooks defined by i_sdlsound.c / i_sdlmusic.c upstream.
// They are bound to config entries, so they must exist even though
// neither libsamplerate nor Timidity is used on the 3DS.

int use_libsamplerate = 0;
float libsamplerate_scale = 0.65f;
char *timidity_cfg_path = "";

void I_InitTimidityConfig(void)
{
}

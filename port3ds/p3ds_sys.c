//
// Nintendo 3DS system glue: startup, shutdown, logging, threads, the
// software keyboard and fatal errors.
//

#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/iosupport.h>

#include "port3ds.h"

static boolean started;
static boolean exiting_with_error;
static int audio_refcount;
static s32 main_prio = 0x30;
boolean p3ds_have_cstick;

// --- Logging ----------------------------------------------------------------
// stdout/stderr go through our own device, which forwards to the console
// device set up by consoleInit (while the console is shown) and appends to
// the log file on the SD card.

static const devoptab_t *console_dotab;
static boolean console_on;
static FILE *log_file;

static ssize_t LogWrite(struct _reent *r, void *fd, const char *ptr, size_t len)
{
    if (console_on && console_dotab != NULL && console_dotab->write_r != NULL)
    {
        console_dotab->write_r(r, fd, ptr, len);
    }

    if (log_file != NULL)
    {
        fwrite(ptr, 1, len, log_file);
        fflush(log_file);
    }

    return (ssize_t) len;
}

static devoptab_t log_dotab =
{
    .name = "log",
    .structSize = 0,
    .write_r = LogWrite,
};

// Call after consoleInit(): remember its device and put ours in front.
static void InstallLogDevice(void)
{
    console_dotab = devoptab_list[STD_OUT];
    console_on = true;
    devoptab_list[STD_OUT] = &log_dotab;
    devoptab_list[STD_ERR] = &log_dotab;
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
}

void P3DS_ConsoleOff(void)
{
    console_on = false;
}

// --- Startup / shutdown -----------------------------------------------------

static void WaitForStart(void)
{
    printf("\nPress START to exit.\n");

    while (aptMainLoop())
    {
        hidScanInput();

        if (hidKeysDown() & KEY_START)
        {
            break;
        }

        gspWaitForVBlank();
    }
}

// Called through atexit().  exit() is how I_Quit and I_Error end the
// game, so this is where everything gets torn down.
static void P3DS_Shutdown(void)
{
    if (!started)
    {
        return;
    }

    if (exiting_with_error)
    {
        WaitForStart();
    }

    if (log_file != NULL)
    {
        fclose(log_file);
        log_file = NULL;
    }

    if (audio_refcount > 0)
    {
        ndspExit();
        audio_refcount = 0;
    }

    if (p3ds_have_cstick)
    {
        irrstExit();
    }

    gfxExit();
    started = false;
}

void P3DS_Init(void)
{
    bool new3ds = false;

    // New 3DS: 804 MHz and the bigger L2 cache.
    osSetSpeedupEnable(true);

    gfxInitDefault();
    gfxSetScreenFormat(GFX_TOP, GSP_RGB565_OES);
    gfxSet3D(true);
    consoleInit(GFX_BOTTOM, NULL);

    mkdir("sdmc:/3ds", 0777);
    mkdir(P3DS_DATA_DIR, 0777);
    chdir(P3DS_DATA_DIR);

    // Music conversion goes through a temporary .mid file (M_TempFile), and
    // there is no /tmp on the 3DS; keep it on the SD card with the config.
    setenv("TMPDIR", P3DS_DATA_DIR, 1);

    log_file = fopen(P3DS_LOG_FILE, "w");
    InstallLogDevice();

    // Lets us put the audio thread on the system core.
    APT_SetAppCpuTimeLimit(30);

    // The New 3DS C-Stick is read through the ir:rst service.
    p3ds_have_cstick = R_SUCCEEDED(irrstInit());

    svcGetThreadPriority(&main_prio, CUR_THREAD_HANDLE);
    APT_CheckNew3DS(&new3ds);

    started = true;
    atexit(P3DS_Shutdown);

    printf("Heretic for Nintendo 3DS\n");
    printf("Data directory: %s\n", P3DS_DATA_DIR);
    printf("New 3DS: %s, C-Stick: %s\n\n", new3ds ? "yes" : "no",
           p3ds_have_cstick ? "yes" : "no");
}

void P3DS_FatalScreen(const char *title, const char *message)
{
    // The game may have taken the bottom screen; bring the console back.
    consoleInit(GFX_BOTTOM, NULL);
    InstallLogDevice();

    printf("\n*** %s ***\n%s\n", title, message);
    exiting_with_error = true;
}

// --- Audio ------------------------------------------------------------------

boolean P3DS_AudioInit(void)
{
    if (audio_refcount == 0)
    {
        if (R_FAILED(ndspInit()))
        {
            printf("ndspInit failed: is sdmc:/3ds/dspfirm.cdc present?\n");
            return false;
        }

        ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    }

    ++audio_refcount;
    return true;
}

void P3DS_AudioRelease(void)
{
    if (audio_refcount > 0 && --audio_refcount == 0)
    {
        ndspExit();
    }
}

// --- Threads ----------------------------------------------------------------

struct Thread_tag *P3DS_CreateThread(void (*entry)(void *), void *arg,
                                     unsigned int stack_size, int prio_delta,
                                     const char *name)
{
    // Core 2 is the New 3DS's spare core, core 1 the system core (usable
    // thanks to APT_SetAppCpuTimeLimit), -1 means wherever.
    static const int cores[] = { 2, 1, -1 };
    Thread t = NULL;
    int prio = main_prio + prio_delta;
    unsigned int i;

    if (prio < 0x18)
    {
        prio = 0x18;
    }
    else if (prio > 0x3f)
    {
        prio = 0x3f;
    }

    for (i = 0; i < sizeof(cores) / sizeof(*cores); ++i)
    {
        t = threadCreate(entry, arg, stack_size, prio, cores[i], false);

        if (t != NULL)
        {
            printf("%s thread: core %i, priority 0x%02x.\n",
                   name, cores[i], prio);
            break;
        }
    }

    return t;
}

// --- Software keyboard ------------------------------------------------------

boolean P3DS_InputText(char *buf, size_t bufsize, const char *initial,
                       const char *hint)
{
    SwkbdState kb;
    SwkbdButton button;
    char tmp[256];

    if (bufsize == 0)
    {
        return false;
    }

    if (bufsize > sizeof(tmp))
    {
        bufsize = sizeof(tmp);
    }

    swkbdInit(&kb, SWKBD_TYPE_QWERTY, 2, (int) bufsize - 1);
    swkbdSetValidation(&kb, SWKBD_ANYTHING, 0, 0);

    if (hint != NULL)
    {
        swkbdSetHintText(&kb, hint);
    }

    if (initial != NULL)
    {
        swkbdSetInitialText(&kb, initial);
    }

    memset(tmp, 0, sizeof(tmp));
    button = swkbdInputText(&kb, tmp, bufsize);

    if (button != SWKBD_BUTTON_CONFIRM)
    {
        return false;
    }

    memcpy(buf, tmp, bufsize - 1);
    buf[bufsize - 1] = 0;

    return true;
}

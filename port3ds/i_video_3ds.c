//
// Nintendo 3DS video layer: replaces src/i_video.c.
//
// The game draws a 320x200 8-bit paletted frame into I_VideoBuffer.  We
// convert it through the palette to RGB565 and write it straight into the
// top screen's framebuffer, either aspect-corrected to 320x240 (default)
// or 1:1 with letterboxing.  The bottom screen hosts a text console for
// startup messages.
//
// With the 3D slider up, the left and right framebuffers get different
// renders of the 3D view (see port3ds.h for how the frame is assembled).
//

#include <3ds.h>
#include <string.h>
#include <limits.h>

#include "config.h"
#include "doomtype.h"
#include "doomdef.h"
#include "d_event.h"
#include "deh_str.h"
#include "i_input.h"
#include "i_joystick.h"
#include "i_system.h"
#include "i_timer.h"
#include "i_video.h"
#include "m_config.h"
#include "m_argv.h"
#include "tables.h"
#include "v_video.h"
#include "w_wad.h"
#include "z_zone.h"

#include "port3ds.h"

void P3DS_PollInput(void);

// Top screen geometry (landscape, per eye).
#define TOP_W 400
#define TOP_H 240

// --- Variables the rest of the engine expects -----------------------------

pixel_t *I_VideoBuffer = NULL;

char *video_driver = "";
char *window_position = "center";
int usemouse = 0;
int png_screenshots = 0;
int fullscreen = true;
int aspect_ratio_correct = true;
int integer_scaling = false;
int smooth_pixel_scaling = false;
int vga_porch_flash = false;
int force_software_renderer = true;
int usegamma = 0;
boolean screenvisible = true;
boolean screensaver_mode = false;
unsigned int joywait = 0;

// Stereo settings.  The convergence (per-eye image shift in pixels at
// full slider) follows the separation as stereo_separation / 16.
int stereo_3d = 1;
int stereo_separation = 96;     // 96/16 = 6 map units per eye

fixed_t r_stereo_offset = 0;
int r_stereo_pshift = 0;

// Bottom screen.
int bottom_screen = 1;
int hud_on_bottom = 1;
pixel_t *I_BottomBuffer = NULL;
pixel_t *I_BarBuffer = NULL;

boolean I_HudOnBottom(void)
{
    return hud_on_bottom && bottom_screen;
}

#define BOTTOM_W 320
#define BOTTOM_H 240

// --- Internal state ---------------------------------------------------------

static boolean initialized = false;
static u16 palette565[256];
static byte rgbpalette[256 * 3];   // for I_GetPaletteIndex

// For each output row (0..TOP_H-1), the source row to show, or -1 for
// a black border row.
static int row_map[TOP_H];
static int dest_x0;                 // left edge of the image on screen

// Stereo capture buffers (view window only).
static byte *eye_view[2];
static int eye_x, eye_y, eye_w, eye_h;
static boolean stereo_frame;        // both eyes captured this frame

// --- Palette ----------------------------------------------------------------

static inline u16 ToRGB565(int r, int g, int b)
{
    return (u16) (((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

void I_SetPalette(byte *doompalette)
{
    int i;

    for (i = 0; i < 256; ++i)
    {
        // Mask to 6 bits per channel like the VGA DAC did.
        int r = gammatable[usegamma][*doompalette++] & ~3;
        int g = gammatable[usegamma][*doompalette++] & ~3;
        int b = gammatable[usegamma][*doompalette++] & ~3;

        rgbpalette[i * 3 + 0] = r;
        rgbpalette[i * 3 + 1] = g;
        rgbpalette[i * 3 + 2] = b;
        palette565[i] = ToRGB565(r, g, b);
    }
}

int I_GetPaletteIndex(int r, int g, int b)
{
    int best = 0, best_diff = INT_MAX;
    int i;

    for (i = 0; i < 256; ++i)
    {
        int dr = r - rgbpalette[i * 3 + 0];
        int dg = g - rgbpalette[i * 3 + 1];
        int db = b - rgbpalette[i * 3 + 2];
        int diff = dr * dr + dg * dg + db * db;

        if (diff < best_diff)
        {
            best = i;
            best_diff = diff;
            if (diff == 0)
            {
                break;
            }
        }
    }

    return best;
}

// --- Scaling setup ----------------------------------------------------------

static void SetupScaling(void)
{
    int y;

    dest_x0 = (TOP_W - SCREENWIDTH) / 2;

    for (y = 0; y < TOP_H; ++y)
    {
        if (aspect_ratio_correct)
        {
            // 200 -> 240: each 5 source rows become 6 output rows,
            // like a VGA monitor showing mode 13h.
            row_map[y] = (y * SCREENHEIGHT) / TOP_H;
        }
        else
        {
            int sy = y - (TOP_H - SCREENHEIGHT) / 2;
            row_map[y] = (sy >= 0 && sy < SCREENHEIGHT) ? sy : -1;
        }
    }
}

// --- Stereo -----------------------------------------------------------------

static float SliderLevel(void)
{
    if (!stereo_3d)
    {
        return 0.0f;
    }

    return osGet3DSliderState();
}

boolean I_StereoActive(void)
{
    return initialized && SliderLevel() > 0.0f;
}

fixed_t I_StereoEyeOffset(void)
{
    return (fixed_t) (SliderLevel() * stereo_separation * (FRACUNIT / 16));
}

// Convergence: how far the projection is shifted per eye, in pixels.  The
// renderer applies it (R_SetStereoView), not the blit.
int I_StereoConvergence(void)
{
    return (int) (SliderLevel() * stereo_separation / 16 + 0.5f);
}

void I_StereoSaveView(int eye, int x, int y, int w, int h)
{
    int row;

    if (eye < 0 || eye > 1 || w <= 0 || h <= 0)
    {
        return;
    }

    eye_x = x;
    eye_y = y;
    eye_w = w;
    eye_h = h;

    for (row = 0; row < h; ++row)
    {
        memcpy(eye_view[eye] + row * w,
               I_VideoBuffer + (y + row) * SCREENWIDTH + x, w);
    }

    // The left eye is rendered last, so once it is saved the frame has
    // both views.
    if (eye == 0)
    {
        stereo_frame = true;
    }
}

// --- Blitting ---------------------------------------------------------------

// Plain mono blit of I_VideoBuffer to one framebuffer.
static void BlitMono(u16 *fb)
{
    int x, y;

    for (x = 0; x < SCREENWIDTH; ++x)
    {
        u16 *col = fb + (dest_x0 + x) * TOP_H;
        const byte *src = I_VideoBuffer + x;

        // Framebuffer columns run bottom-to-top.
        for (y = 0; y < TOP_H; ++y)
        {
            int sy = row_map[y];
            col[TOP_H - 1 - y] = sy < 0 ? 0 : palette565[src[sy * SCREENWIDTH]];
        }
    }
}

// Stereo blit for one eye.  Inside the view window, pixels that still
// match the clean left-eye render come from this eye's render; the eye
// images are already separated and converged by the renderer, so nothing
// is shifted here.  Anything that was drawn on top afterwards (HUD
// messages, menus, PAUSED...) differs from the clean render and is shown
// as-is, at screen depth, to both eyes.
static void BlitStereo(u16 *fb, int eye)
{
    const byte *clean = eye_view[0];
    const byte *view = eye_view[eye];
    int x, y;

    for (x = 0; x < SCREENWIDTH; ++x)
    {
        u16 *col = fb + (dest_x0 + x) * TOP_H;
        const byte *src = I_VideoBuffer + x;
        int vx = x - eye_x;
        boolean in_view_x = vx >= 0 && vx < eye_w;

        for (y = 0; y < TOP_H; ++y)
        {
            int sy = row_map[y];
            byte p;

            if (sy < 0)
            {
                col[TOP_H - 1 - y] = 0;
                continue;
            }

            p = src[sy * SCREENWIDTH];

            if (in_view_x)
            {
                int vy = sy - eye_y;

                if (vy >= 0 && vy < eye_h && p == clean[vy * eye_w + vx])
                {
                    p = view[vy * eye_w + vx];
                }
            }

            col[TOP_H - 1 - y] = palette565[p];
        }
    }
}

// Bottom screen: the middle 320 columns of the bottom buffer, scaled like
// the top one.
static void BlitBottom(void)
{
    u16 *fb = (u16 *) gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL);
    int x, y;

    if (!bottom_screen || gamestate != GS_LEVEL || I_BottomBuffer == NULL)
    {
        memset(fb, 0, BOTTOM_W * BOTTOM_H * sizeof(u16));
        return;
    }

    for (x = 0; x < BOTTOM_W; ++x)
    {
        u16 *col = fb + x * BOTTOM_H;
        const byte *src = I_BottomBuffer + P3DS_UI_XOFF + x;

        for (y = 0; y < BOTTOM_H; ++y)
        {
            int sy = row_map[y];
            col[BOTTOM_H - 1 - y] = sy < 0 ? 0 : palette565[src[sy * SCREENWIDTH]];
        }
    }
}

void I_FinishUpdate(void)
{
    u16 *left, *right;

    if (!initialized)
    {
        return;
    }

    BlitBottom();

    left = (u16 *) gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL);

    if (stereo_frame)
    {
        right = (u16 *) gfxGetFramebuffer(GFX_TOP, GFX_RIGHT, NULL, NULL);
        BlitStereo(left, 0);
        BlitStereo(right, 1);
        stereo_frame = false;
    }
    else
    {
        BlitMono(left);

        if (osGet3DSliderState() > 0.0f)
        {
            // Slider up but nothing stereo this frame (menus, automap,
            // intermission): show the same image to both eyes.
            right = (u16 *) gfxGetFramebuffer(GFX_TOP, GFX_RIGHT, NULL, NULL);
            memcpy(right, left, TOP_W * TOP_H * sizeof(u16));
        }
    }

    gfxFlushBuffers();
    gfxSwapBuffers();
}

// --- Init / shutdown ----------------------------------------------------------

void I_InitGraphics(void)
{
    byte *doompal;
    int eye;

    // The startup console is done with the bottom screen; from here on
    // it is ours (log output keeps going to the file).
    P3DS_ConsoleOff();
    gfxSetDoubleBuffering(GFX_BOTTOM, true);

    // Clear both buffers of both eyes so the borders start black.
    for (eye = 0; eye < 2; ++eye)
    {
        memset(gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL), 0,
               TOP_W * TOP_H * 2);
        memset(gfxGetFramebuffer(GFX_TOP, GFX_RIGHT, NULL, NULL), 0,
               TOP_W * TOP_H * 2);
        memset(gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL), 0,
               BOTTOM_W * BOTTOM_H * 2);
        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }

    I_VideoBuffer = Z_Malloc(SCREENWIDTH * SCREENHEIGHT, PU_STATIC, NULL);
    V_RestoreBuffer();
    memset(I_VideoBuffer, 0, SCREENWIDTH * SCREENHEIGHT);

    eye_view[0] = Z_Malloc(SCREENWIDTH * SCREENHEIGHT, PU_STATIC, NULL);
    eye_view[1] = Z_Malloc(SCREENWIDTH * SCREENHEIGHT, PU_STATIC, NULL);

    I_BottomBuffer = Z_Malloc(SCREENWIDTH * SCREENHEIGHT, PU_STATIC, NULL);
    memset(I_BottomBuffer, 0, SCREENWIDTH * SCREENHEIGHT);

    I_BarBuffer = Z_Malloc(SCREENWIDTH * SCREENHEIGHT, PU_STATIC, NULL);
    memset(I_BarBuffer, 0, SCREENWIDTH * SCREENHEIGHT);

    SetupScaling();

    doompal = W_CacheLumpName(DEH_String("PLAYPAL"), PU_CACHE);
    I_SetPalette(doompal);

    initialized = true;
}

void I_ShutdownGraphics(void)
{
    initialized = false;
}

// --- Frame / input hooks ----------------------------------------------------

void I_StartFrame(void)
{
}

void I_StartTic(void)
{
    if (!initialized)
    {
        return;
    }

    if (!aptMainLoop())
    {
        I_Quit();
    }

    P3DS_PollInput();
}

void I_UpdateNoBlit(void)
{
}

void I_ReadScreen(pixel_t *scr)
{
    memcpy(scr, I_VideoBuffer, SCREENWIDTH * SCREENHEIGHT * sizeof(*scr));
}

// --- Things that make no sense on a 3DS -----------------------------------

void I_GraphicsCheckCommandLine(void) { }
void I_CheckIsScreensaver(void) { }
void I_SetGrabMouseCallback(grabmouse_callback_t func) { (void) func; }
void I_DisplayFPSDots(boolean dots_on) { (void) dots_on; }
void I_SetWindowTitle(const char *title) { (void) title; }
void I_InitWindowTitle(void) { }
void I_InitWindowIcon(void) { }
void I_EnableLoadingDisk(int xoffs, int yoffs) { (void) xoffs; (void) yoffs; }

void I_RegisterWindowIcon(const unsigned int *icon, int width, int height)
{
    (void) icon;
    (void) width;
    (void) height;
}

void I_GetWindowPosition(int *x, int *y, int w, int h)
{
    (void) w;
    (void) h;
    *x = 0;
    *y = 0;
}

void I_BindVideoVariables(void)
{
    M_BindIntVariable("usegamma",                  &usegamma);
    M_BindIntVariable("aspect_ratio_correct",      &aspect_ratio_correct);
    M_BindIntVariable("stereo_3d",                 &stereo_3d);
    M_BindIntVariable("stereo_separation",         &stereo_separation);
    M_BindIntVariable("bottom_screen",             &bottom_screen);
    M_BindIntVariable("hud_on_bottom",             &hud_on_bottom);
}

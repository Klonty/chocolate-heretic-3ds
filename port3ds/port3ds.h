//
// Shared declarations for the Nintendo 3DS platform layer.
//

#ifndef PORT3DS_H
#define PORT3DS_H

#include <stddef.h>

#include "doomtype.h"
#include "m_fixed.h"

// Where WADs, config files and savegames live on the SD card.
#define P3DS_DATA_DIR "sdmc:/3ds/heretic"
#define P3DS_LOG_FILE P3DS_DATA_DIR "/heretic.log"

// True if the ir:rst service (C-Stick) is available.
extern boolean p3ds_have_cstick;

// Show an error on the bottom screen and wait for START before returning.
void P3DS_FatalScreen(const char *title, const char *message);

// Stop mirroring stdout to the bottom-screen console (the game takes the
// screen over).  Output still goes to the log file.
void P3DS_ConsoleOff(void);

// Create a thread at the main thread's priority plus prio_delta
// (negative = higher priority), preferring a core the game loop is not
// running on.  Returns NULL on failure.  The type is libctru's Thread
// (opaque here).
struct Thread_tag;
struct Thread_tag *P3DS_CreateThread(void (*entry)(void *), void *arg,
                                     unsigned int stack_size, int prio_delta,
                                     const char *name);

// NDSP init/shutdown, shared by sound effects and music.
boolean P3DS_AudioInit(void);
void P3DS_AudioRelease(void);

// Software keyboard.  Fills buf (bufsize bytes) and returns true if the
// user confirmed; false if cancelled.
boolean P3DS_InputText(char *buf, size_t bufsize, const char *initial,
                       const char *hint);

//
// Stereoscopic 3D.
//
// When the 3D slider is up, the game renders the 3D view twice, once per
// eye, with the camera shifted sideways by r_stereo_offset (see
// R_SetupFrame in heretic/r_main.c).  HUD, menus and other 2D overlays
// are drawn once and shown to both eyes at screen depth.
//
// Convergence (where the zero-parallax plane sits) comes from shifting
// each eye's projection centre, not from shifting the finished image, so
// every screen column is genuinely rendered and the edges do not smear.
//
// Per frame, D_Display does:
//
//   if (I_StereoActive()) {
//       r_stereo_offset = +I_StereoEyeOffset();
//       R_SetStereoView(+I_StereoConvergence());  render;  I_StereoSaveView(1, ...);
//       r_stereo_offset = -I_StereoEyeOffset();
//       R_SetStereoView(-I_StereoConvergence());  render;  I_StereoSaveView(0, ...);
//       r_stereo_offset = 0;
//   }
//   ...draw HUD into I_VideoBuffer...
//   I_FinishUpdate();   // builds both eye images
//

// True if the 3D slider is up and stereo is turned on in the config.
boolean I_StereoActive(void);

// Half the eye separation, in map units (fixed point), scaled by the
// 3D slider.
fixed_t I_StereoEyeOffset(void);

// Per-eye projection shift in pixels for the current slider position.
// Applied by the renderer through R_SetStereoView().
int I_StereoConvergence(void);

// Copy the just-rendered view window out of I_VideoBuffer for the given
// eye (0 = left, 1 = right).
void I_StereoSaveView(int eye, int x, int y, int w, int h);

// Camera offset (fixed point, along the view's right vector) used by
// R_SetupFrame.  Zero in normal mono rendering.
extern fixed_t r_stereo_offset;

// Extra horizontal shift (screen pixels) for the player's weapon sprite,
// so it appears just in front of the screen.  Zero in mono rendering.
extern int r_stereo_pshift;

// Config variables (bound in I_BindVideoVariables).
extern int stereo_3d;            // 0 = off, 1 = on (follows the slider)
extern int stereo_separation;    // eye separation at full slider, 1/16 map units, 0..240

//
// The top screen renders the 3D view at 400x200 (SCREENWIDTH is 400 on
// this build) and scales it to 400x240.  The classic 320-wide UI is
// centred inside that framebuffer, and the bottom screen shows the
// middle 320 columns at 1:1.
//

#define P3DS_UI_XOFF 40
#define P3DS_UI_W    320

//
// Bottom screen: live automap with the status bar underneath.
//
// The bottom screen shows the middle 320x200 columns of the buffer:
//
//   rows   0..41  status bar
//   rows  42..157  automap (116 rows)
//   rows 158..199  inventory bar
//
// While the automap is up on the top screen, the status bar follows it
// there and the bottom screen keeps its last status bar image.
//

#define P3DS_STATBAR_H 42       // rows 0..41
#define P3DS_MAP_Y    42        // first automap row
#define P3DS_MAP_H    116       // automap rows
#define P3DS_INVBAR_Y 160       // first row of the inventory bar
#define P3DS_INVBAR_H 40        // and its height

extern int bottom_screen;        // config: 0 = off, 1 = on
extern int hud_on_bottom;        // config: 1 = status bar on the bottom screen

// True if the status bar is drawn on the bottom screen.
boolean I_HudOnBottom(void);

// 320x200 paletted buffer shown on the bottom screen while playing.
extern pixel_t *I_BottomBuffer;

// Scratch buffer the status bar is composed in; it keeps its native
// coordinates and its top 42 rows are then copied to the bottom screen.
extern pixel_t *I_BarBuffer;

// Draw the automap into buf (rows 0..157) - heretic/am_map.c.
void AM_DrawToBuffer(pixel_t *buf);

// Compose the status bar and copy it to the top of the bottom screen -
// heretic/sb_bar.c.
void SB_DrawToBuffer(pixel_t *buf);

// Draw the inventory bar (and the bar backdrop behind it) into buf at
// its usual place at the bottom - heretic/sb_bar.c.
void SB_DrawInventoryTo(pixel_t *buf);

#endif

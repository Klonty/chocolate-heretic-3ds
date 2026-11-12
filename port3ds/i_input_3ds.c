//
// Nintendo 3DS input: replaces src/i_input.c and src/i_joystick.c.
//
// Buttons are turned into keyboard events so that the unmodified game
// code (menus, cheats, inventory) keeps working.  The sticks are posted
// as one ev_joystick event per tic.  The in-game key of every button can
// be changed in heretic.cfg (3ds_button_a = <Doom key code>, ...); the
// menu keys are fixed.
//
// Default layout:
//   Circle Pad      move / strafe
//   C-Stick         turn / look (New 3DS)
//   L / R           turn left / right
//   A               fire                  (menus: select)
//   B               use                   (menus: back)
//   ZR / ZL         next / previous weapon
//   X               use inventory item    (menus: back)
//   Y               inventory right       (menus: back)
//   D-Pad left/right inventory left/right (menus: arrows)
//   D-Pad up/down   look up / down        (menus: arrows)
//   START           menu
//   SELECT          automap
//   Touch screen    tap an inventory slot to select it, tap it again to use
//

#include <3ds.h>
#include <math.h>
#include <string.h>

#include "doomkeys.h"
#include "doomtype.h"
#include "doomdef.h"
#include "d_event.h"
#include "i_input.h"
#include "i_joystick.h"
#include "i_video.h"
#include "m_config.h"
#include "m_controls.h"
#include "m_fixed.h"

#include "port3ds.h"

// Menu state, so the same button can mean "select" and "fire".
extern boolean MenuActive;
extern boolean askforquit;

// Inventory state (heretic/sb_bar.c, heretic/g_game.c).
extern int inv_ptr;
extern int curpos;
extern int inventoryTics;

// --- Variables other code expects ------------------------------------------

int novert = 0;
int vanilla_keyboard_mapping = true;
float mouse_acceleration = 2.0f;
int mouse_threshold = 10;

int use_analog = 1;
int joystick_turn_sensitivity = 10;
int joystick_move_sensitivity = 10;
int joystick_look_sensitivity = 10;

// Analog range reported by libctru.
#define CIRCLE_RANGE  156
#define CSTICK_RANGE  120
#define DEADZONE_PCT  12

// In-game key for each button; rebindable through the config file.
int btn_a      = KEY_RCTRL;        // fire
int btn_b      = ' ';              // use
int btn_x      = KEY_ENTER;        // use artifact
int btn_y      = ']';              // inventory right
int btn_l      = KEY_LEFTARROW;    // turn left
int btn_r      = KEY_RIGHTARROW;   // turn right
int btn_zl     = 'e';              // previous weapon
int btn_zr     = 'r';              // next weapon
int btn_dup    = KEY_PGDN;         // look up
int btn_ddown  = KEY_DEL;          // look down
int btn_dleft  = '[';              // inventory left
int btn_dright = ']';              // inventory right
int btn_select = KEY_TAB;          // automap

typedef struct
{
    u32 hid;            // libctru KEY_* bit
    int *game_key;      // key sent while playing (configurable)
    int menu_key;       // key sent while a menu is open (0 = same as game)
} button_map_t;

static const button_map_t button_map[] =
{
    { KEY_DUP,     &btn_dup,    KEY_UPARROW },
    { KEY_DDOWN,   &btn_ddown,  KEY_DOWNARROW },
    { KEY_DLEFT,   &btn_dleft,  KEY_LEFTARROW },
    { KEY_DRIGHT,  &btn_dright, KEY_RIGHTARROW },
    { KEY_L,       &btn_l,      0 },
    { KEY_R,       &btn_r,      0 },
    { KEY_ZL,      &btn_zl,     0 },
    { KEY_ZR,      &btn_zr,     0 },
    { KEY_A,       &btn_a,      KEY_ENTER },
    { KEY_B,       &btn_b,      KEY_BACKSPACE },
    { KEY_X,       &btn_x,      KEY_BACKSPACE },
    { KEY_Y,       &btn_y,      KEY_BACKSPACE },
    { KEY_SELECT,  &btn_select, 0 },
};

#define NUM_BUTTONS (sizeof(button_map) / sizeof(*button_map))

// Key currently held down for each button (0 = none), so a release
// always matches the press even if the menu opened in between.
static int held_key[NUM_BUTTONS];
static int held_confirm[NUM_BUTTONS];
static boolean start_held;

// Touch: key_useartifact must stay down for a tic to register.
static int touch_release_key;

static void PostKey(int key, boolean down)
{
    event_t ev;

    if (key == 0)
    {
        return;
    }

    memset(&ev, 0, sizeof(ev));
    ev.type = down ? ev_keydown : ev_keyup;
    ev.data1 = key;

    if (key >= ' ' && key < 127)
    {
        ev.data2 = key;
        ev.data3 = key;
    }

    D_PostEvent(&ev);
}

// --- Gyro aiming ------------------------------------------------------------
//
// The gyroscope reports how fast the console is rotating.  That is
// integrated over real time into degrees (p3ds_gyro_turn/look), which
// G_BuildTiccmd turns into view rotation, so the view moves as far as the
// console did (times the sensitivity) regardless of frame rate.
//
// The sensor reads slightly off zero at rest (bias).  It is measured for
// half a second when the gyro is switched on, and re-measured whenever the
// console has been held still for a second, so the view does not creep.

int gyro_aim = 0;
int gyro_style = 0;
int gyro_sensitivity = 8;

float p3ds_gyro_turn = 0.0f;
float p3ds_gyro_look = 0.0f;

#define GYRO_AXES          3        // roll, yaw, pitch
#define GYRO_CALIB_SECONDS 0.5f
#define GYRO_STILL_SECONDS 1.0f
#define GYRO_STILL_DPS     1.0f     // max wobble while "held still"
#define GYRO_DRIFT_DPS     3.0f     // max bias change accepted at once
#define GYRO_DEADZONE_DPS  0.4f
#define GYRO_MAX_ACCUM     90.0f

static boolean gyro_on;
static float gyro_coeff = 14.375f;  // raw units per degree/second
static float gyro_bias[GYRO_AXES];
static boolean gyro_calibrated;
static u64 gyro_last_tick;

// Running window used for both the first calibration and re-calibration.
static float win_sum[GYRO_AXES];
static float win_min[GYRO_AXES], win_max[GYRO_AXES];
static float win_time;
static int win_count;

static void ResetGyroWindow(void)
{
    int i;

    for (i = 0; i < GYRO_AXES; ++i)
    {
        win_sum[i] = 0.0f;
        win_min[i] = 1e9f;
        win_max[i] = -1e9f;
    }
    win_time = 0.0f;
    win_count = 0;
}

static void GyroWindowAdd(const float *raw, float dt)
{
    int i;

    for (i = 0; i < GYRO_AXES; ++i)
    {
        win_sum[i] += raw[i];
        if (raw[i] < win_min[i]) win_min[i] = raw[i];
        if (raw[i] > win_max[i]) win_max[i] = raw[i];
    }
    win_time += dt;
    win_count++;
}

// Called every poll.  active is false while a menu is up, the game is
// paused or no level is running; the gyro keeps calibrating but the view
// does not move.
static void UpdateGyro(boolean active)
{
    angularRate rate;
    float raw[GYRO_AXES];
    float dps[GYRO_AXES];
    float dt, gain, turn;
    u64 now;
    int i;

    if (gyro_aim <= 0)
    {
        if (gyro_on)
        {
            HIDUSER_DisableGyroscope();
            gyro_on = false;
        }
        p3ds_gyro_turn = p3ds_gyro_look = 0.0f;
        return;
    }

    if (!gyro_on)
    {
        float coeff;

        HIDUSER_EnableGyroscope();
        if (R_SUCCEEDED(HIDUSER_GetGyroscopeRawToDpsCoefficient(&coeff))
         && coeff > 0.0f)
        {
            gyro_coeff = coeff;
        }
        gyro_on = true;
        gyro_calibrated = false;
        gyro_last_tick = svcGetSystemTick();
        ResetGyroWindow();
        p3ds_gyro_turn = p3ds_gyro_look = 0.0f;
        return;
    }

    now = svcGetSystemTick();
    dt = (float) (now - gyro_last_tick) / (float) SYSCLOCK_ARM11;
    gyro_last_tick = now;

    // A long stall (loading a level) is not a long rotation.
    if (dt > 0.1f)
    {
        dt = 0.1f;
    }

    // libctru names these fields roll/yaw/pitch, but on hardware x is the
    // console tipping towards/away from you, z is tilting it like a
    // steering wheel and y is turning it left/right.
    hidGyroRead(&rate);
    raw[0] = rate.z;    // roll (steering wheel)
    raw[1] = rate.y;    // yaw (turning)
    raw[2] = rate.x;    // pitch (tipping)

    GyroWindowAdd(raw, dt);

    if (!gyro_calibrated)
    {
        if (win_time >= GYRO_CALIB_SECONDS)
        {
            for (i = 0; i < GYRO_AXES; ++i)
            {
                gyro_bias[i] = win_sum[i] / win_count;
            }
            gyro_calibrated = true;
            ResetGyroWindow();
        }
        p3ds_gyro_turn = p3ds_gyro_look = 0.0f;
        return;
    }

    // Held still for a while: take the average as the new bias.  A steady
    // deliberate turn also has little wobble, so only small corrections
    // are accepted.
    if (win_time >= GYRO_STILL_SECONDS)
    {
        boolean still = true;

        for (i = 0; i < GYRO_AXES; ++i)
        {
            float mean = win_sum[i] / win_count;

            if (win_max[i] - win_min[i] > GYRO_STILL_DPS * gyro_coeff
             || fabsf(mean - gyro_bias[i]) > GYRO_DRIFT_DPS * gyro_coeff)
            {
                still = false;
            }
        }

        if (still)
        {
            for (i = 0; i < GYRO_AXES; ++i)
            {
                gyro_bias[i] = win_sum[i] / win_count;
            }
        }
        ResetGyroWindow();
    }

    if (!active)
    {
        p3ds_gyro_turn = p3ds_gyro_look = 0.0f;
        return;
    }

    for (i = 0; i < GYRO_AXES; ++i)
    {
        dps[i] = (raw[i] - gyro_bias[i]) / gyro_coeff;
        if (fabsf(dps[i]) < GYRO_DEADZONE_DPS)
        {
            dps[i] = 0.0f;
        }
    }

    if (gyro_sensitivity < 1) gyro_sensitivity = 1;
    if (gyro_sensitivity > 16) gyro_sensitivity = 16;
    gain = gyro_sensitivity / 4.0f;
    turn = gyro_style ? dps[1] : dps[0];

    p3ds_gyro_turn += turn * dt * gain;
    if (gyro_aim >= 2)
    {
        p3ds_gyro_look += dps[2] * dt * gain;
    }

    if (p3ds_gyro_turn > GYRO_MAX_ACCUM) p3ds_gyro_turn = GYRO_MAX_ACCUM;
    if (p3ds_gyro_turn < -GYRO_MAX_ACCUM) p3ds_gyro_turn = -GYRO_MAX_ACCUM;
    if (p3ds_gyro_look > GYRO_MAX_ACCUM) p3ds_gyro_look = GYRO_MAX_ACCUM;
    if (p3ds_gyro_look < -GYRO_MAX_ACCUM) p3ds_gyro_look = -GYRO_MAX_ACCUM;
}

static int ScaleAxis(int value, int range)
{
    int dead = range * DEADZONE_PCT / 100;
    int sign = value < 0 ? -1 : 1;
    int mag = value < 0 ? -value : value;

    if (mag <= dead)
    {
        return 0;
    }

    mag = (mag - dead) * FRACUNIT / (range - dead);

    if (mag > FRACUNIT)
    {
        mag = FRACUNIT;
    }

    return sign * mag;
}

// Bottom screen is 320x240 showing a 320x200 buffer.  The inventory bar
// is seven 31-pixel slots from x = 50; its first row is 160, or
// P3DS_INVBAR_Y when the status bar moved down and made room for it.
static void HandleTouch(void)
{
    touchPosition tp;
    player_t *plr = &players[consoleplayer];
    int bx, by, bar_y, i, first, slot;

    if (!bottom_screen || gamestate != GS_LEVEL || MenuActive || paused)
    {
        return;
    }

    hidTouchRead(&tp);
    bx = tp.px;
    by = tp.py * SCREENHEIGHT / 240;
    bar_y = I_HudOnBottom() ? P3DS_INVBAR_Y : 160;

    // Weapon selector: press that weapon's number key for a tic.
    slot = SB_WeaponSlotAt(bx, by);
    if (slot >= 0)
    {
        static int *const weapon_key[] = {
            &key_weapon1, &key_weapon2, &key_weapon3, &key_weapon4,
            &key_weapon5, &key_weapon6, &key_weapon7,
        };

        if (touch_release_key)
        {
            PostKey(touch_release_key, false);
        }
        PostKey(*weapon_key[slot], true);
        touch_release_key = *weapon_key[slot];
        return;
    }

    if (by < bar_y || by >= bar_y + P3DS_INVBAR_H
     || bx < 50 || bx >= 50 + 7 * 31)
    {
        return;
    }

    i = (bx - 50) / 31;
    first = inv_ptr - curpos;
    slot = first + i;

    if (slot < 0 || slot >= plr->inventorySlotNum
     || plr->inventory[slot].type == arti_none)
    {
        return;
    }

    if (slot == inv_ptr)
    {
        // Tapping the selected item uses it.
        PostKey(key_useartifact, true);
        touch_release_key = key_useartifact;
    }
    else
    {
        inv_ptr = slot;
        curpos = i;
        inventoryTics = 5 * 35;
    }
}

void P3DS_PollInput(void)
{
    circlePosition circle, cstick;
    u32 down, up;
    event_t ev;
    unsigned int i;
    boolean in_menu;

    hidScanInput();

    if (p3ds_have_cstick)
    {
        irrstScanInput();
    }

    down = hidKeysDown();
    up = hidKeysUp();
    in_menu = MenuActive || askforquit;

    UpdateGyro(!in_menu && !paused && gamestate == GS_LEVEL);

    if (touch_release_key)
    {
        PostKey(touch_release_key, false);
        touch_release_key = 0;
    }

    for (i = 0; i < NUM_BUTTONS; ++i)
    {
        if (down & button_map[i].hid)
        {
            int key = *button_map[i].game_key;

            if (in_menu && button_map[i].menu_key)
            {
                key = button_map[i].menu_key;
            }

            held_key[i] = key;
            PostKey(key, true);

            // Yes/no prompts (quit, quicksave...) want 'y' or 'n'.
            if (askforquit)
            {
                int confirm = 0;

                if (button_map[i].hid == KEY_A)
                {
                    confirm = 'y';
                }
                else if (button_map[i].hid == KEY_B)
                {
                    confirm = 'n';
                }

                if (confirm)
                {
                    held_confirm[i] = confirm;
                    PostKey(confirm, true);
                }
            }
        }

        if (up & button_map[i].hid)
        {
            if (held_key[i])
            {
                PostKey(held_key[i], false);
                held_key[i] = 0;
            }

            if (held_confirm[i])
            {
                PostKey(held_confirm[i], false);
                held_confirm[i] = 0;
            }
        }
    }

    // START is the menu key in every context.
    if (down & KEY_START)
    {
        PostKey(KEY_ESCAPE, true);
        start_held = true;
    }

    if ((up & KEY_START) && start_held)
    {
        PostKey(KEY_ESCAPE, false);
        start_held = false;
    }

    if (down & KEY_TOUCH)
    {
        HandleTouch();
    }

    // Sticks.  libctru reports "up" as positive; the game wants "down"
    // positive on the vertical axes, like an SDL joystick.
    hidCircleRead(&circle);

    if (p3ds_have_cstick)
    {
        hidCstickRead(&cstick);
    }
    else
    {
        cstick.dx = 0;
        cstick.dy = 0;
    }

    memset(&ev, 0, sizeof(ev));
    ev.type = ev_joystick;
    ev.data1 = 0;
    ev.data2 = ScaleAxis(cstick.dx, CSTICK_RANGE);    // turn
    ev.data3 = ScaleAxis(-circle.dy, CIRCLE_RANGE);   // forward/back
    ev.data4 = ScaleAxis(circle.dx, CIRCLE_RANGE);    // strafe
    ev.data5 = ScaleAxis(-cstick.dy, CSTICK_RANGE);   // look
    ev.data6 = 0;
    D_PostEvent(&ev);
}

// --- i_input.h -------------------------------------------------------------

void I_BindInputVariables(void)
{
    M_BindIntVariable("novert", &novert);
    M_BindIntVariable("vanilla_keyboard_mapping", &vanilla_keyboard_mapping);

    M_BindIntVariable("3ds_button_a",      &btn_a);
    M_BindIntVariable("3ds_button_b",      &btn_b);
    M_BindIntVariable("3ds_button_x",      &btn_x);
    M_BindIntVariable("3ds_button_y",      &btn_y);
    M_BindIntVariable("3ds_button_l",      &btn_l);
    M_BindIntVariable("3ds_button_r",      &btn_r);
    M_BindIntVariable("3ds_button_zl",     &btn_zl);
    M_BindIntVariable("3ds_button_zr",     &btn_zr);
    M_BindIntVariable("3ds_button_dup",    &btn_dup);
    M_BindIntVariable("3ds_button_ddown",  &btn_ddown);
    M_BindIntVariable("3ds_button_dleft",  &btn_dleft);
    M_BindIntVariable("3ds_button_dright", &btn_dright);
    M_BindIntVariable("3ds_button_select", &btn_select);

    M_BindIntVariable("gyro_aim",          &gyro_aim);
    M_BindIntVariable("gyro_style",        &gyro_style);
    M_BindIntVariable("gyro_sensitivity",  &gyro_sensitivity);
}

void I_ReadMouse(void)
{
}

void I_StartTextInput(int x1, int y1, int x2, int y2)
{
    (void) x1;
    (void) y1;
    (void) x2;
    (void) y2;
}

void I_StopTextInput(void)
{
}

// --- i_joystick.h ----------------------------------------------------------

void I_InitJoystick(void)
{
}

void I_ShutdownJoystick(void)
{
}

void I_UpdateJoystick(void)
{
}

void I_BindJoystickVariables(void)
{
    M_BindIntVariable("use_analog", &use_analog);
    M_BindIntVariable("joystick_turn_sensitivity", &joystick_turn_sensitivity);
    M_BindIntVariable("joystick_move_sensitivity", &joystick_move_sensitivity);
    M_BindIntVariable("joystick_look_sensitivity", &joystick_look_sensitivity);
}

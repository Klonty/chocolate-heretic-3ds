# Heretic for the Nintendo 3DS

A port of Chocolate Doom's Heretic to the Nintendo 3DS, with stereoscopic
3D on the top screen and a live automap on the bottom one. Everything
game-related is the unmodified Chocolate Heretic code; this directory holds
the 3DS platform layer that replaces SDL.

## Status

- Built and played on a **New 3DS** (Luma3DS + Homebrew Launcher). The
  original 3DS is untested.
- Only vanilla Heretic IWADs work: `HERETIC.WAD` from the original 1.3
  release (tested with MD5 `66d686b1ed6d35ff103f15dbd30e0341`, e.g. GOG's
  `dos/base/heretic/`), the shareware `heretic1.wad`, or the free
  [Blasphemer](https://github.com/Blasphemer/blasphemer/releases)
  `blasphem.wad` (tested with 0.1.8). The 2025 KEX re-release's
  `heretic.wad` is not a vanilla IWAD and is rejected at startup
  (`R_GenerateLookup: texture >64k`).
- Sound and music need `sdmc:/3ds/dspfirm.cdc`; without it the game runs
  silently. Music is OPL emulation (DBOPL at 22050 Hz) and writes a
  temporary `doom.mid` into the data directory while a song is registered.
- Single player only: networking is compiled in but has no transport.

## Requirements

- A 3DS or New 3DS with custom firmware (Luma3DS) and the Homebrew Launcher.
- Game data, one of:
  - `HERETIC.WAD` from your own copy of the game. The 2025 re-release's
    `heretic.wad` is not a vanilla IWAD and will not work; GOG ships the
    original under `dos/base/heretic/`.
  - The shareware `heretic1.wad` (episode 1 only).
  - `blasphem.wad` from [Blasphemer](https://github.com/Blasphemer/blasphemer/releases),
    a free replacement for Heretic's data. Keep its name; no renaming
    needed. Its savegames are kept apart from Heretic's.

  If several are present, `HERETIC.WAD` wins, then `heretic1.wad`. With none,
  the game says what to copy where.
- For sound: `sdmc:/3ds/dspfirm.cdc`, the DSP firmware dump that all 3DS
  homebrew needs for audio. Dump it on your console with the DSP1 homebrew.

## Installing

1. Copy `heretic.3dsx` to `sdmc:/3ds/heretic/heretic.3dsx`.
2. Copy your IWAD to `sdmc:/3ds/heretic/` (`HERETIC.WAD`, `heretic1.wad`
   or `blasphem.wad`).
3. Start it from the Homebrew Launcher.

Config files and savegames also go to `sdmc:/3ds/heretic/`. The startup log
is shown on the bottom screen and written to `sdmc:/3ds/heretic/heretic.log`,
which is the first place to look when something goes wrong.

## Controls

| Input            | In game                         | In menus          |
|------------------|---------------------------------|-------------------|
| Circle Pad       | Move / strafe                   |                   |
| C-Stick          | Turn / look (New 3DS)           |                   |
| L / R            | Turn left / right               |                   |
| A                | Fire                            | Select / Yes      |
| B                | Use (doors, switches)           | Back / No         |
| ZR / ZL          | Next / previous weapon          |                   |
| X                | Use inventory item              | Back              |
| Y                | Inventory right                 | Back              |
| D-Pad ◀ ▶        | Inventory left / right          | Navigate          |
| D-Pad ▲ ▼        | Look up / down                  | Navigate          |
| START            | Menu                            | Menu              |
| SELECT           | Automap on the top screen       |                   |
| Touch screen     | Tap an inventory slot to select it, tap it again to use it |
| 3D slider        | Stereoscopic depth              |                   |

Running is always on. Saving a game opens the 3DS keyboard for the name.

Every in-game button can be rebound in `chocolate-heretic.cfg`: `3ds_button_a`,
`3ds_button_b`, `3ds_button_x`, `3ds_button_y`, `3ds_button_l`,
`3ds_button_r`, `3ds_button_zl`, `3ds_button_zr`, `3ds_button_dup`,
`3ds_button_ddown`, `3ds_button_dleft`, `3ds_button_dright` and
`3ds_button_select` hold Doom key codes (the same values the `key_*`
entries use). Menu keys are fixed.

## Screen layout

The 3DS top screen is 400×240 per eye, and the 3D view is rendered at the
full 400×200 (a wider field of view rather than a stretched or letterboxed
320×200) and scaled to 400×240. The classic 320-wide interface — menus,
messages, the intermission and finale screens, title pages — is centred in
the middle of the screen.

By default the bottom screen is the HUD, laid out like this:

| Rows | Content |
|---|---|
| 0–41 | status bar |
| 42–157 | live automap |
| 158–199 | inventory bar |

Tapping a slot in the inventory bar selects it; tapping it again uses it.

Two settings in `chocolate-heretic.cfg` change this:

- `hud_on_bottom 0` leaves the status bar on the top screen (centred,
  with the view above it) and the inventory bar underneath the automap
  on the bottom screen. This also gives back the SCREEN SIZE option,
  which only shrinks the view when the bar is on the top.
- `bottom_screen 0` turns the bottom screen off completely; the status
  bar then stays on the top screen too.

While the automap is shown on the top screen (SELECT), the status bar
follows it there for as long as it is up.

## Stereoscopic 3D

With the 3D slider up, the game renders the 3D view twice per frame, once
per eye. Nothing comes out of the screen: a wall you are touching sits on
the screen glass, and the rest of the scene goes back from there. The weapon,
status bar, messages and menus sit on the glass too.

How deep the scene goes is set with the **3D DEPTH** slider in Options →
More (the second options page); the hardware slider scales it smoothly from
flat to full. It is stored as `stereo_separation` in `chocolate-heretic.cfg`
(how far behind the screen distant scenery goes at full slider, in 1/16
pixel per eye, 0–240, default 96). `stereo_screen_distance` (map units,
default 16) is the distance that sits on the glass; anything nearer would
come out of the screen. `stereo_3d 0` ignores the hardware slider entirely.

Rendering two 400×200 views costs a second software render per frame, 25%
more pixels than a 320-wide view. On a New 3DS it runs at the full 35 fps.
If the frame rate is a problem, set `hud_on_bottom 0` and lower the screen
size in the options, or play with the slider down.

## Building

Install [devkitPro](https://devkitpro.org/wiki/Getting_Started) with the
`3ds-dev` package group, then:

```
cd port3ds
make
```

This produces `heretic.3dsx`. Only libctru is used; there are no portlib
dependencies.

## How the port is put together

- `include/SDL.h`, `sdl_shim.c`: the dozen SDL calls the engine's shared
  code uses (mutexes, threads, ticks, byte swapping) implemented on libctru.
- `include/config.h`, `include/txt_*.h`: build configuration and stubs for
  the textscreen library (used only by the DOS-style startup screen).
- `p3ds_sys.c`: startup, shutdown, the log file, threads, the software
  keyboard and fatal errors.
- `i_video_3ds.c`: palette conversion and blitting to both screens, plus
  the stereo assembly. Replaces `src/i_video.c`.
- `i_input_3ds.c`: buttons, sticks and touch. Replaces `src/i_input.c` and
  `src/i_joystick.c`.
- `i_sound_3ds.c`: sound effects on NDSP hardware channels. Replaces
  `src/i_sdlsound.c`.
- `opl_3ds.c`, `dbopl.c`: OPL (AdLib) music emulation streamed to NDSP.
  Uses the DOSBox OPL emulator from Chocolate Doom 2.2.1 instead of the
  current Nuked OPL3, which is far too heavy for the 3DS's ARM11. Replaces
  `opl/opl_sdl.c`.
- `stubs_3ds.c`: ENDOOM screen and multiplayer launch GUI, both left out.

Changes to the shared code are mostly under `#ifdef __3DS__`: the wider
framebuffer in `src/i_video.h`, the centred 320-wide interface in
`src/v_video.c`, the stereo camera offset, full-screen view and screen-size
steps in `src/heretic/r_main.c`, the border patches in
`src/heretic/r_draw.c`, the weapon sprite shift in `src/heretic/r_things.c`,
the two-eye render and bottom-screen draw in `src/heretic/d_main.c`, the
bottom-screen automap and its stride in `src/heretic/am_map.c`, the status
bar drawable into either buffer and the menu-close status bar refresh in
`src/heretic/sb_bar.c` and `src/heretic/mn_menu.c`, the 3D depth slider and
keyboard save names in `src/heretic/mn_menu.c`, config variables in
`src/m_config.c`, control and sample-rate defaults in `src/m_controls.c` and
`src/i_sound.c`, and module registration in `src/i_sound.c` and `opl/opl.c`.

Networking is compiled in but has no transport, so only single player works.

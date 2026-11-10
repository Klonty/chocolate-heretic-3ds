# Heretic for the Nintendo 3DS

A port of Chocolate Doom's Heretic to the Nintendo 3DS, with stereoscopic
3D on the top screen and a live automap on the bottom one. Everything
game-related is the unmodified Chocolate Heretic code; this directory holds
the 3DS platform layer that replaces SDL.

## Requirements

- A 3DS or New 3DS with custom firmware (Luma3DS) and the Homebrew Launcher.
- A Heretic IWAD: `HERETIC.WAD` from your own copy of the game, or the
  freely distributable shareware `heretic1.wad`. The 2025 re-release's
  `heretic.wad` is not a vanilla IWAD and will not work; GOG ships the
  original under `dos/base/heretic/`.
- For sound: `sdmc:/3ds/dspfirm.cdc`, the DSP firmware dump that all 3DS
  homebrew needs for audio. Dump it on your console with the DSP1 homebrew.

## Installing

1. Copy `heretic.3dsx` to `sdmc:/3ds/heretic/heretic.3dsx`.
2. Copy your IWAD to `sdmc:/3ds/heretic/` (`HERETIC.WAD` or `heretic1.wad`).
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

Every in-game button can be rebound in `heretic.cfg`: `3ds_button_a`,
`3ds_button_b`, `3ds_button_x`, `3ds_button_y`, `3ds_button_l`,
`3ds_button_r`, `3ds_button_zl`, `3ds_button_zr`, `3ds_button_dup`,
`3ds_button_ddown`, `3ds_button_dleft`, `3ds_button_dright` and
`3ds_button_select` hold Doom key codes (the same values the `key_*`
entries use). Menu keys are fixed.

## Bottom screen

While playing, the bottom screen shows the automap, following the player,
with the inventory bar beneath it. Set `bottom_screen 0` in `heretic.cfg`
to turn it off.

## Stereoscopic 3D

With the 3D slider up, the game renders the 3D view twice per frame with the
camera shifted left and right. The status bar, messages and menus stay at
screen depth, and the weapon sits just in front of it.

The depth is set with the **3D DEPTH** slider in Options → More (the second
options page); the hardware slider scales it. It is stored as
`stereo_separation` in `heretic.cfg` (eye separation at full slider, in 1/16
map units, 0–240, default 96). `stereo_3d 0` ignores the hardware slider
entirely.

Rendering two views costs a second software render per frame. On a New 3DS
it runs at the full 35 fps; on an original 3DS, lower the screen size in the
options or play with the slider down.

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

Changes to the shared code are all under `#ifdef __3DS__`: the stereo camera
offset in `src/heretic/r_main.c`, the weapon sprite shift in
`src/heretic/r_things.c`, the two-eye render and bottom-screen draw in
`src/heretic/d_main.c`, the bottom-screen automap and inventory in
`src/heretic/am_map.c` and `src/heretic/sb_bar.c`, the 3D depth slider and
keyboard save names in `src/heretic/mn_menu.c`, config variables in
`src/m_config.c`, control and sample-rate defaults in `src/m_controls.c` and
`src/i_sound.c`, and module registration in `src/i_sound.c` and `opl/opl.c`.

Networking is compiled in but has no transport, so only single player works.

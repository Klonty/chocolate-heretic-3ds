# Chocolate Heretic for the Nintendo 3DS

A port of [Chocolate Heretic](https://github.com/chocolate-doom/chocolate-doom)
(which is [Chocolate Doom](https://www.chocolate-doom.org/)'s accurate Heretic
engine) to the Nintendo 3DS.

- The 3D view is rendered at the full 400x200 and scaled to the top screen,
  with stereoscopic 3D for the whole 3D view and the HUD at screen depth.
- The bottom screen carries the status bar, the inventory bar and a live
  automap.
- Sound effects play on the NDSP hardware channels, music is emulated OPL.

Everything game-related is the unmodified Chocolate Heretic code. The 3DS
platform layer lives in [`port3ds/`](port3ds/) and replaces SDL with libctru;
all changes to the shared code are behind `#ifdef __3DS__`.

**Full documentation, controls, configuration and build instructions:
[`port3ds/README.md`](port3ds/README.md).**

## Quick start

1. Install [devkitPro](https://devkitpro.org/wiki/Getting_Started) with the
   `3ds-dev` package group.
2. Build:

   ```
   cd port3ds
   make
   ```

3. Copy `heretic.3dsx` and a vanilla Heretic IWAD to `sdmc:/3ds/heretic/` on
   your SD card and start it from the Homebrew Launcher.

## Status

- Built and played on a **New 3DS** (Luma3DS + Homebrew Launcher). The
  **original 3DS is untested**; expect lower frame rates, especially with
  stereo 3D enabled.
- Needs a vanilla Heretic IWAD: `HERETIC.WAD` from the original 1.3 release
  (GOG's `dos/base/heretic/HERETIC.WAD`, MD5 `66d686b1ed6d35ff103f15dbd30e0341`)
  or the shareware `heretic1.wad`. The 2025 KEX re-release is **not** a
  vanilla IWAD and is rejected at startup.
- Audio needs `sdmc:/3ds/dspfirm.cdc`, the DSP firmware dump (dump it with the
  DSP1 homebrew). Without it the game runs silently.
- Single player only: networking is compiled in but has no transport.
- Music is OPL emulation (DBOPL, 22050 Hz). When a song is first played, a
  temporary `doom.mid` is written to the data directory and removed again.

## Licence

GNU GPL v2, like Chocolate Doom — see [`COPYING.md`](COPYING.md). Heretic is a
trademark of its respective owners; you need your own copy of the game.

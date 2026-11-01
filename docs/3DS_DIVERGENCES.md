# OpenSWOS 3D — how this fork differs from OpenSWOS, and why

> **Status: current as of 2026-11-01 (commit `f3faca1`).** Most of what is described here has
> been *built and tested on a PC*; the parts that need an original/New 3DS to prove are marked
> **[unverified on hardware]**. Fix this file when you change what it describes.
>
> `docs/3DS.md` is the player's guide (installing, seasons, controls). This file is for whoever
> maintains the code: what we changed in the upstream tree, and the reasoning that led there.

**Upstream** is [OpenSWOS](https://github.com/benbaker76/OpenSWOS) by Ben Baker and contributors. This fork
(`Znidz/OpenSWOS3DS`, branch `stereo3d`) branched at upstream commit **`67197ef`** (2026-10-28,
"leagues: the Coppa Italia Serie D…"). Since then the fork has **36 files changed, ~4,770 lines added,
16 removed**: 17 new files, 19 upstream files touched. It does not change the match simulation.

Contents: [1 Principles](#1-principles) · [2 What is new](#2-new-files) · [3 What we touched upstream](#3-upstream-files-we-touched) ·
[4 Decisions, by area](#4-decisions-by-area) · [5 Ideas tried or rejected](#5-ideas-investigated-and-not-built) ·
[6 Settings and log lines](#6-reference-settings-and-log-lines) · [7 Open items and known stale text](#7-open-items-unverified-things-and-known-stale-text) ·
[8 Staying in step with upstream](#8-staying-in-step-with-upstream)

---

## 1. Principles

These are the rules the individual decisions follow. They were mostly arrived at by being wrong once.

1. **One source tree, hooks that cost one test elsewhere.** The 3DS is "the ordinary SDL build plus a
   few things" (`Makefile.3ds`). Changes to shared files are either `#ifdef __3DS__` or sit behind
   `g_stereoOn`, so every other target pays one branch at most. *Why:* upstream's own rule is that
   a shared file that only compiles on one target breaks the others in the same commit
   (`.claude/CLAUDE.md`, "Keep both targets building").
2. **Depth is never match state.** The 3D planes are written while drawing and read while presenting;
   the simulation never reads them, saves them or sends them. *Why:* it keeps the replay rig, the
   rollback registry and saves valid without re-proving them, and means a depth bug can only ever
   look wrong, never play wrong.
3. **Measure, then change.** Four performance ideas were rejected after measuring (section 5), and
   the per-frame log lines exist because guesses were wrong twice. When the log could not answer a
   question, the next build adds the line that can.
4. **The art is decided by the owner's screenshots, not by us.** The stands, boards, shadows, number
   and flag rules come from marked-up photos and from the on-console 3D checker's log, because the
   people who can see the 3D are the people holding the 3DS. Where we guessed (the first hoarding
   rows), we were off and had to re-measure.
5. **Cheat where the 3DS cannot keep up, but say so.** A fade is now a cut; the pitch is flat on the
   original 3DS; the whole-match recorder is off. Each is a conscious trade, recorded below.
6. **Never patch out a panic or guard a NULL to make something draw.** Inherited from upstream and kept:
   the 3DS crash on exit was root-caused from the crash screen's PC, not guarded away (section 4.9).

---

## 2. New files

| file | what it is | why it exists |
|---|---|---|
| `Makefile.3ds` | devkitARM + libctru + SDL2 build; makes `.elf`, `.cia`, optional `.3dsx` | the 3DS needs its own toolchain; kept beside `Makefile.amiga`/`Makefile.switch` as the other targets are |
| `.github/workflows/build-3ds.yml` | builds on every push to `stereo3d`, publishes a release | the owner has no devkitPro machine; the 3DS is installed from a phone via the release's QR code |
| `src/n3ds.c` | the top screen's two eyes, buttons, first-run data setup, quit handling, the frame-time log | the 3DS has no window: SDL's video is bypassed and the picture goes straight to the framebuffers |
| `src/n3ds_screens.c` | the 3D title scene, the touch screen (menus, radar, info), own 5×7 font | upstream has no second screen |
| `src/n3ds_debug.c` | the on-console 3D checker and live tuning page | fewer deployments (section 4.8) |
| `src/stereo.c`, `include/stereo.h` | the depth model: planes, per-row composition, config, probe | the 3D itself; compiled into every SDL build, **off unless asked** |
| `include/n3ds.h` | the 3DS hooks' declarations | |
| `packaging/3ds/*` | `.rsf`, banner, banner sound, icon, title logo | CIA metadata |
| `docs/3DS.md`, `docs/images/3ds/*` | the player's guide and its pictures | |
| `docs/3DS_DIVERGENCES.md` | this file | |
| `include/audio_tune.h` | the audio settings the checker's AUDIO page edits | 4.8 |

---

## 3. Upstream files we touched

All changes are guarded unless stated. `+/-` lines are against `67197ef`.

| file | change | why |
|---|---|---|
| `src/sprites.c` (+13) | `Stereo_BeginSprite` per queued sprite, `Stereo_SetRows`/`Stereo_MarkRow` per drawn row in `blitChunkySprite` | the only place that knows each sprite's pixels and rows; depth is recorded as they are drawn |
| `src/pitch.c` (+4) | `Stereo_PitchDrawn(fb)` after the pitch is drawn | copies the pitch into a plane of its own, so an eye can show the pitch behind a player who has moved aside |
| `src/animated_patterns.c` (+7) | `Stereo_BeginGround(1/0)` around the crowd tile draw | the crowd is part of the pitch drawing; without this it was treated as a sprite |
| `src/platform_sdl.c` (+77) | on 3DS: no window/renderer; `render_frame_impl` calls `N3DS_Present`; button polling and quit watchdog; `Platform_TakeMenuTap`; desktop red/cyan anaglyph test view | SDL video is unused on the 3DS |
| `src/main.c` (+47) | 3DS start-up order (screens first, `chdir("romfs:/")`, data setup), stereo enable and 400×240, block-buffered log, `SDL_Init` without video/joystick, bigger thread stack | the first-run setup must be able to draw progress; the log write cost (4.9) |
| `src/menu_system.c` (+43) | `MenuTapAt`: a tap selects the entry under it and presses fire | touch-screen menus without changing the menu code |
| `src/frontend.c` (+27) | `N3DS_Loading` progress calls through start-up | a ~1 minute silent start looked like a hang |
| `src/audio_sdl.c` (+39) | 22,050 Hz, 200 ms queue, pump thread above the game's priority on 3DS; single-owner shutdown; underrun counters; pump timer | section 4.7 |
| `src/fade.c` (+32) | on 3DS a fade runs its steps at once | section 4.6 |
| `src/replay_v2.c` (+6) | the whole-match recorder does not start on the 3DS | section 4.6 |
| `src/game_loop.c` (+4/−3), `include/raster_bars.h` (+18) | the Amiga's `RASTER` phase markers time the frame on the 3DS; `FPROF_*` side timers | the `[frame]` log line, section 4.6 |
| `src/input_sdl.c` (+8) | `InputSDL_TouchSkip` — the B button as the "skip" button | the 3DS has B; the web build's hook only had fire |
| `src/swos_io.c` (+2) | the user folder is `sdmc:/3ds/OpenSWOS3D` | where 3DS homebrew keeps its files |
| `include/miniz_swos.h` (+4) | keep miniz's file API on the 3DS | the linked-in data setup reads the player's `.zip` downloads from the SD card |
| `tools/swosdata/…` (`sd_sources.c/.h`, `swosdata_cli.c`) | `overDir` / `--teams DIR`: a season's team files over the Amiga copy's | "seasons from team data alone" (4.2) |
| `README.md` | the front page is now the 3DS fork's, links upstream prominently | the fork must not read as the official project (4.10) |

---

## 4. Decisions, by area

Each item is *problem → what we found → decision → alternatives*.

### 4.1 Building and CI

- **The `.cia` is made from the `.elf`; the `.3dsx` is optional.** `3dsxtool` rejects the unaligned
  relocations in the game's packed, Amiga-layout structures (pointers at 2-byte offsets inside `Sprite`
  and friends). `makerom` takes the ELF as it is. *Alternative:* re-align the structures — rejected, that
  is the Amiga-memory-layout work upstream has planned as a whole (`CLAUDE.md`, roadmap step 3), not
  something to start for a launcher format.
- **`int32_t` is `long` on `arm-none-eabi` but `int` in 25 game headers.** `Makefile.3ds` makes the
  compiler's own 32-bit type `int` (`-U__INT32_TYPE__ -D__INT32_TYPE__=int`). Same size and ABI, so
  libctru/SDL linked with `long` still link. *Alternative:* edit 25 headers — rejected as a merge hazard.
- **SDL2 is built from source in CI** (`release-2.30.x`). devkitPro packages only SDL 1.2 for the 3DS;
  SDL2 supports the 3DS upstream. Static, into the portlibs. libctru is the image's own; the package
  server is used only if it is missing (several early CI commits were exactly this).
- **`-Wl,--wrap=mkdir`.** libctru's `mkdir("sdmc:")` fails on a bare device name, which broke the data
  setup's folder-by-folder creation ("cannot create the folder"). The wrapper counts a device name as
  existing. *Alternative:* patch the converter's path walk — rejected, the converter is shared with the
  PC setup.
- **Flags: `-march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft -O2`**, plus upstream's
  `-fno-builtin` and warning set, `-DSWOS_TEST_RULES=1 -DSWOS_SAVE_FORMAT_JSON=1`. `-fno-builtin` is
  inherited, not chosen: it stops the compiler inlining small `memcpy`/`memset`. **Trying without it, or
  `-O3`/LTO, is an open A/B** (section 7) — the 16 KB instruction cache can make bigger code slower.
- **Netplay is not compiled** (`Makefile.3ds` has no `NETPLAY`), so the rollback registry, GGPO and the
  lobby are absent. The match-report/recorder machinery that came with them is dead weight we left alone.
- **CI releases.** Tag `stereo3d-<run number>`; the notes list commit subjects since the last release
  (a tester should read what changed); the QR code is **drawn in the job** (`segno`) and attached, because
  a hot-linked QR image failed to show once; `OpenSWOS3D.symbols.txt` is attached so a crash screen's PC
  can be looked up (section 4.9). The workflow triggers on pushes to `stereo3d` only.

### 4.2 Boot, data and seasons

- **The data setup runs on the console.** `tools/swosdata` is linked into the game: the first start
  converts the player's own ADFs into the `assets/` folder on the SD card (~1 minute), then never again
  (`assets/ASSETS.ini`). *Why:* no PC step for a tester, and the original game's data is never in the app
  or the repo (upstream's rule since 2026-10-19).
- **A loading screen with a progress bar** (`N3DS_Loading`) draws the title scene on the top screen
  between the heavy start-up steps. *Why:* start-up was silent for long enough to look like a crash.
- **Seasons.** The SEASON button lists folders in `sdmc:/3ds/OpenSWOS3D/seasons/`; picking one restarts
  into it (the choice is `season.txt`). A season folder may hold only **team files** (`TEAM.000…`,
  `POOLPLYR.DAT`, `.TMD`): the converter's `SdSources.overDir` reads those *in place of* the Amiga copy's
  and everything else (graphics, sound, menus) still comes from the 96/97 disks. *Why:* the community
  season updates (sensiblesoccer.de) are mostly team data, and shipping whole games per season would
  triple the SD card use. Careers and saved competitions belong to the season they started in.

### 4.3 The display pipeline

- **No SDL video.** The picture goes from the 8bpp frontbuffer straight into the two RGB565 eye
  framebuffers. *Why:* the SDL texture path would add a conversion and a copy per frame the 3DS cannot
  spare, and the 3DS's two eyes are separate framebuffers anyway.
- **The framebuffers are the screen turned on its side** (pixel `(X,Y)` is at `fb[X*240 + 239-Y]`).
  Writing a row at a time touches a new cache line per pixel, so rows are taken **8 at a time and each
  column's 8 pixels written together** (4 × 32-bit words); a later pass writes **two pixels per store**
  with a slow path only for the columns that merge two source pixels. Output is byte-identical to the
  simple loop (tested).
- **Render 400×272, show 400×240.** The game's height is fixed (272); the width is the 3DS's own 400, so
  horizontal scaling is 1:1 and only the vertical 272→240 is resampled. Where an output row covers two
  source rows, **the brighter one wins above a luma of 190** — a one-pixel white line (a touchline) used
  to fall in a dropped row and vanish.
- **The screen-pinned HUD (score, names, numbers) is drawn 1:1** over the scaled picture (`Stereo_HudRects`),
  at its own depth. *Why:* resampling small text 272→240 made it unreadable. This is why the sub-pixel
  scroll's separate HUD layer is switched off on the 3DS: the depth planes cannot see a second picture.
- **Original 3DS: outside a match every other frame is shown** (a menu does not need 60), **in a match
  every frame is.** An earlier version dropped match frames as well and the game ran visibly slow
  (`481b177`); the log's *shown/s* beside the *game clock* is how this was found.
- **The touch screen is drawn in memory and copied once** (`s_shadow`), and only when something changed.
  *Why:* drawing straight into the live framebuffer tore the radar. Menus are shown **pixel-exact** when
  they fit 320×240 (`pick_menu_window`; every upstream menu does), scaled bilinearly otherwise. The radar
  redraws at ~15 Hz.

### 4.4 The stereo model (`src/stereo.c`)

- **Planes, not a second render.** While stereo is on, every draw records, per frontbuffer slot, a
  `bg` plane (the pitch), a `stamp` (what the draw wrote) and a `dep` plane (its depth), plus per-row
  spans of where sprites drew. At present time each eye's row is the pitch shifted by its own depth with
  everything over it shifted by *its* depth, nearest last. *Alternative:* render the match twice from two
  cameras — rejected: the game is a sprite blitter on one 2D picture, so a second render would double
  the simulation-side drawing the 3DS cannot afford and would have needed hooks all through the draw code.
- **Depth is in 1/8 px of disparity;** the left eye reads `x+shift`, the right `x−shift`, clamped at
  `limit` (96 = 12 px). The slider scales it; at zero 3D is off.
- **The pitch is a plane tilted away from the viewer** (`pitchFar` −48 → `pitchNear` 0). **On the original
  3DS the tilt is switched off** (flat at −16) unless `stereo.ini` sets other pitch depths: the tilt costs
  a per-row shift, and it was half the per-row work.
- **Players are one card at their feet plus a lean** (`heightGain`), not a depth growing row by row.
  Row-by-row growth *sheared* players (legs vs head in different planes) and, measured from the sprite's
  anchor rather than its feet, flattened the legs. Standing figures on the terraces take their base depth
  from `terrain_rise_y`.
- **The ball takes its height from the gap between it and its shadow on screen**, drawn as a card at the
  shadow like a player and raised by that gap. A ball on its shadow sits exactly on the ground plane.
  *Why:* the ball's `z` is not its screen distance; the shadow is the one reliable "where is the ground
  under it" the draw gives us. Three earlier versions (`8c27e44`, `10d5458`, `2770b25`) got here by watching
  it float, sink, then stand.
- **Goals are found by sprite name** (`_goal1TopSprite`, `_goal2BottomSprite`) and their height is measured
  **from the goal line** (`s_groundRow`), not from the bottom of their picture, which includes the net
  behind the line. *Why:* measuring from the picture's bottom lifted the net off the ground.
  **Open:** the bottom goal's net hangs *below* its line and the crossbar is above it, so the rule that is
  right for the top goal probably under-lifts the net's base at the bottom. We have measured it (bar ≈ +6
  over the pitch tapering to ≈ +2 at the net's lower edge) but not changed it; waiting for the owner to say
  what looks wrong.
- **Menus are 3D without touching menu code.** `DrawMenu` paints from `_frontbufferMaskBuffer` and erases by
  copying it back, so at present time *that buffer is the backdrop plane.* The backdrop sits just behind
  the screen, anything that differs from it (text, pictures) floats in front, coloured entries stand out
  as slabs, the selected one furthest. A frame whose picture is mostly not that buffer is shown flat.
- **Per-sprite rules** (newest; **[unverified on hardware]**, all tunable on the TUNE page):
  - **Shadows are flat on the pitch.** Sprite pixels in palette index **8** get the ground depth at their
    own row (`shadow_idx`, 0 turns it off). *Evidence:* every sprite pixel the owner set to pitch level
    in the checker (≈45 marks) is index 8. *Risk:* index 8 is also used elsewhere on some sprites.
  - **Player number and marker are one flat depth**, the player's head height plus `number_lift` (default
    32 = two whole-pixel steps) — the owner's decision. *Why it was split:* a per-row depth rounds to whole
    pixels per eye, so a small sprite crossed a rounding step mid-number.
  - **Corner flags stand like a player** (upright, same lean) — the owner's decision after we first made
    them flat. A pole is as tall as a player.
  - **A figure at an advertising board is sorted against it by where its feet are:** feet at or below the
    board's mid-row → in front (drawn over it, depth ≥ the board's); above → behind (its legs go behind
    the board). *Evidence:* at the top end figures' feet are above the board, at the bottom end below it
    (owner's screenshot). Figures used to be absorbed into the board.

### 4.5 Stands, boards and crowd (art decisions from the owner's photos)

- **Stands as built, not as a curve.** The owner described the real structure: a 3 px front wall, four rows
  of people each a step higher, a ~5 px wall between tiers, then more rows. `s_riseTab[]` is built from
  `stands_start`, `wall1_rows/rise`, `people_rows`, `row_h`, `row_step`, `wall2_rows/rise` (defaults in
  section 6). The older smooth `tier_rise` is still used for the **side** stands. *Alternative:* the first
  version — a stepped height per 14-row tier — looked like terraces cut in the wrong places.
- **The front wall starts at world row 73 (was 74: the heads of the figures standing at the foot of the stands are on row 73, and at 74 they got the wall's depth, 5 units nearer than their own feet — 2026-11-01 marks).** Starting earlier raised the grass strip *behind* the boards
  (the row of grass between wall and board); at 74 it stays on the ground.
- **Advertising boards are a wall standing on the pitch:** each row up its face is a little further out
  (`hoard_rise` = base, `hoard_slope` per row in tenths of 1/8 px). Defaults are the owner's marks:
  ≈ +10 at the base and ≈ +34 at the top row of the top board, i.e. base 26 over the ground and 2.4 per
  row. *Earlier* the board was one depth plus a steep per-row term, which split it between two levels.
- **Board rows are measured, not guessed.** Top end world rows **89–103** (was 92: a 2026-11-01 screenshot showed the top 3 rows of the board at the stands' depth); the bottom end was first
  796–807 and the first estimate **missed the real board** — the owner's marks at rows 808–812 were
  classed as pitch. Now **800–812**. *Alternative:* find the board by its colour at run time — rejected as
  slower and fragile across pitch types. If a pitch type has the boards elsewhere, these four settings are
  the fix (`HOARD TOP/BOTTOM`, `HOARD 2 TOP/BOT`).
- **Edge characters on the grass strip behind the boards** are unchanged and still look wrong
  (listed in section 7).

### 4.6 Performance — what was found and what was done

**Frame budget (log of build `5a63c1f`; build `d5bdb5d`'s `[frame]` line then gave: undraw+scroll 9.0 ms, game logic+crowd+sprites+HUD ~2 ms, present ~27 ms of which composing ~11, writing ~12, bottom screen 1.9; audio 0 underruns, original 3DS, 3D on).** ~26.5 game ticks/s → **~37.7 ms a frame**;
the present call is **~27 ms** of that (bottom screen 1.9, top screen 22–25); everything the game itself does
is **~10–11 ms.** So the original code is at most about a quarter of a frame — which is why most of the
work is on the present path, and why dropping original code was judged secondary.
(An earlier figure of "45 ticks/s, 11.6 ms" was from a build where the 3D slider may have been off, so it
is not a regression baseline.)

Built (each is output-identical where it can be, and tested):

| change | evidence | result |
|---|---|---|
| both eyes composed in one pass (`Stereo_ComposeRowPair`) | per-eye loops duplicated the pitch copy and sprite pass | ~5× faster compose |
| look only at where sprites were drawn (row spans) | the pass visited every pixel of every row | ~29× faster on a PC |
| top screen written column-tiled, two pixels per store | sideways framebuffer, cache-line per pixel | identical output, faster write |
| stands rows and side strips as runs of copies | host benchmark: a stands row cost 5–25× a pitch row | side strips ~2.5× cheaper, output hash identical over 800+ camera/strength/setting combinations |
| touch screen drawn in memory, copied once and only when changed; menus redraw changed rows only | tearing; wasted redraw | no tearing, fewer copies |
| fades finish at once on the 3DS | game clock is ~half speed, and the timer thread rendered full 3D frames of a fade nobody sees; two fades per replay scene | **[unverified on hardware]** |
| pitch redrawn a clipped row of a tile at a time (`DrawChunkyPitch`, `src/pitch.c`) | `[frame]` of build d5bdb5d: undraw+scroll was a constant **9.0 ms** a frame; on SDL the whole pitch is redrawn every frame (the Amiga blitter scrolled it), with two bounds checks per pixel | output identical to the old loop over 16,000 camera/width cases; **[unverified on hardware]** |
| plane copy clears only the HUD rows (`Stereo_PitchDrawn`) | 3.1 ms of that 9 ms; the HUD mask is set on a few dozen rows only | stereo golden hash unchanged |
| whole-match recorder off on the 3DS | built and deflated the file at full time (seconds on an original 3DS) and held ~1.6 MB+ all match | **[unverified on hardware]** |

*What the fork leaves alone:* the game's own **instant replay and highlights** use their own ring buffer
(`SaveCoordinatesForHighlights`) and still work; only the OpenSWOS `.SRP` recorder is off.

*A correction recorded for honesty:* the slow windows in the log (clock near 0 ticks/s) were first read as
replay-transition stalls. 13 of 17 start within 8 lines of a `[3dcheck]` line — the **checker freezes the
game clock by design.** Only lines ~20, 33, 380 and 806–816 of that log are plausibly real.

### 4.7 Audio

- **22,050 Hz on the 3DS** (44,100 elsewhere). The music is *synthesised on the CPU* (RJP1 player); at 44.1 kHz
  it could not keep the queue full beside the 3D drawing, which was the crackle. Half the rate halves the
  mixing; the Amiga's own output was near 28 kHz.
- **Queue target 200 ms** (80 elsewhere) to ride out a long frame.
- **The pump thread runs above the game's priority** (`svcSetThreadPriority(…, 0x19)` vs the game's `0x30`).
  One core runs both; at the game's own priority the pump only got a turn when the picture was finished.
- **The crackle was the device buffer (fixed 2026-11-01).** Both phone clips showed a click every 139.3 ms — three
  1,024-frame buffers at 22,050 Hz. The owner set DEVICE BUFFER to 512 on the checker's AUDIO page and the crackle went;
  512 is now the 3DS default (`s_deviceSamples`). The cause inside the driver is not known; 256 was not tried.
- **Diagnostics, not a fix:** the owner reported "a regular rhythm interrupting the audio". The log now
  shows the device settings and **underruns / low-queue refills every 5 s**, and the pump's share of a core.
  In the one log checked, 4 of 89 windows had a single underrun — no steady cause found. **Open.**

### 4.8 Debug tooling (so fewer deployments)

- **The 3D checker** (L + SELECT in a match) freezes the frame and shows it zoomed with a cursor. It reports
  what a pixel *is* (pitch, sprite, hoarding, stands, HUD, overlay) and its depth, and lets the owner set the
  depth they *want*: **point**, **pixel** (a zoomed pixel with its neighbours' palette indices) and **line**
  (two ends). Everything is logged as `[3dcheck]` lines the maintainer can read back. *Why:* each art
  question used to cost a build, an install and a screenshot round trip. The checker pauses the game, so
  its windows distort the frame-rate log — see 4.6.
- **The AUDIO page** (X again, after TUNE; `include/audio_tune.h`, `src/audio_sdl.c`) changes the sound's settings
  *while it plays* and saves them to `audio.ini` beside `stereo.ini`: **device rate** and **device buffer** (the device is
  closed and reopened; the log says what the driver gave), the **queue** length, the **pump's wait and priority**, the
  per-voice **low-pass** (`g_rjp1LowPass`, `src/rjp1.c`), **mono**, **mixer on/off** (is the synthesis what costs the
  time?) and a **test tone** (440 Hz, 1 kHz, or a click every 100 ms) that replaces the mix. The tone is the key test:
  if it crackles the fault is the device or its driver, not the synthesis. The pump keeps running while the checker has
  the game frozen, so the music can be listened to as each setting changes; live numbers (queue now, ticks per second —
  50 is real time — silence pads, underruns) are on the page. *Why:* the clip analysis found a click repeating every
  139.3 ms, which is three 1,024-sample buffers at 22,050 Hz; the buffer-size test (1024 → 2048 should double it) needed
  a build per guess before this.
- **The TUNE page** edits `stereo.ini` values live on the console and saves them. The list is in
  `Stereo_Params` (section 6).
- **`[frame]`** (new): milliseconds per frame by phase — undraw+scroll, sim, crowd, sprites, HUD,
  wait+present — plus the 3D plane copy, the recorder and the audio pump's share. It reuses the Amiga
  profiler's `RASTER()` markers, which compile to nothing elsewhere. **[first log pending]**

### 4.9 Quitting, crashing and the log

- **Quitting must never hang or crash.** HOME-menu close and the in-game exit used to hang or die.
  `N3DS_QuitWatchdog` starts a sequence after 2 s: stop drawing, stop the audio, hand the screens back,
  flush the log, `svcExitProcess`; a **9 s backstop thread** ends the process regardless.
- **The crash on exit was root-caused, not guarded.** Luma's exception screen gave PC `0x0028C14C`, FAR
  `0x10` (a data abort in `_free_r`); CI publishes a sorted symbol list so `PC − 0x100000` maps to a
  function. Cause: the quit watchdog and the normal shutdown both freed the audio mutex/device. Fix:
  `AudioSDL_Shutdown` has a single owner (an atomic test-and-set), and the watchdog ends with
  `svcExitProcess` instead of `exit()`. **[unverified on hardware]** (upstream's rule: never add a NULL
  guard for a crash like this).
- **The log is block-buffered** (16 KB) and flushed every 5 s and before the watchdog. *Why:* every
  unbuffered line was an SD-card transaction (too slow per line), but block-buffering alone lost the tail
  of a session on a crash.

### 4.10 Input, touch and attribution

- **Buttons are read from HID directly** into the input layer's on-screen-controls hook (the one the web
  build's touch controls use); the SDL joystick subsystem is not initialised, so they are not seen twice.
  Circle Pad/D-Pad move, A fire, B fire/skip (`InputSDL_TouchSkip`), START pause, SELECT statistics,
  Y instant replay, X highlights, L+R quit the match.
- **A tap on a menu entry selects and fires it** (`MenuTapAt`): the last entry drawn wins where two overlap;
  invisible, disabled or action-less entries are ignored.
- **Attribution.** The README's front page is the fork's and says plainly it is unofficial, links upstream
  first, and credits Sensible Software, Ben Baker and Zlatko Karakaš; the credits scroll on the title screen
  and are in the info page and the release notes. Upstream's logo and front page were replaced, not
  merged. The original game's data is not in the repo or the app.

---

## 5. Ideas investigated and not built

| idea | outcome |
|---|---|
| skip 3D work on rows that have not changed | a delta sampler (1 frame in 64, 16-byte blocks) found **1–79 %** unchanged, far too variable to rely on; the sampler is still in the log (`[delta]`) |
| build the 3D from the delta against the last frame | same evidence; also the scrolling camera shifts the whole pitch most frames |
| cache the pitch rows | measured: a mid-pitch row already costs ~0.10 µs on the host vs 0.08 with the stands off — nothing to save; the cost was in the stands, which got run-based copies instead |
| a linear palette fade instead of per-step fades | the fades already are palette steps (`ApplyFadeStep` on the copper list); the cost was waiting for game ticks, so the fix was to stop waiting |
| cheaper sprite drawing in the game itself | **unmeasured** until the `[frame]` line exists; the original game code is ≤ ~¼ of a frame |
| a second render for the other eye | rejected in 4.4 |
| flat corner flag | tried (`e927dc8`), reversed by the owner (`752cd87`) |

---

## 6. Reference: settings and log lines

**`sdmc:/3ds/OpenSWOS3D/stereo.ini`** — written with defaults on first run; **an existing file keeps its old
values**, so delete it after an update that changes defaults. Units are 1/8 px of disparity; positive is
toward the viewer.

| key | default | meaning |
|---|---|---|
| `pitch_far`, `pitch_near` | −48, 0 (−16 both on an original 3DS) | the tilt |
| `height_gain` | 16 | a player's lean (per row, /64 px); half of it is the base lean |
| `hud`, `overlay` | 24, 16 | the HUD and untracked overlays |
| `limit` | 96 | max disparity either way |
| `menu_back/text/button/selected` | −8, 16, 32, 48 | menu depths |
| `stands_rise`, `stands_margin` | 48, 40 | side stands: height and distance past the lines |
| `tier_rows`, `tier_step`, `stands_max` | 14, 8, 40 | side stands' tiers |
| `goal_rise` | 0 | a goal's base lift (0: it stands on the grass) |
| `goal_height` | 24 | a goal is a box: its posts and crossbar are one upright card at the goal line up to this many rows; the net's roof recedes behind (top goal), the net below the line lies on the grass nearer (bottom goal) |
| `hoard_rise`, `hoard_slope` | 26, 24 | a board's base lift; gain per row up its face (tenths) |
| `hoard_y0..y3` | 89, 103, 800, 812 | the boards' world rows (top end, bottom end) |
| `stands_start` | 73 | the front wall's world row (top end; the bottom is mirrored) |
| `wall1_rows/rise`, `people_rows`, `row_h`, `row_step`, `wall2_rows/rise` | 3/16, 4, 5, 4, 5/24 | the stands' staircase |
| `shadow_idx` | 8 | palette index drawn flat on the pitch (0 = off) |
| `number_lift` | 32 | number/marker height over the head |

**Log lines** (`SWOS.log`, 5 s blocks unless stated): `[fps]` game clock vs shown frames and present ms;
`[present]` bottom screen / 3D rows+write / composing; `[audio]` device line at open, then underruns and
low-queue refills; `[delta]` share of unchanged 16-byte blocks; `[frame]` per-phase ms (new);
`[3dcheck]` the checker's opens, marks and parameter dumps; `[build]` the commit and "tester build".

---

## 7. Open items, unverified things and known stale text

**Unverified on hardware** (built and tested on a PC only): the fade cut, the recorder-off, the exit-crash
fix, the shadow/number/flag/board-sort rules, the new hoarding rows, the staircase stands, run-based stands
composition's on-device gain.

**Open problems**
- The **bottom goal's** net/crossbar geometry (4.4).
- **Edge characters** on the grass strip behind the boards.
- The **audio rhythm** (4.7) and any remaining **music crackle** — need a log with the `[audio]` and
  `[frame]` lines from a full match.
- Whether the **screen write** or the **row compose** is the bigger half of the 22–25 ms top-screen cost
  — `[present]` now splits them.
- **Compiler-flag A/B** (`-fno-builtin` off, `-O3`, LTO) — needs two builds on the console.
- **Platform builds not re-checked.** Shared-file edits are guarded, and `stereo.c` is compiled into every
  SDL build with its hooks off, but the **Amiga m68k and desktop builds have not been rebuilt in this
  fork** since the changes. Do that before merging anything back (upstream's rule).

**Known stale text**
- `docs/3DS.md` said 454 × 272 (fixed); it still
  says the depths in `stereo.ini` are only the menu ones.
- (fixed 2026-11-01) `README.md` linked `github.com/Znidz/OpenSWOS` (the old name); the repo is
  `Znidz/OpenSWOS3DS`.
- (fixed 2026-11-01) `.claude/CLAUDE.md`'s target table now lists the 3DS.
- `docs/3DS.md` had no pointer to this file before 2026-11-01.

---

## 8. Staying in step with upstream

- **Taken from upstream so far (cherry-picked with `-x`, 2026-11-01):** `b1552ca` the original's 16 menu colours,
  `28e362a` control rows say what they use, `5e68ad6` walked-off players past the original's screen are not
  drawn. **Left:** `461ac1b` (SWOS_WORLDS=0 and layout-dependent reads: a large world-engine change that needs the
  other `worlds:`/`leagues:` commits), the online/lobby/account work (no netplay here) and the world data.
  Upstream is `benbaker76/OpenSWOS`; `git remote add upstream` it to see what is new.

- Upstream's `src/sprites.c`, `pitch.c`, `animated_patterns.c`, `platform_sdl.c`, `main.c`, `menu_system.c`,
  `frontend.c`, `audio_sdl.c`, `fade.c`, `game_loop.c`, `replay_v2.c` are where merges will conflict; each
  fork edit is small and marked `__3DS__`, `g_stereoOn` or `FPROF_*`. Re-apply by searching for those.
- A hook that **moves** (a new sprite draw path, a new pitch draw) needs `Stereo_BeginSprite`/`MarkRow`
  (sprites) or `Stereo_PitchDrawn` (pitch) added there, or that thing is drawn at the overlay depth.
- Anything that changes **sprite palette indices** (new kits, the Jaguar/DOS art tiers) can break the
  shadow rule (index 8); `shadow_idx` is the switch.
- A new **depth setting** goes in four places: `StereoConfig` (include/stereo.h), the defaults array and
  `set_key`/`write_cfg` (stereo.c), and `k_params` (for the TUNE page). The defaults array is *positional*.
- Upstream's `RASTER_BARS` Amiga profiler and the 3DS's `[frame]` timing share the `RASTER()` macro; keep the
  `#elif defined(__3DS__)` branch.

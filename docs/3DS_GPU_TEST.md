# The GPU stereo test (tools/3ds_gputest)

**Status: current (2026-11-17).** A small stand-alone 3DS program, not the game. It answers one
question before anything in the game is rewritten: *can an original 3DS draw a stereoscopic
scene like ours as GPU quads fast enough for 50 ticks a second?*

## Why

The game's present step composes each eye on the CPU: about 25 ms a frame on an original 3DS
(docs/3DS_DIVERGENCES.md §4), which alone is more than the 20 ms a 50 Hz tick allows. The
alternative is the way snes9x_3ds's per-layer fork draws (its `STEREO_LAYERS.md`): build the
frame once as textured quads and let the GPU draw it once per eye, with only a horizontal shift
per quad differing between the eyes.

## What it draws

A stand-in for a match, in the game's own depth model, not its pixels:

- a pitch of 16x16 tiles from one atlas (stands and boards recede in bands, the pitch sits 8 px
  into the screen -- the game's flat -64 on an original 3DS);
- 22 / 44 / 66 players, each cut into 4 strips that lean out like the game's players, with a
  flat shadow on the grass;
- a ball that bounces and rises out of the screen.

Nothing in it is read from the game. The CPU only builds quad lists.

## Buttons

| button | does |
|---|---|
| A | 3D shift on/off (off draws both eyes unshifted: the cost without the effect) |
| Y | vsync on/off (off shows the headroom over 60 frames a second) |
| UP / DOWN | 22 / 44 / 66 players |
| LEFT / RIGHT | pitch quads 16 px or 32 px (4x fewer quads) |
| START | quit |

## Reading the result

The bottom screen and `sdmc:/3ds/OpenSWOS3D/gputest.log` (a `[gputest]` line every 5 s) give
quads a frame, frames a second, the CPU's build time, the time spent waiting, the slowest
frame, and the GPU's own times.

Run it with vsync **off** and 22 players, 3D slider up: the frame time then is what a
renderer like this would cost per tick. The game's logic and drawing into the buffers need
about 10 ms of the 20 a tick allows, so a frame here under about **8 ms** makes 50 ticks a
second plausible; over 14 ms does not. Send the log with the slider at the top and again at
zero.

## What it does not prove

- Converting the game's 8-bit pitch tiles and sprites into textures (once per match, once per
  kit) is not in it; it only shows the drawing side.
- A texture in the wrong orientation or colour order would show here first (the tiles are
  numbered by colour: red/blue players, green pitch, red-and-grey stands, blue boards).
- It draws through citro2d. A game integration would draw through the same libraries in place
  of SDL for the top screen, which is the larger change.

## Building

GitHub builds it on every push to `stereo3d` that touches `tools/3ds_gputest/`
(`.github/workflows/build-3ds-gputest.yml`) and publishes `OpenSWOSGpuTest.cia` and `.3dsx` as
a pre-release `gputest-<run>`, installable from the release page's QR code with FBI. Locally:
`make -C tools/3ds_gputest` (devkitPro with 3ds-dev).

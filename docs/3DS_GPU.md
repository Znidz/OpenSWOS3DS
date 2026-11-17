# The match's top screen on the GPU (3DS)

> **Status: first build, 2026-11-17. [unverified on hardware]** `src/n3ds_gpu.c`, `Stereo_GpuPlan` in `src/stereo.c`.
> Background and the numbers that led here: `docs/3DS_GPU_TEST.md`.

**Why.** On an original 3DS the CPU built both eyes pixel by pixel: compose about 13 ms + write about 12 ms
of every frame (the game ran 27-30 ticks/s, 50 is real time). The GPU test app showed 66 players on 16 px
tiles (1,512 quads) draw at 60 fps with ~3 ms of CPU. The picture is a pitch plane plus a few hundred small
pieces, so each eye is now a list of textured rectangles and the CPU never touches a per-eye pixel.

**What is unchanged.** The depth model: `stereo.c`, `stereo.ini`, the TUNE page. The plan comes from the same
planes (pitch, stamp, depth, spans) and the same rules (ground tilt, stands tiers, boards, lean, overlay depth,
HUD depth). The game, its drawing into the 8bpp frontbuffer and the simulation are untouched.

**How a frame goes** (`N3DSGpu_Present`, called from `present_impl` for match frames):

1. `Stereo_GpuPlan` walks the rows and returns *segs* (pieces of the pitch plane: x range, row range, shift
   `e`; equal neighbouring rows are merged) and *runs* (pieces of the frame's picture standing on it; merged
   downwards, sorted furthest-first by shift, so nearest draws last as the pixel compose does).
   A run is dropped where the pixel is behind what stands there (the old z-test against the pitch/stands/boards).
2. The CPU converts the pitch plane (RGB565) and the frame (RGBA5551, only the 8x8 tiles a run touches; clear
   where the pitch shows) into two textures, swizzled by hand; the HUD sprites go in a small atlas. Two sets
   of textures alternate so the GPU never reads what the CPU is writing.
3. Per eye: the segs (left eye reads the plane from `x - e`, right from `x + e`), the runs (left `+e`, right
   `-e`), the HUD (1:1, shifted by the HUD depth). 400x240 output from the 4xx x 272 frame by nearest sampling
   (a 1 px line can be lost on the rows that are dropped; the CPU path's "bright wins" rule is not reproduced).

**Fallbacks.** Any failure (no memory, a plan that does not fit, the checker open, a menu) draws that frame the
old way. R + SELECT in a match switches the GPU path off and on; the `[fps]` line says `gpu` or `cpu`.

**Log.** Every 5 s: `[gpu] N frames in T ms: convert, build+submit, wait, quads/frame, gpu ms, fallbacks`.

**Not done / to check on hardware.** Look of the pitch/stands/boards against the CPU path (R + SELECT to
compare), thin lines, anything flickering at piece edges, HUD placement, memory, and that the menu <-> match
switch is clean. Not yet moved: the stands/people could be drawn as fewer, taller pieces; the plane conversion
could skip rows that did not change.

## First hardware run (stereo3d-71, 2026-11-17)

Old 3DS, a match: game clock 34-37 ticks/s (was 27-30 on the pixel path), 33-36 shown/s, 0 fallbacks, ~250 quads
a frame, GPU 1-2 ms, building the quads 1.2 ms. **The cost is the CPU's texture conversion: 11.8 ms a frame**
(the pitch plane is 454x272 every frame because the camera scrolls). Fixed for the next build: two pixels per
32-bit write, timing split pitch / frame+HUD in the `[gpu]` line. Also seen: every rectangle's content came out
turned 180 degrees (score panel upside down and mirrored, statistics list reversed): the corners are now swapped.
Still to come if the conversion stays the bottleneck: a cached world-sized pitch texture scrolled by UV, with only
the animated parts (crowd, ads) re-uploaded.

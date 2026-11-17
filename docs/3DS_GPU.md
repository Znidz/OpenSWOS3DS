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

## Filtered pitch, and the CRT filter with a pause-screen switch

- **Pitch drawn filtered.** The 454x272 pitch plane is scaled to 400x240; nearest sampling dropped columns and rows
  (broken pitch lines, moire on the grass's fine pattern). The pitch texture now uses linear filtering; sprites stay crisp.
  The pixel path's "bright pixel wins" rule is not used on the GPU path.
- **CRT filter** = the ini's `[video] crt_mask` (off | scanlines | aperture | slot), `crt_mask_strength`, `crt_vignette`,
  `crt_corners` (same keys and maths as the desktop build: `CrtEffects_Fill`, shared with `crt_effects.c`). One 400x240 mask
  texture, multiplied over each eye (`C3D_AlphaBlend` dst*src), rebuilt only when a setting changes. Matches drawn by the
  GPU only (not menus, not the pixel fallback).
- **Pause screen** (`src/n3ds_crtmenu.c`, bottom screen while a match is paused with START): touch OFF / SCANLINES / APERTURE /
  SLOT, drag the STRENGTH / VIGNETTE / CORNERS sliders (tap a name to switch it off / on), or D-pad (up/down row, left/right
  change). Applies at once; resuming writes SWOS.ini (`Settings_Save` through `g_arcadeOpts`).

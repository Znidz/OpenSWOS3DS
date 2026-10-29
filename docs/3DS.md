# OpenSWOS 3D — the Nintendo 3DS, stereoscopic

A flavour of the SDL build (`Makefile.3ds`, devkitARM + libctru + 3ds-sdl) whose
match view is stereoscopic on the 3DS's top screen. GitHub builds it on every push to
`stereo3d` (`.github/workflows/build-3ds.yml`) and publishes `OpenSWOS3D.cia` and
`OpenSWOS3D.3dsx` as a release.

## Installing

1. Install the `.cia` with FBI (Remote Install → Scan QR Code on the release page
   works from a phone), or run the `.3dsx` from the Homebrew Launcher.
2. Copy your SWOS 96/97 Amiga disks (`.adf`, or a `.zip`/`.lha`/`.hdf` of the game)
   to `sdmc:/3ds/OpenSWOS3D/`.
3. Start it. The first start runs the data setup (`tools/swosdata`, linked in) on the
   console and writes `assets/` there; later starts skip it (`assets/ASSETS.ini`).

Sound needs the DSP firmware dump (`sdmc:/3ds/dspfirm.cdc`), as every 3DS homebrew does.

## How the 3D works (src/stereo.c, include/stereo.h)

While stereo is on, the draws that matter record a depth per pixel in planes beside each
frontbuffer: the pitch (`DrawChunkyPitch`, tilted away from the viewer), sprites
(`DrawQueuedSprites` → `blitChunkySprite`: players stand up row by row from their feet,
the ball rises with its `z`, its shadow lies flat, HUD sprites float in front) and the
animated crowd (part of the pitch). Anything else drawn over a match is told apart by a
colour stamp and gets the overlay depth. At present time each eye's row is the pitch
plane shifted by its depth, then everything over it shifted by its own, nearest last.
None of it is match state.

Menus are 3D too, without touching the menu code: `DrawMenu` paints its backdrop from
`_frontbufferMaskBuffer` and erases items by copying it back, so at present time that
buffer IS the backdrop plane. The backdrop sits just behind the screen, pixels that differ
from it (text, pictures) float in front, and the menu's own coloured entries
(`ENTRY_FRAME_AND_BACK_COLOR` with a colour) stand further out as slabs, the selected one
furthest. A screen whose picture is mostly not that buffer is shown flat.

`sdmc:/3ds/OpenSWOS3D/stereo.ini` holds the depths (written with the defaults on first
run): `menu_back`, `menu_text`, `menu_button`, `menu_selected` for the menus. The 3D slider scales them; at zero the 3D is switched off.

On a desktop, `SWOS_STEREO=1` shows the same two eyes as a red/cyan anaglyph.

## Platform notes

- The picture is the widescreen render at the 3DS's shape, 454 × 272, scaled to 400 × 240.
- SDL does the sound, timer and threads; `src/n3ds.c` owns the screens and reads the
  buttons from HID into `InputSDL_TouchControls` (the web page's on-screen-controls hook).
- `int32_t` is `long` on arm-none-eabi and `int` in the game's own Amiga-era typedefs;
  `Makefile.3ds` makes the compiler's 32-bit type `int` (same ABI on ARM).
- Buttons: Circle Pad/D-Pad move, A fire, B fire/skip, START pause, SELECT statistics,
  Y instant replay, X highlights, L+R quit the match.

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

## The two screens (src/n3ds_screens.c)

| | top screen | touch screen |
|---|---|---|
| menus | a 3D title scene: stands and pitch going into the screen, the OpenSWOS logo floating out of it, a bouncing ball, the credits scrolling | the menu: tap a button to choose it (`MenuTapAt` in menu_system.c selects it and presses fire). On the main menu, a SEASON button |
| matches | the match in 3D | both teams, the score, the minute, a radar of the whole pitch (kit-coloured dots, a white ring on the man you control, the ball bigger when it is in the air) |
| anything else | the game's picture | the controls and the credits |

## Seasons

The files of one season are the two ADFs, a hard disk image (`.hdf`) or a WHDLoad `.lha`,
alone or zipped. The ones in `sdmc:/3ds/OpenSWOS3D/` itself are one season; every folder
in `sdmc:/3ds/OpenSWOS3D/seasons/` is another, e.g.

```
sdmc:/3ds/OpenSWOS3D/seasons/9697/   the classic 96/97 disks
sdmc:/3ds/OpenSWOS3D/seasons/2425/   sensiblesoccer.de's swos2425.hdf
sdmc:/3ds/OpenSWOS3D/seasons/2526/   sensiblesoccer.de's 25/26, once its Amiga version is out
```

A season folder can also hold only a season's **team data** -- the `TEAM.000` ... files
(with `POOLPLYR.DAT` and the `.TMD`s if it has them), loose, in a `.zip`, or on an `.adf`
data disk. Those are gathered into `season_data/<folder>/` and played over the whole game
found elsewhere (the 96/97 disks first): the graphics, sound and menus are that game's,
the teams the season's (`SdSources.overDir` in tools/swosdata; `swosdata --teams DIR` does
the same on a PC).

The main menu's SEASON button (or holding L while the game starts) lists them: "CLASSIC
SWOS 96/97", "SWOS 24/25", ... -- a season the converter does not know yet is named from
its folder (`2526`, `25-26`, `SWOS2526` all read "SWOS 25/26"). Choosing one restarts the
game, which makes its data from that season (about a minute). The choice is in
`season.txt`. Careers and saved competitions belong to the season they were started in.

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

## Credits

**Sensible World of Soccer** is by **Sensible Software** (programming Chris Chapman,
graphics Jon Hare and Stoo Cambridge, music and sound Richard Joseph and the team; see
README.md for the full list) and is © Sensible Software and its current rights holders.
OpenSWOS 3D needs your own copy of the game and contains none of its data.

**OpenSWOS** is by **Ben Baker** (benbaker76) and the OpenSWOS contributors; the DOS
version was reverse-engineered by **Zlatko Karakaš**. OpenSWOS 3D is an unofficial fan port
built on it, under the same GPL v3. The credits scroll on the title screen and are on the
touch screen's info page.

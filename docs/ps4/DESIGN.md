# Space Cadet Pinball for PS4: design

Native PS4 port of [k4zmu2a/SpaceCadetPinball](https://github.com/k4zmu2a/SpaceCadetPinball), the
decompilation of 3D Pinball for Windows (Space Cadet) and Full Tilt! Pinball. Branch `ps4`, title id
**SCPB00001**, built on ps4-kit like the Blur port (native x86-64, no recompilation).

## Pieces

| Area | PC | PS4 |
|---|---|---|
| Window | SDL2 window | SDL2 dummy video driver, 1920x1080 or 1280x720 (Options → Resolution, `ps4.cfg`) |
| Drawing | SDL renderer (HW or SW) | SDL software renderer -> window surface -> `SDL_PS4_PresentFramebuffer` -> write-combined VideoOut buffers, a 1080p set and a 720p set the PS4 scales (`Platform/PS4/ps4_video.cpp`) |
| Menus | ImGui main menu bar, mouse | menu bar hidden; a pause menu on Options / touch pad: How to Play, Options, High Scores, About (`ps4_ui.cpp`) |
| Sound | SDL_mixer (WAV) | SDL_mixer 2.8.1, SDL's sceAudioOut driver (Conker's `sdl2-ps4.patch`) |
| Music | SDL_mixer MIDI | SDL_mixer's built-in Timidity with freepats in `/app0/timidity` (`make_timidity_cfg.py` fills the instruments freepats lacks) |
| Pads | SDL game controllers | DualShock 4s as SDL virtual game controllers fed from ScePad (`ps4_pad.cpp`, from Conker) |
| Files | working dir, exe dir, pref path | `/app0/` (data in the package), `/data/SpaceCadetPinball/` (data, settings, high scores, `log.txt`) |
| Message boxes | SDL | drawn with ImGui until Cross (`ps4ui::ShowMessage`) |

Every game-side change is behind `#ifdef __ORBIS__` (winmain.cpp, options.cpp, high_score.cpp,
pb.cpp, pch.h, SpaceCadetPinball.cpp), so the PC builds are unchanged.

SDL2 is 2.32.10 with `patches/sdl2-ps4.patch`: Conker's PS4 audio driver + virtual joysticks without
HIDAPI + 1080p dummy desktop, and the present hook in the dummy driver's framebuffer
(`include/SDL_ps4framebuffer.h`).

## Controls (DualShock 4)

| Button | Action |
|---|---|
| L1 or L2 | left flipper |
| R1 or R2 | right flipper |
| Cross | plunger (hold to pull, release to launch) |
| D-pad left / right / up | nudge the table left / right / up |
| Triangle | new game |
| Options or touch pad | pause menu (d-pad + Cross to pick, left/right to change, Circle to go back) |

Why: flippers on both shoulder pairs so either grip works (triggers are folded into L1/R1 at a light
pull, so the default "shoulder" bindings cover them); Cross is the natural "launch"; the d-pad nudges
match the PC's bump keys' directions. All of them can be changed in Options > Player Controls; the
game keeps them in `/data/SpaceCadetPinball/imgui_pb.ini`.

## Game data

Not included (Microsoft's). The game looks for `CADET.DAT` (Full Tilt) / `PINBALL.DAT` (Windows XP
Space Cadet) and the WAV files next to it in `/app0/` and then `/data/SpaceCadetPinball/`. Either
copy the files to the console's `/data/SpaceCadetPinball/` by FTP, or put them in `~/scp-build/data`
before building and they go into the package.

## Build

```bash
scripts/ps4/build-pinball.sh deps                  # SDL2 + SDL2_mixer + instruments, once
PINBALL_VERSION=01.00 scripts/ps4/build-pinball.sh # -> out/SpaceCadetPinball-iHaiDeeZ-v01.00.pkg
# PINBALL_NO_DATA=1 leaves ~/scp-build/data (the game's files) out: the package to share
```

## Menus and speed

The menus are ImGui windows drawn by SDL's software renderer. Large translucent ImGui fills are slow
there (each is blended as triangles, pixel by pixel): v00.04's menu crawled with four full-screen
layers. The menu's dark areas are therefore recorded as shades and applied to the frame's pixels in one
integer pass (`ps4ui::ShadeScreen`) after the table is drawn and before ImGui draws. Fonts are static
cuts of Exo 2 and Orbitron (`scripts/ps4/make_fonts.py`) in `/app0/fonts`.

## Open

- Name entry for high scores: no keyboard. The pad lands on OK and the default name is kept;
  the system keyboard (sceImeDialog, UTF-16) is a later step.

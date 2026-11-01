Tarnished HUD - build 6 - Elden Ring 1.17, 1.17.1, 1.17.1 JP, 1.17.2 and 1.17.2 JP (PC)
=======================================================================================

Lets you set the visual width of the HP, FP and stamina bars independently.
Visual only: it never changes your real HP, FP or stamina.

One package for every supported game version: eldenring.exe 2.7.0.0 (1.17),
2.7.1.0 (1.17.1), 2.7.1.1 (1.17.1, Japanese executable), 2.7.2.0 (1.17.2)
and 2.7.2.1 (1.17.2, Japanese executable).
The DLL detects the version and finds what it needs in the game by itself.
No menu files: the bar layout (the HUD part that lets the bars grow) is
applied in memory while the game loads it, so loaders that only load DLLs
work too. Offline only, with Easy Anti-Cheat disabled.

Installation
The archive has a DLLs folder with TarnishedHUD.dll and TarnishedHUD.ini.
Keep the two together. Do not load Adaptive Bar Scaling at the same time:
both change the same bars. If you had build 4 or older, remove its
mod\menu\01_000_fe.gfx (keeping it does no harm).
Tested in game: earlier builds with Mod Organizer 2. Builds 5 and 6 (1.17.2,
1.17.2 JP and the in-memory layout) were checked offline only, and Mod Engine 3, Mod Engine
2 and Elden Mod Loader are not tested: the steps follow each loader's own
setup. Each loader also has to support your game version. Reports welcome.

Mod Organizer 2 (earlier builds tested)
- Needs Elden Ring support in MO2 that loads DLL mods from a mod's DLLs
  folder. Install the archive as a normal mod and enable it:
  <MO2>\mods\Tarnished HUD\DLLs\TarnishedHUD.dll and TarnishedHUD.ini.
- The log (TarnishedHUD.log) is written to MO2's overwrite\DLLs.

Mod Engine 3 (not tested)
- Extract the archive next to your .me3 profile, for example into a
  TarnishedHUD folder: TarnishedHUD\DLLs\TarnishedHUD.dll (+ INI).
- Add it to the profile (path relative to the .me3 file), keeping your
  other entries:
    [[natives]]
    path = "TarnishedHUD/DLLs/TarnishedHUD.dll"
- Start the game with that profile.

Mod Engine 2 (not tested)
- Copy the DLLs folder into Mod Engine 2's mod folder:
  <Mod Engine 2>\mod\DLLs\TarnishedHUD.dll and TarnishedHUD.ini.
- In config_eldenring.toml, section [modengine], keeping DLLs you already
  load:
    external_dlls = [
      "mod\\DLLs\\TarnishedHUD.dll"
    ]
- Start the game with Mod Engine 2's launcher.

Elden Mod Loader (not tested)
- Elden Mod Loader goes in ELDEN RING\Game, next to eldenring.exe
  (dinput8.dll and mod_loader_config.ini), with a Game\mods folder.
- Copy the CONTENTS of DLLs (not the folder) into Game\mods:
  Game\mods\TarnishedHUD.dll and Game\mods\TarnishedHUD.ini.
- The bar layout must be in place before the game loads the HUD: if the
  bars stop short of their full length (the log says "01_000_fe.gfx was not
  opened after the hook"), lower load_delay in Game\mod_loader_config.ini
  (for example 0-1000).

Changing settings - no restart needed
- With NativeTweaks: System > NativeTweaks > Mods > Tarnished HUD, change the
  values and press Save; they apply in game at once.
- Or edit TarnishedHUD.ini: the mod re-reads it about once per second.
- Turning the mod or a bar off gives that bar its vanilla length back.
- An invalid value is rejected and the active settings stay; see TarnishedHUD.log.

Works with other mods that change the same HUD file in memory (NativeTweaks'
HUD options, Multiple Spirit Ashes): each one changes only its own parts.
If another mod has already changed the bar placements, Tarnished HUD leaves
the layout alone and says so in the log.
Calibrated at 2560x1440; other resolutions should work too.

Changes
- Build 6: supports the Japanese 1.17.2 executable (eldenring.exe 2.7.2.1).
  Before, the mod logged "Unsupported executable version" there and stayed
  off. Nothing else changed: same INI options, defaults and install steps.
- Build 5: supports 1.17.2 (2.7.2.0) in the same download as 1.17, 1.17.1 and
  the Japanese 1.17.1. The bar layout is applied in memory: no menu folder.

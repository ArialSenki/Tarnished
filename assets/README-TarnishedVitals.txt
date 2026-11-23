Tarnished Vitals - v6 - Elden Ring 1.17, 1.17.1, 1.17.1 JP, 1.17.2 and 1.17.2 JP (PC)
=====================================================================================

Regeneration, maximum and infinite options for HP, FP and stamina, each with
its own settings in TarnishedVitals.ini.

One package for every supported game version: eldenring.exe 2.7.0.0 (1.17),
2.7.1.0 (1.17.1), 2.7.1.1 (1.17.1, Japanese executable), 2.7.2.0 (1.17.2)
and 2.7.2.1 (1.17.2, Japanese executable).
The DLL detects the version and finds what it needs in the game by itself.
Offline only, with Easy Anti-Cheat disabled. Works together with Tarnished
HUD or Adaptive Bar Scaling (they only change the bars).

Installation
The archive has a DLLs folder with TarnishedVitals.dll and
TarnishedVitals.ini. Keep the two together.
Tested in game: earlier versions. v4 and v5 (one DLL for every version, 1.17.2, 1.17.2 JP) were
checked offline only; Mod Engine 3, Mod Engine 2 and Elden Mod Loader are
not tested with it: the steps follow each loader's own setup. Each loader
also has to support your game version. Reports welcome.

Mod Organizer 2 (earlier versions tested)
- Needs Elden Ring support in MO2 that loads DLL mods from a mod's DLLs
  folder. Install the archive as a normal mod and enable it:
  <MO2>\mods\Tarnished Vitals\DLLs\TarnishedVitals.dll and TarnishedVitals.ini.
- The log is written to MO2's overwrite\DLLs.

Mod Engine 3 (not tested)
- Extract the archive next to your .me3 profile, for example into a
  TarnishedVitals folder: TarnishedVitals\DLLs\TarnishedVitals.dll (+ INI).
- Add it to the profile (path relative to the .me3 file), keeping your
  other entries:
    [[natives]]
    path = "TarnishedVitals/DLLs/TarnishedVitals.dll"
- Start the game with that profile.

Mod Engine 2 (not tested)
- Copy the DLLs folder into Mod Engine 2's mod folder:
  <Mod Engine 2>\mod\DLLs\TarnishedVitals.dll and TarnishedVitals.ini.
- In config_eldenring.toml, section [modengine], keeping DLLs you already
  load:
    external_dlls = [
      "mod\\DLLs\\TarnishedVitals.dll"
    ]
- Start the game with Mod Engine 2's launcher.

Elden Mod Loader (not tested)
- Elden Mod Loader goes in ELDEN RING\Game, next to eldenring.exe
  (dinput8.dll and mod_loader_config.ini), with a Game\mods folder.
- Copy the CONTENTS of DLLs (not the folder) into Game\mods:
  Game\mods\TarnishedVitals.dll and Game\mods\TarnishedVitals.ini.

Changing settings
- With NativeTweaks: change the values on the Tarnished Vitals page and press
  Save; they apply in game at once.
- After editing TarnishedVitals.ini by hand, restart the game.
- Turning Infinite on or off always needs a restart.
- An invalid value saved in NativeTweaks is rejected and the active settings
  stay; at game start it leaves the mod off. See TarnishedVitals.log.

Changes
- v6: new INI option AllowUnknownGameVersion (General, off by default): 1 keeps the mod working on a game version it does not know yet, after a game update. Every piece is still found by signature and checked; if a check fails, nothing changes.
- v5: supports the Japanese 1.17.2 executable (eldenring.exe 2.7.2.1).
  Before, the mod logged "Unsupported executable version" there and stayed
  off. Nothing else changed: same INI options, defaults and install steps.
- v4: supports 1.17.2 (2.7.2.0) in the same download as 1.17, 1.17.1 and
  the Japanese 1.17.1. One DLL for every version; NativeTweaks Save applies in game.

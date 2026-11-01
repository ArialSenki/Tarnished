# Changelog

Same entries as the Nexus pages. Newest first.

# Tarnished HUD

[Nexus page](https://www.nexusmods.com/eldenring/mods/11123)

## Build 6

- Supports the Japanese Elden Ring 1.17.2 executable (eldenring.exe 2.7.2.1). Before this, Tarnished HUD logged "Unsupported executable version" on it and stayed off.
- Same download for 1.17, 1.17.1 and 1.17.2, worldwide and Japanese: the DLL detects the version by itself. The Japanese 1.17.2 has the same code layout as the worldwide 1.17.2, so the mod finds the same game data in both (checked offline against both executables).
- No other changes: same INI options, defaults and install steps. Updating: replace the DLL; your INI can stay.

## Build 5

- Supports Elden Ring 1.17.2 (eldenring.exe 2.7.2.0) in the same download as 1.17, 1.17.1 and 1.17.1 JP.
- No menu folder: the bar layout is applied in memory by the DLL, so DLL-only loaders like Elden Mod Loader work too. Remove the old mod\menu\01_000_fe.gfx (keeping it does no harm).
- Changes only the bar placements in 01_000_fe.gfx, so other mods that patch that file in memory keep their changes; if another mod already moved the bars, the layout is left alone (see TarnishedHUD.log).
- Install steps for Mod Organizer 2, Mod Engine 3, Mod Engine 2 and Elden Mod Loader.

## Build 4

- One download for Elden Ring 1.17, 1.17.1 and 1.17.1 JP: the DLL detects the game version and finds what it needs by itself.
- Live settings with NativeTweaks: change the values in game and press Save; they apply at once, no restart.
- The INI is re-read about once per second while you play, so manual edits apply without restarting too.
- Turning the mod or a bar off gives that bar its vanilla length back; an invalid value is rejected and the active settings are kept.

## Build 3

- Added support for version 2.7.1.1 JP Version (Should be the equivalent to 1.17.1)

## Build 2

- Added support for 1.17.1

## Build 1

- Added Support for 1.17

# Tarnished Vitals

[Nexus page](https://www.nexusmods.com/eldenring/mods/11105)

## Version 5

- Supports the Japanese Elden Ring 1.17.2 executable (eldenring.exe 2.7.2.1). Before this, Tarnished Vitals logged "Unsupported executable version" on it and stayed off.
- Same download for 1.17, 1.17.1 and 1.17.2, worldwide and Japanese: the DLL detects the version by itself. The Japanese 1.17.2 has the same code layout as the worldwide 1.17.2, so the mod finds the same game data in both (checked offline against both executables).
- No other changes: same INI options, defaults and install steps. Updating: replace the DLL; your INI can stay.

## Version 4

- One download for Elden Ring 1.17, 1.17.1, 1.17.1 JP and 1.17.2 (exe 2.7.2.0): the DLL detects the game version and finds what it needs by itself.
- Settings saved in NativeTweaks apply in game without restarting (turning Infinite on or off still needs a restart).
- Install steps for Mod Organizer 2, Mod Engine 3, Mod Engine 2 and Elden Mod Loader (the archive now has a DLLs folder).
- Same INI options and defaults as before.

## Version 3

- Added support for version 2.7.1.1 JP Version (Should be the equivalent to 1.17.1)

## Version 2

- Added 1.17.1 Support

## Version 1

- Added 1.17 support

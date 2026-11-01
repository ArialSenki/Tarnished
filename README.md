# Tarnished HUD and Tarnished Vitals

Two Elden Ring mods, both offline only with Easy Anti-Cheat disabled.

- **Tarnished HUD** sets the visual width of the HP, FP and stamina bars independently. It never changes your real values. [Nexus page](https://www.nexusmods.com/eldenring/mods/11123)
- **Tarnished Vitals** customizes health, FP and stamina through an INI file: maximum values and regeneration. [Nexus page](https://www.nexusmods.com/eldenring/mods/11105)

Both support Elden Ring 1.17, 1.17.1 and 1.17.2, including the Japanese executables, with one DLL.

## Downloads and changelog

The latest builds are on the [Releases](https://github.com/ArialSenki/Tarnished/releases) page and on Nexus. [CHANGELOG.md](CHANGELOG.md) lists every version, as on Nexus. Player instructions are in `assets/README-*.txt`, which ship in each download.

## Build

Windows x64 with the Visual Studio 2022 Build Tools (C++):

```
build.cmd
```

That produces `out\TarnishedHUD.dll`, `out\TarnishedVitals.dll` and `out\BarModsAudit.exe`. `package.ps1` builds the download zips into `release\`.

`BarModsAudit.exe <eldenring.exe>...` checks, without running the game, that each executable passes the same version, pattern and RTTI checks the HUD DLL uses.

## Layout

| Path | What it is |
|---|---|
| `src/TarnishedVitals.c` | Tarnished HUD |
| `src/gfx_patch.h` | The bar layout, applied in memory while the game loads its HUD |
| `src/vitals/TarnishedVitals.c` | Tarnished Vitals |
| `src/audit.c` | The offline audit tool |
| `assets/` | The INI and README of each mod |

No game files are included.

## License

[MIT](LICENSE). Elden Ring is a trademark of FromSoftware / Bandai Namco. These are fan-made mods, not affiliated with them.

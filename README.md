[![CI-Release](https://github.com/SkiierCo/PumpkinSpice/actions/workflows/ci.yml/badge.svg)](https://github.com/SkiierCo/PumpkinSpice/actions/workflows/ci.yml)

# PumpkinSpice

A Wii U plugin that loads Wii U Menu themes from the SD card. Theme files replace the Menu's own files while the
Menu runs; nothing on the console's storage is changed. Turn PumpkinSpice off (or remove the theme) and restart the
Menu to get the stock Menu back.

## Requirements
- Aroma with the [WiiUPluginLoaderBackend](https://github.com/wiiu-env/WiiUPluginLoaderBackend) and the
  [ContentRedirectionModule](https://github.com/wiiu-env/ContentRedirectionModule) in
  `sd:/wiiu/environments/aroma/modules`.
- The `titles` folder (EULA text, app icons) needs ContentRedirectionModule API version 2 or newer.

## Installation
Copy `pumpkinspice.wps` into `sd:/wiiu/environments/aroma/plugins`.

## Themes
Each theme is a folder in `sd:/wiiu/pumpkinspice/themes`:

```
sd:/wiiu/pumpkinspice/themes/<Theme name>/content/...                      Wii U Menu files (/vol/content)
sd:/wiiu/pumpkinspice/themes/<Theme name>/titles/<title ID>/<folder>/...   files of other titles
```

- `content` mirrors the Menu's `content` folder. Only the files the theme changes are needed; everything else comes
  from the Menu. Files the Menu doesn't have (e.g. a new music stream) are added.
- `titles/<title ID>/<folder>` replaces files in that title's folder for the Wii U Menu, for files the Menu reads
  from other titles. `<title ID>` is the 16-digit ID without the dash.
  - EULA text: `titles/0005001b10050000/content/00/<language>/DeviceEula.msbt`
  - App icons: `titles/<app title ID>/meta/iconTex.tga` (e.g. `0005001010047200` for EUR System Settings)
- To hide a Menu file, add an empty file named `.deleted_<file name>` in the same folder.

Example (EUR):
```
sd:/wiiu/pumpkinspice/themes/Persona Advanced RELOADED/content/Common/Package/Men.pack
sd:/wiiu/pumpkinspice/themes/Persona Advanced RELOADED/content/Common/Sound/Men/stream/BGM_MEN.bfstm
sd:/wiiu/pumpkinspice/themes/Persona Advanced RELOADED/content/EuEnglish/Message/AllMessage.szs
sd:/wiiu/pumpkinspice/themes/Persona Advanced RELOADED/titles/0005001b10050000/content/00/EuEnglish/DeviceEula.msbt
sd:/wiiu/pumpkinspice/themes/Persona Advanced RELOADED/titles/0005001010047200/meta/iconTex.tga
```

Title IDs differ by region (Wii U Menu: `0005001010040000` JPN, `0005001010040100` USA, `0005001010040200` EUR), so
`titles` entries are per region. Entries for other regions are ignored.

## Settings
Open the plugin config menu (L, DPAD Down and Minus on the GamePad):
- **Enable PumpkinSpice**: turns theme loading on or off.
- **Theme**: the theme to load. Without a choice, the first theme folder (alphabetical) is used.

Changes apply the next time the Wii U Menu starts.

## Logging
`make` logs errors only (via OSReport). `make DEBUG=1` adds information logging and `make DEBUG=VERBOSE` verbose
logging via the [LoggingModule](https://github.com/wiiu-env/LoggingModule) (falls back to UDP port 4405 and CafeOS
logging if the module is missing).

## Building
With Docker:
```
docker build . -t pumpkinspice-builder
docker run -it --rm -v ${PWD}:/project pumpkinspice-builder make
docker run -it --rm -v ${PWD}:/project pumpkinspice-builder make clean
```

Format the code: `docker run --rm -v ${PWD}:/src ghcr.io/wiiu-env/clang-format:13.0.0-2 -r ./src -i`

## Credits and license
PumpkinSpice by Fil, based on the [SDCafiine plugin](https://github.com/wiiu-env/sdcafiine_plugin) by Maschell.
Licensed under the GNU General Public License v3.0 (see `LICENSE`).

[![CI](https://github.com/SkiierCo/PumpkinSpice/actions/workflows/ci.yml/badge.svg)](https://github.com/SkiierCo/PumpkinSpice/actions/workflows/ci.yml)

# PumpkinSpice

A Wii U plugin that loads Wii U Menu themes from the SD card. Theme files replace the Menu's own files while the
Menu runs; nothing on the console's storage is changed. Turn PumpkinSpice off (or remove the theme) and restart the
Menu to get the stock Menu back.

## Requirements
- A USA or EUR Wii U Menu. There's no JPN support.
- Aroma with the [WiiUPluginLoaderBackend](https://github.com/wiiu-env/WiiUPluginLoaderBackend) and the
  [ContentRedirectionModule](https://github.com/wiiu-env/ContentRedirectionModule) in
  `sd:/wiiu/environments/aroma/modules`.
- The `titles` folder (EULA text, app icons) needs ContentRedirectionModule API version 2 or newer.
- No StyleMiiU (the plugin Themiify uses). Both theme the Wii U Menu, so PumpkinSpice stays off while `stylemiiu.wps`
  is in the same plugins folder; its config menu shows "Off while StyleMiiU is installed".

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

### Code patches
A theme can change some of the Menu's behaviour with a `code.txt` in its folder (one option per line, `#` starts a
comment):

```
page1_hide_right_arrow                        launcher page 1: the right arrow stays usable but is not drawn
folder_footer_color                           folder name bar, settings and back corners take the folder color
plaza_offset <x> <z>                          Wara Wara Plaza icon ring and centre moved by (x, z); -z moves it up
plaza_ring_radius <x> <z>                     icon ring radii (stock 190 180)
plaza_crowd_radius <outer> <outer2> <inner>   Mii crowd scatter radii (stock 150 300 50)
```

Example (Persona Advanced RELOADED):
```
page1_hide_right_arrow
folder_footer_color
plaza_offset 75 -35
plaza_ring_radius 150 115
plaza_crowd_radius 70 140 35
```

Supported Wii U Menu code: USA 5.5.x and EUR 5.5.5 stock, and the TVii (OG Menu) patched Menu. The plugin asks the
system where `men.rpx` is loaded (its RPL list), checks the exact instructions and values at every place it changes,
and writes them through the kernel. If the running Menu doesn't match a supported build (another build, or a Menu that
is already modified) the code patches are skipped and only the files are loaded. Requires the KernelModule (part of
Aroma).

## Settings
Open the plugin config menu (L, DPAD Down and Minus on the GamePad):
- **Enabled**: turns theme loading on or off.
- **Theme**: the theme to load. Without a choice, the first theme folder (alphabetical) is used.

Changes apply the next time the Wii U Menu starts. With StyleMiiU installed, the menu shows "Off while StyleMiiU is
installed" and nothing is loaded.

## Logging
`make` logs errors only (via OSReport). `make DEBUG=1` adds information logging and `make DEBUG=VERBOSE` verbose
logging via the [LoggingModule](https://github.com/wiiu-env/LoggingModule) (falls back to UDP port 4405 and CafeOS
logging if the module is missing).

`make SDLOG=1` also writes a step-by-step log to `sd:/wiiu/pumpkinspice/log.txt` (theme, layers, the Menu build found,
every code patch write and its read-back); each line is written and closed on its own, so the log survives a hang.

## Changes
**v1.1**
- Code patches work on consoles: the KernelModule import is linked with its linker script, the Menu's code is found
  through the system's RPL list (it isn't at its link address on a console), Menu code is read and written through the
  kernel, and the instruction cache is cleared on every core.
- PumpkinSpice stays off while StyleMiiU is installed.
- Shorter config menu labels.
- Optional SD card log (`make SDLOG=1`).

**v1.0.0**: first release.

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

[![CI](https://github.com/SkiierCo/PumpkinSpice/actions/workflows/ci.yml/badge.svg)](https://github.com/SkiierCo/PumpkinSpice/actions/workflows/ci.yml)

# PumpkinSpice

A Wii U plugin that loads Wii U Menu themes from the SD card. Theme files replace the Menu's own files while the
Menu runs; nothing on the console's storage is changed. Turn PumpkinSpice off (or remove the theme) and restart the
Menu to get the stock Menu back.

## Requirements
- A JPN, USA or EUR Wii U Menu (code patches checked against JPN 5.5.5, USA 5.5.x, EUR 5.5.5 and the TVii Menu).
- Aroma with the [WiiUPluginLoaderBackend](https://github.com/wiiu-env/WiiUPluginLoaderBackend) and the
  [ContentRedirectionModule](https://github.com/wiiu-env/ContentRedirectionModule) in
  `sd:/wiiu/environments/aroma/modules`.
- The `titles` folder (EULA text, app icons) needs ContentRedirectionModule API version 2 or newer.
- StyleMiiU (the plugin Themiify uses) is switched off. Both theme the Wii U Menu, so while PumpkinSpice is enabled
  it renames `stylemiiu.wps` in the same plugins folder to `stylemiiu.wps1`. StyleMiiU is still loaded until the
  next boot, so on that boot the theme isn't applied and the config menu shows "StyleMiiU switched off: theme applies
  after a reboot". To use StyleMiiU again, disable PumpkinSpice and rename the file back.

## Installation
Copy `pumpkinspice.wps` into `sd:/wiiu/environments/aroma/plugins`.

## Themes
Each theme is a folder in `sd:/wiiu/pumpkinspice/themes`:

```
sd:/wiiu/pumpkinspice/themes/<Theme name>/content/...                Wii U Menu files
sd:/wiiu/pumpkinspice/themes/<Theme name>/titles/<title ID>/...      files of other apps (icons, EULA)
```

- `content` mirrors the Menu's `content` folder. Only the files the theme changes are needed; everything else comes
  from the Menu. Files the Menu doesn't have (e.g. a new music stream) are added.
- `titles` holds files the Menu shows from other apps. A title ID is split into two folders, its first 8 digits and
  its last 8 digits, the same way the console stores it: System Settings (EUR) `00050010-10047200` is
  `titles/00050010/10047200`.
  - App icon: `titles/00050010/10047200/meta/iconTex.tga`
  - EULA text: `titles/0005001b/10050000/content/00/<language>/DeviceEula.msbt`
- To hide a Menu file, add an empty file named `.deleted_<file name>` in the same folder.

PumpkinSpice loads `content` plus one group of `titles` per first-8-digit folder (`00050010` apps, `00050030`
applets, `0005001b` system data). Loading every app separately made the Menu stop at startup on consoles with many
other plugins. The older layout, `titles/<16-digit title ID>/...`, still loads, but is skipped when a theme also uses
the two-folder layout.

Example (EUR):
```
sd:/wiiu/pumpkinspice/themes/Persona Advanced RELOADED/content/Common/Package/Men.pack
sd:/wiiu/pumpkinspice/themes/Persona Advanced RELOADED/content/Common/Sound/Men/stream/BGM_MEN.bfstm
sd:/wiiu/pumpkinspice/themes/Persona Advanced RELOADED/content/EuEnglish/Message/AllMessage.szs
sd:/wiiu/pumpkinspice/themes/Persona Advanced RELOADED/titles/0005001b/10050000/content/00/EuEnglish/DeviceEula.msbt
sd:/wiiu/pumpkinspice/themes/Persona Advanced RELOADED/titles/00050010/10047200/meta/iconTex.tga
```

Title IDs differ by region (Wii U Menu: `0005001010040000` JPN, `0005001010040100` USA, `0005001010040200` EUR), so
`titles` entries are per region. Entries for other regions are ignored.

### Code patches
A theme can change some of the Menu's behaviour with a `code.txt` in its folder (one option per line, `#` starts a
comment):

```
page1_hide_right_arrow                        launcher page 1: the right arrow stays usable but is not drawn
folder_footer_color                           folder name bar, settings and back corners take the folder color
shrink_long_titles                            launcher titles longer than one line shrink to about one line (min 0.55)
plaza_offset <x> <z>                          Wara Wara Plaza icon ring and centre moved by (x, z); -z moves it up
plaza_ring_radius <x> <z>                     icon ring radii (stock 190 180)
plaza_crowd_radius <outer> <outer2> <inner>   Mii crowd scatter radii (stock 150 300 50)
```

`folder_footer_color` only works with the updated version of Leon Pascal's folder edit; it has no effect on the
default folders.

Example (Persona Advanced RELOADED):
```
page1_hide_right_arrow
folder_footer_color
shrink_long_titles
plaza_offset 75 -10
plaza_ring_radius 150 115
plaza_crowd_radius 70 140 35
```

The plugin asks the system where `men.rpx` is loaded (its RPL list) and searches its code for each place it changes,
by the surrounding instructions (`src/menu_patterns.h`; addresses and branch offsets left out), so a place is found
wherever it is: in the USA 5.5.x and EUR 5.5.5 stock Menus, the TVii (OG Menu) patched Menu, and a Menu modified
elsewhere. Each patch is applied only if all of its code is there and found exactly once; one that isn't (for example
code another mod already changed, or plaza radii that aren't the stock values) is skipped and the others still apply.
Changes are written through the kernel. Requires the KernelModule (part of Aroma).

## Settings
Open the plugin config menu (L, DPAD Down and Minus on the GamePad):
- **Enabled**: turns theme loading on or off.
- **Theme**: the theme to load. Without a choice, the first theme folder (alphabetical) is used.
- **App icons and EULA**: loads the theme's `titles` folder. Off, only `content` is loaded (one layer), for a console
  where the Wii U Menu still stops at startup with the theme on.

Changes apply the next time the Wii U Menu starts. On the boot where PumpkinSpice switched StyleMiiU off, the menu
shows "StyleMiiU switched off: theme applies after a reboot".

## Logging
`make` logs errors only (via OSReport). `make DEBUG=1` adds information logging and `make DEBUG=VERBOSE` verbose
logging via the [LoggingModule](https://github.com/wiiu-env/LoggingModule) (falls back to UDP port 4405 and CafeOS
logging if the module is missing).

`make SDLOG=1` also writes a step-by-step log to `sd:/wiiu/pumpkinspice/log.txt` (theme, layers, where each place
was found in the Menu code, every code patch write and its read-back); each line is written and closed on its own, so
the log survives a hang.
At each boot the last boot's log moves to `log-previous.txt` (so the log of a boot that crashed is kept) and older logs
are deleted.

## Changes
**v1.3**
- JPN Wii U Menu: the code patches find every place in JPN 5.5.5 too (checked with the same patterns).
- Fewer layers for the `titles` folder: one per group (`titles/<first 8 digits>/<last 8 digits>/...`) instead of one
  per title
  (Persona Advanced RELOADED: 4 layers instead of 15; with 15, a console with many other plugins stopped at startup,
  halt 248). The older `titles/<16-digit ID>/...` layout still loads. New setting **App icons and EULA** to leave the
  `titles` folder out.
- PumpkinSpice switches StyleMiiU off (renames `stylemiiu.wps` to `stylemiiu.wps1`) instead of staying off itself;
  the theme applies from the boot after.
- The SD card log (`make SDLOG=1`) keeps only this boot's and the previous boot's log instead of growing.
- Code patches find their places in the Menu by code patterns instead of a fixed address table per build, so they
  also work on a Menu modified elsewhere; each patch is skipped on its own when its code isn't found, instead of all
  of them. `tests/scan_test.cpp` checks the search on Menu images (moved, modified, already patched).

**v1.2**
- New code patch `shrink_long_titles`: a launcher title that wraps or has its own line break is scaled down to about
  one line's height (at least 0.55) so it fits the theme's title banner; one-line titles stay full size. Hooks the
  title balloon's resize in all three supported Menus.
- CI no longer publishes nightly releases; releases come from version tags.

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

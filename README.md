[![CI](https://github.com/SkiierCo/PumpkinSpice/actions/workflows/ci.yml/badge.svg)](https://github.com/SkiierCo/PumpkinSpice/actions/workflows/ci.yml)

# PumpkinSpice

PumpkinSpice is a Wii U plugin that loads Wii U Menu themes from the SD card. The theme files replace the Menu's own
files while the Menu is running and nothing on the console's storage gets changed, so if you turn PumpkinSpice off or
remove the theme and restart the Menu you get the default Menu back.

Just so it's clear I don't use any of StyleMiiU's or Themiify's source even though they get mentioned a lot in here.
PumpkinSpice and StyleMiiU are just forks of the same plugin.

## Requirements
- A JPN, USA or EUR Wii U Menu (code patches checked against JPN 5.5.5, USA 5.5.5, EUR 5.5.5 and the TVii Menu).
- Aroma with the [WiiUPluginLoaderBackend](https://github.com/wiiu-env/WiiUPluginLoaderBackend) and the
  [ContentRedirectionModule](https://github.com/wiiu-env/ContentRedirectionModule) in
  `sd:/wiiu/environments/aroma/modules`.
- The `titles` folder (EULA text, app icons) needs ContentRedirectionModule API version 2 or newer.
- StyleMiiU (the plugin Themiify uses) is switched off. Both change the Wii U Menu, so while PumpkinSpice is enabled
  it renames `stylemiiu.wps` in the same plugins folder to `stylemiiu.wps1`. StyleMiiU is still loaded until the
  next boot, so on that boot the theme isn't applied and the config menu shows "StyleMiiU switched off: theme applies
  after a reboot". To use StyleMiiU again, disable PumpkinSpice: it renames the file back, and StyleMiiU loads from
  the next boot.

## Installation
Copy pumpkinspice.wps into sd:/wiiu/environments/aroma/plugins.

## Theme packages (.ps)
A theme now comes as just one package, sd:/wiiu/pumpkinspice/themes/[Theme name]/[Theme name].ps (or themes/[Theme
name].ps). Packages are similar to BPS patches but more integrated with the console and tools for a more streamlined
approach, so there's nothing to patch by hand and a downloaded theme works right away. A package only has the theme's
own changes in it and no Nintendo files. The first time a package starts PumpkinSpice rebuilds the finished files from
the console's own Wii U Menu files into that folder (content, titles and code.txt) then deletes the .ps and from then
on it's a regular theme. It only rebuilds the console's region (from the running Menu) plus the files every region
shares. If the default version of a file on the console isn't the one the package was made from that file gets skipped
and logged. If the rebuild fails or the console gets turned off during it nothing happens and the .ps gets tried again
on the next restart. To update a theme just put the new .ps in its folder and the old theme gets deleted once the new
one is in place. The format is in docs/PS-FORMAT.md.

I make packages with my tool LatteLab from a theme folder laid out like below and the default files of each region it
covers. LatteLab can also rebuild a package the same way. The format code is in lib/wiiumenu and it's shared with
LatteLab.

## Themes
Each theme is a folder in sd:/wiiu/pumpkinspice/themes:

```
sd:/wiiu/pumpkinspice/themes/<Theme name>/content/...                Wii U Menu files
sd:/wiiu/pumpkinspice/themes/<Theme name>/titles/<title ID>/...      files for the System Apps (icons, EULA)
```

Your theme’s content folder works like the Menu’s own content folder. You only need to put in the files your theme
changes and the Menu uses its own for everything else. If you add a file the Menu doesn’t have (like a new music
stream) it gets added too.

The titles folder is for files the Menu shows from other apps like app icons and the EULA text. Each app goes in two
folders named after its title ID, the first 8 digits then the last 8 digits, the same way the console stores it. For
example System Settings (EUR) is 00050010-10047200 so its icon goes in titles/00050010/10047200/meta/iconTex.tga. The
EULA text goes in titles/0005001b/10050000/content/00 inside the folder for your language, so for EUR English that’s
titles/0005001b/10050000/content/00/EuEnglish/DeviceEula.msbt.

If you want to hide one of the Menu’s files put an empty file in the same folder named .deleted_ with the file’s name
after it, so to hide Men2.pack you’d add .deleted_Men2.pack.

PumpkinSpice loads content plus one group of titles per first 8 digit folder (00050010 apps, 00050030 applets and
0005001b system data). I did it that way because loading every app on its own made the Menu stop at startup on
consoles with a lot of other plugins. The older layout titles/[16-digit title ID]/... still loads but it gets skipped
when a theme also uses the two folder layout.

Example (EUR):
```
sd:/wiiu/pumpkinspice/themes/Persona Advanced RELOADED/content/Common/Package/Men.pack
sd:/wiiu/pumpkinspice/themes/Persona Advanced RELOADED/content/Common/Sound/Men/stream/BGM_MEN.bfstm
sd:/wiiu/pumpkinspice/themes/Persona Advanced RELOADED/content/EuEnglish/Message/AllMessage.szs
sd:/wiiu/pumpkinspice/themes/Persona Advanced RELOADED/titles/0005001b/10050000/content/00/EuEnglish/DeviceEula.msbt
sd:/wiiu/pumpkinspice/themes/Persona Advanced RELOADED/titles/00050010/10047200/meta/iconTex.tga
```

Title IDs are different per region (Wii U Menu: 0005001010040000 JPN, 0005001010040100 USA and 0005001010040200 EUR)
and entries for nonexistent regions get ignored so make sure you copy and paste these IDs.

### Code patches
A theme can change some of the Menu's behaviour with a code.txt in its folder. It's one option per line and # starts a
comment:

```
page1_hide_right_arrow                        launcher page 1: the right arrow stays usable but is not drawn
folder_footer_color                           folder name bar, settings and back corners take the folder color
shrink_long_titles                            launcher titles longer than one line shrink to about one line (min 0.55)
plaza_offset <x> <z>                          Wara Wara Plaza icon ring and centre moved by (x, z); -z moves it up
plaza_ring_radius <x> <z>                     icon ring radius (default 190 180)
plaza_crowd_radius <outer> <outer2> <inner>   Mii crowd scatter radius (default 150 300 50)
```

folder_footer_color only works with the updated version of Leon Pascal's folder edit and it doesn't do anything on the
default folders.

Theme Example (Persona Advanced RELOADED):
```
page1_hide_right_arrow
folder_footer_color
shrink_long_titles
plaza_offset 45 0
plaza_ring_radius 150 115
plaza_crowd_radius 70 140 35
```

PumpkinSpice doesn’t use fixed addresses for the code patches. When the Menu starts it asks the system where men.rpx
got loaded in memory (its RPL list) then looks through the Menu’s code for the instructions around each spot it
patches\
The patterns for that are in src/menu_patterns.h and they leave out addresses and branch offsets, so a spot still gets
found even if it was moved\
That’s why the patches work on the JPN, USA and EUR 5.5.5 default Menus, the TVii (OG) Menu and Menu changes you've
made to your own console.

A patch only gets applied when all of its code is found and each piece is found exactly once\
If it isn’t, like if you already changed that code or a plaza radius isn’t the default value anymore, just that patch
gets skipped and the rest still apply, The patches are written through the kernel so you need the KernelModule which
comes with Aroma.

## Transferring themes
If you installed your themes with Themiify or StyleMiiU you don’t have to redo anything because StyleMiiU already
uses the same layout as PumpkinSpice. Your themes are in sd:/wiiu/themes and each one has a content folder with the
Men.pack and Men2.pack in content/Common/Package, cafe_barista_men.bfsar in content/Common/Sound/Men and
AllMessage.szs in content/\<language\>/Message. Just copy the whole theme folder into sd:/wiiu/pumpkinspice/themes and
it'll show up in the selector, the Theme setting and shuffle. The folder name sometimes has the theme's ID in brackets
after it and you can rename it if you want. Other files like metadata.json aren't used so you can leave them in there
and if you copy instead of move the theme still works in StyleMiiU too.

The language folder depends on your region and language so a USA Menu uses UsEnglish, UsFrench, UsSpanish or
UsPortuguese and a EUR Menu uses EuEnglish, EuFrench, EuGerman, EuItalian, EuSpanish, EuDutch, EuPortuguese or
EuRussian. The Menu only reads the folders for its own region so a theme that only has UsEnglish text won't change the
text on a EUR console.

If you made a theme folder by hand for StyleMiiU with Men.pack, Men2.pack or cafe_barista_men.bfsar just sitting in
it, put the packs in content/Common/Package and the bfsar in content/Common/Sound/Men inside your theme folder first.

## Theme selector
When the Wii U Menu starts for the first time after a boot PumpkinSpice shows SDCafiine's selector with the themes on
your SD card. A starts the selected theme, B starts without a theme and L/R change the page. Whatever theme you pick
becomes the Theme setting. MINUS turns autostart on or off for a selected theme (the one with the arrow, marked
"(autostart)"). With autostart on that theme starts after "Preparing theme..." without the list and you can press
MINUS there to open the list. Holding MINUS while the console starts clears autostart and shows the list. Coming back
to the Menu from a game uses the same choice unless Shuffle themes is on, then every Menu start gets a random theme.

While a theme package is being prepared a screen shows the progress (only on the first start)\
PLEASE DO NOT TURN OFF YOUR CONSOLE. If it fails a screen shows why and sends you to the default/current menu.

## Settings
Open the plugin config menu (L, DPAD Down and Minus on the GamePad):
- **Enabled**: turns theme loading on or off.
- **Theme**: the theme to load. Without a choice, the first theme folder (alphabetical) is used.
- **Shuffle themes**: a random theme every time the Wii U Menu starts and never the same one twice in a row. While
  it's on the Theme and Autostart theme settings aren't used.
- **App icons and EULA**: loads the theme's `titles` folder. Off, only `content` is loaded (one layer), for a console
  where the Wii U Menu still stops at startup with the theme on.
- **Advanced settings**: **Autostart theme** (Off, or the theme the selector starts by itself) and **Skip "Preparing
  theme..." screen** (the autostart theme starts with no screen; off by default).

Changes apply when you exit the plugin config menu\
If you chose a new **Theme** it'll restart the Menu, themes load very quickly. On the boot where PumpkinSpice switches
StyleMiiU off the menu shows "StyleMiiU switched off: theme applies after a reboot".

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
**v1.4.2**
- The config menu now has a Shuffle Themes switch!
  now on every Menu restart the theme shuffles, keep in mind you must have transfered all your themes to
  PumpkinSpice otherwise it'll just keep showing Persona Advanced Reloaded. ([See Transferring themes](#transferring-themes))

**v1.4.1**
- Someone brought to my attention that it would be in good faith to have the ability to reenable StyleMiiU without
  opening your SD Card and renaming it again so PumpkinSpice now reenables StyleMiiU when disabled.
- Side Note : I have nothing against StyleMiiU and it's maintainers, I originally just disabled it entirely because it
  messed with PumpkinSpice.

**v1.4**
- Theme packages (.ps) that I make with my tool LatteLab. They're similar to BPS patches but more streamlined and
  integrated with the console and tools so a downloaded theme works right away. On the first start PumpkinSpice
  rebuilds the files for the console's region from its own Menu files and the package turns into a regular theme
  folder. ([See Theme packages](#theme-packages-ps))
- A theme selector at the first Wii U Menu start (SDCafiine's selector but for themes) with autostart for a theme you
  pick. Hold MINUS at startup to clear it. ([See Theme selector](#theme-selector))
- A screen shows the progress while a package is being prepared and the reason if it fails.
- New settings Autostart theme and Skip "Preparing theme..." screen under Advanced settings.
- Changing the Theme in the config menu restarts the Wii U Menu when you close the config menu.

**v1.3**
- JPN Wii U Menu: the code patches find every place in JPN 5.5.5 too (I checked with the same patterns).
- Fewer layers for the titles folder: one per group (titles/[first 8 digits]/[last 8 digits]/...) instead of one per
  title. Persona Advanced RELOADED went from 15 layers to 4 because with 15 a console with a lot of other plugins
  stopped at startup (halt 248). The older titles/[16-digit ID]/... layout still loads. New setting App icons and EULA
  to leave the titles folder out.
- PumpkinSpice switches StyleMiiU off (renames stylemiiu.wps to stylemiiu.wps1) instead of staying off itself and the
  theme applies from the boot after.
- The SD card log (make SDLOG=1) only keeps this boot's and the previous boot's log now instead of growing.
- Code patches find their places in the Menu by code patterns instead of a fixed address table per build so they also
  work on a Menu modified elsewhere. Each patch gets skipped on its own when its code isn't found instead of all of
  them. tests/scan_test.cpp checks the search on Menu images (moved, modified and already patched).

**v1.2**
- New code patch shrink_long_titles: a launcher title that wraps or has its own line break gets scaled down to about
  one line's height (at least 0.55) so it fits the theme's title banner and one-line titles stay full size. It hooks
  the title balloon's resize in all three supported Menus.
- CI doesn't publish nightly releases anymore because releases come from version tags now.

**v1.1**
- Code patches work on consoles now. The KernelModule import is linked with its linker script, the Menu's code is
  found through the system's RPL list (it isn't at its link address on a console), Menu code is read and written
  through the kernel and the instruction cache gets cleared on every core.
- PumpkinSpice stays off while StyleMiiU is installed.
- Shorter config menu labels.
- Optional SD card log (make SDLOG=1).

**v1.0.0**: first release.

## Building
I build it with Docker:
```
docker build . -t pumpkinspice-builder
docker run -it --rm -v ${PWD}:/project pumpkinspice-builder make
docker run -it --rm -v ${PWD}:/project pumpkinspice-builder make clean
```

To format the code:
```
docker run --rm -v ${PWD}:/src ghcr.io/wiiu-env/clang-format:13.0.0-2 -r ./src -i
```

## Credits and license
PumpkinSpice is by Fil and it's based on the [SDCafiine plugin](https://github.com/wiiu-env/sdcafiine_plugin) by
Maschell. It's licensed under the GNU General Public License v3.0 (see LICENSE).

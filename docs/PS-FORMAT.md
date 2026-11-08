# PumpkinSpice theme package (`.ps`), format version 1

A `.ps` file holds a theme's own work only: the files it adds, and for every Nintendo file it changes, the changes
(BPS patches, taken inside archives at the level of single layouts, textures, messages and sounds). It contains no
Nintendo file. On its first start PumpkinSpice rebuilds the finished files on the console from that console's own
Menu files and turns the package into a regular theme folder, so nobody has to patch anything by hand.

## File layout

All numbers are big-endian.

| Offset | Size | |
| --- | --- | --- |
| 0x00 | 4 | magic `PSPK` |
| 0x04 | 4 | format version (1) |
| 0x08 | 4 | manifest size in bytes |
| 0x0C | manifest size | manifest: UTF-8 JSON |
| after the manifest | rest of the file | data area: the blobs the manifest points to |

A blob is given as `"data": [offset, size]`, with the offset counted from the start of the data area.

## Manifest

```json
{
 "format": "PumpkinSpice theme package",
 "version": 1,
 "name": "Persona Advanced RELOADED",
 "code": "page1_hide_right_arrow\nplaza_offset 45 0\n",
 "files": [ node, ... ]
}
```

- `name`: the theme's name.
- `code`: optional, the theme's `code.txt` (see the README).
- `files`: one node per top-level file the theme changes or adds.

### Nodes

Every node has `path` and `op`.

| `op` | Fields | Result |
| --- | --- | --- |
| `add` | `data` | the blob as it is (the theme's own file; replaces a stock file of that name) |
| `patch` | `stock_crc`, `data` | the stock file with the BPS patch in the blob applied |
| `archive` | `stock_crc`, `children` | the stock archive (SARC, or Yaz0-compressed SARC) with each child node applied to its members |
| `remove` | | the member is left out (inside archives only) |

- `path` of a top-level node: `content/...` (the Wii U Menu's `/vol/content/...`) or
  `titles/<first 8 digits>/<last 8 digits>/...` of a title ID (the console's
  `/vol/storage_mlc01/<sys|usr>/title/<first 8>/<last 8>/...`; system titles, first 8 digits `0005001x` and
  `00050030`, are in `sys`). `path` of a child node: the member's name in its archive (as stored, e.g. `blyt/LoungeScaleMeter.bflyt`).
- `stock_crc`: CRC32 (as in BPS and zip) of the stock file or member, as 8 hex digits. A BPS patch also checks its
  source itself; the `archive` check stops early when the stock archive is not the one the package was made from.

### Rebuilding an `archive` node

Matches how LatteLab builds archives, so a rebuilt archive equals one built from the theme's source:

1. If the stock file is Yaz0-compressed, decompress it (remember the header's alignment).
2. Parse the SARC. Members without a child node stay as they are, with the alignment they had.
3. Apply each child node. A member that ends up different from stock (patched, added, or a rebuilt nested
   archive that changed) gets the default alignment for its type and data (`sarc::default_alignment`).
4. Write the SARC (members ordered by name hash, same hash multiplier and tail padding as stock).
5. If the stock file was compressed: unchanged contents keep the stock compressed bytes, changed contents are
   compressed again with the stock header alignment.

## Region and version

A package can carry files for several regions. A top-level node can have `"regions": ["EUR"]` (or `"USA"`, `"JPN"`);
a node without it is for every region. LatteLab sets it from the path:

| Path | Region |
| --- | --- |
| `content/Eu<language>/...`, `content/Us<language>/...`, `content/Jp<language>/...` | EUR, USA, JPN |
| `titles/00050010/<last 8 digits>/...`, `titles/00050030/<last 8 digits>/...` (apps, applets) | the 6th of the last 8 digits: `0` JPN, `1` USA, `2` EUR (e.g. `10047200` EUR, `1001210A` USA) |
| everything else (`content/Common/...`, system data such as the EULA's `0005001b/10050000`) | all |

PumpkinSpice takes the console's region from the running Wii U Menu's title ID (`00050010-10040000` JPN,
`-10040100` USA, `-10040200` EUR) and rebuilds only that region's nodes and the shared ones. A `patch` or `archive`
node whose stock file is still missing is skipped; a `stock_crc` or BPS source check that fails skips that file with a
log line, so a package made from another Menu version never produces a broken file.

## On the console

- A theme is shipped like any theme folder, with the package in place of the files:
  `themes/<name>/<name>.ps` (any file name ending in `.ps`; `themes/<name>.ps` next to the folder works too).
- The first start turns it into the regular theme folder: every file for the console's region is rebuilt into
  `themes/<name>.building`; when all of them are done, the folder that was there is moved aside as
  `<name>.replaced`, `<name>.building` becomes `themes/<name>` (`content`, `titles`, `code.txt`), and the
  `.replaced` folder is deleted with the `.ps` in it. From then on it is a regular theme folder. If the console is
  turned off in between, the next start puts the moved folder back and rebuilds, or deletes a `.replaced` folder that
  is no longer needed.
- If any file fails (for example not enough memory), `<name>.building` is removed and nothing else changes: the `.ps`
  stays and the next start tries again.
- To update, put the new `.ps` into the theme folder: the next start rebuilds it and deletes the old theme.

## Making a package

LatteLab compares a theme laid out as a PumpkinSpice theme folder with the stock files and writes the
package: archives are compared member by member (recursively), equal members are left out, changed members become
BPS patches, members only in the theme become `add` nodes. Top-level files without a stock file (new music, new
icons) become `add` nodes, and the tool lists them so the author can check that they are the theme's own work.

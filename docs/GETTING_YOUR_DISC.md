# Getting your own disc data

Every person who runs the game needs their own dump of a Super Mario
Galaxy disc they own. This is the one step no one can do for you and the
one file that must never be shared, uploaded, or committed. The repo's
`.gitignore` blocks the common formats, but the rule is yours to keep,
not git's.

Why it can't be shared: the disc contents are Nintendo's copyrighted
data. A repo that contains them gets taken down and the person who pushed
them carries the exposure. The code in this repo is legal because it
contains none of it. Keep it that way.

## 1. Dump the disc

Options, in order of preference:

- **A Wii with homebrew and CleanRip** (or any dumper that produces a full
  disc image). Writes `.iso`, often split as `.part0`, `.part1`. Dolphin
  opens the first part directly.
- **A Wii U** with a disc dumper that outputs a Wii image.
- A dump you made earlier with any of the above.

Any region works for the code. For running the game, **EUR (RMGP01) or USA
(RMGE01)** are the tested ones. JP (RMGJ01) and KOR (RMGK01) should work
but nobody has tried; you would be first.

## 2. Convert to RVZ (optional, recommended)

RVZ is Dolphin's compressed, lossless format. Super Mario Galaxy goes from
~4.4 GB ISO to ~3 GB RVZ with no loss. Keep this as your archive copy.

- Dolphin: right-click the game -> Convert File -> Format RVZ, compression
  zstd, level 5 or above.
- Or on the command line, Dolphin's `DolphinTool convert`.

## 3. Extract to a folder

The port's tools read an extracted folder, not the image. Put it outside
the repo, for example `~/smg/extracted/`.

- Dolphin: right-click the game -> Properties -> Filesystem -> right-click
  the disc -> Extract Entire Disc. About 30 minutes, about 4 GB.
- Command line (what the agent uses on macOS): `cargo install nodtool`,
  then `nodtool extract "Super Mario Galaxy.rvz" ~/smg/extracted`.

You should end up with `sys/main.dol`, `sys/fst.bin` and `files/` (Dolphin
puts them under a `DATA/` subfolder; that is fine).

## 4. Cook it

Once P0-02 has vendored the cooker:

```
python tools/cook/cook.py ~/smg/extracted ~/smg/game            # ~1 GB, movies black
python tools/cook/cook.py ~/smg/extracted ~/smg/game --with-movies   # ~3.3 GB
```

Point the native build at it with `--game ~/smg/game`. The browser build
imports it through the import screen (Phase 1) or the P0-07 copy helper.

## 5. What to check before you push anything

```
git status --short | grep -i -E 'iso|rvz|wbfs|game/|extracted/'
```

should print nothing, ever. If you are not sure whether a file is game
data, it is.

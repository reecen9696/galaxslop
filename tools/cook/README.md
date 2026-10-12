# tools/cook

GalaxyQuest's offline cooker (vendored at `d8a039c`, D-007). Turns an
extracted Super Mario Galaxy disc into the little-endian layout the port
loads. Stdlib-only Python 3.10+.

```
python tools/cook/cook.py ~/smg/extracted ~/smg/game                 # ~1 GB, movies black
python tools/cook/cook.py ~/smg/extracted ~/smg/game --with-movies   # adds ~2.3 GB
python tools/cook/cook.py --verify ~/smg/game                        # structure + manifest
python tools/cook/cook.py --write-manifest ~/smg/game                # rewrite manifest only
```

Input is an extracted folder (`sys/fst.bin`, `sys/main.dol`, `files/`); a
Dolphin `DATA/` subfolder is found automatically. It does not read disc
images; see `docs/GETTING_YOUR_DISC.md` for extracting one.

## What it does

- Byte-swaps RARC, J3D, KCL/collision, BCSV, layouts, fonts, messages,
  particles and JAudio data to little-endian (`rarc_swap`, `j3d`, `kcl`,
  `collision`, `bcsv`, `lyt`, `bmg`, `jpa`, `jaudio`). Archives are
  Yaz0-decompressed. Cooked archives start with `CRAR` (reversed `RARC`),
  which is how the DVD layer knows a folder is cooked.
- Textures stay in GX formats; the renderer decodes them.
- Extracts `ErrorMessageArchive.arc`, `GalaxyID.bcsv` and `StoryEvent.bcsv`
  from `main.dol` into `<out>/sys/` (`dol_data.py`). These never go in git
  (D-011).
- Writes `<out>/manifest.json`: path, size, sha1 of every file (skipped
  with `--only`). The browser import uses it to check an OPFS copy.
- The HOME-menu archives (`HomeButton*.arc`) are U8, not RARC; they are
  copied decompressed and reported as notes. The port doesn't use them.

## Speed

`native/yaz0.c` is the Yaz0 decoder. `tools/rarc.py` builds it on first use
with `cc` (`libyaz0.dylib` / `libyaz0.so`, gitignored) and loads it through
ctypes; on Windows it expects a prebuilt `yaz0.dll`. Without it, Yaz0 falls
back to pure Python. Measured on an M-series Mac with the native decoder:
EUR disc, 2,371 files, **~9 s**, 1.0 GB out.

## Regions

Tested by GalaxyQuest: RMGP01 (EUR) and RMGE01 (USA). Verified here: RMGP01,
`main.dol` sha1 `9bd1daa5…511f` (matches Petari's config). JP and KOR are
untested; if they break, the DOL table offsets in `dol_data.py` are the
first place to look.

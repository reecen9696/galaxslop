# assets/mods: the port's own resources

Files the disc doesn't have, made for this port. `tools/mods/build_mods.py`
cooks them (`mods.cmake`, run by the build) into `build/<dir>/mods`, which the
DVD layer serves next to the game's files (`platform/src/dvd/dvd.cpp`: a path
the disc lacks is looked up here). In the browser build they are embedded in
the wasm at `/mods`. `SMG_MODS=<dir>` reads another folder; `SMG_MODS=0` none.

These are original resources, not game data, which is why `.gitignore` lets
these `.arc` files in.

| Path | What |
| --- | --- |
| `ObjectData/Barney.arc` | Barney, the NPC dog (model and animations). Source: `tools/mods/barney/`. |
| `Sound/Bark.wav` | His bark (`platform/src/port/sfx.cpp`). |

To update Barney: export from Blender (`tools/mods/barney/source/export_barney.py`)
and copy the new `Barney.arc` over this one; the build cooks it again. GX
treats clockwise triangles as front faces, which the exporter writes; an
export with Blender's anticlockwise order shows the inside of the model.

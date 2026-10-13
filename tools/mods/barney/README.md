# Barney

Barney is the user's dog, made into a Super Mario Galaxy character: the port's own
NPC in the Star Festival plaza (`decomp/src/Game/NPC/BarneyNPC.cpp`). This folder
is the model's source; the game reads the cooked archive
`assets/mods/ObjectData/Barney.arc` (see `assets/mods/README.md`).

Built in the style of the game's MoonRabbit: a few smooth forms per joint, rigid
per-joint parts, detail in vertex-colour gradients rather than geometry, glossy
layered eyes, a separate garment (his burgundy jumper). 13,612 triangles, 24
joints, ~108 game units tall and 180 long. Clips at 60 fps: `Wait` (blinks,
breathing, tail wag, panting), `Walk`, `Run` (tongue swings), `Happy`, `Talk`
(two barks and a growl), `Sit`, `Jump`, `Blink`.

| Path | What |
| --- | --- |
| `Barney.blend` | Editable Blender 5.2 file: mesh with a `Col` vertex-colour layer, rig, eight actions. |
| `source/build_barney.py` | Builds the whole character from scratch (headless). |
| `source/render_previews.py` | Renders `previews/` (`-- Action:frame ...` for poses). |
| `source/export_barney.py` | Purpose-built J3D exporter: writes `game/Barney.bmd`, the eight `.bck` files and `game/Barney.arc` (big-endian RARC) next to the blend, plus a GLB. |
| `source/check_import.py`, `source/validate_animation.py` | Independent checks with J3DBlend (GPL-3, cloned separately: `J3DBLEND=<dir>`). |
| `exports/manifest.json` | Counts and clip lengths of the last export. |

```sh
B=/Applications/Blender.app/Contents/MacOS/Blender   # Blender 5.2
$B -b --factory-startup --python source/build_barney.py            # -> Barney.blend
$B -b Barney.blend --factory-startup --python source/export_barney.py   # -> game/Barney.arc
cp game/Barney.arc ../../../assets/mods/ObjectData/Barney.arc          # then rebuild the game
```

The exporter writes rigid (one joint per vertex) vertex-coloured, untextured
shapes, which is all this model needs; it is not a general BMD exporter.

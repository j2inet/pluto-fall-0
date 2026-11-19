# Pluto Fall — Assets Documentation

This folder documents everything in `PlutoFall/assets/`: how the files are laid out, how the game
finds them, and the JSON formats that define the game's content.

## Contents

| Document | Covers |
|---|---|
| [README.md](README.md) (this file) | Folder layout, how paths work, how to add content |
| [json-basics.md](json-basics.md) | The JSON dialect and the shared value types used by every file (colours, vectors, transforms, multi-language strings, function references, model references, projectiles) |
| [game-config.md](game-config.md) | `data/game.json` — global settings, player craft, weapons, sounds |
| [enemies-and-items.md](enemies-and-items.md) | `data/enemies/*.json` and `data/items/*.json` — enemy and power-up types |
| [levels.md](levels.md) | `levels/*.json` — level definitions, spawns, background objects |
| [motion-and-fire-functions.md](motion-and-fire-functions.md) | The named movement and weapon patterns referenced from enemies, items, levels and backgrounds |
| [dialogs.md](dialogs.md) | `dialog/*.json` — story dialogs, questions, branching, next-level selection |
| [credits.md](credits.md) | `data/credits.json` — the credits screen |
| [localization.md](localization.md) | `lang/strings.json` — languages and UI text |
| [media-files.md](media-files.md) | Models (`.glb`/`.gltf`), textures, images, sounds and music |

## Folder layout

```
PlutoFall/assets/
├── data/
│   ├── game.json            Global configuration. The game looks for this file to locate the assets folder.
│   ├── credits.json         Credits screen content.
│   ├── enemies/             One enemy type per .json file (or an array of types per file).
│   │   ├── drone.json
│   │   ├── fighter.json
│   │   ├── saucer.json
│   │   ├── cruiser.json     (boss)
│   │   └── sentinel.json    (boss)
│   └── items/               One power-up type per .json file (or an array per file).
│       ├── health.json
│       ├── life.json
│       ├── bomb.json
│       └── maxhealth.json
├── levels/                  Level definitions. Referenced by dialogs' "nextLevels".
│   ├── level1_belt.json     landscape
│   ├── level1_direct.json   portrait
│   ├── level2_core.json     portrait, terminal (final level)
│   └── level2_charon.json   landscape, terminal (final level)
├── dialog/                  Story dialogs. Referenced by game.json "startDialog" and levels' "nextDialogs".
│   ├── intro.json
│   ├── midpoint.json
│   └── midpoint_alt.json
├── lang/
│   └── strings.json         Supported languages and all UI text.
├── models/                  3D models in glTF 2.0 binary (.glb) or text (.gltf) form.
├── textures/                Texture images used by models (found via each model's "textureFolder").
├── images/                  2D images: title art, dialog backgrounds, portraits, scrolling level backgrounds.
├── sounds/                  Sound effects (WAV, MP3 or MP4/M4A).
└── music/                   Looping background music (WAV, MP3 or MP4/M4A).
```

Only `data/game.json`, `data/credits.json`, `lang/strings.json` and the `data/enemies` /
`data/items` folders have fixed locations. Every other file is found because some JSON file names it,
so you are free to reorganise `levels/`, `dialog/`, `models/` and the media folders as long as the
references are updated.

### How the files reference each other

```
data/game.json
 ├─ startDialog ──────────► dialog/intro.json
 │                            └─ nextLevels[].level ──► levels/level1_*.json
 │                                                        ├─ enemies[].class ──► data/enemies/<name>.json
 │                                                        ├─ powerups[].class ─► data/items/<name>.json
 │                                                        └─ nextDialogs[] ────► dialog/midpoint*.json
 │                                                                                 └─ nextLevels ──► levels/level2_*.json (terminal)
 ├─ players[].model ──────► models/player_*.glb ──(textureFolder)──► textures/
 ├─ credits ──────────────► data/credits.json
 ├─ titleImage / selectBackground ─► images/
 ├─ titleMusic ───────────► music/
 └─ sounds.* ─────────────► sounds/
```

The game flow is: **title → player select → `startDialog` → level chosen by the dialog → one of the
level's `nextDialogs` (chosen at random) → level chosen by that dialog → …** until a level marked
`"terminal": true` is completed, which returns to the title screen.

## Paths

* All paths inside JSON files are **relative to the `assets` folder**, e.g. `"models/enemy_drone.glb"`,
  never relative to the JSON file that contains them.
* Use forward slashes (`/`). Backslashes also work on Windows but must be escaped in JSON (`\\`).
* Absolute paths are accepted but not recommended.
* Names are case-insensitive on Windows, but keep them consistent anyway.

### Where the game looks for the assets folder

At start-up the game searches, in order, for a folder named `assets` that contains `data/game.json`:

1. next to the executable;
2. in the current working directory;
3. in up to five parent folders of the executable (and in a `PlutoFall/assets` folder inside each).

When building in Visual Studio, a post-build step also copies `PlutoFall/assets` next to the
executable (`bin/<Platform>/<Configuration>/assets`).

## Missing or broken files

The game is designed to keep running when content is missing. Problems are written to the debugger
output window (Visual Studio *Output* pane) with the prefix `[PlutoFall]`.

| Problem | What happens |
|---|---|
| Model fails to load | A 1×1×1 cube of a random colour is used (the same colour for every use of that file during a run). |
| Image (PNG etc.) fails to load | A solid colour surface is used (colour derived from the file name). |
| Texture referenced by a model fails to load | A solid white texture is used, so the material's base colour shows. |
| Sound or music fails to load | Nothing is played. |
| JSON file has a syntax error | The file is ignored; the log gives the line and column. |
| Unknown enemy / item class in a level | A fallback object (a random-colour cube) is spawned and logged. |
| Unknown motion function name | `forward` is used. |
| Unknown fire function name | `none` is used (the enemy never fires). |
| Dialog file fails to load | The dialog is skipped and the title screen is shown. |
| Level file fails to load | The title screen is shown. |
| `data/game.json` missing | Built-in defaults are used (see [game-config.md](game-config.md)). |

## Adding content — quick recipes

**A new enemy type:** put a model in `models/`, create `data/enemies/<name>.json`
([enemies-and-items.md](enemies-and-items.md)), then spawn it from a level with `"class": "<name>"`.

**A new level:** create `levels/<name>.json` ([levels.md](levels.md)) and add it to some dialog's
`nextLevels` ([dialogs.md](dialogs.md)).

**A new language:** add an entry to `languages` in `lang/strings.json` and a translation for each
string ([localization.md](localization.md)). Any multi-language field in other files can then carry
text for that language too.

**A new player craft:** add an entry to `players` in `data/game.json` ([game-config.md](game-config.md)).

## Regenerating the sample assets

All shipped models, images, sounds and music are procedural placeholders produced by
`tools/generate_assets.py` (requires Python 3.9+ and Pillow). Running it overwrites the files in
`models/`, `textures/`, `images/`, `sounds/` and `music/`; it does not touch any JSON.

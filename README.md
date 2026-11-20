# Pluto Fall

*A game from Joel Ivory Johnson.*

A cooperative (1–4 players) 3D scrolling shooter for Windows, written in C++23 with
**Direct3D 11**, **Direct2D/DirectWrite**, **XAudio2** (+ Media Foundation decoding) and **XInput**.
Levels scroll either top-to-bottom (**portrait**) or right-to-left (**landscape**), and everything
(levels, enemies, items, dialogs, credits, UI text) is defined in JSON.

## Building

1. Open `PlutoFall.sln` in **Visual Studio 2026** (toolset `v145`, workload *Desktop development with C++*).
2. Pick `Debug|x64` or `Release|x64` (ARM64 is also configured) and build.
3. Run. The project's debugger working directory is `PlutoFall\`, and a post-build step copies
   `PlutoFall\assets` next to `PlutoFall.exe` (`bin\<Platform>\<Configuration>\`).

The language standard is set to `/std:c++latest` (C++23 features such as `std::expected`,
`std::ranges`, `std::format` and `std::span` are used). No third-party libraries are required — the
JSON parser and glTF loader are included in the source. Shaders are HLSL compiled at runtime with
`D3DCompile`, so there is no separate shader build step.

At runtime the game searches for the `assets` folder next to the executable, in the current directory,
and in parent folders of the executable.

## Controls

| Action | Keyboard | XInput controller |
|---|---|---|
| Move | WASD or arrow keys | Left stick / D-pad |
| Primary fire (unlimited bullets) | Ctrl | A or Right trigger |
| Secondary fire (bombs) | Shift | B or Left trigger |
| Start | Enter | Start |
| Menu / pause | Esc | Back (View) |
| Join (player select) | Enter | A |
| Back out (player select) | Shift | B |
| Toggle fullscreen | Alt+Enter or F11 | — |

**Player select:** press A/Enter to join (up to 4 players, any mix of keyboard and controllers),
Left/Right to choose a craft, fire to lock in, B/Shift to unlock or leave. When everyone who joined
has locked in, the opening dialog begins.

**Dialog:** fire skips to the end of the current line; once a line is shown, fire/A/Enter continues.
Questions are answered with Up/Down and fire.

**Credits:** Up/Down speeds up/slows the scroll; fire or Esc returns to the title.

## Project layout

```
PlutoFall.sln
DialogEditor/          dialogue editor (Win32), see "Dialogue editor" below
PlutoFall/
  PlutoFall.vcxproj
  src/                 C++ sources
  assets/
    data/game.json     global configuration (player craft, weapons, sounds, start dialog...)
    data/enemies/*.json enemy types (one per file, or an array)
    data/items/*.json  power-up types
    data/credits.json  credits
    levels/*.json      level definitions
    dialog/*.json      dialog scripts
    lang/strings.json  UI text in every language
    models/ textures/ images/ sounds/ music/
tools/generate_assets.py   regenerates the procedural placeholder art and audio (needs Pillow)
```

### Source overview

| File | Purpose |
|---|---|
| `Main.cpp` | Win32 window, message loop, fullscreen toggle |
| `Game.*` | Owns all subsystems and the active `Screen`; fades between screens |
| `Graphics.*` | D3D11 device/swap chain + Direct2D/DirectWrite on the same back buffer (16:9 letterboxed, 1920×1080 virtual canvas) |
| `Renderer3D.*`, `ShaderSource.h` | Model rendering; pixel shader permutations generated from each material's features |
| `Model.*`, `Gltf.cpp` | glTF 2.0 (`.gltf` / `.glb`) loader and model cache |
| `Texture.*` | WIC image loading for D3D textures and D2D bitmaps |
| `Audio.*` | XAudio2 with effects/music submix buses; Media Foundation decodes WAV/MP3/MP4 |
| `Input.*` | Keyboard + 4 XInput controllers behind one logical button set |
| `Json.*` | Small JSON DOM/parser (allows comments and trailing commas) |
| `Localization.*` | Multi-language strings |
| `MotionFunctions.*`, `FireFunctions.*` | Named, data-driven movement and weapon patterns |
| `GameData.*` | Loads all JSON definitions; `SelectNextLevel()` |
| `*Screen.cpp` | Title, Options, Credits, Player Select, Dialog and Gameplay screens |

### Fallbacks

* A model that cannot be loaded is replaced by a **1×1×1 cube of a random colour**.
* A PNG (or other image) that cannot be loaded is replaced by a **solid colour surface**.
* A missing sound is silently skipped. Unknown enemy/item classes appear as fallback cubes and are logged.

Diagnostic messages go to the debugger output window (`OutputDebugString`).

## Coordinates

* **Playfield coordinates** are relative to the centre of the visible screen on the `z = 0` plane.
  Landscape shows x ∈ [-16, 16], y ∈ [-9, 9]; portrait narrows the field to x ∈ [-8, 8] with HUD panels at the sides.
  Players, enemies, items and projectiles use these coordinates.
* **Level coordinates** are used by background objects. The camera moves along +x (landscape) or +y (portrait)
  by the scroll distance, so a background object at `[100, 0, 20]` passes the screen when the level has scrolled
  ~100 units. Larger `z` is further from the camera and scrolls slower (parallax).
* **Models** are authored with the nose along **+X** and the top along **+Y**. The game turns them to face the
  direction of travel: viewed from the side (tilted) in landscape and from above in portrait.

## Multi-language strings

Anywhere text is shown, the JSON value may be a plain string or an object keyed by language:

```json
"text": { "en": "Good hunting.", "es": "Buena caza.", "ja": "健闘を祈る。" }
```

Missing languages fall back to English, then to the first entry. Languages are listed in
`lang/strings.json`; the player can switch language in Options (defaults to the Windows UI language).

## Data formats

### `data/game.json`

```jsonc
{
  "titleImage": "images/title.png",
  "titleMusic": "music/title.wav",
  "startDialog": "dialog/intro.json",       // first dialog after player select
  "credits": "data/credits.json",
  "website": "https://j2i.net/apps/pluto-fall",
  "startLives": 3, "startHealth": 10, "startBombs": 3,
  "fireInterval": 0.11,                     // seconds between bullets while fire is held
  "bombRadius": 5.0, "bombDamage": 12,
  "gameOverWhenAnyPlayerDies": true,        // false = continue while any player has lives left
  "playerBullet": { "color": "#66F2FF", "size": [0.7,0.14,0.14], "damage": 1, "radius": 0.25, "speed": 26 },
  "playerBomb":   { "color": "#FFD933", "size": [0.5,0.5,0.5], "damage": 2, "radius": 0.4, "speed": 13 },
  "players": [ { "name": {"en": "Arrow"}, "model": "models/player_arrow.glb", "textureFolder": "textures",
                 "transform": {"scale": 1}, "speed": 11, "radius": 0.45 } ],
  "sounds": { "shoot": "...", "bomb": "...", "explosion": "...", "bigExplosion": "...", "hit": "...",
              "playerHit": "...", "powerup": "...", "extraLife": "...", "menuMove": "...", "menuSelect": "...",
              "gameOver": "..." }
}
```

A projectile may use a `"model"` (+ `"textureFolder"`, `"transform"`) instead of the default glowing box.

### Enemy types — `data/enemies/<name>.json`

```jsonc
{
  "name": "saucer",                         // class name used by levels (defaults to the file name)
  "model": "models/enemy_saucer.glb",       // .glb or .gltf
  "textureFolder": "textures",              // where the model's texture images are looked up
  "transform": { "scale": 1.0, "rotation": [0, 0, 0], "translation": [0, 0, 0] },
  "life": 8,                                // default life points
  "radius": 0.9,                            // collision radius
  "score": 300,
  "boss": false,
  "contactDamage": 3,
  "motion": { "name": "hold", "distance": 7, "speed": 5, "holdTime": 5 },   // default motion function + params
  "fire":   { "name": "homed", "interval": 1.3, "delay": 1.2, "speed": 9, "count": 3, "spread": 12 },
  "projectile": { "color": "#B3FF4D", "size": [0.3,0.3,0.3], "damage": 2, "life": 2, "radius": 0.25, "sound": "" },
  "deathSound": "sounds/explosion.wav",
  "drop": "health"                          // optional item class dropped on death
}
```

`transform` may also be 16 numbers: a row-major matrix in DirectXMath's row-vector convention.
Rotation angles are degrees (pitch, yaw, roll).

Materials come from the glTF file: base colour factor/texture, emissive factor/texture, metallic, roughness,
alpha mode (`OPAQUE`/`MASK`/`BLEND`), `doubleSided` and `KHR_materials_unlit`. A shader permutation is compiled
for each combination of features in use.

### Item types — `data/items/<name>.json`

```jsonc
{
  "name": "life",
  "model": "models/item_life.glb", "textureFolder": "textures", "transform": { "scale": 1.1 },
  "radius": 0.8,
  "effects": { "lives": 1, "health": 0, "maxHealth": 0, "bombs": 0, "score": 500 },
  "motion": { "name": "scroll" },
  "sound": "sounds/extra_life.wav",
  "spin": 120,                              // degrees per second
  "glow": "#665010"                         // pulsing highlight colour
}
```

### Levels — `levels/*.json`

```jsonc
{
  "name": { "en": "Sector 1: The Kuiper Belt" },
  "orientation": "landscape",               // or "portrait"
  "length": 220,                            // scroll distance until the end of the play area
  "scrollSpeed": 4.0,                       // units per second
  "terminal": false,                        // true = return to the title screen when completed
  "music": "music/level1.wav",
  "backgroundColor": "#02030A",
  "backgroundImage": "images/space_landscape.png",   // optional tiling, scrolling image
  "backgroundParallax": 0.2,
  "nextDialogs": [ "dialog/midpoint.json", "dialog/midpoint_alt.json" ],  // one is chosen at random
  "enemies": [
    { "at": [40, 0], "class": "fighter", "position": [18, 3, 0],
      "motion": { "name": "wave", "amplitude": 3 },   // optional; otherwise the class default
      "fire": "homed",                                // optional
      "boss": false, "life": 6 }                      // optional overrides
  ],
  "powerups": [ { "at": [60, 0], "class": "bomb", "position": [18, -2, 0] } ],
  "background": [
    { "model": "models/asteroid.glb", "textureFolder": "textures", "transform": { "scale": 1.5 },
      "position": [80, 4, 20], "spin": [10, 30, 0], "motion": "static" }
  ]
}
```

* `at` is the scroll distance at which the object becomes active: the **x** value is used in landscape
  levels and the **y** value in portrait levels.
* `position` is the starting playfield position (relative to the screen centre). Spawn just outside the
  visible edge (e.g. x = 18 in landscape, y = 11 in portrait) for objects to fly in.
* Non-boss enemies are destroyed once they move well outside the playfield. Bosses stay on screen until destroyed.
* When the scroll reaches `length` it stops, any remaining spawns appear, and play continues until every
  boss is destroyed. The players then fly off the edge of the screen, the view fades to black, and one of
  `nextDialogs` is chosen at random (or the title screen is shown for terminal levels).
* If a player runs out of lives the game returns to the title screen (see `gameOverWhenAnyPlayerDies`).

### Motion functions

A motion function maps *(time since activation, orientation)* → *(x, y, z)*. Each is written in terms of
the scroll direction ("forward": left in landscape, down in portrait) and the perpendicular "across" axis,
so the same definition works in both orientations.

| Name | Parameters (defaults) | Behaviour |
|---|---|---|
| `forward` | `speed` (4) | Moves in the scroll direction |
| `wave` | `speed` (3.5), `amplitude` (2), `frequency` Hz (0.4), `phase` (0) | Forward on a sine wave |
| `hold` | `target` [x,y] **or** `distance` (6), `speed` (6), `holdTime` (0 = forever), `exitSpeed` (6) | Glides to a position and holds it |
| `scroll` | `factor` (1) | Moves with the scrolling background |
| `static` | — | Stays at its start position |
| `circle` | `radius` (2), `angularSpeed` rad/s (2), `speed` (2.5) | Loops while drifting forward |
| `dive` | `distance` (4), `entrySpeed` (6), `delay` (1), `speed` (14) | Enters, pauses, then dives |

Register new ones with `MotionRegistry::Register("name", factory)`.

### Fire functions

A fire function receives *(time, enemy position, living player positions, orientation)* and returns
either nothing or a volley of projectile spawns. All accept `interval` (s), `delay` (first shot, s),
`speed`, `count` and `spread` (degrees).

| Name | Behaviour |
|---|---|
| `none` | Never fires |
| `straight-ahead` | Fires in the scroll direction (a fan when `count` > 1) |
| `homed` | Aims at the nearest player |
| `radial` | Ring of `count` shots |
| `spiral` | Rotating stream; extra parameter `rotation` (degrees per shot) |

Register new ones with `FireRegistry::Register("name", factory)`.

### Dialogs — `dialog/*.json`

```jsonc
{
  "typeSound": "sounds/type.wav",           // looped while text is typed (WAV, MP3 or MP4)
  "music": "music/dialog.wav",              // optional
  "charactersPerSecond": 40,
  "boxColor": "#05103AC8", "borderColor": "#73BFFF", "textColor": "#FFFFFF",
  "segments": [
    {
      "id": "briefing",
      "background": "images/bg_bridge.png",  // full-screen background
      "terminal": false,                     // true = dialog ends after this segment (unless a goto fires)
      "entries": [
        { "speaker": { "en": "Commander Vega" }, "portrait": "images/portrait_commander.png",
          "text": { "en": "How do you want to get there?" },
          "key": "route",                    // makes this entry a question
          "responses": [
            { "id": "belt",   "text": { "en": "Through the belt" }, "goto": "belt" },
            { "id": "direct", "text": { "en": "Straight in" } }
          ] }
      ]
    }
  ],
  "nextLevels": [
    { "key": "route=belt", "level": "levels/level1_belt.json" },
    { "key": "default",    "level": "levels/level1_direct.json" }
  ]
}
```

Answers are stored as `key → response id`. When the dialog ends, `SelectNextLevel(choices, nextLevels)`
picks the level: each `key` is a list of conditions joined with `&` (or `,`), each `question=answer`,
`question!=answer`, or just `question` (answered at all). The first option whose conditions all hold
wins; `default`, `*` or an empty key is the fallback. Answers accumulate across dialogs in a play-through.

### Credits — `data/credits.json`

```json
{ "credits": [
  { "title": { "en": "Programming" },
    "contributors": [ { "value": "Joel Ivory Johnson", "color": "#8FD8FF" } ] }
] }
```

### Settings

Volumes, language and fullscreen are saved to `%LOCALAPPDATA%\PlutoFall\settings.json`.

## Dialogue editor

`DialogEditor` (in the same solution) is a Windows tool for editing the dialog files in
`assets/dialog`. Build it alongside the game and run `DialogEditor.exe`.

**Finding the assets folder.** The editor uses the folder given with `/asset`, for example
`DialogEditor.exe /asset "C:\Projects\pluto-fall\PlutoFall\assets"`. Without it, it looks for a
folder named `assets` next to the executable, then in each parent folder in turn. If none is found it
offers to let you browse for one. The folder in use is shown in the toolbar and title bar.

> Note: the game's build copies `PlutoFall\assets` next to the executables (`bin\<Platform>\<Configuration>\assets`),
> so running `DialogEditor.exe` straight from that folder edits that **copy**, and the next game build can
> overwrite your changes. When debugging from Visual Studio the project passes
> `/asset "<solution>\PlutoFall\assets"` so the source files are edited. Use `/asset` the same way in a shortcut.

**Left pane** – every `.json` file in `assets/dialog`, with the root `"title"` (if any) shown under the
file name. Double-click a file (or right-click → **Open**) to edit it. Right-click also offers **Rename**
(renames the file on disk immediately) and **Delete** / **Undelete**. Delete only *marks* the file (a trash
can appears); files are deleted when you click **Save** and confirm the list. Answering **No** to that
confirmation cancels the whole save. **New dialogue** asks for a file name and creates a starter file.

**Right pane** – one tab per open file (`*` = unsaved; right-click a tab or press Ctrl+W to close it).
Each tab has:

* a **language** selector listing the languages used in the file (default `en`) and a **+** button to
  add a language code; text fields show and edit only the selected language;
* the **segments** list, which can be added to, removed from and reordered;
* the selected segment's **id** (renaming it updates `goto` references), **terminal** flag and
  **background** image (click the thumbnail for a full-screen view; **Change...** opens an image
  picker for `assets/images`);
* the segment's **entries**: speaker, text, portrait, and optionally a question **key** with
  **responses** (id, text, goto);
* the dialog's **next levels** (key and level).

**What is preserved.** The editor works directly on the file's text structure, so comments,
formatting, key order, number formatting and any attributes it doesn't know about are kept. Untouched
files are saved byte-for-byte identical. Plain-string text (shared by every language) stays a plain string
when edited in English and becomes a per-language object when another language is edited (the original
text is kept as `en`).

**Shortcuts:** Ctrl+S save · Ctrl+N new dialogue · Ctrl+W close tab · F5 reload the file list.

The editor's JSON and dialog-model code has portable tests in `DialogEditor/tests/ModelTests.cpp`
(see the comment at the top of that file for build commands).

## Regenerating the placeholder assets

```bash
pip install pillow
python tools/generate_assets.py
```

# JSON Basics and Shared Value Types

Every data file in `assets` is JSON. This page describes the dialect the game accepts and the value
types that appear in many different files. The other pages refer back to these types by name
(for example "*Color*" or "*FunctionSpec*").

## The JSON dialect

The game uses its own parser, which accepts standard JSON (UTF-8) plus a few conveniences for
hand-edited files:

* `// line comments` and `/* block comments */`
* trailing commas in arrays and objects: `[1, 2, 3,]`
* a UTF-8 byte-order mark at the start of the file

Other rules:

* Keys are **case-sensitive**: `"scrollSpeed"` works, `"scrollspeed"` does not.
* **Unknown keys are ignored**, so you can add your own notes such as `"_comment": "..."`.
* **Missing keys take their default value**, listed in each format's table.
* If a number is given as a string (`"3"`) it is still read as a number; booleans may be
  `true`/`false`, `1`/`0`, or the strings `"true"`/`"yes"`/`"1"`.
* Class names (enemy and item types) and function names are **case-insensitive**.

## Shared value types

### Path

A string path relative to the `assets` folder, using `/` separators.

```json
"model": "models/enemy_drone.glb"
```

### LocString (multi-language string)

Any text shown to the player. It can be written two ways:

```jsonc
"text": "Same text in every language"
```

```jsonc
"text": { "en": "Good hunting.", "es": "Buena caza.", "ja": "健闘を祈る。" }
```

Keys are language codes from `lang/strings.json` (case-insensitive). When showing text, the game
picks, in order:

1. the exact current language code (e.g. `es`);
2. an entry with the same base language (`es-mx` matches `es`, and vice versa);
3. a plain (non-object) string;
4. English (`en`);
5. the first entry.

Numbers and booleans are also accepted and shown as text.

### Color

Either a hex string or an array of 0–1 numbers.

| Form | Example | Meaning |
|---|---|---|
| `"#RGB"` | `"#F80"` | 4-bit per channel, opaque |
| `"#RRGGBB"` | `"#FF8800"` | opaque |
| `"#RRGGBBAA"` | `"#05103AC8"` | last byte is alpha (opacity) |
| `[r, g, b]` | `[1.0, 0.5, 0.0]` | opaque |
| `[r, g, b, a]` | `[0, 0, 0.2, 0.75]` | with alpha |

The leading `#` is optional. An invalid colour falls back to the field's default.

### Vec2 and Vec3

Positions, sizes and similar values.

| Form | Example | Vec3 result | Vec2 result |
|---|---|---|---|
| array of 3 | `[1, 2, 3]` | (1, 2, 3) | (1, 2) |
| array of 2 | `[1, 2]` | (1, 2, *default z*) | (1, 2) |
| single number | `1.5` | (1.5, 1.5, 1.5) | (1.5, 1.5) |
| object | `{ "x": 1, "y": 2, "z": 3 }` | missing components keep their defaults | |

### Transform

Applied to a model before the game positions and orients it. Two forms are accepted.

**Object form** (recommended):

```jsonc
"transform": {
  "scale": 1.5,                 // number (uniform) or [x, y, z]
  "rotation": [0, 90, 0],       // degrees: [pitch (about X), yaw (about Y), roll (about Z)]
  "translation": [0, 0.2, 0]    // offset in model units
}
```

All three keys are optional. They are applied in the order **scale, then rotation, then translation**.
An object may instead contain `"matrix": [16 numbers]`.

**Matrix form:** an array of 16 numbers, a 4×4 matrix written row by row, using DirectXMath's
row-vector convention (the translation is in elements 13–15):

```json
"transform": [1,0,0,0,  0,1,0,0,  0,0,1,0,  0,0.2,0,1]
```

A missing or invalid transform is the identity (no change).

### ModelSpec (model reference)

Several definitions describe a model with these three keys placed directly in the object:

| Key | Type | Default | Meaning |
|---|---|---|---|
| `model` | Path | — | A `.glb` or `.gltf` file. If missing or unloadable, a random-colour cube is used. |
| `textureFolder` | Path | the model's own folder | Folder searched first for the model's texture images. |
| `transform` | Transform | identity | Applied to the model before placement. |

Models should be authored with the **nose along +X and the top along +Y**; see
[media-files.md](media-files.md#models).

### FunctionSpec (motion or fire function reference)

Names a motion function or fire function and gives its parameters. Three forms are accepted:

```jsonc
"motion": "forward"                                            // name only, default parameters
"motion": { "name": "wave", "speed": 3, "amplitude": 2.5 }     // parameters inline
"motion": { "name": "wave", "params": { "speed": 3 } }         // parameters in "params"
```

The available names and their parameters are listed in
[motion-and-fire-functions.md](motion-and-fire-functions.md).

### ProjectileSpec

Describes a projectile, used for the player's bullets and bombs in `game.json` and for each enemy
type's shots.

| Key | Type | Meaning |
|---|---|---|
| `model`, `textureFolder`, `transform` | ModelSpec | Optional. When `model` is present the projectile is drawn with it; otherwise as a glowing box. |
| `color` | Color | Colour of the default glowing box. |
| `size` | Vec3 | Size of the default glowing box. The box's long axis should be X; it is turned to face its direction of travel. |
| `damage` | number | Health removed from whatever it hits. |
| `life` | number | How much damage the projectile can absorb before it is destroyed. Only meaningful for enemy projectiles, which players can shoot down. |
| `radius` | number | Collision radius. |
| `speed` | number | Speed in units per second. Used for player projectiles; enemy projectile speed comes from the fire function. |
| `sound` | Path | Sound played when fired. |

Defaults depend on where the projectile is used; see [game-config.md](game-config.md) and
[enemies-and-items.md](enemies-and-items.md).

## Units and coordinates

* Distances are in **game units**. The visible screen is 32 × 18 units in landscape levels.
* Times are in **seconds**, angles in **degrees** unless a table says otherwise (a few motion
  parameters use radians, and are marked as such).
* Speeds are units per second.

The coordinate systems used for spawns and background objects are explained in
[levels.md](levels.md#coordinate-systems).

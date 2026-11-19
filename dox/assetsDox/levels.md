# Levels — `levels/*.json`

A level file describes one stage: its scroll direction and length, what spawns and when, decorative
background objects, music, and which dialogs may follow it. Levels are loaded when a dialog selects
them through its `nextLevels` list ([dialogs.md](dialogs.md)).

## Example

```jsonc
{
  "name": { "en": "Sector 1: The Kuiper Belt", "es": "Sector 1: El cinturón de Kuiper" },
  "orientation": "landscape",
  "length": 220,
  "scrollSpeed": 4.0,
  "terminal": false,
  "music": "music/level1.wav",
  "backgroundColor": "#02030A",
  "backgroundImage": "images/space_landscape.png",
  "backgroundParallax": 0.2,
  "nextDialogs": [ "dialog/midpoint.json", "dialog/midpoint_alt.json" ],

  "enemies": [
    { "at": [12, 0], "class": "drone",   "position": [18, 2, 0] },
    { "at": [40, 0], "class": "fighter", "position": [18, -3, 0], "fire": "homed" },
    { "at": [60, 0], "class": "drone",   "position": [18, 0, 0],
      "motion": { "name": "circle", "radius": 3, "angularSpeed": 1.5, "speed": 3 } },
    { "at": [220, 0], "class": "cruiser", "position": [21, 0, 0], "boss": true }
  ],

  "powerups": [
    { "at": [30, 0], "class": "bomb", "position": [18, -2, 0] }
  ],

  "background": [
    { "model": "models/asteroid.glb", "textureFolder": "textures", "transform": { "scale": 1.5 },
      "position": [80, 4, 20], "spin": [10, 30, 0] },
    { "model": "models/pluto.glb", "textureFolder": "textures", "transform": { "scale": 30 },
      "position": [140, -28, 150], "spin": [0, 4, 0] }
  ]
}
```

## Top-level keys

| Key | Type | Default | Meaning |
|---|---|---|---|
| `name` | LocString | empty | Shown as a banner when the level starts. |
| `orientation` | `"landscape"` or `"portrait"` | `"landscape"` | **Landscape** scrolls right-to-left (players fly right). **Portrait** scrolls top-to-bottom (players fly up). Any value other than `"portrait"` means landscape. |
| `length` | number | 200 (minimum 1) | Scroll distance to the end of the play area. |
| `scrollSpeed` | number | 3 (minimum 0.1) | Scroll speed in units per second. Play-area duration ≈ `length / scrollSpeed` seconds. |
| `terminal` | bool | `false` | `true`: completing this level returns to the title screen (the end of the game). |
| `music` | Path | none (previous music continues) | Looping level music. |
| `backgroundColor` | Color | `[0.01, 0.01, 0.04]` | Fills the screen behind everything. |
| `backgroundImage` | Path | none | Optional image tiled and scrolled behind the 3D scene ([media-files.md](media-files.md#scrolling-level-backgrounds)). |
| `backgroundParallax` | number | 0.25 | How fast the background image scrolls relative to the level: 1 = same speed, 0 = stationary. |
| `nextDialogs` | list of Paths | empty | Dialogs that may follow this level. One is chosen at random. If empty, the title screen follows. Each entry may be a string, or an object with a `"file"` (or `"dialog"`) key; a single string instead of an array is also accepted. |
| `enemies` | array of Spawns | empty | Enemies placed in the level. |
| `powerups` | array of Spawns | empty | Items placed in the level. `items` is accepted as another name for the same list (both are read). |
| `background` | array of Background objects | empty | Decorative 3D objects. |

## Coordinate systems

Two coordinate systems are used. Both are measured in game units on the z = 0 play plane, with +x
right and +y up on screen.

### Playfield coordinates (spawn positions)

Relative to the **centre of the visible screen**, whatever the scroll position.

| Orientation | Visible playfield | Typical off-screen spawn line | Players fly toward |
|---|---|---|---|
| Landscape | x from -16 to 16, y from -9 to 9 | x = 18 (right edge) | +x (right) |
| Portrait | x from -8 to 8, y from -9 to 9 | y = 11 (top edge) | +y (up) |

In portrait levels the sides of the 16:9 window outside x = ±8 are covered by HUD panels. In
landscape levels the player HUD covers roughly the top 2 units of the screen, so avoid putting
important spawns at y > 7.

Players are kept 0.7 units inside the playfield edges.

### Level coordinates (background objects)

Fixed positions in the level that scroll past. The camera starts at (0, 0) and moves by the scroll
distance: along +x in landscape levels, along +y in portrait levels. A background object at
`[100, 4, 20]` in a landscape level is centred on screen (horizontally) when the level has scrolled
100 units.

The **z** coordinate is depth: z = 0 is the play plane, positive z is further away. Distant objects
look smaller and scroll past more slowly (parallax). At depth z the visible area is wider by a factor
of about `(21.7 + z) / 21.7`; for example at z = 22 it is twice as wide (x from about -32 to 32 in
landscape). Negative z (closer than the play plane, up to about -20) is allowed but will cover the action.

## Spawns (`enemies` and `powerups` entries)

```jsonc
{ "at": [40, 0], "class": "saucer", "position": [18, 3, 0],
  "motion": "wave", "fire": { "name": "radial", "count": 8 }, "boss": false, "life": 12 }
```

| Key | Type | Default | Meaning |
|---|---|---|---|
| `at` | Vec2 | `[0, 0]` | Scroll distance at which the object becomes active. **Landscape levels use the x value; portrait levels use the y value.** The other value is ignored. |
| `class` | string | — | Enemy or item type name ([enemies-and-items.md](enemies-and-items.md)). `type` is accepted as another name for this key. Unknown names spawn a random-colour cube and are logged. |
| `position` | Vec3 | `[0, 0, 0]` | Starting position in **playfield coordinates**. Keep z at 0 for anything that should collide on the play plane (collisions ignore z, but objects drawn at other depths will look misplaced). |
| `motion` | FunctionSpec | the type's `motion` | Overrides the motion function. The spawn's parameters replace the type's parameters entirely; they are not merged. |
| `fire` | FunctionSpec | the type's `fire` | Enemies only. Overrides the fire function (parameters are not merged either). |
| `boss` | bool | the type's `boss` | Enemies only. Makes this instance a boss (or not). |
| `life` | number | the type's `life` | Enemies only. Overrides life points. |

Notes:

* Spawns do not need to be listed in order; the game sorts them by their activation value.
* All spawns with activation ≤ the current scroll distance appear immediately, so several spawns
  with the same `at` appear together.
* The motion function's clock starts when the object appears; see
  [motion-and-fire-functions.md](motion-and-fire-functions.md).

## Background objects (`background` entries)

```jsonc
{ "model": "models/station.glb", "textureFolder": "textures",
  "transform": { "rotation": [70, 0, 0], "scale": 2.5 },
  "position": [60, 6, 25], "spin": [0, 0, 10], "motion": "static" }
```

| Key | Type | Default | Meaning |
|---|---|---|---|
| `model`, `textureFolder`, `transform` | ModelSpec | cube | The object's model. |
| `position` | Vec3 | `[0, 0, 10]` | Position in **level coordinates**. |
| `spin` | Vec3 | `[0, 0, 0]` | Continuous rotation in degrees per second about the x, y and z axes. |
| `motion` | FunctionSpec | none (stays at `position`) | Optional motion function. It starts at `position` with the level's clock (time since the level began) and produces a level-coordinate position. |

Background objects have no collision and never interact with players, enemies or projectiles. They are
present for the whole level and are simply not drawn while out of view.

## How a level ends

1. Scrolling stops when the scroll distance reaches `length`.
2. Any spawns that have not yet appeared appear now (this is how a boss placed at or past the end
   arrives).
3. Play continues until **every boss is destroyed**. If there are no bosses, the level ends at once.
   Regular enemies left on screen do not delay the end.
4. All enemy projectiles are cleared and the players automatically fly off the edge of the screen.
5. The screen fades to black.
6. If the level is `terminal`, or `nextDialogs` is empty, the title screen is shown. Otherwise one
   entry of `nextDialogs` is picked at random and that dialog begins.

If a player runs out of lives the game ends instead (see `gameOverWhenAnyPlayerDies` in
[game-config.md](game-config.md)).

## Start of a level

The players fly in from off-screen over 1.6 seconds while the level name is shown. Scrolling (and
spawning) starts immediately, so the first spawns should use an `at` of about 10 or more. Players are
invulnerable for the first two seconds.

## Shipped levels

| File | Orientation | Length / speed | Boss | Terminal | Followed by |
|---|---|---|---|---|---|
| `level1_belt.json` | landscape | 220 / 4.0 | cruiser | no | `midpoint` or `midpoint_alt` |
| `level1_direct.json` | portrait | 200 / 4.0 | cruiser | no | `midpoint` or `midpoint_alt` |
| `level2_core.json` | portrait | 240 / 4.5 | sentinel | yes | — |
| `level2_charon.json` | landscape | 240 / 4.5 | sentinel | yes | — |

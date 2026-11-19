# Motion and Fire Functions

Motion functions decide **where** an object is; fire functions decide **when and how** an enemy shoots.
Both are referenced by name with a [FunctionSpec](json-basics.md#functionspec-motion-or-fire-function-reference):

```jsonc
"motion": "forward"
"motion": { "name": "wave", "speed": 3, "amplitude": 2.5 }
"fire":   { "name": "homed", "params": { "interval": 1.0, "count": 3 } }
```

Names are case-insensitive. Parameters not given take the defaults below; unknown parameters are ignored.

## Where they can be used

| Location | Motion | Fire |
|---|---|---|
| Enemy type (`data/enemies/*.json`) | default for the type (default `forward`) | default for the type (default `none`) |
| Item type (`data/items/*.json`) | default for the type (default `scroll`) | — |
| Level spawn (`enemies` / `powerups` entry) | overrides the type | overrides the type (enemies) |
| Level background object | optional | — |

## Directions: "forward" and "across"

Motion functions are written in terms of two axes so that the same definition works in both
orientations:

| Orientation | **forward** (the direction the scenery moves) | **across** (perpendicular) |
|---|---|---|
| Landscape | left (-x) | up (+y) |
| Portrait | down (-y) | right (+x) |

"Forward" is toward the players' side of the screen, so `forward` with a positive speed moves an
enemy toward the players.

## Motion functions

Each motion function computes a position from the time since the object appeared (for background
objects, the time since the level started) and its starting position. The z coordinate stays at the
starting z.

### `forward`

Moves in the forward direction at constant speed.

| Parameter | Default | Meaning |
|---|---|---|
| `speed` | 4 | Units per second. Negative values move backward. |

### `wave`

Moves forward while oscillating across on a sine wave.

| Parameter | Default | Meaning |
|---|---|---|
| `speed` | 3.5 | Forward speed, units per second. |
| `amplitude` | 2 | Maximum sideways offset in units. |
| `frequency` | 0.4 | Oscillations per second. |
| `phase` | 0 | Starting phase in **radians** (use 3.14159 to start by moving the other way). |

### `hold`

Glides to a position, decelerating as it arrives, and holds there. Optionally leaves after a while.

| Parameter | Default | Meaning |
|---|---|---|
| `target` | none | `[x, y]` in **playfield coordinates** to move to. Because these are absolute screen coordinates, a target suits only one orientation. |
| `distance` | 6 | Used when `target` is absent: move this far forward from the start position. Works in both orientations. |
| `speed` | 6 | Approach speed (sets how long the glide takes). |
| `holdTime` | 0 | Seconds to stay before leaving. 0 = stay forever. |
| `exitSpeed` | 6 | When leaving, the object moves forward, speeding up as it goes; larger values leave faster. |

### `scroll`

Moves with the scrolling scenery, as if fixed to the ground. Good for power-ups and turrets.

| Parameter | Default | Meaning |
|---|---|---|
| `factor` | 1 | Multiple of the level's `scrollSpeed`. 0.5 drifts at half the scenery speed. |

### `static`

Stays at the start position (on screen, not scrolling). No parameters.

### `circle`

Loops around in a circle while drifting forward. Starts on the circle, so there is no jump at the start.

| Parameter | Default | Meaning |
|---|---|---|
| `radius` | 2 | Circle radius in units. |
| `angularSpeed` | 2 | Rotation speed in **radians** per second (6.28 = one loop per second). |
| `speed` | 2.5 | Forward drift speed. |

### `dive`

Enters, pauses, then accelerates forward.

| Parameter | Default | Meaning |
|---|---|---|
| `distance` | 4 | How far forward to move during entry. |
| `entrySpeed` | 6 | Speed during entry. |
| `delay` | 1 | Seconds to pause after entering. |
| `speed` | 14 | Strength of the dive acceleration. |

## Fire functions

A fire function is consulted every frame while its enemy is on screen. It receives the enemy's age,
its position and the positions of all living players, and either does nothing or fires a **volley**
of one or more projectiles. What the projectiles look like and how much damage they do comes from the
enemy type's `projectile` ([enemies-and-items.md](enemies-and-items.md)).

### Common parameters

All fire functions accept these:

| Parameter | Default | Meaning |
|---|---|---|
| `interval` | 1.6 | Seconds between volleys (minimum 0.05). |
| `delay` | 0.8 | Seconds after the enemy appears before the first volley. |
| `speed` | 8 | Projectile speed, units per second. |
| `count` | 1 | Projectiles per volley. |
| `spread` | 15 | For fans: degrees from the centre line to each outer shot. |

Enemies only fire while inside the visible playfield, so with a long `delay` an enemy may never fire.

### `none`

Never fires. This is the default for enemy types without a `fire` entry.

### `straight-ahead`

Fires in the forward direction (toward the players' side of the screen). With `count` > 1, the shots
fan out evenly across ±`spread` degrees.

### `homed`

Aims at the nearest living player at the moment of firing (the shots fly straight; they do not
steer). With `count` > 1, fans across ±`spread` degrees around that aim.

### `radial`

Fires `count` shots evenly around a full circle, with a random starting angle each volley.
Default `count` is 12. `spread` is not used.

### `spiral`

Fires a rotating stream: each volley is turned further than the last.
Defaults: `interval` 0.12, `count` 2 (shots evenly spaced around the circle in each volley).

| Extra parameter | Default | Meaning |
|---|---|---|
| `rotation` | 17 | Degrees the pattern turns between volleys. |

## Adding new functions

New functions are added in C++ (`MotionFunctions.cpp` / `FireFunctions.cpp`) and registered by name
with `MotionRegistry::Register("name", factory)` or `FireRegistry::Register("name", factory)`. Once
registered, they can be used from any JSON file by that name.

# Enemy and Item Types

Enemy types live in `data/enemies/` and power-up (item) types in `data/items/`. Levels spawn them by
class name ([levels.md](levels.md)).

## Files and naming

* Every `.json` file in the folder is loaded, in alphabetical order of file name. Other files are ignored.
* A file holds **one definition object**, or an **array of definition objects**.
* The class name is the `name` key; if there is none, the file name without `.json` is used.
  Class names are case-insensitive (stored lower-case).
* If two definitions share a name, the one loaded last wins.

## Enemy types — `data/enemies/<name>.json`

### Example

```jsonc
{
  "name": "saucer",
  "model": "models/enemy_saucer.glb",
  "textureFolder": "textures",
  "transform": { "scale": 1.0 },
  "life": 8,
  "radius": 0.9,
  "score": 300,
  "contactDamage": 3,
  "boss": false,
  "motion": { "name": "hold", "distance": 7, "speed": 5, "holdTime": 5, "exitSpeed": 5 },
  "fire": { "name": "homed", "interval": 1.3, "delay": 1.2, "speed": 9, "count": 3, "spread": 12 },
  "projectile": { "color": "#B3FF4D", "size": [0.3, 0.3, 0.3], "damage": 2, "life": 2, "radius": 0.25 },
  "deathSound": "sounds/explosion.wav",
  "drop": "health"
}
```

### Keys

| Key | Type | Default | Meaning |
|---|---|---|---|
| `name` | string | file name | Class name used by levels. |
| `model`, `textureFolder`, `transform` | [ModelSpec](json-basics.md#modelspec-model-reference) | cube | The enemy's model. Author it with the nose along +X; the game turns enemies to face the players. |
| `life` | number | 3 | Default life points. A level spawn can override it. |
| `radius` | number | 0.8 | Collision radius. Used for bullets, bomb hits, bomb blasts and contact with players. |
| `score` | integer | 100 | Points awarded to the player who dealt the final hit. |
| `boss` | bool | `false` | Default boss flag. A level spawn can override it. See [Bosses](#bosses). |
| `contactDamage` | number | 2 | Damage to a player who touches the enemy. A non-boss enemy also takes 4 damage from the collision. |
| `motion` | [FunctionSpec](json-basics.md#functionspec-motion-or-fire-function-reference) | `forward` | Default motion function and parameters. |
| `fire` | FunctionSpec | `none` | Default fire function and parameters. |
| `projectile` | [ProjectileSpec](json-basics.md#projectilespec) | see below | What the enemy fires. |
| `deathSound` | Path | `sounds.explosion` / `sounds.bigExplosion` | Played when destroyed. |
| `drop` | string | none | Item class spawned where the enemy is destroyed. It then moves with the scrolling background. |

**`projectile` defaults for enemies:** `color` `[1.0, 0.55, 0.15]` (orange), `size`
`[0.45, 0.18, 0.18]`, `damage` 1, `life` 1, `radius` 0.22. The projectile's speed and direction come
from the fire function, so `speed` here is not used. If `sound` is set it plays each time the enemy fires.

### Behaviour

* An enemy fires only while it is inside the visible playfield, and only during normal play.
* A **non-boss** enemy is removed once it has drifted more than 4 units outside the playfield, but
  only after it has been on screen at least once (or 15 seconds after spawning), so enemies can spawn
  off-screen and fly in.
* Enemy projectiles can be shot down: each player bullet removes its `damage` from the projectile's `life`.

### Bosses

An enemy is a boss when its type has `"boss": true` or the level spawn sets `"boss": true`.

* Once a boss has entered the screen it is kept on screen, whatever its motion function does.
* A boss is never removed for being off screen.
* While a boss is alive the HUD shows a boss health bar (the total of all living bosses).
* When a boss spawns, a flashing **WARNING** banner is shown.
* The level cannot end while a boss is alive (see [levels.md](levels.md#how-a-level-ends)).

## Item types — `data/items/<name>.json`

Items (power-ups) are collected when a player touches them.

### Example

```jsonc
{
  "name": "life",
  "model": "models/item_life.glb",
  "textureFolder": "textures",
  "transform": { "scale": 1.1 },
  "radius": 0.8,
  "effects": { "lives": 1, "score": 500 },
  "motion": { "name": "scroll", "factor": 1.0 },
  "sound": "sounds/extra_life.wav",
  "spin": 120,
  "glow": "#665010"
}
```

### Keys

| Key | Type | Default | Meaning |
|---|---|---|---|
| `name` | string | file name | Class name used by levels and by enemies' `drop`. |
| `model`, `textureFolder`, `transform` | ModelSpec | cube | The item's model. |
| `radius` | number | 0.8 | Pick-up radius. |
| `effects` | object | — | What the item gives; see below. The effect keys may also be placed directly in the item object instead of inside `effects`. |
| `motion` | FunctionSpec | `scroll` | Default motion function. |
| `sound` | Path | `sounds.extraLife` if it gives lives, otherwise `sounds.powerup` | Played when collected. |
| `spin` | number | 90 | Rotation speed about the model's vertical axis, in degrees per second. |
| `glow` | Color | `[0.2, 0.2, 0.2]` | Colour of the item's pulsing highlight. |

### `effects`

All are integers and default to 0.

| Key | Effect |
|---|---|
| `lives` | Added to the player's lives. |
| `health` | Added to the player's health, capped at the player's maximum health. |
| `maxHealth` | Raises the player's maximum health by this amount, and also fills the new capacity (adds the same amount to health). |
| `bombs` | Added to the player's bomb count. |
| `score` | Added to the player's score. |

An item is removed when it drifts more than 4 units outside the playfield (after having been on
screen, or 15 seconds after spawning).

## Shipped types

| Enemy | Notes |
|---|---|
| `drone` | Weak; follows a sine wave; does not fire. |
| `fighter` | Flies straight; fires straight ahead. |
| `saucer` | Glides in, holds for 5 s, leaves; fires 3-shot fans at the nearest player; drops `health`. |
| `cruiser` | Boss. Holds position; radial bursts; drops `life`. |
| `sentinel` | Boss. Circles; spiral fire. |

| Item | Effect |
|---|---|
| `health` | +4 health |
| `life` | +1 life, +500 score |
| `bomb` | +2 bombs |
| `maxhealth` | +2 maximum health and +4 health (its own +2 health plus +2 to fill the new capacity) |

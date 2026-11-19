# `data/game.json` — Global Configuration

`data/game.json` holds settings that apply to the whole game: where play starts, the player craft,
the players' weapons, starting stats and common sounds. It also marks the location of the assets
folder: the game identifies the folder by finding this file in it.

If the file is missing or invalid, every key takes its default value below.

## Example

```jsonc
{
  "titleImage": "images/title.png",
  "titleMusic": "music/title.wav",
  "startDialog": "dialog/intro.json",
  "credits": "data/credits.json",
  "website": "https://j2i.net/apps/pluto-fall",
  "selectBackground": "images/bg_orbit.png",

  "startLives": 3,
  "startHealth": 10,
  "startBombs": 3,
  "fireInterval": 0.11,
  "bombRadius": 5.0,
  "bombDamage": 12,
  "gameOverWhenAnyPlayerDies": true,

  "playerBullet": { "color": "#66F2FF", "size": [0.7, 0.14, 0.14], "damage": 1, "radius": 0.25, "speed": 26 },
  "playerBomb":   { "color": "#FFD933", "size": [0.5, 0.5, 0.5], "radius": 0.4, "speed": 13 },

  "players": [
    {
      "name": { "en": "Arrow", "es": "Flecha", "ja": "アロー" },
      "model": "models/player_arrow.glb",
      "textureFolder": "textures",
      "transform": { "scale": 1.0 },
      "speed": 11,
      "radius": 0.45
    }
  ],

  "sounds": {
    "shoot": "sounds/shoot.wav",
    "bomb": "sounds/bomb.wav",
    "explosion": "sounds/explosion.wav",
    "bigExplosion": "sounds/big_explosion.wav",
    "hit": "sounds/hit.wav",
    "playerHit": "sounds/player_hit.wav",
    "powerup": "sounds/powerup.wav",
    "extraLife": "sounds/extra_life.wav",
    "menuMove": "sounds/menu_move.wav",
    "menuSelect": "sounds/menu_select.wav",
    "gameOver": "sounds/game_over.wav"
  }
}
```

## Top-level keys

| Key | Type | Default | Meaning |
|---|---|---|---|
| `titleImage` | Path | `images/title.png` | Full-screen title art. Also shown dimmed behind Options and Credits. |
| `titleMusic` | Path | none | Music for the title, options, credits and player-select screens. |
| `startDialog` | Path | `dialog/intro.json` | First dialog after player select. |
| `credits` | Path | `data/credits.json` | Credits file ([credits.md](credits.md)). |
| `website` | string | `https://j2i.net/apps/pluto-fall` | Opened in the browser by *Visit Game Website*. |
| `selectBackground` | Path | same as `titleImage` | Background of the player-select screen. |
| `startLives` | integer | 3 | Lives each player starts with. |
| `startHealth` | integer | 10 | Starting health, and starting maximum health. |
| `startBombs` | integer | 3 | Bombs each player starts with. |
| `fireInterval` | number | 0.11 | Seconds between bullets while primary fire is held. |
| `bombRadius` | number | 5.0 | Radius of a bomb's blast. |
| `bombDamage` | number | 12 | Damage dealt to every enemy inside the blast. |
| `gameOverWhenAnyPlayerDies` | bool | `true` | `true`: the game ends when any player runs out of lives. `false`: it ends only when every player is out of lives. |
| `playerBullet` | ProjectileSpec | see below | The player's primary weapon. |
| `playerBomb` | ProjectileSpec | see below | The player's secondary weapon. |
| `players` | array | one default craft | Craft the players choose from on the player-select screen. |
| `sounds` | object | see below | Common sound effects. |

Lives, health, maximum health, bombs and score carry over from level to level within a play-through.

## `playerBullet` and `playerBomb`

Both are [ProjectileSpec](json-basics.md#projectilespec) objects. They are fired in the direction the
players fly (right in landscape levels, up in portrait levels).

| Key | `playerBullet` default | `playerBomb` default |
|---|---|---|
| `color` | `[0.4, 0.95, 1.0]` (cyan) | `[1.0, 0.85, 0.2]` (gold) |
| `size` | `[0.7, 0.14, 0.14]` | `[0.5, 0.5, 0.5]` |
| `damage` | 1 | *not used* |
| `radius` | 0.25 | 0.4 |
| `speed` | 26 | 13 |
| `sound` | `sounds.shoot` | `sounds.bomb` |

Behaviour:

* **Bullets** are unlimited. A bullet that touches an enemy removes `damage` from the enemy's life
  and disappears. A bullet that touches an enemy projectile removes `damage` from that projectile's
  `life` and disappears.
* **Bombs** are limited (`startBombs`, plus bomb power-ups). A bomb flies until it touches an enemy,
  then explodes: every enemy within `bombRadius` loses `bombDamage` life, and every enemy projectile
  in that radius is destroyed. A bomb that leaves the screen disappears without exploding. A bomb's
  own `damage` value is ignored; the blast uses `bombDamage`.
* `life` is ignored for player projectiles.

## `players` entries

Each entry is one selectable craft. Players move between them with Left/Right on the player-select
screen.

| Key | Type | Default | Meaning |
|---|---|---|---|
| `name` | LocString | `"Default"` (only if the list is empty) | Shown under the craft on the player-select screen. |
| `model`, `textureFolder`, `transform` | ModelSpec | cube | The craft's model. Author it with the nose along +X. |
| `speed` | number | 11 | Movement speed in units per second at full stick deflection. |
| `radius` | number | 0.45 | Collision radius: smaller is easier to dodge with. |
| `color` | Color | cyan | Read but currently unused. HUD colours come from the player number (P1 cyan, P2 red, P3 green, P4 gold). |

If `players` is empty or missing, one default craft is created (its model falls back to a random-colour cube).

## `sounds`

All keys are optional. A missing file simply plays nothing.

| Key | Default | Played when |
|---|---|---|
| `shoot` | `sounds/shoot.wav` | A player fires a bullet (unless `playerBullet.sound` is set). |
| `bomb` | `sounds/bomb.wav` | A player launches a bomb (unless `playerBomb.sound` is set). |
| `explosion` | `sounds/explosion.wav` | A regular enemy is destroyed (unless the enemy has a `deathSound`). |
| `bigExplosion` | `sounds/big_explosion.wav` | A boss is destroyed, a bomb explodes, or a player loses a life. |
| `hit` | `sounds/hit.wav` | A bullet hits an enemy. |
| `playerHit` | `sounds/player_hit.wav` | A player takes damage. |
| `powerup` | `sounds/powerup.wav` | A power-up without its own `sound` is collected. |
| `extraLife` | same as `powerup` | A power-up that grants lives (and has no `sound`) is collected. |
| `menuMove` | `sounds/menu_move.wav` | Menu selection moves; dialog advances. |
| `menuSelect` | `sounds/menu_select.wav` | A menu item is chosen; a player joins or locks in. |
| `gameOver` | `sounds/game_over.wav` | Game over. |

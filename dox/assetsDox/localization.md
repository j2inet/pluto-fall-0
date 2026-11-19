# Localization — `lang/strings.json`

This file declares the languages the game supports and holds the text of the user interface
(menus, HUD, prompts). Story text, names and level titles are not here: they are written as
multi-language strings ([LocString](json-basics.md#locstring-multi-language-string)) in the files
that use them.

## Example

```jsonc
{
  "languages": [
    { "code": "en", "name": "English",  "locale": "en-us" },
    { "code": "es", "name": "Español",  "locale": "es-es" },
    { "code": "ja", "name": "日本語",    "locale": "ja-jp" }
  ],
  "strings": {
    "menu.start": { "en": "Start Game", "es": "Comenzar", "ja": "ゲームスタート" },
    "game.over":  { "en": "GAME OVER",  "es": "FIN DEL JUEGO", "ja": "ゲームオーバー" }
  }
}
```

## `languages`

The languages offered in the Options screen, in the order they are cycled.

| Key | Type | Default | Meaning |
|---|---|---|---|
| `code` | string | `en` | Language code used as the key in every LocString (case-insensitive). Use a base code such as `es`; a regional code such as `es-mx` also works. |
| `name` | string | the code | Name shown in Options. Write it in the language itself. |
| `locale` | string | the code | Locale given to DirectWrite for text layout and font fallback (important for Japanese, Chinese and Korean glyph choice). |

If the list is missing or empty, English (`en`) is the only language.

**Choosing the language:** the player's choice from Options is saved in
`%LOCALAPPDATA%\PlutoFall\settings.json`. On first run the game uses the Windows display language if
it is in this list (matching `es-MX` to `es`, for example), otherwise English.

**Fonts:** text uses the *Segoe UI* font. Windows substitutes a suitable font for scripts Segoe UI
lacks, so most languages display without extra setup.

## `strings`

An object mapping each UI string key to a LocString. If a key is missing from the file, the key
itself (e.g. `menu.start`) is shown on screen, which makes omissions easy to spot.

`{0}` in a string is replaced with a number where noted.

| Key | Where it appears |
|---|---|
| `title.name` | Large game title on the title screen |
| `title.byline` | "A game from Joel Ivory Johnson" on the title screen |
| `menu.start` | Title menu: Start Game |
| `menu.credits` | Title menu: Credits |
| `menu.website` | Title menu: Visit Game Website |
| `menu.options` | Title menu: Options |
| `menu.quit` | Title menu: Quit |
| `hint.menu` | Control hint at the bottom of the title screen |
| `options.title` | Options screen heading |
| `options.sfx` | Sound effect volume label |
| `options.music` | Music volume label |
| `options.language` | Language label |
| `options.fullscreen` | Fullscreen label |
| `options.on` / `options.off` | Fullscreen value |
| `options.back` | Back item |
| `hint.options` | Control hint on the Options screen |
| `hint.credits` | Control hint on the Credits screen |
| `select.title` | Player-select heading |
| `select.join` | Empty slot prompt (press A / Enter to join) |
| `select.lock` | Joined, not yet locked in |
| `select.ready` | Locked in |
| `select.leave` | Hint under an unlocked slot |
| `select.unlock` | Hint under a locked slot |
| `select.back` | Footer when nobody has joined |
| `select.waiting` | Footer while waiting for everyone to lock in |
| `select.starting` | Footer during the countdown to launch |
| `device.keyboard` | Device name for the keyboard |
| `device.controller` | Device name for a controller; `{0}` is the controller number (1–4) |
| `hud.lives` | HUD label: lives |
| `hud.bombs` | HUD label: bombs |
| `hud.health` | HUD label: health |
| `hud.respawning` | HUD while a player waits to respawn |
| `hud.boss` | Label over the boss health bar |
| `game.warning` | Banner when a boss appears |
| `game.clear` | Banner while the players fly out at the end of a level |
| `game.over` | Game-over text |
| `pause.title` | Pause menu heading |
| `pause.resume` | Pause menu: Resume |
| `pause.quit` | Pause menu: Quit to Title |

## Adding a language

1. Add an entry to `languages` (for example `{ "code": "pt", "name": "Português", "locale": "pt-br" }`).
2. Add a `"pt"` translation to every entry in `strings`.
3. Optionally add `"pt"` text to LocStrings in the other files: dialog text and speakers, level names,
   craft names and credit headings. Wherever a translation is missing, English is shown.

# Credits — `data/credits.json`

The credits screen scrolls a list of sections upward. Each section has a heading (the nature of the
contribution) followed by one line per contributor. The file location is set by `credits` in
`data/game.json` (default `data/credits.json`).

## Example

```jsonc
{
  "credits": [
    {
      "title": { "en": "Programming", "es": "Programación", "ja": "プログラム" },
      "contributors": [
        { "value": "Joel Ivory Johnson", "color": "#8FD8FF" },
        { "value": "Claude (Anthropic)", "color": "#D9A6FF" }
      ]
    },
    {
      "title": "Special Thanks",
      "contributors": [ "You, for playing!" ]
    }
  ]
}
```

The file may also be just the array, without the surrounding `{ "credits": ... }` object.

## Keys

### Section

| Key | Type | Meaning |
|---|---|---|
| `title` | LocString | Section heading, shown in gold. |
| `contributors` | array | The lines under the heading, in order. |

### Contributor

Either an object or a plain string (shown in white).

| Key | Type | Default | Meaning |
|---|---|---|---|
| `value` | string | empty | The text of the line. It is not translated. |
| `color` | Color | white | Colour of the line. |

## Behaviour

* Headings are 80 pixels tall, contributor lines 54 pixels, with a 70-pixel gap after each section
  (on the 1920 × 1080 virtual screen).
* The list starts below the bottom of the screen and scrolls up at 80 pixels per second.
  Holding **Up** speeds it up (to 900 px/s); holding **Down** slows it (to 15 px/s).
* Pressing **fire** or **Esc / Back** returns to the title screen, as does the last line scrolling
  off the top.
* Lines fade in and out near the top and bottom edges.

# Dialogs — `dialog/*.json`

A dialog is a story scene played between levels: full-screen background images, speaker portraits,
text revealed like a typewriter, optional questions, and branching. When a dialog ends it chooses the
next level from the player's answers.

The first dialog is named by `startDialog` in `data/game.json`. Later dialogs are chosen at random
from each completed level's `nextDialogs`.

## Example

```jsonc
{
  "typeSound": "sounds/type.wav",
  "music": "music/dialog.wav",
  "charactersPerSecond": 42,
  "boxColor": "#05103AC8",
  "borderColor": "#73BFFF",
  "textColor": "#FFFFFF",

  "segments": [
    {
      "id": "briefing",
      "background": "images/bg_bridge.png",
      "entries": [
        { "speaker": { "en": "Commander Vega" },
          "portrait": "images/portrait_commander.png",
          "text": { "en": "Pilots, listen up." } },

        { "speaker": { "en": "Commander Vega" },
          "portrait": "images/portrait_commander.png",
          "text": { "en": "How do you want to get there?" },
          "key": "route",
          "responses": [
            { "id": "belt",   "text": { "en": "Through the asteroid belt." }, "goto": "belt" },
            { "id": "direct", "text": { "en": "Straight down their throat." }, "goto": "direct" }
          ] }
      ]
    },
    { "id": "belt", "background": "images/bg_orbit.png", "terminal": true,
      "entries": [ { "speaker": "HALCYON", "portrait": "images/portrait_ai.png", "text": "Plotting a course." } ] },
    { "id": "direct", "background": "images/bg_orbit.png", "terminal": true,
      "entries": [ { "speaker": "Lt. Okoro", "portrait": "images/portrait_pilot.png", "text": "Punching it!" } ] }
  ],

  "nextLevels": [
    { "key": "route=belt",   "level": "levels/level1_belt.json" },
    { "key": "route=direct", "level": "levels/level1_direct.json" },
    { "key": "default",      "level": "levels/level1_belt.json" }
  ]
}
```

## Top-level keys

| Key | Type | Default | Meaning |
|---|---|---|---|
| `typeSound` | Path | none | Sound looped while text is being revealed (WAV, MP3 or MP4). It stops when the line is fully shown. A short click works best. |
| `music` | Path | none (previous music continues) | Looping music for the dialog. |
| `charactersPerSecond` | number | 40 (minimum 1) | Typewriter speed. |
| `boxColor` | Color | `[0.02, 0.04, 0.12, 0.78]` | Background of the text box, name plate and answer list. Use an alpha below 1 for a semi-transparent box. |
| `borderColor` | Color | `[0.45, 0.75, 1.0]` | Solid border around the boxes and portrait. Also used for the "continue" arrow and answer highlight. |
| `textColor` | Color | white | Colour of the spoken text. |
| `segments` | array of Segments | — | The scenes, played in order. `sections` is accepted as another name. |
| `nextLevels` | array of NextLevel options | empty | Levels the dialog may lead to. `next-levels` is accepted as another name. If empty, the title screen follows the dialog. |

## Segments

A segment is one scene: a background image and a list of lines.

| Key | Type | Default | Meaning |
|---|---|---|---|
| `id` | string | `section0`, `section1`, … (by position) | Name used by `goto`. Must be unique within the file. |
| `background` | Path | none (black) | Full-screen background image for the whole segment. |
| `entries` | array of Entries | empty | The lines spoken in this segment. |
| `terminal` | bool | `false` | `true`: the dialog ends when this segment finishes, instead of continuing to the next segment. |

## Entries

An entry is one line of text, and optionally a question.

| Key | Type | Default | Meaning |
|---|---|---|---|
| `speaker` | LocString | none | Name shown on a plate above the text box. Omit for narration. |
| `portrait` | Path | none | Image of the speaker shown above the text box on the left (cropped to a square). |
| `text` | LocString | empty | What the speaker says. Long text wraps within the box. |
| `key` | string | none | Makes the entry a **question**. The answer is stored under this key. |
| `responses` | array of Responses | none | The answers offered. If present, they are shown after the text is fully revealed. |

A question can also be written as a nested object, which is equivalent:

```jsonc
{ "speaker": "Commander Vega",
  "question": { "key": "plan", "text": "Your call, pilots.",
                "responses": [ { "id": "assault", "text": "Hit it head-on." } ] } }
```

When `question` has its own `text`, that replaces the entry's `text`.

### Responses

| Key | Type | Meaning |
|---|---|---|
| `id` | string | The value stored for the question's `key` when this response is chosen. `identifier` is accepted as another name. |
| `text` | LocString | The answer as shown to the player. |
| `goto` | string | Optional. The `id` of a segment to jump to immediately when chosen. An unknown id is logged and ignored. |

## Playback

1. Each entry's text is revealed character by character while `typeSound` loops.
2. Pressing **fire** reveals the rest of the line at once.
3. When the line is complete:
   * a normal entry waits for **fire, A, Enter or Start** (a blinking ▼ is shown), then moves on;
   * a question shows its responses; the player picks one with **Up/Down** and confirms with
     fire, A, Enter or Start.
4. Choosing a response records `key → id`. If the response has a `goto`, play jumps to the first entry
   of that segment immediately (skipping any remaining entries in the current segment).
5. After a segment's last entry: if the segment is `terminal`, the dialog ends; otherwise the next
   segment in the file begins. The dialog also ends after the last segment.

Any player's keyboard or controller can advance the dialog.

**Branching pattern:** put each branch in its own segment, reach it with `goto`, and mark each branch
`"terminal": true` so it doesn't run on into the next branch. Remember that segments also play in file
order: a branch segment placed right after the question's segment will be reached by simply continuing,
unless every answer to the question has a `goto`.

## Choosing the next level: `nextLevels`

```jsonc
"nextLevels": [
  { "key": "plan=evacuate",                "level": "levels/level2_charon.json" },
  { "key": "route=belt & pursue=yes",      "level": "levels/level2_core.json" },
  { "key": "route!=belt",                  "level": "levels/level2_alt.json" },
  { "key": "default",                      "level": "levels/level2_core.json" }
]
```

| Key | Type | Meaning |
|---|---|---|
| `key` | string | Conditions on the player's answers (see below). |
| `level` | Path | Level file to load when this option is chosen. |

The answers used are **all answers given so far in the play-through**, not only those from this
dialog. If a question is asked again, the newer answer replaces the older one.

### Key syntax

A key is a list of conditions separated by `&` or `,`. Spaces around conditions are ignored.

| Condition | True when |
|---|---|
| `question=answer` | the question `question` was answered with the response id `answer` |
| `question!=answer` | the question was not answered with `answer` (including never asked) |
| `question` | the question has been answered at all |

### Selection rules

1. Options are checked **in order**; the first whose conditions are all true is chosen.
2. Options whose key is `default`, `*` or empty are skipped in step 1. If nothing else matched, the
   first such option is chosen.
3. If no option matched and there is no default, the first option in the list is chosen.

## Layout on screen

| Element | Position (on a 1920 × 1080 screen) |
|---|---|
| Text box | bottom of the screen, 1800 × 310 |
| Speaker name plate | just above the text box, left |
| Portrait | 360 × 360, above the name plate on the left |
| Answers | a box above the text box on the right |

## Shipped dialogs

| File | Asks | Leads to |
|---|---|---|
| `intro.json` | `route`: `belt` / `direct` | `level1_belt` or `level1_direct` |
| `midpoint.json` | `plan`: `assault` / `evacuate` | `level2_charon` if `plan=evacuate`, else `level2_core` |
| `midpoint_alt.json` | `pursue`: `yes` / `charon` | `level2_charon` if `pursue=charon`, else `level2_core` |

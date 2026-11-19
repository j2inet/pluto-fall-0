# Media Files — Models, Textures, Images, Sounds and Music

These are the non-JSON files in `assets`. None of the folder names are required by the game: every
file is found through a path in some JSON file. The shipped layout is a convention.

| Folder | Contents | Referenced from |
|---|---|---|
| `models/` | 3D models, `.glb` or `.gltf` | `model` keys in game.json, enemy/item types, level backgrounds, projectiles |
| `textures/` | Images used by models | Models' image URIs, resolved through `textureFolder` |
| `images/` | 2D images | `titleImage`, `selectBackground`, level `backgroundImage`, dialog `background` and `portrait` |
| `sounds/` | Sound effects | game.json `sounds`, `sound`/`deathSound` keys, dialog `typeSound` |
| `music/` | Looping music | `titleMusic`, level `music`, dialog `music` |

## Models

**Format:** glTF 2.0, either binary (`.glb`) or JSON (`.gltf`) with external `.bin` buffers or
base64 `data:` URIs.

**Supported:**

* the node hierarchy (with `matrix` or translation/rotation/scale on each node), taken from the
  default scene;
* triangle primitives with `POSITION`, and optionally `NORMAL` (smooth normals are generated if
  missing), `TEXCOORD_0` and `COLOR_0`;
* float or normalized-integer attributes, and 8/16/32-bit indices (non-indexed meshes work too);
* materials: base colour factor and texture, emissive factor and texture
  (`KHR_materials_emissive_strength` is honoured), metallic and roughness factors, `alphaMode`
  (`OPAQUE`, `MASK` with `alphaCutoff`, `BLEND`), `doubleSided`, and `KHR_materials_unlit`.

**Not supported (ignored):** animation, skinning, morph targets, cameras, lights, sparse accessors,
normal/occlusion/metallic-roughness texture maps, texture transforms, primitive modes other than
triangles. The model is baked into a single static mesh in its rest pose.

**Orientation and scale conventions:**

* Author every craft, enemy and item with the **nose pointing along +X** and the **top along +Y**
  (glTF is +Y up). The game turns models to face the direction of travel. In landscape levels they
  are seen from the side, tilted toward the camera; in portrait levels they are seen from above.
* One unit in the model is one game unit. The screen is 32 × 18 units in landscape levels; the
  shipped player craft are about 1.8 units long, regular enemies 1.5, bosses about 5.
* Use the `transform` in the JSON definition to adjust scale or rotation rather than re-exporting.

**Shading:** a pixel shader is generated for each combination of material features in use (base
colour texture, emissive texture, alpha mask, unlit). Lit materials use a single directional light,
ambient light and a blue rim light. Emissive colours glow regardless of lighting, which suits
engines and cockpit lights.

**If a model cannot be loaded** (missing file, unsupported content, no triangles), a 1×1×1 cube of a
random colour is drawn instead.

## Textures

Texture images used by models: PNG recommended; JPEG, BMP, GIF, TIFF and other formats Windows can
decode also work. Images larger than 4096 pixels on a side are scaled down. Mipmaps are generated
automatically. Colour textures are treated as sRGB.

**How a model's image is found.** For an image URI such as `"rock.png"` (or `"tex/rock.png"`) the
game tries, in order:

1. `<textureFolder>/<file name only>` (`textures/rock.png`)
2. `<textureFolder>/<uri>` (`textures/tex/rock.png`)
3. the URI relative to the model file's folder (as the glTF specification intends).

`textureFolder` comes from the definition that uses the model; if it is empty, the model's own folder
is used. Images embedded in a `.glb` (through a buffer view) or in a `data:` URI need no file.

**If an image cannot be loaded**, a solid white texture is used, so the material's base colour shows.

The shipped `textures/` folder holds `rock.png` (asteroids), `pluto.png` and `charon.png` (planets).
The item models embed their striped textures inside the `.glb`.

## Images

Any format Windows can decode (PNG recommended). Transparency is supported.

**If an image cannot be loaded**, a solid colour (chosen from the file name, so it is the same every
run) is drawn in its place.

| Use | Recommended size | How it is drawn |
|---|---|---|
| Title image (`titleImage`) | 16:9, e.g. 1920 × 1080 | Fills the screen, cropped to fit if the aspect differs, with a slow drift and zoom. |
| Player-select background (`selectBackground`) | 16:9 | Fills the screen (cropped), dimmed. |
| Dialog background (segment `background`) | 16:9 | Fills the screen (cropped). |
| Speaker portrait (`portrait`) | square, e.g. 512 × 512 | Shown in a 360 × 360 frame, cropped to a square. |

The screen is a 1920 × 1080 virtual canvas, letterboxed to the window, so a 16:9 image always fills it exactly.

### Scrolling level backgrounds

A level's `backgroundImage` is drawn behind the 3D scene and tiles endlessly as the level scrolls
(at `backgroundParallax` times the level's speed), so it should **tile seamlessly** in the scroll direction.

| Orientation | Image is scaled to | Repeats | Suggested shape |
|---|---|---|---|
| Landscape | the screen height | horizontally, moving left | wide, e.g. 1024 × 576; left and right edges must match |
| Portrait | the screen width | vertically, moving down | tall, e.g. 576 × 1024; top and bottom edges must match |

## Sounds and music

**Formats:** anything Media Foundation can decode — WAV (PCM), MP3, and MP4/M4A (AAC). Mono or stereo,
any sample rate.

* Each file is decoded completely into memory when first used and kept for the rest of the run. Keep
  music loops reasonably short (the shipped tracks are about 14–16 seconds).
* Music and looping sounds (`typeSound`) loop the **whole file**, so trim any silence at the ends for
  a seamless loop.
* The same sound effect cannot restart within 25 ms, and at most 48 effects play at once, so rapid
  fire from four players doesn't overload the mix.
* Sound effects and music have separate volume controls in Options.
* Starting a music track that is already playing leaves it playing (it does not restart).

The shipped sounds are short procedural effects; the shipped music is four procedural loops
(`title`, `dialog`, `level1`, `level2`).

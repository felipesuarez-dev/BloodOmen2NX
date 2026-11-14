# Text

Status: **read, not ported**. How the original turns a string of a string
table into glyphs on screen. The font is in
[`FORMATS/textures.md`](FORMATS/textures.md#font) and the accent codes in
[`FORMATS/strtable.md`](FORMATS/strtable.md#text-encoding). Every rule names
the function it was read from; the dumps are local (`re/decomp/font/`).

## Parsing a string

A `Text` object (vtable `0x878e88`, constructor `FUN_00771c00`) keeps a
string as a list of rows, each a list of items. The parser is
`FUN_00772320`. An item is a glyph code or a control value.

A character outside a command becomes a glyph item with its own code. The
space is one too. A character that fails the library's character test and is
not a space is reported (`text: unsupported character`) and dropped.

`@` starts a command of one character:

| Command | Effect |
|---|---|
| `@@` | the glyph `@` (not in the font of this release) |
| `@n` | a new row |
| `@p` | a new row that starts with the control value 1 |
| `@l` | control value 2: the row is aligned left |
| `@r` | control value 3: the row is aligned right |
| `@c` | control value 4: the row is centred |
| `@w` | reserved: reported and ignored |
| any other | ignored |

`@l`, `@r` and `@c` are accepted only at the beginning of a row; anywhere
else they are reported and ignored.

`\` starts a command of one character that gives one glyph item, most of
them a controller button of the font:

| Command | Glyph code | Glyph |
|---|---|---|
| `\q`, `\y` | `0xc8` | square |
| `\b`, `\g` | `0xc9` | triangle |
| `\a`, `\c` | `0xca` | circle |
| `\x` | `0xcb` | cross |
| `\1`, `\2`, `\3`, `\4` | `0xcc` to `0xcf` | L1, L2, R1, R2 |
| `\u`, `\d`, `\h`, `\l` | `0xd0` to `0xd3` | up, down, right, left |
| `\[`, `\]`, `\*`, `\#` | `0xd4` to `0xd7` | not in the font of this release |
| `\` and a space | `0x20` | a space |
| `\\` | `0x5c` | not in the font |
| `\n` | | a new row, like `@n` |
| any other | | reported (`text: unsupported command`) and dropped |

The parser also reports, without changing the result, two spaces in a row, a
row that begins or ends with a space, two new rows in a row, a text that ends
inside a command and a text that ends with a new row.

What the control value 1 of `@p` does is not read.

## Placing the glyphs of a string

`FUN_00575980` turns one string into one textured quad per glyph. It gets the
start of the pen (x, y), a row height, a horizontal scale, a colour, the
string and the font. The coordinates are those of a 640 x 480 screen with y
down. With the pen at the start:

- A line feed moves the pen down by the row height and back to the start x.
- A space moves the pen right by `space_width` times the scale.
- Any other character: its glyph, or the glyph of `^` when the font has none
  for it; when the font has neither (the font of this release has no `^`),
  the pen moves as for a space. The quad
  goes from the pen x to `x + glyph width * scale` and from `y - row height`
  to y: y is the bottom of the row and every glyph is as tall as the row.
  Then the pen moves right by `(glyph width + kerning) * scale`.
- The texture coordinates of the quad are the rectangle of the glyph, with
  the bottom row of the texture rectangle at the top of the quad: the font
  texture is stored upside down.

Each quad is queued blended and without depth write (flags `0x10800`, see
[`FORMATS/models.md`](FORMATS/models.md#render-state-and-draw-order)), with
the sort value -200000: after every model of the frame, the HUD included.

The font model that queues a string for this function is the class with
vtable `0x0086de2c` (constructor `FUN_00575680`, queueing method at
`0x575770`).

## Not read

- Who builds the string, the pen start, the row height, the scale and the
  colour for a UI text: the text components of the UI (`FUN_00755850`,
  `FUN_00755970`) and of the HUD and the menus. Without it the place of a
  menu line on screen is not known.
- How the row alignment (control values 2, 3 and 4) moves the pen start, and
  where `base_height` and `leading` are used.
- The second function that looks glyphs up (`0x56dfb8`).
- `%d` and the other format codes of the strings.

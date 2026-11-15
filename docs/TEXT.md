# Text

Status: **read; the parser is ported**. How the original turns a string of a string
table into glyphs on screen. The font is in
[`FORMATS/textures.md`](FORMATS/textures.md#font) and the accent codes in
[`FORMATS/strtable.md`](FORMATS/strtable.md#text-encoding). Every rule names
the function it was read from; the dumps are local (`re/decomp/font/`).

## Parsing a string

A `Text` object (vtable `0x878e88`, constructor `FUN_00771c00`) keeps a
string as a list of rows, each a list of items. The parser is
`FUN_00772320`. An item is a glyph code or a control value.

A character outside a command becomes a glyph item with its own code. The
space is one too. A character that is neither a printable ASCII character
(0x21 to 0x7e; the test is the C library's `isgraph`, `FUN_007a541e`) nor a
space is reported (`text: unsupported character`) and dropped. That is what
happens to the three raw bytes above 0x7f of the string tables.

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

## The text of a menu

Read on the main menu, from the code that builds it to the font queue, and
checked against the running game.

- The text renderer is the object the renderer returns from its virtual
  +0x13c (vtable `0x86db68`, constructor `FUN_0056db00`, set-up
  `FUN_0056dc20`). It keeps the font (+8), a y scale (+0x10), an x scale
  (+0x14) and the half width and half height of the screen (+0x24, +0x2c and
  +0x28, +0x30), which `FUN_0056dc20` takes from the renderer. Its default
  style is the font `debug` with size 0.0375 (`FUN_0056e1a0`).
- A style is a font, a size and an aspect (`FUN_0056e2b0`, virtual +0x10):
  y scale = size × half height / (`leading` + `base_height`), x scale = y
  scale × aspect. The size is the height of a row in screen units, where the
  screen goes from -1 to 1 in both axes.
- The main menu (`FUN_00557710`, every frame) sets the font `main/main`,
  size `0x3d555555` (0.0520833) and aspect 1: with the font header (20 and
  5) and a 480-line text space the scales are 0.5.
- Measuring a string (`FUN_0056df40`, virtual +0x30), in screen units: per
  character, a space or a character without a glyph advances `space_width`
  × x scale; a glyph advances (`kerning` + its width) × x scale and makes the
  row (`leading` + its height) × y scale high at least; a row with no glyph
  is (`leading` + `base_height`) × y scale high; `\n` starts a row. The width
  is the widest row divided by the half width; the height is the sum of the
  rows divided by the half height.
- A menu is a tree of elements (base constructor `FUN_0055c520`; first child
  +0x20, next sibling +0x18; width +4, height +8, x +0xc, y +0x10 in screen
  units with y growing down; layout mode +0x24). An item takes its text from
  the string table, key `fe_` plus its name with spaces turned to `_`
  (`FUN_00557d00`), and its size from the measure (`FUN_0055d3b0`).
- The layout of the children (`FUN_0055c6c0`), with the gap `0x841c08`
  (0.01) and one half (`0x83f5c8`):

  | Mode | Children |
  |---|---|
  | 1 | A column spread over the height: each child moves down by (height − sum of the child heights) / (count + 1) before it; centred across |
  | 2 | A row spread over the width: (width − sum of the child widths) / (count − 1) between them; centred down |
  | 3 | A centred column: the first child starts at top + (height − sum of the heights − (count − 1) × gap) / 2, one gap between them; each one centred across |
  | 4 | A row: starts at left + (width − sum of the widths − (count + 1) × gap) / 2, one gap after each; centred down |

  The main menu (`FUN_005581d0`) is a mode-3 column over the whole screen
  (x −1, y −1, width 2, height 2) with five items.
- Drawing an item (`FUN_0055d2a0`): the colour is (0.5, 0.5, 0.5) when the
  item is disabled (+0x28), (1, 0, 0) when it is the selected one (+0x29)
  and (1, 1, 1) otherwise, with the alpha the caller passes; the text goes
  at x = item x and y = −(item y) − item height, in the upward screen units
  of the text renderer.
- From screen units to the 640×480 text space (`FUN_0056e300`, virtual
  +0x34): x × half width + half width, half height − y × half height; the
  row height is (`leading` + `base_height`) × y scale and the scale is the x
  scale. It queues the string on the font model (`0x575770`), which packs
  the colour as `0xAARRGGBB` (each channel × 255, truncated) and places the
  glyphs as in "Placing the glyphs of a string". The font model then maps
  the 640×480 space to the screen: the oracle draws the same menu in an
  800×600 window.

Checked in the running game (Wine oracle in the main menu, `gdb` on
`0x575770` and `0x56e300`): the five menu items are queued every frame from
one call site, with row height 12.5 and scale 0.5. The pen start of each
item recomputed with the rules above from the font and the `spanish` table
(local script `re/decomp/font/menu/menu_layout_ref.py`) equals the queued
one to the sixth decimal: x 270.75, 294.25, 299.5, 257.25, 273.0 and y
216.45, 231.35, 246.25, 261.15, 276.05. In the captured frame the first
item is red and the others white.

## Not read

- Who builds the string, the pen start, the row height, the scale and the
  colour for the texts of the HUD and of the other menus (the text
  components `FUN_00755850`, `FUN_00755970`, the hint of the HUD).
- How the row alignment of a parsed string (control values 2, 3 and 4)
  moves the pen start: the menu does not parse its strings.
- The second function that looks glyphs up (`0x56dfb8`).
- `%d` and the other format codes of the strings.
- The input of the menu (which item is selected and how the selection
  moves) and the fade that gives the alpha.

## Port

`parse_text` (`include/bo2/engine/text.hpp`, `src/engine/text.cpp`) is
`FUN_00772320`: it takes the bytes of a stored value and returns the rows of
glyph and control items and the warnings of the original; it does not look at
the locale. `layout_string` (`include/bo2/engine/pixmap_font.hpp`,
`src/engine/pixmap_font.cpp`) is `FUN_00575980`: it places the glyphs of a
string with the advance and the texture coordinates of the original; the view
coordinates and the renderer queue are not ported. The `Text` object and its
own lists are not ported.

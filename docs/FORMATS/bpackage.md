# `bpackage`: compiled script packages

Status: **format read and ported** (`src/engine/script_package.cpp`, which
reads the 480 entries of the game). A `bpackage` entry is a package of
script classes compiled by the game's own script compiler. The layout below
is read from the function that writes a package (`FUN_0058cbb0`, a method of
`goe::PackageBinaryLoader`, and the two functions it calls per class,
`FUN_0058d000` and `FUN_0058d420`) and checked by a reference reader on the
480 entries of the game: every one is consumed to its last byte. What the
code does when it runs is in [`../SCRIPTS.md`](../SCRIPTS.md).

## What the game data has

- 480 entries in the 179 containers, four names: `native` (168), `creatures`
  (162), `sounds` (149) and `camera` (1). A level container carries its own
  copy of each.
- One more package in the `packages` section of each of the 165 regions,
  with the classes of the entities of that region (see "Region packages").
- `native` declares the classes the executable implements (`basescript`,
  `creaturescript`, `bo2creaturescript`, `monsterscript`, `timer`,
  `position`, ...). `creatures` and `sounds` import `native` and define the
  classes of the game in script: in `slums/sl01`, 340 and 20 classes.
- 419 distinct class names in the whole game. In all packages together:
  67,805 class records, 207,039 methods, 205,790 method bodies (84,840 of
  them native, without code), 199,314 fields, 4,362 enumerations and
  3,603,894 words of code.
- The names are not in the payload: a name is an index into the symbol list
  of the entry, as in a string table ([`big.md`](big.md)).

## Primitives

The writer aligns before each value, counting from the start of the payload:

| Value | Bytes | Alignment | Written by |
|---|---|---|---|
| `u8` | 1 | none | `FUN_0069d310` |
| `u16` | 2 | 2 | `FUN_0069d3a0`, `FUN_0069d360` |
| `u32` | 4 | 4 | `FUN_0069d3e0` |
| name | `u16` symbol index | 2 | `FUN_0058cf00` |
| chunk | `u32` size, then that many bytes | 4 | `FUN_0069c8f0` |

The size of a chunk counts the bytes after the size word; the content can
end up to three bytes before the end of the chunk (padding).

## Package

```
u16   version (1)
u16   import count
name  import[count]
u16   class count
chunk declaration[class count]
chunk link[class count]
u32   0
```

The classes are written sorted (`FUN_0058ce60` compares two of them).

## Declaration of a class (`FUN_0058d000`)

```
name  class name
u16   kind (class +0x1c): 1 in the 44 script classes the executable
      implements, 5 in its 14 value classes (`position`, `timer`,
      `effect`, ...), 0 in the 361 classes written in script
u16   size in bytes of the data of an object (class +0x1e; `FUN_0057fa20`
      allocates that much)
u16   slot count: the methods that can be replaced per state
u16   state count, enum count, field count, method count, body count
name  state[state count]: the first one is `default`
enum[enum count]:
  name  enum name
  u16   (first word of the record)
  u16   value count
  name  value name[value count]
  u32   value[value count]
field[field count]:
  name  field name
  u16   flags (first word of the record; 12 values in the data)
  u32   offset of the field in the data of the object
  type  field type
  default value (below)
method[method count]:
  name  method name
  u8    (record +4): 0, 1, 2, 3, 4 or 5 in the data
  u8    (record +5): 2 in the methods that run as a thread, else 0 or 1
  u16   slot (record +6), or 0xffff when the method has none
  type  result type
  u16   parameter count
  parameter[count]: type, u32 offset in the locals of a call
body[body count]:
  u16   index of the method it implements
  u16   flags (body +4): bit 0 set means native, no code
  u16   state the body belongs to (body +6)
  u16   size in bytes of the locals of a call (body +8)
  u16   size in cells of the stack of a call (body +10)
  u16   local count
  local[count]: type, u32 offset in the locals
  without bit 0: u32 word count, then the code, one u32 per word
```

The first method of every class is `start`, in slot 0: the body a state
runs when the object enters it. A class has one body of `start` per state
that defines one.

A type (`FUN_0058ce80`) is a `u16` kind followed by what the kind needs:

| Kind | Followed by | What it is |
|---|---|---|
| 0 | nothing | no value (the result of a method that returns nothing) |
| 1 | nothing | a boolean, one byte |
| 2 | nothing | an integer, 32 bits |
| 3 | nothing | a float |
| 4 | nothing | a string |
| 5 | nothing | a byte (the result of `getstate`) |
| 6 | the element type, then a `u32` count | an array |
| 7 | the element type | a collection of values of that type (no data uses it) |
| 8 | nothing | a word (no data uses it) |
| 9 | a class name | a value of a value class (`position`, `timer`, ...) |
| 10 | a class name | a reference to a script object |
| 11 | an enum name | a value of an enumeration |

What each kind holds in memory is in [`../SCRIPTS.md`](../SCRIPTS.md)
("Values").

Kinds 2 and 3 are told apart by the defaults of the fields: the 16,386
defaults of kind 3 that are not zero are float bit patterns, and the ones
of kind 2 are small integers. A field of kind 1 takes one byte.

The default value of a field (`FUN_0058cf20`) depends on the kind of its
type: for 1, 2, 3, 5 and 11, a `u16` 1 and a `u32`; for 4, a `u16` that says
whether a name follows; for 6, 7 and 10, a `u16` 0; for 9, a `u16` that says
whether a chunk with the object follows; for any other kind, nothing.

## Link record of a class (`FUN_0058d420`)

What the loader needs to connect the class with the others:

```
u16   1 and the name of the parent class, or 0
per state: u16 0 when the class has its own slot table for the state, or
           u16 1 and the name of the nearest ancestor whose table it shares
slot list, until a u16 0xffff, for the states with an own table:
  u16 state, u16 slot, name of a class, u16 body index of that class
body list, until a u16 0xffff:
  u16 body index
  reference list, until a u16 0xffff:
    u16 kind, u16 index of a code word of the body, then by kind:
      0, 1, 2: a name
      3, 4:    a name and a u16
      5:       two names and a u16
```

The slot list says, for each state and slot of the class, which body runs:
its own or the one of an ancestor. A slot without a body is not listed.

The reference list has one entry for each instruction of the body whose
operand is a pointer or a global index in memory. The code words of the
file hold the values the compiler had in memory, so a reader takes these
operands from here:

| Kind | Code word | The operand is |
|---|---|---|
| 0 | the instruction (`0x04`) | a name: a string constant |
| 1 | the instruction (`0x5f`) | a name: a class |
| 2 | the word after the instruction (`0x21`) | a class |
| 3 | the word after the instruction | the method `u16` of a class |
| 4 | the word after the instruction | the body `u16` of a class |
| 5 | the two words after the instruction | the state tables of the first class, and the method `u16` of the second |

## Code

A body is a list of 32-bit words. The top byte of the first word of an
instruction is its code and the low 24 bits an operand. An instruction has
one word, except (`FUN_005b8b80`):

| Words | Codes |
|---|---|
| 2 | `0x02`, `0x21`, `0x26`, `0x47`, `0x48`, `0x4a` to `0x4e`, `0x50` to `0x54`, `0x56` to `0x58` |
| 3 | `0x49`, `0x4f`, `0x55` |

63 codes are used in the game data, from `0x00` to `0x60`. Every body with
code ends with `0x5c`, and the 80,885 jumps of the game land on the first
word of an instruction of their body or right after its last word. The
instructions are described in [`../SCRIPTS.md`](../SCRIPTS.md).

## Region packages

The `packages` section of a region entry ([`region.md`](region.md)) holds
one more package:

```
u32   4
u32   package count (1)
u16   1
u16   0xffff
cstr  package name ("default"), padded to 4
u32   size: the bytes from the package name to the end of the section
u32   0
      the package, in the layout above, up to the end of the section
```

Measured on the 165 regions: one package each, named `default`, importing
`creatures`, read to its last byte with the symbols of the region entry. Its
first class is `resourcedb` (no parent, no states); the others are the
classes of the scripted entities of the region, named like the entity
(`reserved.script` property) and derived from a class of `creatures` or
`native`. 13,158 classes, 16,104 bodies, 345,637 words of code, 17,255
fields. The port reads it with `parse_region_script_package`
(`src/engine/region.cpp`), which gives the same four totals.

## Not read

- The words of a class, an enumeration, a field and a method that the
  layout above leaves without a name.
- The array type (kind 6), which the game data uses only in 336
  parameters.

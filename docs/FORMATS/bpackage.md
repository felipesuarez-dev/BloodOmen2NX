# `bpackage`: compiled script packages

Status: **format read, not ported**. A `bpackage` entry is a package of
script classes compiled by the game's own script compiler. The layout below
is read from the function that writes a package (`FUN_0058cbb0`, a method of
`goe::PackageBinaryLoader`, and the two functions it calls per class,
`FUN_0058d000` and `FUN_0058d420`) and checked by a reference reader on the
480 entries of the game: every one is consumed to its last byte. The
instructions of the code are not interpreted yet.

## What the game data has

- 480 entries in the 179 containers, four names: `native` (168), `creatures`
  (162), `sounds` (149) and `camera` (1). A level container carries its own
  copy of each.
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
u16   kind: 1 in the script classes of `native`, 5 in its value classes
      (`position`, `timer`, ...), 0 in the classes written in script
u16   (class +0x1e)
u16   slot count
u16   base count, enum count, field count, method count, body count
name  base[base count]
enum[enum count]:
  name  enum name
  u16   (first word of the record)
  u16   value count
  name  value name[value count]
  u32   value[value count]
field[field count]:
  name  field name
  u16   (first word of the record)
  u32   (record +8)
  type  field type
  default value (below)
method[method count]:
  name  method name
  u8    (record +4)
  u8    (record +5)
  u16   (record +6): 0xffff or 0 in the data
  type  result type
  u16   parameter count
  parameter[count]: type, u32
body[body count]:
  u16   index of the method it implements
  u16   flags: bit 0 set means native, no code
  u16, u16, u16
  u16   local count
  local[count]: type, u32
  without bit 0: u32 word count, then the code, one u32 per word
```

A type (`FUN_0058ce80`) is a `u16` kind followed by what the kind needs:

| Kind | Followed by | Seen as |
|---|---|---|
| 6 | the element type, then a `u32` | an array |
| 7 | another type | a reference |
| 9, 10, 11 | a name | an object of a class (9), a handle to one (10), a value of an enumeration (11) |
| any other | nothing | 0 result of a method with no value; 1, 2, 3, 4, 5 simple values |

The default value of a field (`FUN_0058cf20`) depends on the kind of its
type: for 1, 2, 3, 5 and 11, a `u16` 1 and a `u32`; for 4, a `u16` that says
whether a name follows; for 6, 7 and 10, a `u16` 0; for 9, a `u16` that says
whether a chunk with the object follows; for any other kind, nothing.

## Link record of a class (`FUN_0058d420`)

What the loader needs to connect the class with the others:

```
u16   1 and the name of the parent class, or 0
per base: u16 1 and the name of the class that provides it, or 0
slot list, until a u16 0xffff:
  u16 base index, u16 slot index, name of a class, u16 body index
body list, until a u16 0xffff:
  u16 body index
  reference list, until a u16 0xffff:
    u16 kind, u16 operand word, then by kind:
      0, 1, 2: a name
      3, 4:    a name and a u16
      5:       two names and a u16
```

The reference list has one entry for each instruction of the body whose
operand is a pointer in memory, in the order of the code: a name (kinds 0
and 1), a class (2), a method of a class (3), a body of a class (4) or a
method reached through a base (5).

## Code

A body is a list of 32-bit words. The top byte of the first word of an
instruction is its code and the low 24 bits an operand. An instruction has
one word, except (`FUN_005b8b80`):

| Words | Codes |
|---|---|
| 2 | `0x02`, `0x21`, `0x26`, `0x47`, `0x48`, `0x4a` to `0x4e`, `0x50` to `0x54`, `0x56` to `0x58` |
| 3 | `0x49`, `0x4f`, `0x55` |

The reference kinds of the link record tell what some of them point at:
`0x04` and `0x5f` a name; `0x21` and `0x26` a class; `0x47`, `0x48`, `0x4b`,
`0x4d`, `0x4e`, `0x51`, `0x53`, `0x54` and `0x57` a body of a class; `0x4a`,
`0x4c`, `0x50`, `0x52`, `0x56` and `0x58` a method of a class; `0x49`,
`0x4f` and `0x55` a method reached through a base. 63 codes are used in the
game data, from `0x00` to `0x60`.

## Not read

- What each instruction does: the interpreter (`goe::ScriptExec`, vtable
  `0x86def0`).
- The meaning of the unnamed words of a class, a field, a method and a body,
  and the names of the simple type kinds.
- The object stored as the default of a field of kind 9.
- How the native classes are bound to the functions of the executable.

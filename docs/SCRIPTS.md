# Scripts

Status: **read, being ported**. The enemies, the doors, the cinematics, the
sounds and the triggers of the game are script classes compiled into the
`bpackage` entries of the containers and into the `packages` section of
every region ([`FORMATS/bpackage.md`](FORMATS/bpackage.md)). This page
describes how the executable loads, binds and runs them, read from its
interpreter (`FUN_0057de80`) and the functions around it, and what of it
was checked in the running game.

Port: `src/engine/script_package.cpp` (the packages),
`src/engine/script_program.cpp` (linking), `src/engine/script_memory.cpp`
and `src/engine/script_machine.cpp` (the machine: objects, strings, frames
and the instructions of "Values and variables", "Arithmetic and logic" and
"Flow", with the loop guard; calls, child threads (`0x59` to `0x5b`) and the
change of state (`0x5e`) are not ported yet).

## Objects, classes and states

- A script object (`goe::ScriptObject`, vtable `0x86df10`, constructor
  `FUN_0057f830`) has a class (+0xc), a data block (+0x10), a pending state
  (+0x14), a main thread (+0x18) and a handle (+8). The handle is a small
  counted object (count at +4) that points back at the object (+8): a
  script variable that refers to an object holds its handle.
- The data block has the size the class declares (`FUN_0057fa20`) and holds
  the fields at their offsets. Its first byte is the current state of the
  object. `FUN_0057fa20` fills the fields of the class and of its ancestors
  with their defaults.
- A class has a parent (+0x18), a name (+8) and one slot table per state
  (+0x24, 8 bytes each: the table, its length and whether the class owns
  it). `FUN_00580810` finds the body of a method for an object: the table
  of the current state, at the slot of the method.
- State 0 is `default`. Slot 0 is the method `start` of the state.
- `FUN_00580730` (virtual +8) starts an object: it must be in state 0, and
  `FUN_00580be0` creates the main thread on the `start` of the state.
  `FUN_00580900` (virtual +0x10) runs the main thread once
  (`FUN_00580cb0`). `FUN_00580770` (virtual +0xc) stops the object: it
  destroys the thread and returns to state 0.
- `FUN_00580830` (virtual +0x14) asks for a state. It accepts a state
  above 0 and below the state count of the class and stores it as pending.
  If the object is not running its thread at that moment, `FUN_00580890`
  changes at once; otherwise the change happens when the run of the thread
  returns (`FUN_00580cb0`). The change destroys the main thread, writes the
  state in the first byte of the data and starts the `start` of the new
  state.

## Threads and frames

- A thread (`goe::ScriptExec`, vtable `0x86def0`) is a list of frames; the
  last one is the frame that runs. A frame (`FUN_0057d3e0`, 0x28 bytes) has
  the object (+4), the next code word (+8), the index of the top of its
  stack (+0xc, -1 when empty), the stack (+0x10), the locals (+0x14), the
  body (+0x18), a waiting flag (+0x1c) and a list of child threads (+0x24).
- The stack is made of 4-byte cells and has the length the body declares.
  The locals are a block of the size the body declares. When the method
  has a result, the first 4 bytes of the locals hold the address where the
  caller wants it. The parameters are copied to their offsets (a parameter
  of kind 10 as the handle of the object) and the locals of the body are
  initialised (`FUN_00587fc0`, `FUN_00588390`).
- `FUN_0057dcf0` (virtual +8) runs a thread once: first it runs the
  children of the last frame and destroys the ones that ended; if the frame
  is waiting and still has children, it returns; then it calls the
  interpreter until the interpreter says to stop or no frame is left. When
  the interpreter reports that a frame ended, the frame is removed and its
  caller continues.
- The interpreter returns 1 when the frame ended or when it pushed the
  frame of a call, and 0 when the thread must stop until its next run.
- A loop guard counts the instructions `0x60` (`DAT_00a6a57c`, reset when
  no interpreter is running): above 10,000 the frame ends with the warning
  `WARNING: Scripted loop ran too long: %s, %s` (`0x8ff5d0`), with the name
  of the class of the object and the name of the method of the body.

## Instructions

`imm` is the low 24 bits of the instruction word and `simm` the same bits
with sign. "Push" and "pop" act on the stack of the frame; "top" is its
last cell. An address is a pointer to memory of the game: a local, a field
or an element. A boolean is the low byte of a cell: the comparisons write
only that byte. The codes in italics are not used by the game data.

### Values and variables

| Code | What it does |
|---|---|
| `0x00` | Mark: when the object has a debugger (+0x1c), calls it with the body, the locals and the word. In the data `imm` grows through each body |
| `0x01` | Pop |
| `0x02` | Push the next word (2 words) |
| `0x03` | Push `simm` |
| `0x04` | Push the address of the entry `imm` of the global name table (`DAT_00a6a5dc` +0x28): a string constant |
| `0x05`, `0x06`, `0x07` | Push the byte, the 32-bit value or the address at `imm` in the locals |
| `0x08` | Push the object of the handle stored at `imm` in the locals, or 0 |
| `0x09`, `0x0a`, `0x0b` | Push the byte, the 32-bit value or the address at `imm` in the data of the object |
| `0x0c` | Push the object of the handle stored at `imm` in the data of the object, or 0 |
| *`0x0d`*, `0x0e`, `0x0f`, *`0x10`* | Top is an object: replace it with the byte, the 32-bit value, the address or the object of the handle at `imm` in its data; 0 when the object is 0 |
| *`0x11`*, *`0x12`*, *`0x13`* | Top is an address: replace it with the byte, the 32-bit value or the address at `imm` from it; nothing when it is 0 |
| *`0x14`*, *`0x15`*, *`0x16`* | Pop an index; top is an address: replace it with the byte at the index, the 32-bit value at four times the index, or the object of the handle there |
| *`0x17`* | Pop an index; top is an address: add `imm` times the index |
| *`0x18`* | Top is an index: when it is below 0 or not below `imm`, the cell under it becomes 0 |
| *`0x19`*, *`0x1a`*, *`0x1b`* | Pop a value; combine it with top through `FUN_005b4c10` or `FUN_005b4ca0` (not read) |
| `0x1c`, `0x1d` | Pop a value, then pop an address: store the byte or the 32-bit value there when the address is not 0 |
| `0x1e` | Pop an object, then pop an address: release the handle stored there and store the handle of the object, counting it |
| `0x1f` | Pop the address of a string, then pop an address: the string stored there becomes that one (`FUN_0068eaa0`, a method of the string at that address; counted) |
| *`0x20`* | As `0x1f` through `FUN_005b4bd0` (not read) |
| `0x21` | Pop the address of a value, then pop an address: the class in the next word copies the value there (its virtual +0x20) (2 words) |
| *`0x22`* to *`0x26`* | Pop two addresses and copy `imm` bytes, handles, strings or values of a class (`0x26`, 2 words) from the first to the second |

### Arithmetic and logic

Each one pops its second operand and replaces top with the result; the ones
with one operand replace top.

| Code | What it does |
|---|---|
| `0x27`, `0x28`, `0x29`, *`0x2a`*, *`0x2b`*, *`0x2c`* | Integers: negate, add, subtract, multiply, divide, remainder. A division by 0 gives 0x80000000 for a negative dividend, 0x7fffffff for a positive one and 0 for 0; a remainder by 0 gives 0 |
| `0x2d`, `0x2e`, `0x2f` | Integers: less, less or equal, equal |
| `0x30`, `0x31`, `0x32`, `0x33`, `0x34` | Floats: negate, add, subtract, multiply, divide. A division by 0.0 gives the largest float (`0x0086dee8`) with the sign of the dividend, or 0 for 0 |
| `0x35`, `0x36`, *`0x37`* | Floats: less, less or equal, equal |
| `0x38` | Strings, by address: equal byte by byte (`FUN_0068ec30`); false when an address is 0 |
| `0x39`, `0x3a`, `0x3b` | Booleans: not, and, or |
| `0x3c`, *`0x3d`* | Float to integer and to boolean. `0x3c` calls `__ftol` (`0x7a3af4`): rounding set to chop, `fistp` to a 64-bit integer, and the cell takes the low 32 bits |
| `0x3e`, *`0x3f`* | Integer to float and to boolean |
| *`0x40`*, *`0x41`* | Byte to integer (with sign) and to float |
| *`0x42`*, *`0x43`*, *`0x44`* | String to integer (`FUN_007a3d4b`), to float (`FUN_007a3e44`) and to boolean |

### Flow

| Code | What it does |
|---|---|
| `0x45` | Jump: the next instruction is `simm` words after the word that follows this one |
| `0x46` | Pop a boolean; jump as `0x45` when it is false |
| `0x60` | Jump as `0x45`, counted by the loop guard |
| `0x5c` | The frame ends |
| `0x5d` | The thread stops until its next run and continues after this instruction |
| `0x5e` | Pop a state and ask the object for it (`FUN_00580830`); the frame ends |
| `0x5f` | Top is an object: it becomes 0 unless its class or an ancestor has the name `imm` |
| `0x59` | A copy of the frame continues after this word as a child thread (`FUN_0057daf0`); the frame jumps `imm` words ahead and waits for its children |
| `0x5a` | As `0x59`, but the frame continues at once |
| `0x5b` | End of a block started by `0x59` or `0x5a`: the frame ends when it has no children; otherwise it waits and runs this instruction again |

### Calls

A call finds its arguments on the stack: the arguments in order, then the
address for the result when the method has one, then the object when the
instruction takes it from the stack. The call removes them. When the object
or the body is missing, the call only removes them.

A body is reached in one of four ways, by the words after the instruction:

| Target | Words | The body is |
|---|---|---|
| body | 1 | the one named |
| method | 1 | the one in the slot of the method, in the table of the current state of the object; for the object of the frame, bit 0 of `imm` picks the table of state 0 |
| table and method | 2 | the one in the slot of the method in the state tables of the class named, in the current state when that class has it, else in state 0 unless the slot is 0 |

| Code | Object | Target | What the call does |
|---|---|---|---|
| `0x47` | popped | body | Calls the native function of the body with the object, the argument count, the arguments and the result address |
| `0x48` | of the frame | body | Call (`FUN_0057f580`): a native body runs at once; a script body gets a new frame on the thread |
| `0x49` | of the frame | table and method | As `0x48` |
| `0x4a` | of the frame | method | As `0x48` |
| `0x4b` | popped | body | As `0x48` |
| `0x4c` | popped | method | As `0x48` |
| `0x4d` | popped | body | Calls a native function that can return a task; the task becomes a child of the frame, and the frame waits for its children |
| `0x4e` to `0x52` | as `0x48` to `0x4c` | as `0x48` to `0x4c` | The body starts as a child thread of the frame (`FUN_0057f6d0`, `FUN_0057d810`) and the frame waits for its children |
| *`0x53`* | popped | body | As `0x4d` without waiting |
| *`0x54`*, `0x55`, `0x56`, *`0x57`*, *`0x58`* | as `0x48` to `0x4c` | as `0x48` to `0x4c` | As `0x4e` to `0x52` without waiting |

In the game data the calls that start a thread (`0x4d` to `0x52`, `0x55`,
`0x56`) always target a method whose byte +5 is 2, and the others never do.

## Values

Every value takes 4 bytes in a local, a field or a stack cell, except a
boolean and a byte, which take one (`FUN_00587d30`; an array takes its
count times the size of its element). What the 4 bytes hold:

| Kind | Holds | Initial value (`FUN_00588390`) | Store (`FUN_00587fc0`) |
|---|---|---|---|
| 1 boolean, 5 byte | the value, one byte | 0 | the low byte |
| 2 integer, 3 float, 11 enumeration | the value | 0 | the word |
| 8 | a word (no data uses it) | 0 | 0 |
| 4 string | a pointer to a counted string | the shared empty string, counted | the string, counted |
| 10 object | a pointer to the counted handle of an object | 0 | the handle, counted |
| 9 value of a class | a pointer to a block the value type owns | the type's virtual +0x18 with no source | +0x18, then +0x20 with the source |
| 6 array | the elements, one after the other | each element | each element |
| 7 | a pointer to a collection object of 0x1c bytes (no data uses it) | an empty collection | a copy |

A value type is the native type of a value class (`goe::NativeType`; one
`NT<name>` class per value class: `NTposition`, `NTtimer`, ...). Its
virtuals, read on `NTposition` (vtable `0x878d68`): +8 make from a source
(`FUN_0079e2f0`), +0xc free, +0x10 read from a stream (`0x79e350`: three
floats), +0x14 write to a stream (`FUN_0079e390`), +0x18 initialise a slot
(`FUN_0079e3c0`: a new block, zeroed or copied), +0x1c destroy a slot
(`FUN_0079e430`), +0x20 assign (`FUN_0079e450`), +0x24 bind the natives of
the class. The default value of a field of kind 9 is the chunk +0x14 wrote:
12 bytes for `position` and `rotation` (three floats), 4 bytes for
`effect`, `sound` and `soundgroup` (measured on `sl01`).

The blocks of the value types read so far:

| Type | Vtable | Block | Initial block (+0x18 with no source) | Default chunk (+0x10) |
|---|---|---|---|---|
| `position` | `0x878d68` | 12 bytes, three floats | zeroed | three floats |
| `rotation` | `0x878d3c` | 12 bytes, three floats (radians) | zeroed (`FUN_0079e0a0`) | three floats (`FUN_0079e020`) |
| `color` | `0x878d10` | 16 bytes, four floats | zeroed (`FUN_0079ddd0`) | four floats (`FUN_0079dd50`) |
| `gridposition` | `0x878bb0` | 8 bytes | left as the allocator gives it (`FUN_0079b080`) | none: +0x10 returns 0 (`FUN_0079b060`) |
| `timer` | `0x878c34` | a counted timer object of 0x14 bytes (`FUN_0079cac0`); assign shares it (`FUN_0079cca0`) | a new timer (`0x79cc30`) | not read |

Assign (+0x20) of the block types copies the block, or zeroes it when the
source is null, or initialises the slot when it is empty (`FUN_0079de70`,
`FUN_0079b0f0`, `FUN_0079e130`).

## Loading and linking

A package is read in three steps by `goe::PackageBinaryLoaderContext`
(`FUN_0058ac40`): the header and the imports (`FUN_0058ad60`), one class
declaration per step (`FUN_0058b500`), and one link record per step
(`FUN_0058c060`). The loader accepts versions 0 and 1; the data has only 1.

Linking writes the code words that the file leaves to the loader (the
reference list of [`FORMATS/bpackage.md`](FORMATS/bpackage.md)):

| Reference | The word becomes |
|---|---|
| 0 | `0x04000000` and the index of the name in the global name table |
| 1 | `0x5f000000` and the index of the name |
| 2 | a pointer to the value type of the class (the map at manager +0x7c) |
| 3 | a pointer to the method record (class +0x38, 0x20 bytes each) |
| 4 | a pointer to the body record (class +0x3c, 0x1c bytes each) |
| 5 | a pointer to the state tables of the first class (+0x24), and in the next word a pointer to the method record of the second |

A body record has the method (+0), the flags (+4), the state (+6), the size
of the locals (+8), the size of the stack (+0xa) and, for a native body,
its function (+0x10).

## Native functions

After linking a class, `FUN_0058c060` binds its native bodies:

- A script class (kind 1, and the classes written in script, which inherit
  it) has a factory: the object at manager +0x6c for its name, or the one
  of its parent, or the default one (manager +0x9c). Each native script
  class has one, `S<name>Factory` (`goe::ScriptFactory`, vtable of the
  base `0x866c40`). Its virtual +0xc binds the natives of the class, +0x10
  creates the C++ object of a script object of the class (`FUN_00764230`
  makes a `Sbasescript`, 0x2c bytes, over `goe::ScriptObject`), and +8
  returns the index of the class name.
- A value class (kind 5) is bound by its value type, virtual +0x24.
- A bind function stores one function per native body, by the index of the
  body: `FUN_00587ac0` or `FUN_005a10d0` for a function that returns at
  once, `FUN_00587b00` or `FUN_005a1110` for one that returns a task (the
  `0x4d` calls). The first two check that the body is native.
- The functions of a script class take the object, the argument count, the
  address of the first argument and the address of the result. The task
  functions take the object, the thread, the argument count and the
  arguments, and return the task or 0.

Measured with a scan of the bind functions against the `native` package
of `sl01` (local script `re/decomp/script/natives.py`): the 58 native
classes bind 505 functions, one per native body and none more; 72 of them
return a task, and they are exactly the bodies whose method has byte +5
equal to 2.

## The natives of `basescript`

Read from their functions (dumps of `natives.py`'s table):

| Body | Method | Function | What it does |
|---|---|---|---|
| 0 | `self` | `FUN_007644a0` | The result (an object local) takes the handle of the object, counted |
| 1 | `getstate` | `FUN_00764550` | The result byte is the state of the object |
| 2 | `isnull` | `FUN_00764560` | The result byte is 1 when the argument is 0 |
| 3 | `isequal` | `FUN_00764580` | The result byte is 1 when the two arguments are equal |
| 4 | `del` | `FUN_007645a0` | Removes an object from the list of the object (+0x20); not read further |
| 5 | `isrunning` | `FUN_007645c0` | The result byte is 1 when the object has a main thread |
| 6 | `join` | `FUN_007645d0` | Returns a task (vtable `0x879ecc`) on the object; not read further |
| 7 | `position` | `FUN_007645e0` | Writes the three arguments into the position block of the result |
| 8 | `rotation` | `FUN_00764600` | As `position`, each one times `0x3c8efa35` (0.0174532924) |
| 9 | `color` | `FUN_00764620` | Writes four floats into the colour block of the result |
| 10, 11 | `printstring`, `printnumber` | `FUN_00764650`, `FUN_007646f0` | Draw the string or the number (`%f`) on the debug overlay (`FUN_00731880`) when the debug channel 5/0x19 is on |
| 12 | `logstring` | `FUN_00764710` | Writes the string to the log (`%s\n`) |
| 13 | `lognumber` | `FUN_007647b0` | Nothing |
| 15 | `random` | `FUN_007647d0` | (b − a) × r × 2^-32 + a, with r the next value of the global generator ([`GAMEPLAY.md`](GAMEPLAY.md), "Random numbers") |
| 16 | `randomint` | `FUN_007647f0` | f = r × 2^-32; b when f is 1.0, else (int)((b − a + 1) × f) + a |
| 17 | `distance` | `FUN_00764810` | sqrt(dy² + dz² + dx²) of two position blocks, 0 when a block is missing or the sum is 0 |
| 18, 19 | `degtorad`, `radtodeg` | `FUN_00764830`, `FUN_00764850` | The argument times `0x3c8efa35` or `0x42652ee1` (57.2957802) |

Body 14, `wait`, is written in script.

## The classes of a level

Every region carries, in its `packages` section, one more compiled package
named `default` ([`FORMATS/bpackage.md`](FORMATS/bpackage.md), "Region
packages"). It imports `creatures` and holds the class `resourcedb` and one
class for each scripted entity, named like the entity (`sfxobj002`,
`regionkeeper001`); the entity names it in its `reserved.script` property.
Each of those classes derives from a class of `creatures` or `native` and
can add fields, states and bodies. 165 regions, 13,158 classes.

## Checked in the running game

A local probe (`re/decomp/script/live_probe.py`, route
`re/decomp/script/probe_route.txt`) reads the script objects of the Wine
oracle standing in `sl01` (the set at manager +0xb4, manager
`DAT_00a6a5dc`), four times 25 seconds apart, and compares them with the
classes of the eight `slums` containers (their `bpackage` entries and their
region packages):

- 396 objects of 224 classes. 395 of them have a class of those files, and
  its kind (+0x1c), data size (+0x1e) and state count in memory are the
  ones of the file. The other one, `gamecameraimpl`, is the class of the
  `camera` package of `code.big` (not compared).
- The state byte of every object is below the state count of its class.
  Objects by state: 226 in state 0, 104 in 1, 57 in 2, 9 in 3.
- 87 objects have a main thread (94 right after the spawn). 78 of them are
  `goe::ScriptExec` threads, and in all 78 the first frame belongs to the
  object, runs a body of its class or of an ancestor, and that body is the
  `start` (slot 0) of the state the object is in. The other 9 are other
  objects: `FUN_00580be0` stores there what a native `start` returns.

## Linking checked on the data

A local check (`re/decomp/script/link_check.py`) links by name, for each of
the 165 regions, the `bpackage` entries of its container and its region
package: 81,752 classes. Every parent, every state owner, every slot entry
and every reference of kinds 3, 4 and 5 resolves to a class of the same
program, with its method or body index in range. The references of kind 2
name a value type: `position`, `gridposition`, `color` and `sound`, which
are value classes of `native`, and `activation_info` and `creature`, which
are native types with no class (`ActivationNativeType`,
`CreatureNativeType`). For `sl01` the program has 497 classes, 1,320 bodies
(505 native), 24,640 code words, 13,361 slot entries and 3,991 references.

The port links the same programs with `ScriptProgram::link`
(`src/engine/script_program.cpp`) and its tests check these numbers and
the 165 programs. In every program each state table, own or copied from its
owner, has the slot count of its class.

## Not read

- What each native function does: one function per row of the table of
  `natives.py`.
- Who creates the script objects of a level and who runs them each frame.
  The lead: the entities that name a class in `reserved.script`
  (`FUN_00737a80`, `FUN_007828c0`) and the factory +0x10 of the class.
- The virtual +0x18 of a script object (`0x580880`), called after a state
  change.
- The collection type (kind 7) and the type with kind 8: no data uses them.
- The text compiler (`FUN_00793280` imports a package from source); the
  game data has only compiled packages.

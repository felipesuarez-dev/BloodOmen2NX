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
and `src/engine/script_machine.cpp` (the machine: objects with the initial
values of their fields, their main thread and their states, strings,
frames and every instruction the game data uses, with the loop guard, the
calls, the child threads and tasks, the binding of native functions, the
end of a frame and the value types `position`, `rotation`, `color`,
`gridposition`, `waypoints`, `creature` and `creaturecontroller`, and
`sound`, `effect` and `soundgroup` with no engine system behind them: a slot
starts at 0, as the original leaves it for a name it does not find; `timer`
and the native functions are not ported yet). Every class of the region
package of `sl01` gets its object with the initial values of its fields
(79 classes, 78 of them scripted entities).

## Objects, classes and states

- A script object (`goe::ScriptObject`, vtable `0x86df10`, constructor
  `FUN_0057f830`) has a class (+0xc), a data block (+0x10), a pending state
  (+0x14), a main thread (+0x18) and a handle (+8). The handle is a small
  counted object (count at +4) that points back at the object (+8): a
  script variable that refers to an object holds its handle. The object
  counts its own handle (`FUN_0057f830`: the handle is made with count 0
  and the object adds 1), so a new object's handle has count 1 and a
  release by a variable never brings it to 0 while the object holds it.
  When a release does bring it to 0, the handle's virtual +4 runs (vtable
  `0x86df04`).
- The data block has the size the class declares (`FUN_0057fa20`) and holds
  the fields at their offsets. Its first byte is the current state of the
  object. `FUN_0057fa20` fills the fields of the class and then of each
  ancestor with their defaults, and skips a field whose offset a class
  nearer the object already filled (it keeps a set of the offsets): a class
  that declares a field of an ancestor again, at the same offset, gives it
  its own default. This happens 11,541 times in the data (for example
  `kainactorscript` declares `captionedname` of `dumbactorscript` again).
  By kind: 1, 2, 3, 5 and 11 store the default value (`FUN_00587fc0`); 4
  stores the record of the default name, counted, or the empty string when
  there is none; 6, 7 and 10 take their initial value (`FUN_00588390`); 9
  initialises the slot from the default chunk (the value type's +0x18).
  Every field of kind 1, 2, 3, 4, 5 and 11 in the data has a default
  (measured).
- A class has a parent (+0x18), a name (+8) and one slot table per state
  (+0x24, 8 bytes each: the table, its length and whether the class owns
  it). `FUN_00580810` finds the body of a method for an object: the table
  of the current state, at the slot of the method.
- State 0 is `default`. Slot 0 is the method `start` of the state.
- `FUN_00580730` (virtual +8) starts an object: it must be in state 0, and
  `FUN_00580be0` creates the main thread on the `start` of the state (slot
  0 of the table of the state; a native body returns the task that becomes
  the main thread, with the object as both self and owner; a script body
  becomes a thread with no arguments), then it runs once with 0. Started in
  another state, the object prints `WARNING: Branch occurred before
  ScriptObject::StartObject() called!` and does nothing.
  `FUN_00580900` (virtual +0x10) runs the main thread once
  (`FUN_00580cb0`): a main thread that stops running is destroyed, and a
  pending state is taken after the run. `FUN_00580770` (virtual +0xc)
  stops the object: it destroys the thread and returns to state 0 with no
  pending state.
- While an object runs, changes state or stops, it is on a stack of
  running objects (`DAT_00a6a588`, depth `DAT_00a6a580`; past 20 entries
  the original prints `ScriptObject::PushThread(): Stack size exceeded!
  The game will likely crash now.`).
- `FUN_00580830` (virtual +0x14) asks for a state. It accepts a state
  above 0 and below the state count of the class and stores it as pending.
  If the object is not running its thread at that moment, `FUN_00580890`
  changes at once; otherwise the change happens when the run of the thread
  returns (`FUN_00580cb0`). The change destroys the main thread, writes the
  state in the first byte of the data, clears the pending state and makes
  the main thread on the `start` of the new state, which first runs at the
  next run of the object (the virtual +0x18 the change calls last,
  `0x580880`, does nothing in a script object).
- A pending state on the owner of a thread stops the thread: `FUN_0057dcf0`
  checks it after each child and before each pass of the interpreter,
  `0x5a` checks it after the copy runs, and a native call by `0x48` to
  `0x4c` that returns with a state pending ends the frame (`FUN_0057f580`;
  `0x47` calls the native itself and goes on).

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
- The end of a frame (`FUN_0057d690`, called by `FUN_0057dcf0` before it
  frees the frame): a frame whose word 0 is not 1 destroys each parameter
  of the method and then each local of the body at its offset in the
  locals (`FUN_00588570`, by kind; see "Values"), then frees the stack and
  the locals. Every frame then destroys its child threads (the virtual +0
  of each) and frees their list. Word 0 is 1 for the copy of a frame
  (`FUN_0057d5c0`), which shares the stack and the locals of the frame it
  copies and frees neither; it is 2 for the frame of a call
  (`FUN_0057f580`) and 2, 3 or 0 for the first frame of a thread, by the
  byte +5 of the method (0, 1 or 2; `FUN_0057d810`). The word that holds
  the address of the result is not a variable and is not destroyed.
- `FUN_0057dcf0` (virtual +8 of vtable `0x86def0`; +4 `FUN_0057da20` says
  whether the thread has frames, +0 `FUN_0057dc70` destroys it, and its
  destructor ends the frames from the last one, `FUN_0057dc80`) runs a
  thread once with an argument it passes on to each child (a child that
  has just started runs once with 0): first it runs the children of the
  last frame and destroys the ones that are no longer running (a child
  that ended in an earlier run leaves the list here); if the frame
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
only that byte. The codes in italics are not used by the game data: no
body of the `bpackage` entries and none of the packages of the 165 regions
has them. Eight codes are used only by the region packages: `0x0d` (342
times), `0x17` and `0x18` (291 times each, in 61 classes: they index
arrays), `0x37` (222), `0x53` (15), `0x54` (67), `0x57` (1) and `0x58` (24).

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
| `0x0d`, `0x0e`, `0x0f`, *`0x10`* | Top is an object: replace it with the byte, the 32-bit value, the address or the object of the handle at `imm` in its data; 0 when the object is 0 |
| *`0x11`*, *`0x12`*, *`0x13`* | Top is an address: replace it with the byte, the 32-bit value or the address at `imm` from it; nothing when it is 0 |
| *`0x14`*, *`0x15`*, *`0x16`* | Pop an index; top is an address: replace it with the byte at the index, the 32-bit value at four times the index, or the object of the handle there |
| `0x17` | Pop an index; top is an address: add `imm` times the index, unless the address is 0 |
| `0x18` | Top is an index (not popped): when it is below 0 or not below `imm`, the cell under it becomes 0 |
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
| `0x35`, `0x36`, `0x37` | Floats: less, less or equal, equal |
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
| `0x53` | popped | body | As `0x4d` without waiting: the task joins the children of the frame and runs once |
| `0x54`, `0x55`, `0x56`, `0x57`, `0x58` | as `0x48` to `0x4c` | as `0x48` to `0x4c` | As `0x4e` to `0x52` without waiting |

In the `bpackage` entries the calls that start a thread (`0x4d` to `0x52`,
`0x55`, `0x56`) always target a method whose byte +5 is 2, and the others
never do; the region packages were not measured for this.

## Values

Every value takes 4 bytes in a local, a field or a stack cell, except a
boolean and a byte, which take one (`FUN_00587d30`; an array takes its
count times the size of its element). What the 4 bytes hold:

| Kind | Holds | Initial value (`FUN_00588390`) | Store (`FUN_00587fc0`) | Destroy (`FUN_00588570`) |
|---|---|---|---|---|
| 1 boolean, 5 byte | the value, one byte | 0 | the low byte | nothing |
| 2 integer, 3 float, 11 enumeration | the value | 0 | the word | nothing |
| 8 | a word (no data uses it) | 0 | 0 | nothing |
| 4 string | a pointer to a counted string | the shared empty string, counted | the string, counted | the string's count goes down by 1 and the string is freed at 0 (`FUN_0068e7e0`); then the 4-byte holder is freed |
| 10 object | a pointer to the counted handle of an object | 0 | the handle, counted | when not 0, the handle's count goes down by 1; at 0 the handle's virtual +4 runs |
| 9 value of a class | a pointer to a block the value type owns | the type's virtual +0x18 with no source | +0x18, then +0x20 with the source | the type's virtual +0x1c |
| 6 array | the elements, one after the other | each element | each element | each element |
| 7 | a pointer to a collection object of 0x1c bytes (no data uses it) | an empty collection | a copy | `FUN_00588670`, then `FUN_0070d050` |

In the original a string variable holds a pointer to a 4-byte holder that
points to the counted string (`FUN_00588390` allocates the holder); the
port's variable is the holder itself (`docs/ARCHITECTURE.md`), so it has
nothing to free but the string.

Measured over the 480 packages and the 165 region packages: kinds 7 and 8
appear nowhere. Arrays (kind 6) appear only as fields (123, of 10 or 20
floats or values of a class) and as parameters of 168 native bodies (20
floats or 20 values); no local and no parameter of a script body is an
array, so a frame never initialises, stores or destroys one.

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

The value types read so far (the fields of kind 9 of the 165 region
packages: `sound` 5,078, `waypoints` 1,532, `soundgroup` 231, `effect` 166,
`creaturecontroller` 97 and `creature` 71 with no default, `position` 58,
`rotation` 20, `color` 3, `timer` 2):

| Type | Vtable | Block | Initial block (+0x18 with no source) | Default chunk (+0x10) |
|---|---|---|---|---|
| `position` | `0x878d68` | 12 bytes, three floats | zeroed | three floats |
| `rotation` | `0x878d3c` | 12 bytes, three floats (radians) | zeroed (`FUN_0079e0a0`) | three floats (`FUN_0079e020`) |
| `color` | `0x878d10` | 16 bytes, four floats | zeroed (`FUN_0079ddd0`) | four floats (`FUN_0079dd50`) |
| `gridposition` | `0x878bb0` | 8 bytes | left as the allocator gives it (`FUN_0079b080`) | none: +0x10 returns 0 (`FUN_0079b060`) |
| `timer` | `0x878c34` | a counted timer object of 0x14 bytes (`FUN_0079cac0`); assign shares it (`FUN_0079cca0`) | a new timer (`0x79cc30`) | not read |
| `waypoints` | `0x878d94` | a list {u32 count, address of the points, count points of three floats} | the source list, shared (`FUN_0079e6e0`; assign `FUN_0079e700` shares too; destroy `FUN_0079e6f0` does nothing) | a u32 count, then the points (`FUN_0079e650`): checked on the 1,529 defaults of the regions, 0 to 18 points; read once, so every object of the class shares the list |
| `sound` | `0x878c08` | a counted sound instance of 0x18 bytes (`FUN_0079ba00`) | 0 with no source; with a source, the sound found by name in the sound bank (`FUN_006cde30`) wrapped in a new instance (`FUN_0079c0b0`); destroy releases it (`FUN_0079c1b0`); assign shares it, counted (`FUN_0079c1d0`) | a name, 2 bytes holding a name index (`0x79bff0`, `FUN_0079c010`; 4-byte chunk) |
| `soundgroup` | `0x878bdc` | the sound group found by name (`FUN_006cdbb0`), not counted | the group of the source name, or 0 (`0x79b410`); destroy does nothing (`0x79b440`); assign copies the word (`0x79b450`) | a name (`FUN_0079b320`; 4-byte chunk) |
| `creature` | `0x878b58` (`CreatureNativeType`, a native type with no class) | the word of an engine creature, not counted | the source word (`FUN_0076b2f0`); destroy leaves 0 (`FUN_0076b300`); assign copies the word (`FUN_0076b310`) | none (`0x4cb480`) |
| `creaturecontroller` | `0x878c60` | a counted engine object (count at +4) | 0 whatever the source (`0x79d4b0`); destroy releases it and leaves 0 (`FUN_0079d4c0`); assign shares it, counted (`FUN_0079d4e0`) | none (`0x79d490`) |
| `effect` | `0x878ce4` | a counted effect instance made by the effect resource of the name (`FUN_005fc7f0`, then its virtual +0x10) | 0 with no source (`0x515140`); destroy releases it and leaves 0 (`0x5151d0`); assign shares it, counted (`0x5151f0`) | a name (`0x515080`, `FUN_005150a0`; 4-byte chunk) |

For the four block types, +0x18 with a source copies the source block into
a new one; +0x1c frees the block and leaves 0 in the slot (`FUN_0079e430`,
`FUN_0079e110`, `FUN_0079de50`, `FUN_0079b0d0`); +0x20 does nothing when the
slot already holds the source block, initialises a slot with no block,
zeroes the block for a source of 0 and copies otherwise (`FUN_0079e450`,
`FUN_0079e130`, `FUN_0079de70`, `FUN_0079b0f0`). A field of kind 9 starts
with +0x18 on the block +0x10 read from its default chunk at load time, or
on 0 when it has none (`FUN_0057fa20`). In the fields of the region package
of `sl01` the other value types are `sound` (56), `waypoints` (18),
`soundgroup` (5) and arrays of `color` (2); its locals of kind 9 are
`creature` (64) and `color` (3).

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

## The natives of the value classes

A method of a value class runs on a value: the object a native function of
`position`, `rotation`, `color` or `gridposition` gets is the address of
the slot that holds the block (the code pushes it with `0x0b` or `0x07`
just before the `0x47`: 5,559 and 2,779 times in the game data, 8,338
calls). Each native calls one helper, and every helper does nothing (a
getter writes 0 to the result) when the slot holds no block; the natives of
`waypoints` work the same way on its list. Getters write
the result cell; setters take their floats from the argument cells, in
order.

| Class | Body | Method | Native | Helper | What it does |
|---|---|---|---|---|---|
| `position` | 0, 1, 2 | `getx`, `gety`, `getz` | `FUN_0076d420`, `FUN_0076d440`, `FUN_0076d460` | `FUN_0079e4a0`, `FUN_0079e4c0`, `FUN_0079e4e0` | The float at +0, +4 or +8 |
| `position` | 3, 4, 5 | `setx`, `sety`, `setz` | `FUN_0076d480`, `FUN_0076d4a0`, `FUN_0076d4c0` | `FUN_0079e500`, `FUN_0079e520`, `FUN_0079e540` | Writes the argument at +0, +4 or +8 |
| `position` | 6 | `setvalue` | `FUN_0076d4e0` | `FUN_0079e560` | Writes the three arguments |
| `rotation` | 0, 1, 2 | `getx`, `gety`, `getz` | `FUN_0076d190`, `FUN_0076d1b0`, `FUN_0076d1d0` | `FUN_0079e180`, `FUN_0079e1a0`, `FUN_0079e1d0` | The angle in degrees: `0x42652ee1` (57.2957802, `0x87a070`) times the float at +0, +4 or +8 |
| `rotation` | 3, 4, 5 | `setx`, `sety`, `setz` | `FUN_0076d1f0`, `FUN_0076d210`, `FUN_0076d230` | `FUN_0079e200`, `FUN_0079e220`, `FUN_0079e240` | `0x3c8efa35` (0.0174532924, `0x87a06c`) times the argument, at +0, +4 or +8 |
| `rotation` | 6 | `setvalue` | `FUN_0076d250` | `FUN_0079e260` | The three arguments, each times `0x3c8efa35` |
| `rotation` | 7 | `rotatez` | `FUN_0076d270` | `FUN_0079e2a0` | +8 += `0x3c8efa35` times the argument (one rounding to float) |
| `color` | 0 | `setvalue` | `FUN_0076cf30` | `FUN_0079dee0` | Writes the four arguments |
| `color` | 1 to 4 | `setred`, `setgreen`, `setblue`, `setalpha` | `FUN_0076cf60`, `FUN_0076cf80`, `FUN_0076cfa0`, `FUN_0076cfc0` | `FUN_0079df10`, `FUN_0079df30`, `FUN_0079df50`, `FUN_0079df70` | Writes the argument at +0, +4, +8 or +0xc |
| `gridposition` | 0, 1 | `getx`, `gety` | `FUN_0076b760`, `FUN_0076b780` | `FUN_0079b130`, `FUN_0079b150` | The word at +0 or +4 |
| `gridposition` | 2, 3 | `setx`, `sety` | `FUN_0076b7a0`, `FUN_0076b7c0` | `FUN_0079b170`, `FUN_0079b190` | Writes the argument at +0 or +4 |
| `gridposition` | 4 | `setvalue` | `FUN_0076b7e0` | `FUN_0079b1b0` | Writes the two arguments |
| `waypoints` | 0 | `count` | `FUN_0076d630` | `FUN_0079e710` | The count of the list, 0 when the slot holds none |
| `waypoints` | 1 | `getwaypoint` | `FUN_0076d650` | `FUN_0079e730` | The result is a `position` slot: point i of the list for 0 ≤ i < count (signed), else three zeros, written into the result's block |

The game data calls `position` 0 to 6, `rotation` 0 to 2, `color` 0 and
`gridposition` 0, 1 and 4. The natives of `timer` (six, one a task) need
the timer object and are not read.

## The natives the scripts of sl01 call

Measured on the program of `sl01` (its `bpackage` entries and its region
package; the calls `0x47`, `0x48`, `0x4b`, `0x4d`, `0x4e` and `0x51` whose
word names a native body, and the virtual calls whose method has one): 2,614
call sites reach 162 distinct native functions. By class: `basescript` 730,
`effect` 601, `sound` 379, `creaturescript` 323, `bo2creaturescript` 161,
`monsterscript` 108, `soundscript` 78, `soundgroup` 48, `position` 33,
`gamecamera` 31, `cinematicscript` 30, `creaturecontroller` 27,
`conversationscript` 14, `detectorscript` 13, `timer` 8, `gridposition` 8,
`rotation` 6, and fewer than 6 for seven more classes.

The most called:

| Calls | Class | Method | Function | Needs |
|---:|---|---|---|---|
| 235 | `basescript` | `self` | `FUN_007644a0` | nothing |
| 170 | `effect` | `starteffect` | `FUN_0076cd10` | the effects |
| 167 | `basescript` | `position` | `FUN_007645e0` | nothing |
| 158 | `effect` | `stop` | `FUN_0076cd30` | the effects |
| 155 | `basescript` | `rotation` | `FUN_00764600` | nothing |
| 132 | `sound` | `startsound` | `FUN_0076bf20` | the sound |
| 118 | `creaturescript` | `creaturebyname` | `FUN_00767270` | the creatures of the level |
| 113 | `effect` | `moverelative` | `FUN_0076cc60` | the effects |
| 110 | `sound` | `moverelative` | `FUN_0076bf00` | the sound |
| 73 | `sound` | `stop` | `FUN_0076bf30` | the sound |
| 65 | `basescript` | `printstring` | `FUN_00764650` | nothing (a debug overlay) |
| 62 | `effect` | `moverelativetocreature` | `FUN_0076cc80` | the effects |
| 52 | `effect` | `settrackbone` | `FUN_0076ccf0` | the effects |
| 45 | `creaturescript` | `creatureself` | `FUN_00767310` | the creatures of the level |
| 42 | `bo2creaturescript` | `gethud` | `FUN_004cdf20` | the HUD |
| 42 | `bo2creaturescript` | `creaturescriptbyname` | `FUN_004cd760` | the creatures of the level |
| 41 | `basescript` | `random` | `FUN_007647d0` | the random generator |
| 38 | `bo2creaturescript` | `getkain` | `FUN_004cd750` | the player |
| 38 | `basescript` | `color` | `FUN_00764620` | nothing |
| 32 | `monsterscript` | `playanimation` (task) | `FUN_004d44a0` | the animation of a creature |

The natives of `basescript` and of the value classes need no engine system
(BO2NX-163, BO2NX-172). The five that find creatures ask the creatures of
the level (next section) and are ported. The rest needs the effects, the
sound, the HUD and the camera: each is a system of its own.

## The natives that find creatures

Five natives of the creature classes give a script another creature or its
object. They use the creature that owns the calling object
(`Sbasecreaturescript` +0x2c) and the creature list of the level. The
binders of the two classes start at `0x7668f0` (`creaturescript`) and
`0x4cd3f0` (`bo2creaturescript`), which Ghidra does not mark as functions.

| Class | Body | Method | Result | Functions | Rule |
|---|---:|---|---|---|---|
| `creaturescript` | 6 | `creaturebyname(string)` | `creature` | `FUN_00767270`, `FUN_00760750` | An empty name, or a cell with no string, gives 0 without a search. Otherwise: the creature of that name for the caller (`FUN_00763050`, which is `FUN_00718160` in the package of the caller's creature, +0x144, the same search as the activation of an object). A name nobody has gives 0 and the log line `<caller's creature> : GetCreatureWithName('<name>') failed` (`0x912b00`) |
| `creaturescript` | 7 | `creatureself()` | `creature` | `FUN_00767310`, `FUN_007607b0` | The creature of the object (+0x2c) |
| `bo2creaturescript` | 0 | `getkain()` | `creature` | `FUN_004cd750`, `FUN_004d8840` | Kain (`FUN_004a5290`): the first creature of the creature list (`DAT_00a836a8`) whose virtual +0x5c returns `FUN_00428640`, the factory registered under the name `kain` (the code at `0x427de0` registers it with the string at `0x8e95cc`; `0x428040`, slot +0x5c of the vtable `0x841d84`, returns it); 0 when there is none |
| `bo2creaturescript` | 1 | `creaturescriptbyname(string)` | object `creaturescript` | `FUN_004cd760`, `FUN_004d8860` | The object (+0x130) of the creature of that name (`FUN_00763050`; this one logs nothing) when it is a `creaturescript`, else 0 |
| `bo2creaturescript` | 11 | `gethud()` | object `hud` | `FUN_004cdf20`, `FUN_004d8c60` | The object (+0x130) of the creature of the HUD: `FUN_0052bd60` returns the HUD (`DAT_0091f040`) and `FUN_00531420` its creature's object |

`getkain` keeps the creatures it found in the list `DAT_00a836a0` and walks
the creature list again only while that list is empty. A creature removed
from the level leaves it (`FUN_00718970`) and the end of the level empties
it (`FUN_00718eb0`).

The two natives that return an object store it the way an assignment does,
0 included: the new handle is counted, and when it differs from the slot's,
the old one is released (`FUN_004cd760`, `FUN_004cdf20`).

"When it is a `creaturescript`" is the `dynamic_cast` from
`Sbasecreaturescript` to `Screaturescript` (`FUN_007a36c2`). The C++ class
of a script object is the class of the factory of its nearest native class
("Native functions" above). The RTTI of `bo2.exe` has 31 native classes
that derive from `Sbasecreaturescript`. Each one derives from
`Screaturescript` exactly when its script class derives from
`creaturescript`, so the cast follows the script hierarchy. In `sl01`, 61
of the 79 classes of the region package are `creaturescript`. Of the rest,
`resourcedb` is not a creature, and for these 17 `creaturescriptbyname`
gives 0:

- the 15 monsters, which are `monsterscript` through `peasant`, `animal`
  or `sarafan`;
- the 2 conversations, which are `conversationscript`.

## The classes of a level

Every region carries, in its `packages` section, one more compiled package
named `default` ([`FORMATS/bpackage.md`](FORMATS/bpackage.md), "Region
packages"). It imports `creatures` and holds the class `resourcedb` and one
class for each scripted entity, named like the entity (`sfxobj002`,
`regionkeeper001`); the entity names it in its `reserved.script` property.
Each of those classes derives from a class of `creatures` or `native` and
can add fields, states and bodies. 165 regions, 13,158 classes.

## The script objects of a level

Read from the code (dumps in `re/decomp/script_runtime/`) and checked with a
live trace of the oracle in `sl01` (below):

- The package of a region. When the region loader (`FUN_00791ee0`) meets
  the `packages` section of a region, it makes the name of the region's
  package once (`FUN_00783900`): from the name of the region (`slums/sl01`)
  it drops what comes before the first letter (`isalpha`), drops the
  trailing ` `, `\t`, `\r` and `\n`, turns every character that is not a
  letter or a digit (`isalnum`) into `_`, and lowers the case (`tolower`):
  `slums_sl01`. `FUN_00783ab0` then makes a package of that name that
  imports `creatures` (or reuses one of the same name, with the warning
  `Script package for this region already exists!`); the classes of the
  section's package are found in it (the objects of the oracle below have the
  paths `slums_sl01/<class>`).
- The check of a region's scripts (`FUN_00737a80`): for each entity of the
  region database with a `reserved.script` property (`FUN_007828c0` gives
  its value, or an empty string), the value is split at `/` and the class is
  looked up in the region's package; when it is missing the game logs
  `'reserved.script' keyword found, but no script '<script>' in package
  '<package>' was found.  RegionDBase is being cleaned.` and removes the
  property.
- A scripted entity of a region becomes a `Creature` (vtable `0x87721c`).
  `FUN_0070e450` sets its script path (+0x134): when the script name has no
  `/` and the creature has a package (+0x144), the path is the name of that
  package (`FUN_00732a30`: the name table entry of its id at +8), `/` and the
  script name; otherwise the script name as given. Then it calls
  `FUN_0070e560`.
- `FUN_0070e560` creates the object when the path is not empty:
  `FUN_00582f50` splits the path at `/` and needs exactly two parts, finds
  the package by name (manager +0x14) and the class in it, and when the
  class has its flag +0x22 set, calls the factory of the class (+0x10,
  virtual +0x10; `FUN_005831b0`). The object must derive from
  `basecreaturescript` (`FUN_00580a30`); if not, the creature logs a warning
  and keeps no script (+0x138 = 0). Otherwise the creature holds the object,
  counted (+0x130), the object points back at the creature (+0x2c) and
  +0x138 = 1.
- The creatures of a region (`FUN_00736da0`): the region database spawns
  its entities with the starts held (`DAT_00a836d4 = 1`). Each entity goes
  through the factory of its template (`FUN_00728fd0`, virtual +0x20); the
  creature factory (`FUN_007289d0`, `CreatureFactory`) builds a `Creature`
  (`FUN_0070d0c0`) with the value of the entity's `reserved.script` as its
  script name and the region's package as its package (+0x144). When every
  entity is built, `FUN_00718290` activates, in the order of the creature
  list (`DAT_00a836a8`), every creature not activated yet (`FUN_0070e010`,
  bit 0 of +0xf0). A creature built while the starts are not held activates
  at once from its constructor. Traced in the oracle (`start.gdb`,
  `factory.gdb`): the 78 scripted creatures of `sl01` start in the order in
  which they were built, except `regionkeeper001`, whose object its own
  creature class starts (`0x4ef66a`).
- Activating a creature (`FUN_0070e0c0`), when it has an object whose class
  has its flag +0x22: every field of kind 10 of the object
  (`FUN_00589030(object, 10)`) is linked to the creature found by the name
  of the field's type class (`FUN_00718160`: the creature of that name in
  the same package, or a creature with no package, searched in the lists
  `DAT_00a836a8` and then `DAT_00a83680`): the field takes the handle of
  that creature's object, counted (names compare with case, `FUN_0068ec30`).
  Then the object starts (virtual +8, `FUN_00580730`). Read in the oracle two
  minutes after the spawn (`links_probe.py`): of 50 object fields of the
  `sl01` classes, 13 hold the object of the creature named like the field's
  type and none the one named like the field; the 33 whose type is
  `exportedcamera` (no creature has that name) are empty; 4 hold another
  object (a script can store into them) and 5 could not be read. A monster brought back to life (`FUN_004ec3f0`) gets the
  initial values again (`FUN_0057fa20`) and is activated again.
- `FUN_0070e8b0` handles the messages of a creature (called from
  `FUN_0070ee70`, the slot +0x54 of `Creature` and of `CameraMark`, vtable
  `0x8793cc`). For the message type `DAT_00910640`, when +0x138 is set, the
  path is not empty and the class has its flag +0x22, it runs the object
  once (virtual +0x10, `FUN_00580900`) with the second word of the message.
  The trace below shows it is a message of every frame.
- A scripted sound instance (`ScriptedSoundInst`, vtable `0x87a11c`) holds
  its own object (+0x48). Its slot +0x3c (`FUN_007a2b20`) runs it once per
  update; when the object is then not in state 2 and has no main thread,
  it calls `FUN_006e20d0(0)`. Its slot +0x1c (`FUN_007a2bf0`,
  `FUN_00799a60`) asks the object for state 2 and runs it once.

Checked in the oracle (`re/decomp/script_runtime/objects.gdb`,
`owner_probe.py`, route `re/decomp/script/probe_route.txt`):

- 20,000 calls of `FUN_00580900` after the spawn in `sl01`: 17,552 from
  `FUN_0070e8b0`, 2,356 from `FUN_007a2b20` and 92 from `FUN_00799a60`.
- 792 constructions of script objects (`FUN_0057f830`) from the start of the
  game to the spawn, all of them through the factory call of
  `FUN_005831b0`.
- The 396 objects at the spawn: 272 belong to a `Creature`, 93 to a
  `ScriptedSound`, 26 (`camerasetup`), 4 (`stringtable`) and 1
  (`gamecameraimpl`) to other owners. Of the 272, 240 have a path
  `<package>/<class>` whose second part is the class of the object:
  `slums_sl01` 78 (the 78 entities of the `sl01` region with a
  `reserved.script`, which name the class alone), `slums_sl02` 66,
  `slums_sl_music` 94, and `native` 2 (`native/hud`,
  `native/hudhintpopup`); the other 32 are `exportedcamera` objects with an
  empty path. So the package of a region carries the name of the region
  with `_` for `/`, not the `default` of the file.

## In the port

The machine runs the game's compiled code as it is; the tests feed it
synthetic classes whose code and expected values come from the rules on
this page. No object of a level runs yet: that needs the native functions
of the game and a host that gives the machine the creatures of the level.

| Part | File | Tests (`ctest -R`) |
|---|---|---|
| Package reader (`bpackage`, the packages of the regions) | `src/engine/script_package.cpp`, `src/engine/region.cpp` | `script_package`, `region`; `bo2nx_check_data` reads all 480 and 165 |
| 32-bit memory | `src/engine/script_memory.cpp` | `script` (`test_memory_*`) |
| Linker: classes, state tables, code words | `src/engine/script_program.cpp` | `script` (`test_link_*`, every region of the game) |
| Values, arithmetic, logic, jumps, loop guard | `src/engine/script_machine.cpp` | `script` (`test_push` to `test_cast`) |
| Calls, natives, the end of a frame | `src/engine/script_machine.cpp` | `script` (`test_native_call` to `test_array_variables`) |
| Child threads, tasks, parallel blocks | `src/engine/script_machine.cpp` | `script` (`test_spawn_waits` to `test_fork_waits`) |
| Objects: initial values, main thread, states | `src/engine/script_machine.cpp` | `script` (`test_field_defaults` to `test_stop`) |
| Value types `position`, `rotation`, `color`, `gridposition` | `src/engine/script_machine.cpp` | `script` (`test_position_type` to `test_unregistered_value_type`) |
| Value types `waypoints`, `creature`, `creaturecontroller`, `sound`, `effect`, `soundgroup`; every object of the region package of `sl01` | `src/engine/script_machine.cpp` | `script` (`test_waypoints_type`, `test_creature_value_types`, `test_create_sl01_objects`) |
| Activating the object of a creature: its object fields linked by name, then the start | `src/engine/script_machine.cpp` | `script` (`test_activate_links_fields`) |
| The creature of an object, a handle stored in an object slot, and `ScriptCreatureHost`, which the natives that find creatures ask | `src/engine/script_machine.cpp` | `script` (`test_creature_and_store_handle`; the 61 `creaturescript` classes of `sl01` in `test_create_sl01_objects`) |
| The natives that find creatures (`creaturebyname`, `creatureself`, `getkain`, `creaturescriptbyname`, `gethud`) | `src/engine/script_creature_natives.cpp` | `script` (`test_creature_natives`; the bodies they bind to in `test_create_sl01_objects`) |

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
- The order of the creature list `DAT_00a836a8`, which is the order in
  which the creatures of a region are activated.
- The argument of the update message that `FUN_0070e8b0` passes to the run
  of the object, and the order in which the creatures get the message.
- What `FUN_006e20d0(0)` does to a scripted sound instance.
- The properties `<class>.<field>` of an entity (for example
  `levelkeeperintro.commondata.farClip`), which look like overrides of the
  defaults of the fields of its object: who reads them is not found yet
  (`FUN_00737a80` is the check of `reserved.script` above, not this).
- The collection type (kind 7) and the type with kind 8: no data uses them.
- The text compiler (`FUN_00793280` imports a package from source); the
  game data has only compiled packages.

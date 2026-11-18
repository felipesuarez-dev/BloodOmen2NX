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
only that byte. The codes in italics are not used by the game data: no body
of the `bpackage` entries and none of the packages of the 165 regions has
them. Eight codes are used only by the region packages: `0x0d` (342 times),
`0x17` and `0x18` (291 times each, in 61 classes: they index arrays),
`0x37` (222), `0x53` (15), `0x54` (67), `0x57` (1) and `0x58` (24). The
region packages hold 16,104 script bodies: 345,637 words, 290,084
instructions.

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
| 4 string | a pointer to a 4-byte holder of a counted string | a new holder of the shared empty string, counted | a new holder of the source holder's string, counted | the string's count goes down by 1 and the string is freed at 0 (`FUN_0068e7e0`); then the 4-byte holder is freed |
| 10 object | a pointer to the counted handle of an object | 0 | the handle, counted | when not 0, the handle's count goes down by 1; at 0 the handle's virtual +4 runs |
| 9 value of a class | a pointer to a block the value type owns | the type's virtual +0x18 with no source | +0x18, then +0x20 with the source | the type's virtual +0x1c |
| 6 array | the elements, one after the other | each element | each element | each element |
| 7 | a pointer to a collection object of 0x1c bytes (no data uses it) | an empty collection | a copy | `FUN_00588670`, then `FUN_0070d050` |

A string variable holds a pointer to a 4-byte holder that points to the
counted string (`FUN_00588390` allocates the holder; `FUN_00587fc0` makes a
new one for each store). A string on the stack is the address of a holder:
`0x06`, `0x0a` and `0x0e` push the holder of a variable, `0x04` the holder of
a name, and `0x1f`, `0x38` and the natives take holders. The code of the
`bpackage` entries and of the regions pushes string fields by value (in
`sl01`, 124 times `0x0a` against 2 times `0x0b`).

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
| `timer` | `0x878c34` | a counted timer of 0x14 bytes (`FUN_0079cac0`; "Timers" below) | a new timer whatever the source (`FUN_0079cc30`); destroy releases it and keeps the slot (`FUN_0079cc80`); assign shares it, counted (`FUN_0079cca0`) | none: +0x10 returns 0 (`FUN_0079cc10`), so a field starts as a new timer |
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
`gridposition` 0, 1 and 4. The natives of `timer` are in "Timers".

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

## The natives of sound, soundgroup and effect

Each native of these three value classes passes its arguments to a helper
of the class with the slot (the object of a native of a value class is the
address of its slot, "The natives of the value classes"). Every helper
starts from what the slot holds, and with 0 it does what the last column
says. 0 is all the port has: no sound or effect system finds a name yet
("Values"). A plain native passes the slot, then the argument cells as
values: one (`*args`) or two (`args[0]`, `args[1]`), and a getter passes
the address of the result. A task native (`*wait`, `play`) passes the slot,
the owner, `args[0]` and `args[1]` (`FUN_0076bb20`, `FUN_0076c030`), and
the effect `moverelative` and `moverelativetocreature` pass the slot and
`args[0]`, `args[1]`, `args[2]` (`FUN_0076cc60`, `FUN_0076cc80`). A plain
setter with a time (`setvolume` and the like) gets a task from the same
helper as its `*wait` form and adds it to the script manager
(`FUN_00583200`) when there is one.

| Class | Body | Method | Native | Helper | With 0 in the slot |
|---|---:|---|---|---|---|
| `soundgroup` | 0 | `setmute(bool)` | `FUN_0076ba60` | `FUN_0079b460` | nothing |
| `soundgroup` | 1 | `setpause(bool)` | `FUN_0076ba80` | `FUN_0079b480` | nothing |
| `soundgroup` | 2 | `setvolume(float, float)` | `FUN_0076baa0` | `FUN_0079b4a0`, `FUN_0079b560` | nothing (no task to add) |
| `soundgroup` | 3 | `setpan(float, float)` | `FUN_0076bac0` | `FUN_0079b4d0`, `FUN_0079b5f0` | nothing |
| `soundgroup` | 4 | `setpitch(float, float)` | `FUN_0076bae0` | `FUN_0079b500`, `FUN_0079b680` | nothing |
| `soundgroup` | 5 | `setmaxdist(float, float)` | `FUN_0076bb00` | `FUN_0079b530`, `FUN_0079b710` | nothing |
| `soundgroup` | 6 | `setvolumewait(float, float), a task` | `FUN_0076bb20` | `FUN_0079b560` | no task |
| `soundgroup` | 7 | `setpanwait(float, float), a task` | `FUN_0076bb40` | `FUN_0079b5f0` | no task |
| `soundgroup` | 8 | `setpitchwait(float, float), a task` | `FUN_0076bb60` | `FUN_0079b680` | no task |
| `soundgroup` | 9 | `setmaxdistwait(float, float), a task` | `FUN_0076bb80` | `FUN_0079b710` | no task |
| `soundgroup` | 10 | `ismuted()` | `FUN_0076bba0` | `FUN_0079b7a0` | low byte of the result 0 |
| `soundgroup` | 11 | `ispaused()` | `FUN_0076bbc0` | `FUN_0079b7c0` | low byte of the result 0 |
| `soundgroup` | 12 | `getvolume()` | `FUN_0076bbe0` | `FUN_0079b7e0` | result 1.0 (`0x3f800000`) |
| `soundgroup` | 13 | `getpan()` | `FUN_0076bc00` | `FUN_0079b800` | result 0 |
| `soundgroup` | 14 | `getpitch()` | `FUN_0076bc20` | `FUN_0079b820` | result 0 |
| `soundgroup` | 15 | `getmaxdist()` | `FUN_0076bc40` | `FUN_0079b840` | result 0 |
| `sound` | 0 | `moverelative(basescript)` | `FUN_0076bf00` | `FUN_0079c200` | nothing |
| `sound` | 1 | `startsound()` | `FUN_0076bf20` | `FUN_0079c220` | nothing |
| `sound` | 2 | `stop()` | `FUN_0076bf30` | `FUN_0079c350` | nothing |
| `sound` | 3 | `isrunning()` | `FUN_0076bf40` | `FUN_0079c360` | low byte of the result 0 |
| `sound` | 4 | `spawnsound()` | `FUN_0076bf60` | `FUN_0079c3a0` | nothing |
| `sound` | 5 | `startlipsound(...)` | `FUN_0076bf70` | `FUN_0079c2b0` | nothing |
| `sound` | 6 | `play(), a task` | `FUN_0076bf90` | `FUN_0079c440` | no task |
| `sound` | 7 | `setvolume(float, float)` | `FUN_0076bfb0` | `FUN_0079c460`, `FUN_0079c520` | nothing |
| `sound` | 8 | `setpan(float, float)` | `FUN_0076bfd0` | `FUN_0079c490`, `FUN_0079c5c0` | nothing |
| `sound` | 9 | `setpitch(float, float)` | `FUN_0076bff0` | `FUN_0079c4c0`, `FUN_0079c660` | nothing |
| `sound` | 10 | `setmaxdist(float, float)` | `FUN_0076c010` | `FUN_0079c4f0`, `FUN_0079c700` | nothing |
| `sound` | 11 | `setvolumewait(float, float), a task` | `FUN_0076c030` | `FUN_0079c520` | no task |
| `sound` | 12 | `setpanwait(float, float), a task` | `FUN_0076c050` | `FUN_0079c5c0` | no task |
| `sound` | 13 | `setpitchwait(float, float), a task` | `FUN_0076c070` | `FUN_0079c660` | no task |
| `sound` | 14 | `setmaxdistwait(float, float), a task` | `FUN_0076c090` | `FUN_0079c700` | no task |
| `sound` | 15 | `getvolume()` | `FUN_0076c0b0` | `FUN_0079c7a0` | result 1.0 (`0x3f800000`) |
| `sound` | 16 | `getpan()` | `FUN_0076c0d0` | `FUN_0079c7d0` | result 0 |
| `sound` | 17 | `getpitch()` | `FUN_0076c0f0` | `FUN_0079c800` | result 0 |
| `sound` | 18 | `getmaxdist()` | `FUN_0076c110` | `FUN_0079c830` | result 0 |
| `effect` | 0 | `move(...)` | `FUN_0076cc40` | `FUN_00515220` | nothing |
| `effect` | 1 | `moverelative(...)` | `FUN_0076cc60` | `FUN_00515260` | nothing |
| `effect` | 2 | `moverelativetocreature(...)` | `FUN_0076cc80` | `FUN_00515420` | nothing |
| `effect` | 3 | `triggerevent()` | `FUN_0076cca0` | `FUN_00515610` | nothing |
| `effect` | 4 | `setattractor(...)` | `FUN_0076ccb0` | `FUN_00515640` | nothing |
| `effect` | 5 | `setattractortrackcreature(...)` | `FUN_0076ccd0` | `FUN_00515670` | nothing |
| `effect` | 6 | `settrackbone(...)` | `FUN_0076ccf0` | `FUN_005155e0` | nothing |
| `effect` | 7 | `starteffect(float)` | `FUN_0076cd10` | `FUN_00515730` | nothing |
| `effect` | 8 | `stop()` | `FUN_0076cd30` | `FUN_005157b0` | nothing |
| `effect` | 9 | `reset()` | `FUN_0076cd40` | `FUN_005157d0` | nothing |
| `effect` | 10 | `isrunning()` | `FUN_0076cd50` | `FUN_005157e0` | low byte of the result 0 |
| `effect` | 11 | `forceupdate()` | `FUN_0076cd70` | `FUN_00515780` | nothing |
| `effect` | 12 | `play(float), a task` | `FUN_0076cd80` | `FUN_00515800` | no task |
| `effect` | 13 | `clone()` | `FUN_0076cda0` | `FUN_005158b0` | the effect held by the result is released (its count at +4 down by 1, freed at 0) and the result becomes 0 |

## More natives of creaturescript

These read or change the creature of the object (+0x2c) or a creature in
an argument. A creature argument is a value of a class: its cell holds the
address of the slot, and the slot holds the creature (`*(*args)`). A
boolean result is the low byte of the result, and only when the result
address is not 0.

| Body | Method | Native | Helper | What it does |
|---:|---|---|---|---|
| 1 | `getposition()` | `FUN_00767170` | `FUN_0075fa70` | When the result's slot holds a block, the block gets the position of the creature (+0xb4, three floats) |
| 2 | `getrotation()` | `FUN_00767180` | `FUN_0075faa0` | The same with the rotation (+0xc0, three floats, as stored) |
| 8 | `creaturevalid(creature)` | `FUN_00767320` | `FUN_00760810` | Whether the creature is not 0 |
| 9 | `creatureequal(creature, creature)` | `FUN_00767340` | `FUN_00760850` | Whether the two creatures are the same |
| 10 | `doesnamedcreatureexist(string)` | `FUN_00767360` | `FUN_00760870` | The name as for `creaturebyname`; with an empty name false, else whether the search (`FUN_00763050`) finds a creature. It logs nothing |
| 11 | `killcreature(creature)` | `FUN_00767400` | `FUN_007603e0` | With a creature: one with a package (+0x144) hibernates (`FUN_00718cf0`), one without is removed (`FUN_00718b80`) |
| 12 | `hibernatecreature(creature)` | `FUN_00767420` | `FUN_00760410` | With a creature, `FUN_00718cf0` |
| 13 | `unhibernatecreature(creature)` | `FUN_00767440` | `FUN_00760430` | With a creature, `FUN_00718d30` |
| 16 | `getscript(creature)` | `FUN_00767630` | `FUN_00760450` | The object of the creature (+0x130), or 0, stored as a handle, counted, whatever its class |
| 27 | `playanimation(string, kind 2)` (task) | `FUN_00767930` | `FUN_0075fbc0` | A name with no characters, or a creature with no animation instance (the slot +0x10 of its +0x90 gives 0), gives no task. Otherwise the name is looked up in that instance (its slot +4 on the name's symbol): when it is missing the log gets `<creature> : Animation <name> requested but ...` (`0x8ed1fc`) and the task is the one of a wait of 10 s (the object's slot +0x28 with `FUN_0073dd20`); when it is there the task is the one of a play behavior (slot +0x28 with `FUN_007965a0`, vtable `0x879b0c`) given the name and the second argument, which keeps it as its count of plays when it is 1 or more, else 0 (`FUN_00796720`). The behavior is the task ("Creature behaviors" below); when a play ends depends on the creature's animation instance ("Animation instances"). In `sl01`: `("default", 0)` from `glyphsmoke2marker` and `("idle", 1)` from `kainactor001` |
| 47 | `wasdrawnlastframe()` | `FUN_00767f80` | `FUN_00760df0` | Whether the frame the creature was last drawn in (+0x140) is not below the frame counter (`DAT_008ff180`) less 1, compared unsigned |
| 69 | `disablesimulate()` | `FUN_007686a0` | `FUN_00762480` | The creature leaves the group `process_msg` (`FUN_0072b550` with `DAT_00a83754`) |
| 70 | `disableeotsimulate()` | `FUN_007686b0` | `FUN_00762420` | The creature leaves the second group (`DAT_00a83744`) |
| 71 | `enablemodellod()` | `FUN_007686c0` | `FUN_00762440` | `DAT_00a79e75` = 1 |
| 72 | `disablemodellod()` | `FUN_007686d0` | `FUN_00762450` | `DAT_00a79e75` = 0 |
| 73 | `disableeditmodesimulate()` | `FUN_007686e0` | `FUN_00762460` | Flags +0xf0 lose 0x10 |
| 74 | `enablesimtimeaccumulatewhileculled()` | `FUN_007686f0` | `FUN_00762470` | Flags +0xf0 gain 0x800 |
| 75 | `disablesimcull()` | `FUN_00768700` | `FUN_007624a0` | Flags +0xf0 gain 4 (the creature always gets the whole frame time) |
| 76 | `enablesimcull()` | `FUN_00768710` | `FUN_007624b0` | Flags +0xf0 lose 4, unless they have 8 |
| 77 | `disablesimwhenscriptdone()` | `FUN_00768720` | `FUN_007624d0` | Flags +0xf0 gain 0x10000 (bit 0 of +0xf2: `FUN_00718340` takes the creature out of `process_msg` once its object has no main thread) |
| 78 | `loadregion(string)` | `FUN_00768730` | `FUN_007621a0` | The name (empty when the cell holds no string), unless the debug option (5, 0x21) is on, goes as a request of kind 1 (`FUN_0074b270`) to `FUN_0074b3a0`: `\` becomes `/`, the letters lower case, and when the region will not be loaded (`FUN_0074b4a0`: it is in the map of loaded regions at `0xa83d70`, unless it is the region being unloaded, `DAT_00a83d60`, or it is the one being loaded, `DAT_00a83d64`; then the last request of the queue for it decides, 0 unload, 1 load) the request goes to the end of the queue (`DAT_00a83d6c`). Nothing waits. In `sl01`: `"slums/sl_music"` from `bo2creature_helperstatic001` and `"slums/sl02"` from `regionkeeper001` |

## Creature behaviors

A native that makes a creature do something over time (`playanimation`,
the commands of `monsterscript`) gives the script a behavior of the
creature as its task. The slot +0x28 of a creature's object makes it
(`FUN_00762df0` for a `creaturescript`; `monsterscript` has its own,
`0x4e4800`): `FUN_007144b0` calls the factory, gives the behavior the
creature (+0x40, kept at +0x10) and the object (+0x44, kept at +0xc),
keeps the factory at +8, gives it the data (+0x4c, its init) and then
+0x48 with the data and the creature's slot +0x74. The behavior goes at
the front of the object's list +0x30 (`FUN_007632a0`) and is the task.

- As a task (the machine's virtuals): +8, run, does nothing; +4 says it
  runs while its byte +4 is 0. A behavior ends by its slot +0x10
  (`0x7143f0`), which sets +4 to 1. When the machine destroys the task
  (+0, `0x714540`), the object takes it out of its list (slot +0x20,
  `FUN_00762e30`).
- The object's slot +0x1c (`FUN_00760900` for a `creaturescript`) hands
  every message of its creature to the behaviors of its list, the newest
  first (`FUN_00762dc0`). `FUN_0070e7e0` skips a behavior that ended (+0x2c
  gives +4) and walks its chain (+0x24 gives +0x14, 0 for these); an
  update goes to the chain from the inner end until one takes it. The
  update (`DAT_00910640`) reaches the behavior's slot +0x64 with the
  message (its time at +4) through the table `DAT_00a834f8`
  (`FUN_007153f0`, `FUN_00715a50`).
- The object runs before its behaviors in an update (`FUN_0070ee70`), so a
  script waiting on a behavior that ends in an update goes on in the next
  update of its creature.

The two behaviors `playanimation` uses:

- Wait (`FUN_0073dd20`, vtable `0x877c5c`): its init (`FUN_0073de20`)
  keeps the time (the first word of the data) at +0x18; each update
  (`FUN_0073de30`) takes the message's time from it (a float operation) and
  the behavior ends when it is 0 or less.
- Play (`FUN_007965a0`, vtable `0x879b0c`): its init (`FUN_00796720`)
  keeps the name (+0x1c) and the count (+0x20: the second word of the data
  when it is 1 or more, else 0), starts the name on the creature's
  animation instance (its slot +0x10) and sets the object's +0x4c to
  whether it started. Each update (`FUN_00796ab0`), while the object's
  +0x4c is set and time is left, the instance advances by the time left
  (its slot +0x24 gives flags and leaves the time past the end of the clip;
  at most 3 advances that leave the time as it was) and moves the creature
  with the clip; when the flags have 4, the clip ended: with a count, the
  count drops and at 0 the object's +0x4c is cleared. When the object's
  +0x4c is clear, the behavior ends. A count of 0 plays for ever.

The animation instance of a creature is `*(*(creature + 0x98) + 0x4c)`
(the slot +0x10 of its member at +0x90, `FUN_005dcab0`). `FUN_005dc790`
makes the member with the creature's model and its animation set (the
fourth argument of `FUN_005c9340`, the sixth of `FUN_0070d0c0`), and asks
the set for the instance (its slot +8); with no set there is none. The set
is the one the factory's slot +0x1c (`FUN_00728fd0`) passes to its slot
+0x20 (`FUN_007289d0` `param_10`, `0x728c7c`) when there is one, else the
factory's (+0x38, `FUN_00727b70`). `FUN_0077a0b0` makes the factory's
(`FUN_0060f820`) from the animations its template names when at least one
of them is found (`FUN_0060f010`, which logs `Unable to find animation
%s`) and a flag of the parse is clear; the template lines are `anim`,
`aniscript`, `forceDummyAnims` and `noDummyAnims` (`FUN_00785c8d`).
Both ask the set cache (`FUN_0060f820` on `DAT_00a6ad44`) by a name:

- `FUN_0077a0b0` asks for the factory's set by the name of the entity type
  (`leverf`, `kainactor`, `psntcnf`), with the reels its `anim` lines
  found.
- `FUN_00728fd0` asks only for a factory with no set of its own (+0x38 is
  0), when the clips of the creature's marker (the list `FUN_00736a40`
  passes, below) and the factory's list at +0x1c (count) and +0x24 (array)
  give at least one clip. The name is the entity's name, `:` (`0x8f02d4`)
  and the region's name (`gate01:slums/sl01`, `birds02:slums/sl01`).

The cache gives the set of the `animatio` entry of that name: the loader of
the entries (`FUN_0066a850`, slot of the resource table at `0x8723cc`) makes
each one's set with `FUN_005e9ae0` when the container loads. A graph
(version 0) gives a set of vtable `0x86ec34`, whose instance is an
`AnimInst`; an alias list (version 1) gives one of vtable `0x86edf8`, whose
instance is an `AnimLiteInst`, with the reel of each reference
(`FUN_005e9840`): with a package (a region name), the first clip of the
marker of that name in that region's cell group (a marker's clip carries
the marker's name, below); with none, the reel of that name
([`FORMATS/animatio.md`](FORMATS/animatio.md), "Container"). Only a name
with no entry makes the cache build a set (`FUN_0060fa10`: an alias list of
the clips when there is no `aniscript` name, else a graph compiled from the
`aniscript` source, which the game data does not ship).

Traced (`re/decomp/animinst/animinst.gdb` and `build.gdb` on
`tools/oracle/routes/sl01_watch.txt`): every request of `sl01` and of
`kain.big` is found, `FUN_0060fa10` never runs and 31 entry sets are loaded
by then (7 of `kain.big`, the 24 `animatio` entries of `sl01.big`). The
rule, applied to the data (an `anim` line in the template asks for the type's
set; else a marker with a clip asks for `<entity>:<region>`), gives the
class read live for every entity of `sl01` (101 seen live of 104), `sl02`
(79) and `sl03` (57).

The marker of an entity (`FUN_006836d0` makes one record per `cellmark`
entry with `FUN_0068b9e0`, vtable `0x876184`) keeps the `cellmdls` block of
its model index at +0x54, the `cellanim` clip of its clip index in a list
(+0x58 count, +0x60 array), renamed to the marker's name (reel +8) and given
the symbol at +8 of the cell group's +4 (reel +10; `FUN_006074e0` at
`0x68374c`, `FUN_0063d150` at `0x683758`), its name at +0x64, its class at +0x68 and the low byte of its
flag at +0x6c. `FUN_00736a40` passes the model block when +0x6c is set and
the clip list when it holds a clip. Over every `cellmark` of the game (9,455
markers) the flag is 1 exactly when the model index is 0 or more.

Read live in `sl01` (`re/decomp/creature/anim_probe.py`): 90 creatures of
the active list have one, of two classes. Vtable `0x86ec4c` (`AnimInst`):
the creatures whose template names animations (the peasants, the rats, the
umahs, the Kain actors, the gates, the doors, `leverf` with its `anim`
line) and Kain. Vtable `0x872164` (`AnimLiteInst`): the markers with a clip
(the birds, the boats, the spotlight, the cameras with a clip) and the
animated parts of the HUD, all from sets of vtable `0x86edf8`
(`re/decomp/creature/lite_probe.py`). The rest have none (the particle
markers, the detectors, the hints, the helpers, `ocean`, whose marker has
no clip). `gate01`'s template (`animgate`) has an `aniscript` and no
`anim` line, so its factory has no set and the marker's request finds the
graph `gate01:slums/sl01` (states `st_open` and `st_close`).

### Animation instances

The slots of an animation instance that the behaviors use: +4 finds a name
and gives its index, +0x10 plays a name, +0x14 starts an index, +0x24
advances. Play is the same function in both classes (`FUN_005e3c80`,
`FUN_00654250`): find; when the index is below 0 the log gets `Animation %s
requested but could...` (`0x902a04`); then +0x14 with the index, whose
result is left in `eax` for the caller.

`AnimInst` (vtable `0x86ec4c`) runs the state machines of an `animatio`
graph ([`FORMATS/animatio.md`](FORMATS/animatio.md), "State machine
runtime"):

- The set's slot +8 (`FUN_005e3430`) makes it (`FUN_005e34f0`, 0x14c
  bytes) and starts on layer 0 the state at +0 of the graph (the first int
  of the stream: 0 in all 2,156 graphs of the game), so a creature plays its
  graph's state 0 from the start (`st_idle` for `kainactor`, `st_up` for
  `leverf`, `st_open` for `gate01`).
- Find (`FUN_005e37b0`): the state of that symbol in the graph's map
  (section 13; `*(*(instance + 0x1c) + 8) + 0x9c`, a tree keyed by the
  symbol), −2 when it has none. A graph maps `end` to −1.
- Start (`FUN_005e3bf0`): an index of −1 on layer 0 logs `Animation
  StateMachine 0 not allowed to end` (`0x9029d8`) and gives 0; otherwise
  the layer's machine (made when missing, `FUN_005e3aa0`) starts it with
  `FUN_00642bc0`: an index from −1 to the state count less one starts that
  state (−1: none) with `FUN_00642c00`, which gives 1; any other gives 0.
- Advance (`FUN_005e3ea0`): every one of the 16 layers with a machine
  advances from the same time (`FUN_00643030`; its flags and time left are
  in [`FORMATS/animatio.md`](FORMATS/animatio.md), "Result and time left");
  the flags of layer `i` go to bits `3 * i` and up, and the time left is
  the smallest the layers leave. The flag 4 of layer 0 is the end of its
  sequence in the requested state.

Traced (`re/decomp/animinst/animinst.gdb`): `leverf002` plays `up` with a
count of 1 (`st_up` plays `cl_up`: `in` and `out` 0, rate 0): at every
update the flags are 0, the clock 0 and the time left 0, so its play never
ends. The `kainactor` creatures hibernate and their plays get no update.

`AnimLiteInst` (vtable `0x872164`, 0x114 bytes) plays one reel at a time,
with no graph. The set (vtable `0x86edf8`: +8 count, +0x10 array of reels)
is at +0x24, the reel in play at +0x1c and its clock at +0x20:

- The set's slot +8 (`FUN_00653b60`) makes it (`FUN_00653d40`) and starts
  index 0 at once, so a marker plays its first clip from clock 0.
- Find (`0x653f30`, `FUN_00653c00` on the set): the first reel whose name
  (the symbol at reel +8) is the name (`FUN_0068ec30`); else, when the set
  has one reel, `default` gives 0 if the short at reel +10 is not 0 (it is
  for a marker's clip, above; `FUN_0063c480` leaves it 0 in a reel it
  compiles); else −1. The names are symbols, lower case.
- Start (`FUN_00654220`): an index below the count (a signed test,
  `0x65422a`, so −1 passes and reads the word before the array; the
  natives above look the name up first) sets +0x1c to that reel and +0x20
  to 0 and gives 1; an index at or past the count gives 0.
- Advance (`FUN_006545b0`): with `d` the reel's duration (its slot +8,
  below), a clock at `d` or more starts over (the clock drops by `d`);
  the time is added to the clock; at `d` or less the time left is 0 and
  the result 0; past `d` the clock stays at `d`, the time left is what
  went past and the result is 7 (the flags 1, 2 and 4).

The duration of a reel (vtable `0x86fa4c`, slot +8, `FUN_0063cdb0`) is
the largest duration of its channels that are not constants, and at least
1/30 s (`0x3d08882f`) when the reel has a channel; 0 when it has none. A
channel's duration is `(frames - 1) / 30` in every codec
([`FORMATS/animreel.md`](FORMATS/animreel.md), "Duration"). Read live in
`sl01`: the birds (241 frames, 8 s) and the boats (101 frames, 3.333 s)
advance 1 s of clock per second of play; the health bar of the HUD (28
frames) holds at 0.9; the cameras stay at 0.

Traced (`re/decomp/creature/behaviors.gdb` on
`tools/oracle/routes/sl01_watch.txt`): `glyphsmoke2marker` has no animation
instance, so its `playanimation("default", 0)` gives no task;
`kainactor001` has one and gets a play of `"idle"` with count 1, but the
`kainactor` creatures hibernate and the behavior gets no update; the
boats, the birds and the spotlight play their clips with count 0 (flags 7
at each end, the time left carried into the next play); `leverf002` plays
with count 1 on steps of 0.01 s; the wait of 1 s of `umahvf005` (a monster)
counts down by the time the scheduler gives it, 0.1 s and then 0.01 s about
every 3 s.

## The natives of monsterscript and detectorscript

The binder of `monsterscript` is the slot +0xc of the vtable `0x866b38` of
`SmonsterscriptFactory` (`FUN_004d3c50`); the one of `detectorscript` the
slot +0xc of `0x866a78` (`FUN_004d1b60`). The calls of `sl01` (above):

| Class | Body | Method | Native | Calls in `sl01` | What it does |
|---|---:|---|---|---:|---|
| `monsterscript` | 1 | `gotowaypoint` (task) | `FUN_004d4170` | 107 | `FUN_004e5d50`: a command to the monster's AI (`Smonsterscript` slot +0x28 with `FUN_0046e3d0`) |
| `monsterscript` | 5 | `playanimation` (task) | `FUN_004d44a0` | 545 | `FUN_004e59b0`: a command to the AI (slot +0x28 with `FUN_00463060`); with an empty animation name it returns 0 and sends nothing |
| `monsterscript` | 18 | `enableai(bool)` | `FUN_004d4ba0` | 3 | `FUN_004e4ad0`: when the value changes (+0xc0), starts or ends the `toplevelai` behavior of the creature (`FUN_004e8650`) |
| `monsterscript` | 50 | `wait` (task) | `FUN_004d5350` | 59 | `FUN_004e5cf0`: a command to the AI (slot +0x28 with `FUN_0073dd20`) |
| `monsterscript` | 58 | `start` (task) | `FUN_004d5600` | 9 | `FUN_004e5d10`: `FUN_004e4900(FUN_00481720)` |
| `detectorscript` | 0 to 4 | | `FUN_004d1c10` to `FUN_004d1e10` | 0 | Nothing: each calls an empty function (`FUN_004e1820` to `FUN_004e1860`) |
| `detectorscript` | 5 | `removeallrangethresholds()` | `0x4d1eb0` | 6 | `FUN_004e1890`: when the object has a sensor (+0xa0, below), empties its set of range thresholds (`FUN_0049a9f0`: the tree at +0x70, its count at +0x74) |
| `detectorscript` | 6 | `addrangethreshold(float)` | `FUN_004d1ec0` | 12 | `FUN_004e1870`: when the object has a sensor, adds the value to the set unless it is there (`FUN_0049aa50`, an ordered set of floats) |

The monster natives need the AI of the monsters (the `toplevelai` behavior,
its commands and when a command ends). The two detector natives need the
sensor of the object.

### The sensor of a detectorscript

`FUN_004e13c0` (slot +0x24 of the `Sdetectorscript` vtable `0x865b64`)
gives the object a sensor (+0xa0) when its class has a script body (not a
native one, `FUN_004e1a10`) for one of the four events, kept as flags:
`onplayerinrange` (+0xa4, slot 22), `oncreatureinrange` (+0xa5, slot 23),
`oncreatureinsight` (+0xa6, slot 24) and `oncreatureleftsight` (+0xa7,
slot 25). The sensor (0x80 bytes, `FUN_0049b210`) gets the creature, the
16 floats at +0x60 (512, 45, 30, 30, 96, 90, 45, 45, 64, 45, 30, 30, 640,
0.2, 0 by default; the creature's slot +0x6c data replaces them when its
first word is 1), a range of 1200, a mask of the kinds it senses (1 when
the class has only `onplayerinrange`, else `DAT_0091e2fc`), what it looks
for (1 for the range events, 2 for the sight events) and 0.25. Its range
thresholds start as 128, 256 and 512; natives 5 and 6 replace them.

Each update of the object (message `DAT_00910640`, slot +0x50,
`FUN_004e1aa0`) runs the sensor with the step (`FUN_0049bc90`) and adds the
step at +0x5c; past 0.25 s (`0x83f760`) it sends the events (slot +0x7c,
`FUN_004e18a0`) and starts again from 0. Each event calls the body of its
slot (`FUN_00580810`, `FUN_00580a40`) with two arguments: the sensed
creature's name (the address of its +0xb0, a string holder) and the value at
+0x28 of its entry (the distance): the entries of the list +0x40 get
`oncreatureinsight`, those of +0x50 `oncreatureleftsight`, and those of +0x60
`oncreatureinrange` and, when the entry's kind is 0 (the player; +0x28 of
the node), `onplayerinrange` as well.

The sensor (`FUN_0049bc90`) works in steps of 0.1 s (+0xc). When its scan
timer (+0x28) is out it scans (`FUN_0049b5f0`) and draws the next scan time
from the C library `rand`: `1 × (1 + (rand() % 100 × 0.02 − 1) × 0.5)`. The
scan walks the creatures that can be sensed (`DAT_0091dc90`, each with a
kind; `FUN_0049a910` adds them: Kain with kind 0 from `FUN_0042af90`, and
two other callers, `FUN_004da655` and `FUN_004e9a7b`). It keeps those of the same package as the sensor's
creature, or with no package, that are not that creature, closer than the
range (squared distance below 1200²) and whose kind is in the mask. A new
one gets an entry when it answers the state query its message handler gets
(slot +0x54, message kind `0x1b`), and a timer drawn the same way from
0.25. Each step, an entry whose timer is out (`FUN_0049ad00`) asks the state
again, keeps the distance (`FUN_0047dab0`: `sqrt((x² + y²) + z²)` of the
creature's position less the sensor creature's), draws its timer again,
and counts the thresholds not above that distance before (from the distance
it kept) and now (from `sqrt((z² + y²) + x²)` of the same difference,
computed inline at `0x49b076`); when that count or the state changed, the
entry goes to the list +0x60 (an in-range event). The scan's range test
compares `(x² + y²) + z²` (`FUN_004082e0`) with the range squared. An entry the scan no longer finds is dropped. Every scan,
every entry and every new entry draws `rand`, so the sensors take part in
the sequence the scheduler draws from.

Slot +0x24 runs when the object starts: `FUN_00762fe0`, the virtual +8 of
a creature's object, calls it and then `FUN_00580730`, so the sensor
exists before the script's `start` runs. The update of a creature reaches
the sensor last: `FUN_0070ee70` first runs the object once
(`FUN_0070e8b0`, which returns 0 for the update), then hands the message
to the creature's behavior (+0x124) and then to the object (slot +0x1c,
`FUN_004e1de0`, which goes to `FUN_004e1d50` and slot +0x50). The events'
time (+0x5c) starts at `FLT_MAX` (`DAT_008662dc`), so the first update
already sends them.

The state query is a `CreatureStateMsg` (vtable `0x840814`, built by
`FUN_0049bc50`: +4 = `0x1b`, +8 = 0, +0xc = 0.5). Its handler in the
message table (`FUN_004a1a60`) calls the behavior's slot +0xe8. Kain's
(`FUN_00430420`) answers yes with +4 = 0 and +0xc = `FUN_00424a80()`, and
leaves +8 at 0; the entry keeps +4 at +0x18, +0xc at +0x1c and +8 at
+0x20, and resets them to `0x1b`, 0, 0 before each query. So for Kain the
state never changes: after the first entry only the count of thresholds
raises an in-range event (+0xc counts only for the sight events, against
0.75, `0x840af0`). In the scan every new candidate draws `rand` once
(`0x49b95c`) whether it answers or not; one that answers gets its entry,
with that draw as its first timer, and the entry goes straight to the
in-range list. The entries of a sensor are keyed by the name of their
creature (`FUN_0049c4b0`).

The in-range events (`0x4e1980`): `oncreatureinrange(string, float)` with
the address of the creature's name (+0xb0, a string holder) and the
distance, then, for an entry of kind 0, `onplayerinrange(creature, float)`
with the address of the entry, whose first word is the creature (a
`creature` value), and the distance.

Traced in the oracle (`re/decomp/detector/sensor.gdb` on
`tools/oracle/routes/sl01_watch.txt`: 45 s at the start of `sl01`, then
12 s walking):

- The six `hint_proximity` objects of `sl01` get their sensor when they
  start (frame 3886, return address `0x762fea`), and set the thresholds
  320 and 340, 320 and 330, 256 and 300, 230 and 240, 256 and 260, 192 and
  200 (001 to 006).
- `hint_proximity001` of `sl01` is not in the creature list read later
  (`drawn_probe.py`; the one of that name there is `sl02`'s): something
  removes it after it starts. 002 to 005 are farther than 1200 from Kain.
- `hint_proximity006` (842 from Kain) finds Kain in its first scan (frame
  3890: answered, state 0, +8 0, +0xc 0.5) and sends
  `oncreatureinrange("kain", 841.906128)` and
  `onplayerinrange(kain, 841.906128)` in the same update. Then it queries
  Kain 217 times with the same answer and sends nothing more: both its
  thresholds stay below the distance.
- Kain has a sensor too (range 896, mask `0xff`, flags 3), and so has every
  peasant, rat and umah of the level (range 800, flags 2); they are not
  made by `FUN_004e13c0`. The trace has 12,059 draws of the sensors, every
  one on the shared generator (the value is `(state >> 16) & 0x7fff` of
  the state after it), and two draws of the sensors in one frame are always
  one step of the generator apart. The sensors draw far more often than the
  scheduler, so the port needs them to draw the same sequence.

## The update of the objects of a level

Every frame the game tick (`FUN_0072edba`) does, in this order:

1. `FUN_0073e840` gives each creature of the creature list (`DAT_00a836a8`)
   its time for the frame (+0xfc) and marks the creatures that sleep (bit
   1 of +0xf0); "The simulation scheduler" below has its two modes.
2. The tasks of the script manager run with the frame time times the first
   time scale (`FUN_00583400`; "Timers").
3. `FUN_00718490` processes the creatures of the group `process_msg`
   (`DAT_00a83754`, `FUN_0072b120`), in the order they joined it: the
   constructor of `Creature` adds each new creature at the end
   (`FUN_0072b4d0`, 0x70d82e) unless `DAT_00a836d6` is set. Every 60 frames
   `FUN_00718340` takes out of the group the creatures with bit 0 of +0xf2
   whose object has no main thread.
4. For each creature that does not sleep and whose byte +0x160 is 0, the
   update message (vtable `0x8420e0`, type `DAT_00910640`) carries its time
   times its time scale: `+0xfc * DAT_00910b10[+0xf8]`, then +0xfc and the
   scale. The ten scales are 1.0, and `FUN_00718220` sets all of them, 0 to
   pause the game. The creature gets the message only when that time is
   above 0, and `FUN_0070e8b0` runs its object once with the time as the
   argument of the run.

A creature gets its script object when it is built (`FUN_0070e560`): with
a script name, the class of that name (the class map is keyed by the id of
the name, and names compare with case, `FUN_00694fc0`), when the class
derives from `basecreaturescript` (`FUN_00580a30`). The object goes to
+0x130, the creature to the object's +0x2c, and +0x138 is set; otherwise
+0x138 stays clear and the creature has no object. In `sl01` the 104
entities of the region are 104 creatures, and 78 of them get an object.
Every entity of the three regions of `slums` but six is a creature in the
creature list read live; the six are cinematic actors and a monster the
scripts had removed by then.

Hibernating, waking and removing a creature (`FUN_00718cf0`,
`FUN_00718d30`, `FUN_00718b80`) only queue it. At the end of
`FUN_00718490` the removed creatures go (their object stops,
`FUN_0070e6e0`), then the hibernating ones move from the creature list to
the list `DAT_00a83680` with +0x160 set (`FUN_00718970`), then the woken ones
move back to the end of the creature list (`FUN_00718a60`). So a creature
removed or sent to sleep during a frame still gets that frame's update, and
the search by name finds a hibernating creature after the active ones.

Read live in `sl01`, without breakpoints (`update_probe.py`, ten samples two
seconds apart): 393 creatures, all ten scales 1.0. In each sample about 110
creatures have a time near 0.01 s, about 260 have 0, and 5 to 29 have 0.1.

### The simulation scheduler

`FUN_0073e840` takes its settings from the tune data of the level keeper
(the `tunedata` entry `levelkeeper` of `code.big`; its header,
`levelkeepertune.h`, names the fields of `CommonLevelKeeperTune`):
+0x08 `creatureCullDist` (0), +0x10 `periodicSimDist` (1200), +0x14
`periodicSimRate` (10) and +0x34 `screenCullData` (1, then the floats 14
and 10). `FUN_0072c300` finds that data through the template named
`levelkeeper`; with none, the step is 1/60 s. The radius is
`creatureCullDist` (`DAT_00a8377c`, set by `FUN_0072c910`). A radius of 0
or less means no limit.

Each creature has two squared distances (+0x104, +0x108), set when it is
built and when the level keeper starts (`FUN_0073e0c0`):

- `screenCullData` is 1 (`FUN_0073e1a0`). The creature keeps a radius R
  at +0x10c, from the bounding radius of its model ("The model radius"
  below; 24 when it gives none), a size the caller passes (1 from the
  constructor, `0x70d7ba`, and from `FUN_0072c910`, `0x72d285`) and what
  +0x10c held: the largest of them, and at least 24. A creature with no visual
  part (+0x58 is not the visual list, `DAT_00a83da8` +4) gets
  `3.4e38` (`FLT_MAX`) for both distances. Otherwise the near distance is
  `20 / 14 × R × 40`, at least 100, and the far one `20 / 10 × R × 40`, at
  least 200. With R = 24 that gives 1371.4 and 1920, whose squares are the
  1,880,816 and 3,686,400 read live in `sl01`.
- Otherwise (`FUN_0073e0f0`), for a visual creature, the near distance is
  the bounding radius of its model × 40 (1,000,000 without a model), at
  least `periodicSimDist`; a creature with no visual part gets 1,000,000.

Each frame, for each creature of the list:

- A creature with bit 2 of +0xf0 (`disablesimcull`) gets the whole frame
  time, and bit 1 is cleared.
- In the first mode (`screenCullData` is not 1), a creature whose squared
  distance to the nearest viewer (`FUN_0073e330`: the positions, +0xb4, of
  the holders of the list `FUN_007544d0`) is beyond the squared radius
  sleeps: bit 1 set, no time. Otherwise its time adds up at +0x100. The
  creature takes it all when it was drawn in the last frame (+0x140 is not
  below the frame counter less 1) and it is not farther than its near
  distance (+0x104). If not, it gets time only in steps of
  1 / `periodicSimRate`: the step itself while the sum is below two steps,
  else the sum less one step, and the rest stays.
- In the second mode, the distance is measured again only one frame in six
  (`DAT_00a83c94`) and kept at +0x114. Within the far distance (+0x108) the
  rule is the same as above. Beyond it the creature waits on a timer
  (+0x110): while the timer runs, the creature sleeps with no time (with
  flag 0x800 its time keeps adding up at +0x100). When the timer runs out it
  restarts at `3 × (1 + 0.01 × (rand() % 40 − 20))` seconds. The creature
  then gets one update, of 0.01 s, or of the time it saved with flag 0x800.
- Once in 100 frames (`rand() % 100 == 0`, drawn once per frame) every step
  of that frame is scaled by `1 + 0.01 × (rand() % 10)`.

Each operation rounds to a float: the x87 unit runs in single precision
(docs/PORTING_GUIDE.md, "Floating point"). `rand` is the C library one
(docs/GAMEPLAY.md, "Random numbers"). In
`sl01` the second mode runs: +0x110 holds timers between 0.7 and 3.0 s,
and 0.1 s, the step of a rate of 10, is the time most of the far creatures
get.

### The model radius

The slot +0x68 of `Creature` (`0x70d8e0`) returns +0x10, the creature's
model object, and that object's virtual +0x3c gives a centre and a radius:

- A `goe::RigidModel` (vtable `0x86fadc`, `0x63ecd0`) asks its model data
  (+0x14; slot +0x40, `0x570f20`). A `goe::AnimModel` (vtable `0x86fb3c`,
  `0x63f3e0`) asks the data of its first part (`**(+0x2c)`, slot +0x20,
  `0x570ce0`) when +0x24 is set. Both copy a sphere the data keeps
  (`FUN_0056bfb0`). With no data the call fails and `FUN_0073e1a0` takes 24.
- The data computes the sphere once, when it is built (`FUN_0056d130`),
  from the positions of the model (stream 0, the `position` chunk of its
  `rmd`, stored by `FUN_0061ed40`), with `FUN_00693fc0`: the centre is the
  middle of the box of the points (per axis `(max + min) × 0.5`), the
  radius the square root of the largest squared distance from the centre,
  added as `(dz² + dy²) + dx²` (`0x694114`; each operation rounded to a
  float, and a later point replaces the largest only when it is strictly
  larger). The box starts
  at `FLT_MAX` (`0x876244`) and `−FLT_MAX` (`DAT_00a74bf8`, set by
  `FUN_006944c0`). A model with stream 0x1e gets that stream's sphere merged
  in; only `FUN_005f4b50` stores stream 0x1e, on data the `collmodc`
  loader and two other paths build, not on the model of an `rmd`.

Which model a creature of a region gets. `FUN_00736a40` builds each entity
through the factory of its type (slot +0x1c, `FUN_00728fd0`, then
`FUN_007289d0`):

- When the entity's property `reserved.marker` is 1 (`FUN_0078b670`:
  `FUN_00734680`, `FUN_00782620`), it looks up the marker of the entity's
  name (`FUN_00739bc0` on the map at +0x38, which `FUN_0078c320` fills
  with the name at marker +0x64). The names compare byte by byte
  (`FUN_0068ec30`), and the marker named `Ocean` in the `cellmark` of
  `sl01` was found for the entity `ocean` (traced), so a marker's name
  reaches the map in lower case: the name table lower-cases a name it
  registers (`FUN_005a0f20` calls `FUN_0068f430`, which maps each byte with
  the C library `tolower`, `FUN_007a4456`). Every entity name of the game
  (15,584) is lower case. The marker's model
  object (marker data +0x54, the `cellmdls` block of its model index, when
  the marker's flag +0x6c is set) goes to the factory, which uses it
  instead of its own (`FUN_007289d0`, `param_9`); its clips (+0x58) go
  with it ("Creature behaviors"). A block with no `rmd` gives no radius. A
  marker whose model index is −1 (its flag is then 0), and an entity with
  no marker, pass no model.
- Otherwise the factory's own model (+0x34, `FUN_00727b70`): the first
  `model` line of the type's template (`FUN_0077a0b0`, `FUN_0077b5e0(0)`;
  the next lines are levels of detail), found by name among the loaded
  resources (`FUN_005eed10`, `FUN_006aaa30`). The name is made canonical
  first (`FUN_0069a380`): `FUN_00699d90` turns `\` into `/`, lower-cases,
  drops a drive `X:`, a known root and the extension after the last `.`,
  and `FUN_0069a170` keeps only the part before the first `/` when the
  rest equals it (`sfxobj\sfxobj` is the entry `sfxobj`). The model
  `levelkeeper` is in `code.big`.

Traced in the oracle (`radius.gdb` and `factory_model.gdb` on the load of
`sl01` with its neighbours; `radius_check.py` applies this rule to the data):
the model radius of all 104 creatures of the `sl01` region (to the six
decimals the trace prints) and all 82 of `sl02` is the one the rule gives. 16 of `sl01` take the marker's model
(`ocean` 3886.923096, `spotlight` 371.669556, `gate01` 149.244507,
`boat_01` 109.499756, `birds02` 67.882240; the four detectors,
`levelloader01` and the two steam markers have blocks with no `rmd`) and 88
their template's (`psntcnf` 40.428162, `0x4221b670`; `umahvf` 40.246319, `psntanm`
38.104610, `checkpoint` 37.416573, `sfxobj` 27.712812, `levelkeeper` and
`regionkeeper` 24.494898, `camera/exportedcamera` 10.375216).

### The drawn frame

A creature's +0x140 is the frame counter (`DAT_008ff180`) of the last frame
it was drawn in. The draw of a creature (`FUN_0070f100`, slot +0x14 of
`Creature`) returns at once for a creature with +0x139 set, with flag 0x200
(the template flag `helperobj`) or whose model has no data; otherwise it
draws the model (`FUN_005c9b50`) and sets +0x140 when that draw says it drew.
That draw is called only for the objects of the cells that are visible
and for the objects in no cell, below ("Visible cells").

Read live (`drawn_probe.py`, ten samples one second apart, standing at the
start of `sl01` with its neighbours loaded): 11 creatures are drawn each
frame (Kain, `ocean`, two glass models and seven parts of the HUD). Of the
230 creatures with a script object only `ocean` is drawn; the other 229 have
+0x140 at 0, never drawn since the load.

### Visible cells

The cells of a creature. A creature is its own visual object, and its cell
proxy sits at creature +0x5c: flags +0x60, the world matrix +0x68, the
centre of its model sphere in model space +0x6c, the radius +0x78, its list
of cells +0x80, the scene of the list of objects in no cell +0x84. When it
moves (`FUN_00711100`, `FUN_005c9cb0`) the model's slot +0x3c gives the
sphere; the centre goes to world space with the matrix (`FUN_00693c50`) and
`FUN_00607b60` collects the cells it touches in every loaded cell group of
its scene (the group's +0x88): the box centre ± radius must overlap the
group's box (`FUN_00660be0`: both sides strict, an empty box never
overlaps); with a `cellbsp` (`FUN_0066b1e0`), a sphere query from the root:
at a split (vtable `0x876138`, `FUN_0068b760`) with `d = n · centre`, the
first child when `distance < radius + d`, the second when
`d − radius < distance`; a leaf (`0x876150`, `FUN_0068b5c0`) other than
`0xFFFF` marks its cell. With no `cellbsp`, every cell whose box overlaps.
`FUN_005fbbb0` puts the proxy in those cells' lists (cell +0x18) and takes
it out of the others; with none, or with a model that gives no sphere,
the proxy goes to the scene's list of objects in no cell (`FUN_005fbe50`).
Read live in `sl01` with `sl02` loaded (`re/decomp/visible/cells_probe.py`,
`membership_spike.py`): the rule gives the cells of all 313 proxies of the
world scene.

The cells of a frame (`FUN_007f69e0`):

1. The camera cells: `FUN_00607b60` at the camera position (the camera's
   +0x30..+0x38) with a radius of 1.0 (`0x88211c`).
2. Each camera cell is visited (`FUN_007f5d60`) with the whole screen as
   its region. A visit adds a record for the cell (or merges the region
   into the record it already has), then tries each portal of the cell
   that is open (+0x3f, 1 when the portal is made, `FUN_00661750`;
   `FUN_007624e0` and `FUN_007625e0` write it and log `... for
   enableportal`), leads to a cell not being visited (+0x14) and whose
   mask (+0x40) shares a bit with the visit's: the portal's region
   (`FUN_00662000`: empty when `n · (v0 − camera) ≥ 0`, with `n` the
   portal's normal (+0x20) and `v0` its first vertex; otherwise
   its vertices times the view projection as row vectors, `FUN_006af080`,
   clipped by `x ≤ w`, `−x ≤ w`, `−y ≤ w`, `y ≤ w`, `z ≤ w`, `−z ≤ w` in that
   order, `FUN_00661240`, `FUN_00661060`, each point at
   `(x / w · 0.5 + 0.5, y / w · 0.5 + 0.5)`) intersected with the visit's
   region; when that is not empty the portal's target is visited with it
   (`FUN_007f6210`).
3. Each record draws its cell (`FUN_007f6340`, `FUN_007f5a20`,
   `FUN_00605be0`): the renderer's view becomes the record's rectangle, the
   bounding box of its region (`FUN_006615d0`, slot 0x98, `FUN_00566cf0`
   builds the six planes of that part of the frustum), and the proxies of
   the cell's list are drawn when `(flags & 3) == 0` and either flag 4 is
   set or the sphere passes `FUN_00567dd0`: with the centre in world space,
   `n_i · p + d_i − radius` is negative for all six planes.
4. Then the whole screen again, and the scene's list of objects in no cell
   with the same test (`FUN_0065f190`).

Traced (`re/decomp/visible/frame.gdb`, `walk.gdb`): at frame 6000 of
`tools/oracle/routes/sl01_watch.txt` the camera is in `cell01` only, the
portal to `cell02` is outside the view, and the proxies that pass are
exactly the ones the rule gives (`frustum_spike.py`); along
`tools/oracle/routes/sl01_camera.txt`, in the 84 frames that see `cell02`,
its rectangle is the bounding box of the portal's region clipped by the
screen (`portal_spike.py`, to 1e-4).

## Timers

A timer counts down a time and calls an object back when it runs out. It
is a counted block of 0x14 bytes (`FUN_0079cac0`, vtable `0x87a020`): +4
its count, +8 the time left (a float; 0 when it is stopped), +0xc its task
and +0x10 the handle of the object it calls back, counted. Its last release
(`FUN_0079cae0`, `FUN_0079cb10`) parts the timer from its task and releases
the object.

The task of a timer (`FUN_0079c970`, vtable `0x87a00c`, 0xc bytes) holds
the timer (+4) and a restart time (+8, 0 when it does not repeat; set with
the time by `FUN_0079ca40`). Its run (`FUN_0079ca70`) takes the time of
the frame from the timer (`FUN_0079cbb0`): when the time left goes below 0
it becomes 0 and the object of the timer gets the call of the body in slot
15 of its current state (`FUN_00766300`, `FUN_00580810`) with the timer as
its argument. Slot 15 is `ontimer(timer)` in `creaturescript` and
`onglyphdeactivate()` in `monsterscript`. Then, when the timer has no time
left, it starts again with the restart time or stops (`FUN_0079cb90`: no
time left, and the timer and the task part). The task runs while its timer
has time left (`FUN_0079c9e0`); when it ends it parts from its timer
(`FUN_0079ca20`).

The call from outside the scripts (`FUN_00580a40`) runs a native body at
once; a script body gets a thread of its own, owned by the object that runs
at that moment (the top of `DAT_00a6a588`, 0 for none), which runs once with
0 and then ends (`FUN_0057d810`, `FUN_0057dcf0`, `FUN_0057dbe0`).

The script manager keeps a set of tasks at +0xa4, in the order of their
addresses: `FUN_00583200` adds one, and `FUN_00583400`, called by the game
tick (`FUN_0072edba`) with the time of the frame before the creatures are
processed, runs each task once (virtual +8), asks it whether it runs on
(+4) and destroys those that do not (virtual 0, `FUN_0079c9d0` for the task
of a timer).

The natives of `timer` (bound by `FUN_0076c220`; for each, the object is the
slot that holds the timer, and nothing happens without a timer):

| Body | Method | Native | What it does |
|---:|---|---|---|
| 0 | `setcallback(basescript)` | `FUN_0076c2b0`, `FUN_0079ccd0` | With an object, the timer keeps its handle, counted (`FUN_0079ccf0`) |
| 1 | `starttimer(float, bool)` | `FUN_0076c2d0`, `FUN_0079cd80` | A timer with no task gets one with the time and, when the boolean is set, the time as restart; the script manager runs it (`FUN_00583200`). A timer with a task sets it again |
| 2 | `stop()` | `FUN_0076c2f0`, `FUN_0079ce30` | `FUN_0079cb90` |
| 3 | `isrunning()` | `FUN_0076c300`, `FUN_0079ce40` | Whether the timer has time left, in the low byte of the result |
| 4 | `getid()` | `FUN_0076c320`, `FUN_0079cf30` | The address of the timer |
| 5 | `play(float, bool)`, a task | `FUN_0076c340`, `FUN_0079ce70` | As `starttimer`, but the task is not added to the script manager: the frame runs it as a child and waits for it, each run with the time of the run. A timer that has a task already gives that same task, which then has two owners; the port refuses that case |

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
this page. The objects of `sl01` activate and run in `ScriptLevel` with the
natives the port has; the others still do nothing there, and the game does
not drive the level yet.

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
| The natives of the creature scripts: the five that find creatures (`creaturebyname`, `creatureself`, `getkain`, `creaturescriptbyname`, `gethud`) and those that read or change a creature (`creaturevalid`, `creatureequal`, `wasdrawnlastframe`, the simulation flags) | `src/engine/script_creature_natives.cpp` | `script` (`test_creature_natives`, `test_creature_flag_natives`; the bodies they bind to in `test_create_sl01_objects`) |
| The natives of `basescript` that need no other system (values, state, angles, random, distance, log) | `src/engine/script_natives.cpp` | `script` (`test_native_self_and_state` to `test_native_log`, `test_native_binding`) |
| The natives of `position`, `rotation`, `color`, `gridposition` and `waypoints` (the value classes) | `src/engine/script_value_natives.cpp` | `script` (`test_position_natives` to `test_bind_value_natives`) |
| The codes only the region packages use (`0x0d`, `0x17`, `0x18`, `0x37`, `0x53`, `0x54`, `0x57`, `0x58`) | `src/engine/script_machine.cpp` | `script` (`test_region_codes`, `test_start_without_waiting`) |
| Timers: the value type, its task, the natives, the call from outside the scripts and the tasks of the script manager | `src/engine/script_timer.cpp`, `src/engine/script_machine.cpp` | `script` (`test_timers`) |
| The creatures of a level and their objects: one creature per entity, the object of each scripted one, activation in build order, the update of each frame, hibernation and removal, the simulation scheduler (the caller gives the model radius of each entity, the visual list, the drawn frames, the viewers and the keeper tune), the hooks of the native part of an object (`ObjectHooks`: when it starts, before its start body, and after each run in an update), and the queue of region requests of `loadregion` (the regions are not loaded yet) | `src/engine/script_level.cpp`, `src/engine/crt_random.cpp` | `script` (`test_script_level`, `test_scheduler`, `test_region_model_radius`, `test_object_hooks`, `test_region_requests`; `test_sl01_level`: the 104 creatures and 78 objects of `sl01` activate and run 120 updates); `engine` (`test_crt_random`) |
| The scripts of a level in the game loop: `bo2nx --replay ... --scripts` and `--play ... --scripts` load them (`load_level_scripts`), reads the level keeper's tune (`read_keeper_tune`), adds Kain as the `kain` creature and, each tick, runs the scheduler with the follow camera as the viewer and then the update | `src/main.cpp`, `src/engine/script_level.cpp`, `src/platform/sdl/sdl_backend.cpp` (`on_tick`) | `script` (`test_level_scripts_sl01`); the state of the gate replays does not change |

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
- Which holders the viewers of the simulation are (`FUN_007544d0`; one in
  `sl01`), and which creatures are in the visual list (`DAT_00a83da8`).
- Where `FUN_00699d90` gets its known root and its two lists of directory
  prefixes (+4, +0x250, +0x25c); no template model name of the slums
  regions needs them.
- The AI of the monsters behind the `monsterscript` natives; the sight
  part of a sensor (`FUN_004a4e50`, `FUN_0071cf40`) and who makes the
  sensors of Kain and of the monsters; the mask `DAT_0091e2fc`; what
  `FUN_00424a80` measures.
- The visible cells: where a portal's mask (+0x40) comes from (1 for the
  traced portal of `cell01`; the visit starts with all bits), the region
  code of a region that is not one portal clipped by the screen (the hull
  `FUN_006b51f0`, the polygon intersection `FUN_006b5850` and its
  epsilon `0x8766d4`, the union of two paths into one cell,
  `FUN_006615a0`), the order of the two portal lists of `FUN_007f5d60`
  (portals with and without vertices; no slums portal has none), and the
  frustum corners of `FUN_00566cf0` against the port's camera.
- What fills the factory's clip list (+0x1c, +0x24), the flag of the
  template parse that stops `FUN_0077a0b0` from asking for a set, and what
  it does when none of the `anim` lines finds its reel.
- What `FUN_00760900` does before the behaviors for an object whose +0x50
  is set (`FUN_007422b0`), and in which order a `detectorscript` object
  runs its sensor and its behaviors.
- What `FUN_006e20d0(0)` does to a scripted sound instance.
- The properties `<class>.<field>` of an entity (for example
  `levelkeeperintro.commondata.farClip`), which look like overrides of the
  defaults of the fields of its object: who reads them is not found yet
  (`FUN_00737a80` is the check of `reserved.script` above, not this).
- The collection type (kind 7) and the type with kind 8: no data uses them.
- The text compiler (`FUN_00793280` imports a package from source); the
  game data has only compiled packages.

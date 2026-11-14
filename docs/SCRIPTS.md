# Scripts

Status: **read, not ported**. The enemies, the doors, the cinematics, the
sounds and the triggers of the game are script classes compiled into the
`bpackage` entries of the containers
([`FORMATS/bpackage.md`](FORMATS/bpackage.md)). This page describes how the
executable runs them, read from its interpreter (`FUN_0057de80`) and the
functions around it. Nothing of this is in the port yet.

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
  no interpreter is running): above 10,000 the frame ends with a warning.

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
| `0x1f` | Pop the address of a string, then pop an address: the string stored there becomes that one (`FUN_0068eaa0`, counted) |
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
| `0x3c`, *`0x3d`* | Float to integer (truncated) and to boolean |
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

## Not read

- How the native bodies get their function (the body +0x10) and the list of
  native functions of each class.
- The functions that copy a value into a local or a field (`FUN_00587fc0`,
  `FUN_00588390`) and the classes of values (kind 9).
- Who creates the script objects of a level and who runs them each frame.
- The virtual +0x18 of a script object (`0x580880`), called after a state
  change.
- The text compiler (`FUN_00793280` imports a package from source).

# Atom Fisher Uranium
> or fishatom

## WARNING!
fishatom is *my view about Atom as Programming Language* please do not touch this branch without *my permission* (i beg you)

# Language Specification (By linuxerfromscratcher-dot)

The complete language alphabet contains 26 basic instructions, each responsible for a particular low-level operation.

### A – M

**A (Allocate):** Allocates a block of dynamic memory in RAM (the heap). Takes the size from the stack and returns the address of the allocated buffer.

**C (Compare):** Compares the two top elements of the stack and pushes the logical result True (`1`) or False (`0`). A bare `C` is equality, the modifiers select the operator:

* C0 / C1 — equal (`==`)
* C2 — not equal (`!=`)
* C3 — greater (`>`)
* C4 — less (`<`)
* C5 — greater or equal (`>=`)
* C6 — less or equal (`<=`)

Numbers, characters, strings and words are compared by value.

**D (Data):** Loads a specific numeric value directly onto the stack (for example, `D33`).

**E (Execute):** Runs a command block that was linked in with `H` (HEADINCLUDE). The name is given as a literal (`E{sumto}`), as a quoted word (`E"sum"`) or as a string on the stack (`"sum"` then `E`). Command blocks are :{name ... :} sections: the block is entered at `E{name}` and leaves through its `Q` (`RET`). A bare `E` on a number is an error, the numeric jump of Atom 1.x is gone (`J` is the unconditional jump).

**F (File / File System):** Works with file streams through modifiers:

* F1 — Open a file (file name address in memory → descriptor)
* F2 — Read a byte or character from a file
* F3 — Write data from a buffer into a file
* F4 — Close a file using its descriptor

**G (Get / GET_PORT):** Reads a hardware port, a HAL input buffer or a system register.

* `G` — bare `G` reads the system register R7, `G<port>` reads a raw hardware port value
* `G1` … `G8` — HAL input devices: `G1` SATA, `G2` COM_UART, `G3` USB, `G4` VGA, `G5` PS/2, `G7` RJ45, `G8` AUDIO
* `G9` … `G9(4)` — legacy RJ ports: `G9` LEGACY_RJ, `G9(1)` RJ9, `G9(2)` RJ11, `G9(3)` RJ14, `G9(4)` RJ25
* The value is pushed onto the stack; `-1` means the device has no data. A device window starts at `0x0700 + device * 64` in RAM: byte 0 is the item count, byte 1 and up are the items, so `D2 M2048` loads two bytes into the PS/2 buffer and `D65 M2049` fills it with `A`.

**H (Headinclude):** Links a standard or third party library into the program at compile time:

* `H{math}` — resolves `math.mh` through the library search path
* `H"lib/math.mh"` — explicit file, `H(lib/math.mh)` and `H'math.mh'` work as well
* The commands of the library become callable with `E{name}`, and are already linked before the first instruction of the program runs
* Search path: the directory of the program, `ATOM_LIB`, the directory of the interpreter, its parent, `<exe dir>/lib`, `<parent>/lib`, `lib`, `.` and `LD_LIBRARY_PATH`
* The HAL itself lives in the library, so it stays editable from the `.mh` side

**I (Input):** Interactive input through buffer modifiers:

* I1 — Read data from a buffer and move it onto the stack
* I2 — Read data from a buffer into RAM at address 2 (address 1 is reserved for service byte packing)
* I3 — Read data directly into hardware register R7

**J (Jump):** Jumps to the specified numeric label (classic `goto`). Labels are global, so every file numbers its own range. The subcommand decides whether the jump happens:

* bare `J5` — unconditional jump to label 5
* `J5(1)` — jump to label 5 if the top of the stack is not zero
* `J5(2)` — jump to label 5 if the top of the stack is zero

**K (Kernel / Syscall):** Calls a micro-OS system function (a kernel interrupt used for process management). A bare `K` is a kernel interrupt, `K10` and up are syscalls, `K1` … `K9` are the C FFI:

* `K1"lib.so"` — open a shared library, pushes the handle (`K1` also searches the library path)
* `K2"atom_sum"` — resolve a symbol in the library of the handle on the stack, pushes its address
* `K3(n)` — call the resolved symbol with `n` arguments; the arguments are the `n` stack values on top, the symbol is below them
* `K4` — close the handle
* `K5` / `K6"text"` / `K7` — allocate, build a C string, free
* `K8` / `K9` — store 32 bits / load 8 bits at a pointer

Two backends are available and reported at startup: `libffi` when it is found by `pkg-config`, otherwise the built-in `sysv64` trampoline for x86-64 System V (up to 6 arguments). Windows uses `LoadLibraryA`/`GetProcAddress`.

**M (Memory):** Full RAM access. Reading is universal, writing is only allowed inside `.mh` libraries. `D7 M16` writes 7 to `memory[16]`, `D16 M` reads it back.

**N (Next):** Increment — increases the value on the top of the stack by exactly 1.

**O (Output):** Universal output — prints a number, character, or an entire text buffer in the `/text/` format to the screen.

**P (Push/Pop / Duplicate):** Stack operations, including duplicating the top element (`DUP`) or removing an unnecessary one.

**Q (Quit):** Terminates the session and forcefully stops program reading/execution.

**R (Register):** Fast operations involving the processor's internal hardware registers.

**S (Setup / Store):** Initializes system environments or registers, or stores the current state in memory.

**T (Transform / Arithmetic):** Universal calculation block:

* T1 — Addition (`+`)
* T2 — Subtraction (`-`)
* T3 — Multiplication (`*`)
* T4 — Division (`/`)

**U (Pack / Unpack):** Operations with packed data. A packed record is 8 bytes in RAM: a `0xF3 0x0A` header, then a 16-bit cluster and a 32-bit size, little endian.

* `U1` — pops the target address, the size and the cluster from the stack, writes the record and pushes the address back
* `U2` — pops an address, reads the record, keeps a copy in an internal shadow buffer so the stored data cannot be lost, and pushes the cluster and the size

**V (Vector):** Configures and redirects hardware interrupt vectors. A bare `V` prints the vector table configuration (`256` handlers), `V<int>` installs a handler address from the stack, and `V<int>"label"` redirects a vector to a code label.

**W (Nope):** Does nothing at all. `W`, `W2`, `W1 W3` are all valid and never block.

**X (XOR / Logic):** Bitwise and logical operations:

* X1 — Bitwise AND
* X2 — Bitwise OR
* X3 — Exclusive XOR

**Y (Yield):** Transfers processor control to another process (voluntarily yields the current scheduler time slice). A bare `Y` yields, `Y1` switches to context 1.

**Z (Zero):** Immediately clears the entire stack at the kernel level or checks whether the top of the stack is zero.

## 3. Linking File Specification (`.mh`)

* **`F/{file}/:` (Virtual File Container):** Creates a file in the system and automatically fills it with initial numeric values that form the individual stack "contents" for this component before execution begins.

* **`S{sector}:` (Sector Mapping):** Maps a block of data or code to a specific disk sector, providing a direct bridge for interaction with the SATA hardware driver through `H1`.

* **`M{address}:` (Adaptive Address Block with Nullification):** Defines a target address. If the system is running in an ultra-low-RAM mode (tiny RAM) and does not have enough address space available, this address is **nullified**, and execution and data flow are automatically shifted to base system cells (`3`, `4`, `5`, and so on).

* **Hexadecimal Bytecode (`0x...`):** Direct injection of raw machine code that is transferred by the linker directly into the final `.atmo` or `.rom` binary files byte-for-byte.

* **Isolated Stack Context:** Each individual section in the mapping file has its own independent set of stack data, providing complete subsystem autonomy.

## 4. How to Work with Them

Programming in Atom is based on the principle of passing data through the stack.

1. **Loading data:** First, the required numbers or variables are placed onto the stack using the `D` command (or read using `I1–I3`).

2. **Processing:** Commands such as `T1–T4` (transformation), `C` (comparison), or `X1–X3` (logic) take this data from the stack, process it, and return the result to the top of the stack.

3. **Storing or outputting:** The resulting value can be stored in memory using the `M` command or printed to the screen using `O`.

## 5. Syntax

The syntax of the **Atom** language is designed to be as simple as possible in order to avoid unnecessary characters.

* **Command format:** An uppercase Latin letter (from A to Z) followed by an optional numeric argument, with no spaces between them (for example: `D42`, `M100`, `W5`).

* **Separators:** Commands are separated from one another using spaces or new lines.

* **Jump labels:** Labels used for jumps are represented by a number at the beginning of a line followed by a colon (for example, `10:`).

* **Comments:** Everything following the `;` character until the end of the line is ignored by the interpreter. C-style comments are also supported: `//` for single-line comments and `/* ... */` for comments that may span multiple lines.

* **Imports:** The `import` keyword on a line loads another `.mh` file and appends its instructions exactly at that position (relative paths are resolved against the importing file's directory). Imports may be nested:
  ```
  import "lib/math.mh"
  import other_lib.mh
  ```
  Imported files are included in the AOT cache validation, so editing an imported file automatically invalidates the cached program.

## 6. Command Blocks

A library file is a sequence of command blocks, each one is a named routine that `E` can call:

```
:// lib/math.mh
::{double}
D2
T3
Q
:
```

Inside a block `Q` is a `RET`: the block ends and control comes back to `E{name}`. Outside of a block `Q` terminates the program and everything after it is deleted from the compiled code. The bundled library `lib/math.mh` provides `greet`, `dbl`, `square`, `cube`, `neg`, `fact`, `sumto` and `countdown` (the last one is an iterative loop that keeps its counter in RAM byte 0).

# Building

* **Default:** `make` compiles `dist/atomc` and links the FFI against `libffi` when `pkg-config libffi` finds it, otherwise it uses the built-in x86-64 System V trampoline.
* **Fortran support:** `FORTAN=1` (the default) additionally builds `libatom_fortran.so` from `src/arithmetics.f90` and loads it as a JIT module at startup, so `T1` … `T4` are executed by Fortran `bind(c)` functions through the FFI. `make FORTAN=0` skips it and the C implementation is used. A missing `.so` is not an error, the VM falls back to C.
* **Tests:** `make test` compiles the interpreter and runs the `.mh` suite in `tests/mh/` via `tests/run_tests.sh`.
  * `<name>.setup` — shell fixture reset, run before the program
  * `<name>.input` — stdin for the program
  * `<name>.guard` — optional executable guard; a non-zero exit skips the test and the script output becomes the reason
  * `<name>.expect` — expected output, one pattern per line:
    * `text` — the exact line must appear
    * `~text` — the substring must appear
    * `~/regex/` — the extended regular expression must match
    * a leading `!` inverts any of the three forms
  * `ATOMC_FLAGS="--no-aot"` or `ATOMC_FLAGS="--trace"` is passed to the interpreter for the whole run

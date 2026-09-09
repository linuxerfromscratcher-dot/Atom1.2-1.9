# Atom1.2-PL
you may think finally, but who actually cares?
So uhh, we added support of come ports(USB, Com,VGA,SATA,RJ-45,audio jack and some legacy Rj ports). I don't remember, if that was in 1.1, but was added packing and unpacking of data. We added a new technology, an .mh files, basically it means memory headers, you can write out some data and Atom code in a file, Hdd/SSD sector, RAM adress.
still the main goal of self hosting isn't done fully, but we try!
Here are our plans:
first concept(1.0)-1.999(last stess testing and compiling, basically last breath till official, stable release) then we will publish first, stable release. 
After getting into a team, we could do better peformance, so that the project will get more chances to work clockwise
GIANT THANKS TO EBLANROSE!!!

> thanks :) - eblanrose

Here is the manual:
# Technical Specification and Documentation for the Atom Programming Language (v1.2)

This document is a basic guide and architectural specification for the **Atom** programming language — an ultra-minimalist low-level programming language designed to operate as part of a custom micro-OS. Its architecture combines the principles of a linear execution pipeline and stack-based data processing (in the spirit of Forth concepts), aiming to provide maximum execution speed and minimal hardware resource requirements.

## 1. How the Language Works

The **Atom** language is built around the concept of a single **data stack** and linear sequential instruction execution.

* **Stack model:** All operands and calculation results pass through a global data stack. A command can take values from the stack, perform an operation on them, and place the result back onto the stack.
* **Linear pipeline:** A program executes instruction by instruction from left to right without complex tree-like nesting or syntax structures such as curly braces or semicolons.
* **Atomicity:** Each command is encoded using a single letter of the Latin alphabet (from `A` to `Z`), making parsing extremely fast and suitable for operation on "bare metal".

## 2. Commands (Alphabet from A to Z)

The complete language alphabet contains 26 basic instructions, each responsible for a particular low-level operation.

### A – M

**A (Allocate):** Allocates a block of dynamic memory in RAM (the heap). Takes the size from the stack and returns the address of the allocated buffer.

**B (Branch / Conditions):** Controls program flow through modifiers:

* B1 — Start of a condition (if-then)
* B2 — Alternative branch (else)
* B3 — End of a conditional block (end if)
* B4 — Intermediate condition check (elseif)

**C (Compare):** Compares the two top elements of the stack. Pushes the logical result True (`1`) or False (`0`) onto the stack.

**D (Data):** Loads a specific numeric value directly onto the stack (for example, `D33`).

**E (Execute):** Dynamically executes an instruction or block of code at the address located on the top of the stack.

**F (File / File System):** Works with file streams through modifiers:

* F1 — Open a file (file name address in memory → descriptor)
* F2 — Read a byte or character from a file
* F3 — Write data from a buffer into a file
* F4 — Close a file using its descriptor

**G (Get):** Reads a value directly from a hardware port or system register.

**H (Hardware / HAL):** Direct low-level interaction through the Hardware Abstraction Layer:

* H1 — SATA (storage devices and SSDs)
* H2 — COM port (UART / serial interface)
* H3 — USB (controller and connected peripherals)
* H4 — VGA (palette configuration, display mode)
* H5 — PS/2 (keyboard, mouse)
* H6 — HDMI (digital video/audio stream)
* H7 — RJ-45 (Ethernet networking)
* H8 — Audio Jack (audio chip)
* H9(n) — Legacy RJ ports with a numeric parameter in parentheses (`H9(1)` — RJ9, `H9(2)` — RJ11, `H9(3)` — RJ14, `H9(4)` — RJ25)

**I (Input):** Interactive input through buffer modifiers:

* I1 — Read data from a buffer and move it onto the stack
* I2 — Read data from a buffer into RAM at address 2 (address 1 is reserved for service byte packing)
* I3 — Read data directly into hardware register R7

**J (Jump):** Performs an unconditional jump to the specified label (classic `goto`).

**K (Kernel):** Calls a micro-OS system function (a kernel interrupt used for process management).

**L (Loop):** Organizes loop blocks through modifiers:

* L1 — Start of a loop
* L2 — End of a loop

**M (Memory):** Full RAM access, including reading or writing a value at a specific address.

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

**U (Unpack / Pack):** Operations involving packed data:

* U1 — Pack data
* U2 — Unpack data

**V (Vector):** Configures and redirects hardware interrupt vectors.

**W (Wait):** Pauses execution, delays execution, or waits for the next processor cycle.

**X (XOR / Logic):** Bitwise and logical operations:

* X1 — Bitwise AND
* X2 — Bitwise OR
* X3 — Exclusive XOR

**Y (Yield):** Transfers processor control to another process (voluntarily yields the current scheduler time slice).

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

* **Comments:** Everything following the `;` character until the end of the line is ignored by the interpreter.

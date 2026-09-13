# Atom1.2-PL
ATTENTION!!! YOU NEED TO GO TO fishatom BRANCH, THIS ONE IS OUTDATED.
you may think finally, but who actually cares?
So uhh, we added support of come ports(USB, Com,VGA,SATA,RJ-45,audio jack and some legacy Rj ports). I don't remember, if that was in 1.1, but was added packing and unpacking of data. We added a new technology, an .mh files, basically it means memory headers, you can write out some data and Atom code in a file, Hdd/SSD sector, RAM adress.
still the main goal of self hosting isn't done fully, but i try!
Here are my plans:
first concept(1.0)-1.999(last stess testing and compiling, basically last breath till official, stable release) then i will publish first, stable release. 
After getting into a team, we could do better peformance, so that the project will get more chances to work clockwise
GIANT THANKS TO EBLANROSE!!!
Here is the manual:

Technical Specification and Documentation of the Atom Programming Language (v1.3)

This document serves as the foundational guide and architectural specification for the Atom programming language—an ultra-minimalist low-level language designed to operate within a custom micro-OS. The architecture combines the principles of a linear pipeline and stack-based data processing (in the spirit of Forth concepts), ensuring maximum execution speed, human-readable mnemonics, and minimal hardware resource requirements.

1. How the Language Works
The Atom language is built on the concept of a single data stack and linear sequential instruction execution.
Stack Model: All operands and calculation results pass through a global data stack. A command can take values from the stack, perform an operation on them, and push the result back.
Linear Pipeline: The program executes instruction by instruction from left to right without complex tree-like nestings or syntactic structures like curly braces or semicolons.
Dual-Layer Execution: Each instruction maps from a clear human-readable mnemonic down to a single Latin letter code (from A to Z), ensuring instant parsing and direct execution on raw hardware.

2. Its Commands (Alphabet from A to Z with Mnemonics)
The complete alphabet of the language contains 26 basic instructions, each responsible for a specific low-level operation:
A / ALLOC (Allocate): Allocates a block of dynamic memory in RAM (heap). Takes the size from the stack and returns the address of the allocated buffer.
B / IF, ELSE, ENDIF, ELSEIF (Branch / Conditions): Flow control via modifiers:
B1 / IF — Start of condition (if-then)
B2 / ELSE — Alternative branch (else)
B3 / ENDIF — End of conditional block (end if)
B4 / ELSEIF — Intermediate check (elseif)
C / CMP (Compare): Compares the top two elements of the stack. Writes the logical result True (1) or False (0) to the stack.
D / LOAD (Data): Loads a specific numeric value directly onto the stack (e.g., LOAD 33 / D33).
E / EXEC (Execute): Dynamically executes an instruction or code block at the address located at the top of the stack.
F / FILE_* (File / File System): File stream operations via modifiers:
F1 / FILE_OPEN — Open file (name address in memory $\rightarrow$ descriptor)
F2 / FILE_READ — Read a byte or character from the file
F3 / FILE_WRITE — Write data from the buffer to the file
F4 / FILE_CLOSE — Close file by descriptor
G / GET_PORT (Get): Reads a value directly from a hardware port or system register. Hardware ports correspond to the HAL (H) peripherals:
G1/ INPHAL SATA — SATA, inputting data from SATA cable
G2/ INPHAL COM_UART — COM-port (UART), inputting data from UART COM port
G3/INPHAL USB — USB, inputting data from USB(currently only USB flash drive)
G5/INPHAL PS/2 — PS/2, input bytes from PS/2 mouse or keyboard
G7/INPHAL RJ45 — RJ-45, inputting data from ethernet port into special input buffer
G8/INPHAL AUDIO— Audio jack, input signals from audio jack(via speaker or headphones) in special input buffer
G9(n)/ INPHAL LEGACY_RJ — Legacy RJ ports, inputting signals from legacy RJs directly in special input buffer
H / HEADINCLUDE: include an standard library or a third party library(third party libs will be expected in 2.0 or newer).
ATTENTION TO DEVS: THE CODE THAT ARE IN THOSE HALs MUST TO BE EDITABLE, OR IN H, OR IN G, BOTH.
I / INPUT_* (Input): Interactive reading via buffer modifiers:
I1 / INPUT_BUF — Read data from the buffer and push it to the stack
I2 / INPUT_RAM — Read data from the buffer into RAM at address 2 (address 1 is reserved for service byte packing)
I3 / INPUT_REG — Read data directly into the hardware register R7
J / JUMP (Jump): Unconditional jump to the specified label (classic goto).
K / SYS_CALL (Kernel): Calls a micro-OS system function (kernel interrupt for process management) More K commands will appear in 1.4.
L / LOOP, ENDLOOP (Loop / Cycles): Organization of loop blocks via modifiers:
L1 / LOOP — Start of loop
L2 / ENDLOOP — End of loop
M / MEM (Memory): Full interaction with RAM (reading or writing(in .mh files only) a value at a specific address).
N / INC (Next): Increment (increases the value at the top of the stack by exactly 1).
O / WRITE (Output): Universal output: prints a number, character, or an entire text buffer to the screen.
P / PUSH, POP, DUP (Stack Operations): Basic stack operations:
P1 / PUSH — Push from the top of the stack(addition/duplication)
P2 / POP — Pop/removal from the top of the stack
P3 / DUP — Dup/duplication of the top element of the stack
Q / QUIT (Quit): Session termination, ironclad stop of program reading/execution, everything that is written after Q will be ignored and will be deleted from the asembly/compiled code.
R / REG (Register): Fast operations with internal hardware processor registers, like writing a value into vm registers.
S / SETUP (Setup / Store): Initialization of system environments, registers, or saving the current state to memory(like to zero every value in the stack, memory addresses and registers).
T / ADD, SUB, MUL, DIV (Transform / Arithmetic): Universal computational block:
T1 / ADD — Add two values at the top of the stack.
T2 / SUB — Subtract the top values of the stack.
T3 / MUL — Multiply the top values of the stack(value 1 * value 2).
T4 / DIV — Divide the top values of the stack.
U / PACK, UNPACK (Unpack / Pack): Working with packed data:
U1 / PACK — Package data into FAT32(or any else formatting) format
U2 / UNPACK — Unpackage data from FAT32(or any else formatting) format, while storing the data to prevent data deleting.
V / VECTOR (Vector): Configuration and redirection of hardware interrupt vectors, will add more in 1.4.
W / NOPE (no opeartion):well, it explains itself
X / AND, OR, XOR (XOR / Logic): Bitwise and logical operations:
X1 / AND — Bitwise AND
X2 / OR — Bitwise OR
X3 / XOR — Exclusive XOR
Y / YIELD (Yield): Transferring processor control to another process (voluntary exit from the scheduler quantum) will add too in 1.4.
Z / CLEAR (Zero): Complete instantaneous reset of the entire stack at the kernel level or checking the top for zero.
3. Linking File Specification (.mh)
FILE/{file}/ (Virtual file container): Creates a file in the system and automatically populates it with initial numeric values that form the individual stack "payload" for this component even before startup.
SECTOR {sector} (Media sector mapping): Binds a data or code block to a specific disk sector (a direct bridge for interacting with the SATA hardware driver via H1 / HAL SATA).
ADDR {address} (Adaptive address block with annulment): Defines the target address. If the system operates in tiny RAM mode and lacks such space, this address is annulled, and execution/data flow automatically shifts to basic system cells (3, 4, 5, etc.).
Hexadecimal bytecode (0x...): Direct injection of raw machine code transferred by the linker directly into the final .atmo or .rom binary files byte-for-byte.
Isolated stack context: Each separate section in the mapping file possesses its own independent set of stack data, ensuring complete subsystem autonomy.
Assembly or any else programming language: you can write code on any other programming language, as long as you have a compiler/intepreter/transpiler in your system(please set configuration in atom.config, if not, the idle compiler will be GCC)

4. How to Work with Them
The programming process in Atom is based on the principle of data transfer via the stack.
Data loading: First, the required numbers or variables are placed onto the stack using the LOAD (D) command or read via INPUT_* (I1–I3).
Processing: Commands like ADD/SUB/MUL/DIV (T1–T4), CMP (C), or AND/OR/XOR (X1–X3) take this data from the stack, process it, and return the result back to the top of the stack.
Saving or output: The resulting value can be saved to memory using the MEM (M) command or outputted to the screen via WRITE (O).
Sequence Logic Example:
Command LOAD 10 (D10) places the number 10 on the stack.
Command LOAD 2 (D2) places the number 2 on the stack.
The next command, ADD (T1), takes 10 and 2, adds them, and pushes the result back onto the stack.
Command WRITE (O) takes 12 and outputs it to the screen.

5. Syntax Proper
The syntax of the Atom language is maximally simplified to avoid unnecessary characters:
Command format: A capital mnemonic word or a capital Latin letter (from A to Z) plus an optional numeric argument with no spaces between them (e.g., LOAD 42, D42, MEM 100, M100, WAIT 5, W5).
Separators: Commands are separated from each other by spaces or newlines.
Jump labels: Jump labels are designated by a number followed by a colon at the beginning of a line (e.g., 10:).
Comments: Anything following the ; symbol to the end of the line is ignored by the interpreter.
NO SPACES! Atom works almost like a bare-bones assembler, with no leading spaces if it's an if or loop section.
Text in slashes: When you want to output text somewhere, use / instead of quotation marks ("), for example: WRITE /Hello, world!/.

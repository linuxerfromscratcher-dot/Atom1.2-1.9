``` mermaid
graph LR
A[fishatom-1.2] --> B[fishatom1.2.1]
```
# What changed?

 1. imports!
 2. Comments!
 3. TESTS!
 
``` mermaid
graph LR
A[fishatom-1.2.1] --> B[fishatom1.2.2]
```
# What changed?

1. REF!
2. FFI!!!!!!!!!

  ``` mermaid
graph LR
A[fishatom-1.2.2] --> B[fishatom-1.3]
```
# What changed?

1. NATIVE BINARIES! `atomc -c program.mh -o program` packs the VM and the
   program into one self contained executable, the platform and the ABI layout
   are checked before it runs
2. STRICT C/FORTRAN ABI! `src/atom_abi.h` fixes the word width, the shared
   record layouts and the array order on both sides of the FFI, and the VM
   verifies them at run time before it passes a single value
3. M WRITES! an ordinary program may write a RAM byte again, only the address
   is clamped to the RAM window
4. FIELD WISE IMAGES! the AOT cache and the native payload share one
   serialiser that never dumps a raw struct

 ## Please DO NOT CHANGE MSG

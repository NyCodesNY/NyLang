# NyLang

NyLang is a statically-typed, compiled systems programming language with C-like syntax. It features a custom compiler pipeline written entirely from scratch in C++, featuring its own Lexer, Parser, SSA-inspired Intermediate Representation (IR), and a native x86-64 machine code Assembler.

NyLang compiles directly to native standalone binaries without relying on external assemblers or linkers (like NASM or LD). It includes native object file writers for both Linux (ELF64) and Windows (PE32+).

## Features

- **Zero-Dependency Native Compilation:** Generates executable code directly. It writes its own `.o` ELF files for Linux and standalone `.exe` PE files for Windows.
- **Cross-Compilation:** You can compile Windows `.exe` files from Linux using the `--target windows` flag.
- **Static Typing & Type Inference:** Supports strict types (`int`, `string`, `bool`, `void`) and automatic type inference via the `init` keyword (similar to C++ `auto`).
- **Low-Level Memory Control:** C-style pointers, `&` (address-of), `*` (dereference), and raw pointer arithmetic.
- **Dynamic Memory Allocation:** Direct access to heap memory via `System.Alloc` and `System.Free`.
- **SSA-Inspired IR Layer:** Includes a custom Intermediate Representation (NyIR) to enable future optimization passes like constant folding and dead code elimination.

## Building the Compiler

NyLang requires a C++17 compliant compiler and CMake.

```bash
git clone https://github.com/yourusername/NyLang.git
cd NyLang
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make
```

## Usage

Compile a NyLang source file into an executable:

```bash
./nylang path/to/source.ny [output]
```

### Options

- `--target linux`: (Default) Compiles to a Linux ELF64 relocatable object, which is then linked using `gcc`.
- `--target windows`: Compiles directly to a standalone Windows PE32+ executable (`.exe`).
- `--dump-ir`: Prints the human-readable NyIR representation of the program before machine code generation and exits.

## Language Examples

### Hello World & Type Inference

```ny
function void Main() {
    init msg = "Hello, NyLang!";
    System.Print(msg);
    
    init number = 42;
    System.Print(number);
}
```

### Pointers and Pointer Arithmetic

```ny
function void Main() {
    int x = 42;
    int* ptr = &x;
    
    System.Print("Pointer read:");
    System.Print(*ptr);
    
    *ptr = 100;
    System.Print("Modified x:");
    System.Print(x);
}
```

### Dynamic Memory Allocation

```ny
function void Main() {
    System.Print("Allocating memory...");
    int* ptr = System.Alloc(8);
    
    *ptr = 12345;
    System.Print("Value in allocated memory:");
    System.Print(*ptr);
    
    System.Print("Freeing memory...");
    System.Free(ptr);
}
```

### Dumping IR

Running `./nylang --dump-ir` on the memory example produces the following intermediate representation:

```
; ═══════════════════════════════════════
; NyLang IR Dump
; ═══════════════════════════════════════

define void @Main() {
    %t0 = const_str "Allocating memory..."
    call_print(%t0 : string)
    %t1 = const_int int 8
    %t2 = malloc(%t1)
    %v_ptr = alloca int*
    store %t2 -> %v_ptr
    %t3 = const_int int 12345
    %t4 = load int* from %v_ptr
    deref_store *%t4 <- %t3
    %t5 = const_str "Value in allocated memory:"
    call_print(%t5 : string)
    %t6 = load int* from %v_ptr
    %t7 = deref_load *%t6
    call_print(%t7 : int)
    %t8 = const_str "Freeing memory..."
    call_print(%t8 : string)
    %t9 = load int* from %v_ptr
    free(%t9)
    ret (void)
}
```

## Compiler Architecture

1. **Lexer:** Converts source text into a stream of tokens.
2. **Parser:** Builds an Abstract Syntax Tree (AST) representing the program's structure.
3. **IRGen:** Walks the AST and translates it into NyIR, a flat, typed, three-address instruction set utilizing virtual registers.
4. **IRCodeGen:** Translates the IR instructions into native x86-64 machine code, mapping virtual registers to physical memory locations (stack slots) or CPU registers as dictated by the ABI (System V AMD64 for Linux, Windows x64 for Windows).
5. **Assembler:** Manages raw instruction encoding, label resolution, and section formatting (`.text`, `.data`).
6. **Binary Writers:** Pack the generated machine code and data into the final target executable format (`ElfWriter` or `PeWriter`).

# ToyC Compiler

A self-contained C++20 compiler for the ToyC language used in the compiler
systems practice assignment. It reads ToyC from standard input and writes
GNU-compatible RV32IM assembly to standard output.

## Build

With CMake:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Or with Make:

```sh
make
```

The resulting executable is named `compiler`.

## Run

```sh
./compiler < input.tc > output.s
./compiler -opt < input.tc > output.s
```

The `-opt` argument is accepted for compatibility with performance tests.

## Architecture

- Hand-written lexer with C-style comments
- Recursive-descent parser and typed AST
- Scoped symbol tables and compile-time constant evaluation
- Three-address virtual-register IR with explicit labels and branches
- Short-circuit lowering for `&&` and `||`
- RV32IM backend following the standard integer calling convention
- Stack arguments for calls with more than eight parameters
- Global data emitted in `.data`

The compiler has no runtime or third-party dependencies.

## Local end-to-end test

On Ubuntu/WSL with a C++ compiler, RISC-V binutils, and QEMU user mode:

```sh
sudo apt install -y g++ binutils-riscv64-linux-gnu qemu-user
bash tests/run_tests.sh
```

The script builds the compiler, emits RV32IM assembly, assembles and links each
test with a minimal Linux `_start`, runs it using `qemu-riscv32`, and compares
the exit code with the corresponding `.expected` file.

## Current optimization strategy

Named constants and constant initializers are evaluated during lowering.
Logical operators are lowered directly to short-circuit control flow. The
backend prioritizes correctness by assigning stable stack locations to IR
values; this is intentionally a suitable baseline for later register
allocation and data-flow passes.

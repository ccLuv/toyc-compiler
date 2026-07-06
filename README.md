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
With `-opt`, the IR performs basic-block-local constant propagation and
constant folding, algebraic simplification, dead value/store elimination,
read-only global propagation, function-entry constant hoisting, and
tail-recursion elimination. Logical operators are lowered directly to
short-circuit control flow.

Dead stores are identified with iterative CFG local-variable liveness plus a
backward dependency slice from observable results. Basic-block copy propagation
and common-subexpression elimination remove redundant value chains.

Because ToyC has no external input or I/O and `main` has no parameters, the
optimized driver attempts budgeted whole-program evaluation on optimized IR.
The evaluator uses integer arrays for locals and virtual registers and
pre-resolves labels. Pure functions are memoized by argument values. When
evaluation completes, the compiler emits a constant-time `main` containing
only the computed return value. Programs that exceed the step, wall-clock, or
recursion budget automatically fall back to the normal optimized RV32 backend.

The backend counts accesses to local variables and keeps the hottest locals in
callee-saved registers. Remaining `s1-s11` registers are assigned to virtual
registers with linear-scan live intervals; overlapping values spill to stable
stack slots. Live intervals are extended across loop back-edges to preserve
loop-invariant values. The instruction selector directly consumes allocated
registers instead of routing every operation through temporary registers.
Comparison-plus-branch and expression-plus-local-store patterns are fused, so
typical counting loops use a direct conditional branch, one update, and one
back-edge jump. Loop rotation further changes hot while loops to a bottom-tested
form, removing the unconditional jump from steady-state iterations.
Every function saves and restores the callee-saved registers it uses. Direct
12-bit stack offsets are emitted whenever possible.

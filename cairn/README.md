# Cairn

A stack-based scripting language and virtual machine, written from scratch in C++17.

Cairn takes source text, tokenises it, parses it into an AST, compiles that AST to a
custom bytecode, and executes the bytecode on a hand-written stack VM. No parser
generators, no runtime dependencies, no third-party libraries.

```sh
$ echo 10 | ./build/cairn examples/fizz.cairn
55
```

**Complete: all four phases.**

| Phase | Component | Status |
|-------|-----------|--------|
| 1 | Lexer, source management, diagnostics | done |
| 2 | Recursive-descent parser and AST | done |
| 3 | Bytecode compiler and instruction set | done |
| 4 | Stack-based VM and dispatch loop | done |

## Building

Requires a C++17 compiler and CMake 3.16+.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Without CMake:

```sh
g++ -std=c++17 -O2 -Iinclude src/*.cpp -o cairn
```

## Running

```sh
echo 10 | ./build/cairn examples/fizz.cairn        # compile and run (default)
./build/cairn --bytecode examples/fizz.cairn       # disassemble
./build/cairn --ast examples/fizz.cairn            # print the AST
./build/cairn --tokens examples/operators.cairn    # print the token stream
./build/cairn --quiet examples/broken.cairn        # errors only
./build/cairn --bench 20 examples/bench.cairn      # ns/instruction
```

`examples/fizz.cairn` compiles to:

```
0000     4  INPUT
0001     5  CONST              0   ; 0
0004     6  CONST              1   ; 1
0007     8  GET_LOCAL          2
0010     |  GET_LOCAL          0
0013     |  LE
0014     |  JUMP_IF_FALSE     25   -> 0042
0017     9  GET_LOCAL          1
...
0039     8  LOOP              35   -> 0007
0061    13  HALT
```

Note what is *not* there: no variable names. Every `let` was resolved to a stack slot at
compile time, so the VM never looks up a name.

## Dispatch benchmark

The VM implements both switch and computed-goto dispatch from a single copy of the
instruction bodies, selected at build time:

```sh
cmake -S . -B build-switch -DCMAKE_BUILD_TYPE=Release -DCAIRN_FORCE_SWITCH=ON
cmake --build build-switch
./build/cairn        --bench 20 examples/bench.cairn
./build-switch/cairn --bench 20 examples/bench.cairn
```

On GCC 13.3 / x86-64, over ~92M retired instructions, the switch measured **1.69 ns per
instruction against 2.29 ns for computed goto** — reproducibly, at both -O2 and -O3. That
is the opposite of the usual claim. `DESIGN.md` D31 has the candidate explanations and
what I would measure next to settle it. Numbers will differ on your machine; that is what
the harness is for.

`examples/fizz.cairn` parses to:

```
(program
  (let limit (input))
  (let total 0)
  (let i 1)
  (while (<= i limit)
    (block
      (assign total (+ total i))
      (assign i (+ i 1))))
  (if (> total 100)
    (block
      (print true))
    (block
      (print total))))
```

`examples/broken.cairn` is intentionally malformed and exercises every error path:

```
examples/broken.cairn:4:9: error: unexpected character '@'
    let x = @;
            ^
examples/broken.cairn:6:11: error: integer literal '9223372036854775808' does not fit in 64 bits
    let big = 9223372036854775808;
              ^^^^^^^^^^^^^^^^^^^
```

All four errors are reported in a single run — the lexer recovers rather than aborting.

## The language

```
let n = input;
let total = 0;

while (n > 0) {
    total = total + n;
    n = n - 1;
}

if (total == 55) { print true; } else { print total; }
```

Types are 64-bit integers and booleans. Operators are `+ - * /`, `== != < <= > >=`,
`!`, and `=`. Keywords are `let if else while print input true false`. Comments are
`//` to end of line and `/* ... */`.

## Layout

```
include/cairn/   public headers  (token, source, diagnostic, lexer, ast, parser,
                 value, opcode, chunk, compiler, vm)
src/             implementations + CLI driver
tests/           eight suites, dependency-free harness
examples/        sample programs, including a deliberately broken one
DESIGN.md        decision log: what was chosen, what was rejected, why
```

## Properties

Phase 1 — lexer:

- Single pass, O(n) in source length, one byte of lookahead.
- Zero heap allocations per token: every lexeme is a `string_view` into one buffer.
- All diagnostics collected, never thrown; scanning continues past errors.
- Every token carries line, column and byte offset.

Phase 2 — parser:

- Recursive descent, with precedence climbing driven by a binding-power table.
- Panic-mode recovery with a guaranteed-progress rule, so one missing semicolon
  produces one diagnostic rather than a cascade.
- One typo, one message: the parser stays silent about damage the lexer already
  reported.
- Node kind tags instead of RTTI; traversal via visitor so a forgotten node type
  is a build error rather than a silent fallthrough.
- Every AST node carries a source location for Phase 3 and Phase 4 to report against.

Phase 3 — compiler:

- Flat `std::vector<std::uint8_t>` bytecode; one-byte opcodes, uniform two-byte operands.
- Variables resolved to stack slots at compile time — no runtime name lookup anywhere.
- Constant pool with deduplication; `true`/`false` get dedicated opcodes instead of
  pool entries.
- Relative jump offsets with backpatching; out-of-range branches are a compile error and
  produce no chunk rather than a half-patched one.
- Run-length encoded line table, kept out of the code array.
- Disassembler (`--bytecode`) with resolved jump targets and constant values.

Phase 4 — VM:

- Dispatch loop implemented twice (switch and computed goto) from one copy of the
  instruction bodies, selectable at build time and benchmarked against each other.
- Value stack preallocated with a raw top pointer; the single bounds check is on push,
  against a constant.
- Arithmetic overflow detected with compiler builtins rather than executed — signed
  overflow is UB, so it is never performed and then inspected.
- Runtime errors (type mismatch, division by zero, overflow, stack overflow, bad input)
  reported with a source line via the RLE line table.
- I/O through injected streams, which is what makes end-to-end output assertions possible.

137 tests across eight suites. `examples/broken.cairn`, `examples/syntax-error.cairn` and
`examples/semantic-error.cairn` exercise the three compile-time error paths.

See `DESIGN.md` for why each of those was chosen, and what was rejected.

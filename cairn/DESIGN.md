# Design decisions

One entry per decision: what was chosen, what was rejected, and why. Written as the
code was written, not reconstructed afterwards.

This file doubles as interview preparation. Every "why did you do it that way?"
question about this project should be answerable from here.

---

## Phase 1 — Lexer

### D1. Hand-written scanner, not flex or a regex engine

**Chosen:** a hand-written character-by-character scanner.
**Rejected:** flex/re2c generated lexers; `std::regex`.

Reasons:

1. `std::regex` backtracks. Its worst case is exponential, and even the common case
   is roughly an order of magnitude slower than a direct scan because the pattern is
   interpreted at runtime. A hand-written scanner is a straight-line loop with
   predictable cost.
2. A generated lexer is code I did not write and cannot defend. The whole point of
   the project is understanding the pipeline.
3. Error messages. Generated lexers give poor recovery behaviour by default; hand
   rolling makes `123abc` produce one clear message instead of two confusing tokens.

**Cost:** more code, and the keyword table has to be maintained by hand.

---

### D2. Token holds a `string_view`, not a `std::string`

**Chosen:** the source file is read once into one `std::string`; every token's lexeme
is a non-owning view into that buffer.
**Rejected:** copying each lexeme into an owned `std::string`.

A `std::string` per token means one heap allocation per token (short-string
optimisation hides some of it, but not for identifiers past 15 characters). For a
1 MB source that is hundreds of thousands of allocations for data that already exists
contiguously in memory.

The trade-off is a lifetime constraint: `SourceFile` must outlive every `Token` and
`Diagnostic` derived from it. That constraint is documented on the class, and the
tests assert that lexeme pointers really do fall inside the original buffer.

---

### D3. Diagnostics are collected, not thrown

**Chosen:** a `DiagnosticBag` that accumulates errors; the lexer records and recovers.
**Rejected:** throwing on the first bad character; `exit(1)` inside the lexer.

A user with four typos should learn about all four in one run — this is what
`examples/broken.cairn` demonstrates. Exceptions are also the wrong mechanism here:
a malformed literal is expected input for a compiler, not an exceptional condition,
and throwing from a hot scan loop wrecks its optimisation.

Recovery strategy: consume the offending run, emit an `Invalid` token, keep scanning.
`Invalid` tokens stay in the stream rather than being silently dropped, so Phase 2 can
decide how to resynchronise instead of seeing a hole.

---

### D4. Line and column live in `Token`, computed during the scan

**Chosen:** `advance()` maintains `line_`/`column_`; each token stores its start
position and byte offset.
**Rejected:** storing only byte offsets and computing line/column later by scanning
for newlines.

Offset-only is more compact and is what large compilers do (with a cached line-start
table). At this scale it just means every diagnostic pays an O(n) scan, and retrofitting
positions later is the single most annoying refactor in a compiler. Byte offset is kept
alongside anyway, so a line-start table can be added in Phase 2 without touching the API.

---

### D5. Keyword lookup is a linear scan over a `constexpr` array

**Chosen:** eight entries in a contiguous `constexpr` array, compared in a loop.
**Rejected:** `std::unordered_map<std::string_view, TokenKind>`.

For eight short strings, a linear scan wins: no hash computation, no pointer chasing
into scattered buckets, no static initialisation order concern, and the whole table is
in cache. `unordered_map` would win somewhere past a few dozen keywords, and the
crossover is worth measuring rather than assuming.

A first-character switch would prune faster still, and is the obvious next step if the
keyword set grows.

---

### D6. Integer literals are converted in the lexer, with a manual overflow check

**Chosen:** accumulate digits into `int64_t` in `lex_number`, guarding with
`value > (INT64_MAX - digit) / 10` before each step.
**Rejected:** `std::stoll` (allocates, and signals failure by throwing);
`strtoll` (locale-sensitive, communicates errors through `errno`); deferring
conversion to the parser.

Converting here means overflow is reported with the exact source span while that span
is still in hand. The check is done before the multiply, so no signed overflow — which
is undefined behaviour — is ever executed.

---

### D7. Maximal munch for multi-character operators

`==` must win over `=`, `<=` over `<`. Implemented as a single-byte `match()`
lookahead in the operator switch. This is the standard "longest match wins" rule, and
the test `MaximalMunchPrefersTwoCharacterOperators` pins it.

---

### D8. Custom character classifiers instead of `<cctype>`

**Chosen:** `constexpr` `is_digit` / `is_ident_start` / `is_ident_continue`.
**Rejected:** `std::isalpha`, `std::isdigit`.

`std::isalpha` takes an `int` and requires the argument to be representable as
`unsigned char`. Passing a plain `char` holding `0xFF` is undefined behaviour — a
real bug that appears in a lot of student lexers. The functions are also locale
dependent, so identifier rules would silently change with the environment.
The test `NonAsciiByteIsReportedWithoutUndefinedBehaviour` covers this.

---

### D9. `123abc` is an error, not two tokens

A strict scanner would emit `Integer(123)` then `Identifier(abc)` and let the parser
fail. That produces a parse error pointing at `abc`, some distance from the actual
mistake. Consuming the whole malformed run and emitting one lexical error puts the
caret exactly where the user went wrong.

---

### D10. Dependency-free test harness

**Chosen:** ~60 lines of registration macros in `tests/test_framework.hpp`.
**Rejected:** GoogleTest, Catch2.

The project's stated property is that it has no third-party dependencies. Pulling in a
test framework via FetchContent contradicts that and adds a network requirement to the
build. Two suites are built as separate binaries so a crash in one cannot mask the other.

---

## Open questions carried into Phase 2

- Should `Invalid` tokens be visible to the parser, or filtered out at the boundary?
- Line-start table for O(log n) offset-to-position lookup, or keep per-token positions?
- Interning identifiers now (for fast variable resolution in Phase 3) vs. later.

---

## Measuring

`--time` reports nanoseconds per token. Record a number on your own machine at each
phase, in a Release build, and note it here — the number matters less than being able
to say what it is and what dominates it.

```
./build/cairn --quiet --time <large-file>.cairn
```

Current dominant cost in Phase 1 is the `std::vector<Token>` growth in `tokenize()`,
not the scan itself; `reserve()` uses a bytes/4 heuristic that is deliberately crude.

---

## Phase 2 — Parser and AST

### D11. Recursive descent, written by hand

**Chosen:** recursive descent over the token vector.
**Rejected:** bison/ANTLR; a table-driven LR parser.

Recursive descent is what production compilers use — GCC, Clang, V8, Rust — for the
same reason it was chosen here: error messages and recovery are ordinary code you can
edit, rather than something the generator does to you. LR handles a wider grammar
class, but Cairn's grammar is LL(1) apart from assignment, and nothing in it needs
that power.

The grammar is written out in EBNF at the top of `parser.hpp`. Keeping it next to the
code is what makes it checkable: every production has a function with the same name.

---

### D12. Precedence climbing, not a cascade of functions

**Chosen:** one `binary(min_precedence)` function driven by a `precedence_of()` table.
**Rejected:** the textbook chain `equality() -> comparison() -> term() -> factor() -> unary()`.

The cascade is what most tutorials teach. It means one function per precedence level,
each calling the next, so a five-level grammar pays five nested calls to parse the
literal `1`. Adding an operator means writing a new function and editing its two
neighbours.

Precedence climbing collapses that into a loop with an explicit binding-power table.
Adding `%` is one row. Left-associativity comes from recursing with
`min_precedence + 1`; right-associativity would pass `min_precedence` unchanged, which
is exactly the one-token difference that makes `a = b = c` nest the other way.

Both parse the same language. This is a code-shape argument, not a speed one — the
depth is bounded by the number of precedence levels either way.

---

### D13. Node kind tags instead of `dynamic_cast`

Every `Expr` and `Stmt` carries an `ExprKind` / `StmtKind` byte. Checking whether an
assignment target is a variable is a byte compare, not RTTI.

`dynamic_cast` walks a type hierarchy at runtime and requires RTTI to be enabled;
plenty of low-latency codebases build with `-fno-rtti`, so a design that depends on it
is a design that cannot be dropped into one. The tag also makes Phase 3's compiler able
to `switch` on node kind, which a virtual call cannot do as cheaply.

The virtual `accept` visitor is kept alongside for traversal, because the alternative —
a `switch` with a `static_cast` in every arm — silently compiles when a new node type
is added and forgotten. The visitor makes that a build error.

---

### D14. Exceptions for parser recovery, and only there

**Chosen:** an internal `ParseError` thrown by `fail()`, caught at the statement boundary.
**Rejected:** returning `nullptr` and null-checking at every level.

This looks like it contradicts D3, where the lexer collects errors instead of throwing.
It does not, and the distinction is the point:

- The lexer's error path runs *per byte*. Anything on that path is hot.
- The parser's error path runs *per failed statement*, in a program that is about to be
  rejected. It is as cold as code gets.

What the exception buys is unwinding out of arbitrarily deep expression recursion in one
step. The alternative is threading a null check through `primary`, `unary`, `binary`,
`assignment` and every statement rule — five layers of `if (!x) return nullptr;` that
must all be correct, where one missed check is a null dereference.

`ParseError` is a private nested type and never escapes `parse()`. There is a test that
pins that.

---

### D15. Recovery must make progress

`synchronize()` takes the index where the failed statement began. If the statement
consumed nothing before failing — which happens whenever the very first token is
wrong — recovery forces one step forward.

Without that, input like `} ; }` loops forever: the statement rule fails on `}`,
recovery sees a token that starts a statement and returns, and the parse loop calls the
same rule on the same token again. The first version of this parser had exactly that
bug, and the test `ParserNeverLeaksParseErrorToTheCaller` was what caught it.

The recovery targets are a semicolon just consumed, or a token that can only begin a
statement (`let if while print { }`). The test `MissingSemicolonDoesNotCascade` pins the
behaviour that actually matters: one missing semicolon produces one diagnostic, not four.

---

### D16. One typo produces one diagnostic

An `Invalid` token is a lexical error the lexer already described. When the parser trips
over one, it unwinds *without recording a diagnostic*.

The alternative reads badly: `let x = @;` would report "unexpected character '@'" and
then "expected an expression, found ''" — two messages, one of which describes damage
rather than cause.

`Invalid` tokens are deliberately not filtered out of the stream either. Deleting them
would close the hole and let a malformed program parse as though it were fine. They stay
so the parser trips exactly where the problem is, and stays quiet about it.

---

### D17. No `Grouping` node

Parentheses affect only which tree gets built. Once it is built, `(1 + 2) * 3` and any
other route to the same shape are the same program, so `primary()` returns the inner
expression directly and the node type does not exist.

The cost: an exact source round-trip is impossible — a formatter built on this AST could
not know where the user's parens were. That is a real trade-off for a formatter and
irrelevant for a compiler, which is what this is.

---

### D18. `unique_ptr` tree, not an index arena

**Chosen:** `std::unique_ptr` children.
**Rejected:** nodes in one `std::vector` with `uint32_t` indices for children.

The arena is the faster layout: contiguous nodes, no pointer chasing, no per-node
allocation, and teardown is one `vector` destructor instead of a recursive walk. It is
what a production compiler front end would do.

It is not what this project needs, and the reason is worth stating precisely: the AST is
walked **once**, by the Phase 3 compiler, and then discarded. Nothing hot ever touches
it. The hot loop in this project is the VM's dispatch loop over the bytecode, which is a
flat `vector<uint8_t>` by design — the arena argument, applied where it actually pays.

Optimising the AST layout would be optimising the part of the pipeline that runs once
per program instead of once per instruction.

---

### D19. Golden S-expressions as the parser's test oracle

Tests compare a rendered tree against a literal string: `1 + 2 * 3` must produce
`(+ 1 (* 2 3))`. Precedence, associativity and nesting are all pinned by one readable
assertion, and a failure prints both trees side by side.

The printer is a visitor rather than a virtual `print()` on each node, which is what
lets Phase 3's compiler be a second visitor over the same unchanged tree.

---

## Open questions carried into Phase 3

- Constant folding in the AST before emission, or a peephole pass over the bytecode?
- Intern identifiers now so variable resolution is an index rather than a string compare.
- Fixed-width instructions (simple decode) vs. variable-width (smaller, better I-cache).
- `-9223372036854775808` cannot be written: the lexer rejects the literal as overflow
  before unary minus is applied. Handle it in the parser as a special case, or leave it.

---

## Phase 3 — Bytecode compiler and instruction set

### D20. Bytecode, not a tree-walking interpreter

The AST could be executed directly. It is not, because tree walking is the slow
architecture for the reason this project cares about: every node visit is a virtual
call to an address the branch predictor cannot guess, and the nodes are scattered
across the heap, so each step is a likely cache miss.

Bytecode flattens the program into one contiguous `std::vector<std::uint8_t>`. The VM
walks it with a pointer, prefetching works, and the only unpredictable branch left is
the dispatch switch itself — which is Phase 4's problem, and a much smaller one.

---

### D21. One-byte opcodes, uniform two-byte operands

**Chosen:** opcode is one byte; an instruction takes either zero or exactly two operand
bytes (little-endian `uint16`).
**Rejected:** one-byte operands with `_LONG` variants; fully variable-width encoding.

The classic design uses a one-byte operand and adds `CONSTANT_LONG`, `GET_LOCAL_LONG`
and so on for programs that outgrow 256 constants or locals. That doubles the number of
opcodes to implement and test, and every one is a place to get the decode wrong.

Two bytes everywhere costs one extra byte on instructions that would have fit in one,
and buys a flat 65535 ceiling on constants, locals and jump distances with no special
cases. The decode step becomes uniform: read the opcode, read zero or two bytes, and
`operand_bytes()` is the single source of truth used by both the disassembler and
(in Phase 4) the VM.

The ceilings are enforced with real diagnostics, not assumed away — see D25.

---

### D22. Locals are stack slots resolved at compile time

**Chosen:** every `let` gets an index into the VM's value stack, decided while compiling.
A variable reference emits `GET_LOCAL <slot>`.
**Rejected:** a name-to-value hash map consulted at runtime.

This is the single largest performance decision in the project. A hash map lookup per
variable access means hashing a string, a probable cache miss into a bucket, and a
string compare — per access, inside the loop. Slot indexing is a load at a known offset
from the stack base.

Resolution walks `locals_` backwards, which gives shadowing for free: the innermost
declaration with a matching name is found first.

The consequence is that scope structure must be known statically, which is what D27
turned out to be about.

---

### D23. Each comparison operator gets its own opcode

**Chosen:** `EQ NEQ LT LE GT GE` are six distinct instructions.
**Rejected:** synthesising four of them — `a != b` as `EQ` then `NOT`, `a >= b` as
`LT` then `NOT`.

The synthesis trick saves four opcode slots out of 256, which are not scarce. It costs a
second trip through the dispatch loop for every one of those comparisons: another
indirect branch, another decode, another stack round trip.

In a loop condition — which is where comparisons live — that is the hot path. Spending
four opcodes to halve the instruction count on `i < n` and `i != n` is the right trade,
and it is the trade a real VM makes for exactly the same reason.

---

### D24. `JUMP_IF_FALSE` pops its own condition

The usual design leaves the condition on the stack and emits an explicit `POP` on both
sides of the branch, because languages with short-circuit `&&` and `||` need the value
to survive the jump.

Cairn has no short-circuit operators, so nothing needs it. Popping inside the branch
instruction removes two instructions from every `if` and every loop iteration.

This is a good example of an ISA decision that is only correct given the language's
scope. If `&&` is ever added, this has to be revisited — noted here so the reason is not
lost.

---

### D25. Jump offsets are relative, and range violations are errors

Offsets are `uint16` relative to the byte *after* the operand. Relative keeps them small
and makes a chunk position-independent. Forward jumps are backpatched: emitted with
`0xFFFF`, and the real distance written once the target is known.

When a jump target is further than 65535 bytes, the compiler reports it and returns no
chunk at all. Half-emitted bytecode must never reach the VM — an unpatched jump would
send execution into whatever byte happened to follow. Two tests generate ~20000
statements specifically to hit this path.

---

### D26. Tagged union `Value`, not `std::variant`, not NaN-boxing

**Chosen:** a 16-byte struct with a `Type` byte and a union.
**Rejected:** `std::variant<std::int64_t, bool>`; NaN-boxing.

`std::variant` stores its own discriminant, generates `visit` dispatch, and is not
trivially copyable for arbitrary alternative sets. The hand-written union is trivially
copyable and the tag check is a byte compare.

NaN-boxing packs every value into 8 bytes by hiding payloads in the unused bits of an
IEEE-754 quiet NaN — half the memory traffic on the stack, which is real. It is not here
because Cairn has no doubles yet, so there is no NaN to box into: the technique needs a
float representation to hide inside. If Phase 4 grows a `Double` type, this is the first
thing to revisit.

Constant pool equality is type-strict, so `1` and `true` never collapse into one entry.

---

### D27. Declarations are not statements — a bug found by reading the disassembly

`if (false) let y = 1;` used to compile, and it was wrong. The listing showed it:

```
0000  FALSE
0001  JUMP_IF_FALSE  -> 0007
0004  CONST 0            ; 1     <- skipped at runtime
0007  GET_LOCAL 0                <- reads a slot that was never pushed
```

The compiler assigned `y` slot 0 while walking the tree, because slot assignment is
static (D22). At runtime the push is skipped when the branch is not taken, so every
later slot index is off by one and `GET_LOCAL 0` reads whatever happened to be there.
In a `while` body the same construct pushes a slot per iteration and grows the stack
without bound.

The fix is grammatical: `declaration -> varDecl | statement`, and an `if`/`while` body is
a *statement*, so a bare declaration there is a syntax error with a message telling the
user to add braces. Inside braces it is fine, because a block is a scope and the block's
`end_scope` pops what it pushed.

The general lesson is the one worth carrying: a compile-time model of the runtime stack
is only sound if the code that manipulates the stack runs unconditionally. That
invariant was implicit; now it is enforced by the grammar.

---

### D28. Constant pool deduplication

Identical constants share one pool entry. A loop mentioning `1` twenty times produces one
entry, not twenty. The lookup is a linear scan, which is `O(n²)` over the whole program
in the worst case — fine at this size, and the honest answer for a larger one is a hash
map keyed on `Value`, not a cleverer scan.

---

### D29. Run-length encoded line table

Debug line information is stored as `(line, byte_count)` runs in a separate vector, not
as a parallel array with one `uint32` per bytecode byte.

Source lines are naturally runs — one statement emits several consecutive instructions —
so the RLE form is a fraction of the size. More importantly it keeps line data *out of*
the code array: the bytes the VM walks stay dense, and nothing in the hot loop shares a
cache line with debug metadata.

---

### D30. No optimisation passes

No constant folding, no peephole pass, no dead code elimination. `1 + 2` emits two
`CONST`s and an `ADD`.

This was an open question at the end of Phase 2 and the answer is deliberate: an
optimisation you cannot measure is an optimisation you cannot defend. Phase 4 gives a
working VM and a timing harness. Folding then becomes a change with a before-and-after
number attached, which is worth far more than a fold implemented on the assumption that
it helps.

---

## Open questions carried into Phase 4

- Dispatch: `switch`, computed goto (`&&label` — a GCC/Clang extension), or direct
  threading. Measure all three on the same program; this is the project's headline number.
- Stack as a fixed array with a raw pointer vs `std::vector` with bounds checks.
- Where to check for division by zero and integer overflow: per operation, or not at all.
- Type errors are still unhandled — `1 + true` compiles today and Phase 4 must decide
  whether that is a runtime error or a static check.

---

## Phase 4 — Virtual machine

### D31. Dispatch: I expected computed goto to win, and it lost

Both strategies are implemented from a single copy of the instruction bodies, selected by
macros, so they cannot drift apart. `-DCAIRN_FORCE_SWITCH=ON` builds the switch variant;
`--bench N` reports which one is running and the ns/instruction.

Measured on GCC 13.3, x86-64, `examples/bench.cairn`, ~92M instructions retired:

| Build          | -O2     | -O3     |
|----------------|---------|---------|
| switch         | 1.69 ns | 1.72 ns |
| computed goto  | 2.29 ns | 2.36 ns |

The switch is roughly **26% faster**, reproducibly, across repeated runs and both
optimisation levels. This is the opposite of the folklore, which says computed goto wins
because it replicates the indirect branch at the end of every instruction and gives the
predictor per-opcode history instead of one shared branch site.

The honest position is that the folklore describes a real effect that this workload and
this compiler do not exhibit, and I can offer candidate explanations but have not proved
one:

- GCC already lowers the switch to a jump table, so the "interpreted switch" the folklore
  argues against is not what is being compared.
- The switch's table is PC-relative; `kDispatch` is a static array, so each dispatch may
  add a load from memory that the switch does not pay.
- The bench loop is short and highly regular, so a single shared indirect branch is
  already well predicted and there is little history for per-opcode sites to exploit.

What I would do next to settle it: `perf stat` for branch-miss counts on both builds, and
a workload with a far less predictable opcode mix.

The point of the phase is the harness rather than the winner. A number you measured and
can explain the limits of beats a number you repeated from a blog post.

---

### D32. The stack is preallocated with a raw top pointer

`stack_` is reserved once at construction to a fixed 65536 values and never grows.
The loop keeps `Value* top` into that storage.

The fixed size is not laziness — it is what makes the raw pointer safe. A growing
`std::vector` would reallocate and leave `top` dangling mid-run. Growth would also mean a
capacity check with an unpredictable branch on every push.

The only bounds check is on push, against a constant. It is perfectly predicted in every
run that does not actually overflow, so it costs approximately nothing while turning a
stack overflow into a diagnostic instead of memory corruption.

Pops are unchecked. They are safe by construction rather than by testing: the compiler
emits balanced code, and the earlier phases' tests are what enforce that invariant.

---

### D33. `GET_LOCAL` is the payoff for compile-time slot resolution

`stack_base[slot]` — one load at a known offset. No hashing, no string compare, no
pointer chase. The disassembly of any Cairn program contains no variable names at all;
they were resolved away in Phase 3 (D22).

This is the single decision that most separates this VM from a naive one, and it was made
two phases before the loop that benefits from it existed.

---

### D34. Arithmetic overflow is detected, not performed

Signed integer overflow is undefined behaviour in C++. A VM that computes `a + b` and
then checks whether the result "looks wrong" has already executed the UB, and the
optimiser is entitled to assume it never happens and delete the check.

`__builtin_add_overflow` and friends compile to the arithmetic instruction plus a branch
on the processor's overflow flag — the check is essentially free on x86-64, because the
flag is set by the same instruction that does the work.

`INT64_MIN / -1` is handled separately: it is the one signed division that traps on x86
rather than wrapping, so the guard prevents a SIGFPE, not a wrong answer.

---

### D35. I/O goes through injected streams

`VM(std::ostream&, std::istream&)` rather than `std::cout`/`std::cin` directly. This is
what makes the 26 end-to-end tests possible: they compile a program, run it, and assert on
its exact output.

It also keeps `--bench` honest — benchmark runs write to a `std::ostringstream` sink,
because measuring a dispatch loop through a terminal measures the terminal.

---

### D36. Type errors are runtime errors, and that is a choice

`1 + true` compiles cleanly and fails when it executes. Cairn has no type checker.

A static pass over the AST would catch this at compile time, and for a language this
small it would be perhaps a hundred lines. It is not here because it would have to be
written before there was any way to test what it prevents. With Phase 4 done, that pass
now has 15 runtime-error tests describing exactly the cases it should reject at compile
time instead — which is the right order to build them in.

Every type check in the loop is one predictable branch. Removing them entirely is only
sound with a checker in front, which is precisely the trade a statically typed language
makes.

---

### D37. The instruction counter is in the hot loop

`++instructions_` runs on every instruction, so the reported ns/instruction is very
slightly pessimistic. It is a register increment with no dependency on anything the loop
computes, so it is expected to be near-free, but "expected to be" is not "measured".

It is kept because the benchmark needs a denominator, and a benchmark whose denominator is
estimated is worse than one whose numerator is 1% high. Compile with `-DCAIRN_NO_COUNTER`
to check the difference.

---

### D38. No REPL

The brief allowed a REPL or a file runner; this is a file runner.

A REPL would need variable slots to persist across independently compiled chunks, which
means the compiler's `locals_` becoming session state that outlives one `compile()` call.
That is a real design change to Phase 3, not a wrapper around Phase 4, and it buys
nothing this project is trying to demonstrate.

---

## What I would do next

In the order I would actually do it:

1. `perf stat` on both dispatch builds to settle D31 with branch-miss counts.
2. A static type checker over the AST, using the Phase 4 runtime-error tests as its spec.
3. Constant folding, measured against the current baseline (D30 deliberately left this
   out so there would be a baseline).
4. Superinstructions for common pairs — `GET_LOCAL` + `CONST` + `ADD` shows up in every
   loop increment — which is a dispatch-count reduction rather than a dispatch-speed one.
5. Functions and call frames. This is the largest missing language feature and the one
   that would make the stack model genuinely interesting.

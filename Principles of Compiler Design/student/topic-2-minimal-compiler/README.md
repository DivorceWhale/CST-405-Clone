# Topic 2 — Compiler for a Starter Language

**Project 2 · Weeks 2–5 · Sep 14 – Oct 11, 2026**

A complete six-phase compiler for the starter language (integer declarations,
assignment, addition, `print`). It emits MIPS assembly that runs in SPIM, and
rejects wrong programs with messages that name the line and the problem.

---

## Building and running

Requires `flex`, `bison`, `gcc`, `make`, and `spim` (or QtSPIM).

```bash
cd compiler
make                                        # build ./minicompiler (warning-free under -Wall)
make test                                   # compile and run every tests/*.cm
make clean                                  # remove everything make produced

./minicompiler tests/t2_01_basics.cm out.s        # full trace of all six phases
./minicompiler tests/t2_01_basics.cm out.s -t     # ... plus the token stream
./minicompiler tests/t2_01_basics.cm out.s -q     # quiet: errors and summary only
spim -file out.s                                  # run the generated MIPS
```

The compiler exits **0** on success and **1** if any phase reports an error.

## Seeing each phase on its own

| Phase | How to see its output |
|---|---|
| 1. Lexical analysis | `-t` prints every token with its line, column, kind and text |
| 2. Syntax analysis | the trace prints the AST, indented one level per tree depth |
| 3. Semantic analysis | the trace prints the scope stack and each declaration as it is checked |
| 4. Intermediate code | the trace prints the TAC; it is also saved to `out.tac` |
| 5. Optimization | the trace reports each technique's count and the instruction totals; the result is saved to `out.optimized.tac` |
| 6. Code generation | the trace prints the symbol table (each variable's stack slot); the assembly is `out.s` |

## Test suite

Every test states its expected result in its header comment.

| Test | Kind | Expected result |
|---|---|---|
| `t2_01_basics.cm` | runs | prints `5 10 18` |
| `t2_02_chained.cm` | runs | prints `6 21`; the AST shows `+` is left associative |
| `t2_03_comments.cm` | runs | prints `42`; both comment forms are skipped |
| `t2_06_optimizer.cm` | runs | prints `18 0 7 25`; exercises folding, propagation and `x + 0` |
| `t2_11_symbol_tables.cm` | runs | prints `7`; compile without `-q` to trace every interaction with both symbol tables (see `SYMBOL_TABLES.md`) |
| `t2_04_errors_undeclared.cm` | **fails** (semantic) | `'ghost'` undeclared, line 8 |
| `t2_07_errors_semantic_multi.cm` | **fails** (semantic) | three errors in one run: duplicate `x` (line 9), undeclared `totl` with "did you mean 'total'?" (line 11), reserved name `t1` (line 12) |
| `t2_05_errors_syntax.cm` | **fails** (syntax) | missing `;` after the assignment on line 7 |
| `t2_08_errors_multi_syntax.cm` | **fails** (syntax) | two missing `;` in one run, lines 7 and 9 |
| `t2_09_errors_lexical.cm` | **fails** (lexical) | `'@'` at line 9 col 6 and `'$'` at line 10 col 10, both in one run |
| `t2_10_errors_unterminated_comment.cm` | **fails** (lexical) | unterminated comment reported where it starts, line 8 col 11 |

## Error handling

- **Lexical:** each bad character is reported with its line **and column**, and
  scanning continues so one run reports them all. An unterminated `/* ... `
  is reported at the line where it began.
- **Syntax:** bison reports what it expected (`unexpected ID, expecting ';'`),
  and error productions for all three statement forms add the line where the
  `;` was actually missing. Parsing resumes after each error.
- **Semantic:** undeclared names, duplicate declarations, and names reserved
  for compiler temporaries (`t0`, `t1`, ...), each with its line. An undeclared
  name close to a declared one gets a suggestion (`totl` → `total`).

## Optimizations

The optimizer runs repeated passes until a pass changes nothing, then reports
how many times each technique fired and how many instructions were removed.
For example, `t2_02_chained.cm` goes from **19 to 14** TAC instructions, and
every printed value is computed at compile time.

Required:
- **Constant folding:** `t0 = 2 + 3` becomes `t0 = 5`
- **Constant propagation:** after `x = 5`, a later `y = x + 1` becomes `y = 5 + 1`

Also implemented:
- **Dead code elimination** *(optional extension)*: removes computations into
  compiler temporaries that are never read. Only temporaries are removed;
  assignments to user variables are always kept, because once globals and
  calls exist (Topic 3) proving them unobservable needs analysis this pass
  does not do.
- **Copy propagation** and **algebraic simplification** (`x + 0`, `x * 1`,
  `x * 0`, `x - 0`, `x / 1`).

All facts are forgotten at labels and function boundaries, so the optimizer
stays correct when Topic 4 adds loops.

## Known limitations

- `symtab.c` keeps one flat table of locals per function. A future topic that
  allows the same name to be declared in a nested block would need block
  scopes there as well (semantic analysis already handles them).
- In the starter language every value is known at compile time, so after
  optimization the arithmetic instructions in `codegen.c` are rarely reached.
  They were verified by compiling every test with the optimizer disabled and
  checking the SPIM output.

## Response to Project 1 feedback

| Feedback on Project 1 | What changed in Project 2 |
|---|---|
| Only the supplied tests were in the repo | 5 new tests of our own (`t2_06`–`t2_10`), each with its expected result in its header comment; 6 of the 10 tests are supposed to fail, covering lexical, syntax and semantic errors |
| Starter instruction blocks left in the source | All starter TODO / "YOUR TASK" blocks removed; every file's header now states its phase, what it receives, and what it produces; comments explain why decisions were made |
| A compiled binary was committed | Build products (`minicompiler`, `.s`, `.tac`) are in `.gitignore`, and the Topic 1 `lexer` binary was removed from the repository |
| One "Add files via upload" commit | Work committed in separate commits, one per phase |

## Video walkthroughs

| Team member | Video |
|---|---|
| Luke Hoyle | https://youtu.be/VDIfFT5E3AY |
| Fernando | _link to be added_ |

## Team contributions

<!-- Fill in before submitting: who wrote, debugged, and tested each phase. -->

| Phase | Files | Team member |
|---|---|---|
| 1. Lexical analysis | `scanner.l` | |
| 2. Syntax analysis + AST | `parser.y`, `ast.c` | |
| 3. Semantic analysis | `semantic.c`, `symtab.c` | |
| 4–5. TAC + optimization | `tac.c` | |
| 6. Code generation | `codegen.c` | |

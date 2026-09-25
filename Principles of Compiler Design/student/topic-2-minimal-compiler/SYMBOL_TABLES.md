# The Two Symbol Tables in Our Compiler

## 1. Two tables, two questions

The course notes describe two symbol tables that answer different questions,
and warn that conflating them is the most common conceptual error in this
topic. Our compiler follows that design exactly:

| | Scope stack | Storage map |
|---|---|---|
| **File** | `semantic.c` | `symtab.c` |
| **Question it answers** | "Is this name *visible* here?" | "What *address* does this name have?" |
| **Used during** | Phase 3, semantic analysis | Phase 6, code generation |
| **Shape** | A stack of scopes, pushed and popped | One table of locals per function, plus one table of globals |
| **Holds** | Names only | Names, stack offsets, sizes, array and global flags |
| **Lifetime** | Discarded when semantic analysis ends | Rebuilt for every function generated |
| **Trace tag** | `[SCOPE]` | `[STORAGE]` |

The two tables are kept separate because the questions arise at different
times and need different information. When semantic analysis runs, no
variable has an address yet, and it doesn't need one: it only has to decide
whether a program is legal. By the time the code generator runs, every name
is already known to be legal, so the storage map never checks legality. Its
only job is to hand out and report addresses.

A useful one-line summary: **semantic.c decides whether a name is legal;
symtab.c decides where it lives.**

## 2. Where and how each table is implemented

### The scope stack (`semantic.c`)

The table is an array of scopes, each of which is a list of names. The
current depth tells us which scope is innermost.

```c
typedef struct {
    char* names[MAX_VARS];
    int count;
} Scope;

static Scope scopes[MAX_SCOPE_DEPTH];
static int scopeDepth = 0;
```

**Declaring a name** checks only the *current* scope for a duplicate. That is
what allows an inner scope to reuse an outer name (shadowing) without it
counting as an error:

```c
static int addVarToScope(char* name) {
    Scope* currentScope = &scopes[scopeDepth - 1];
    for (int i = 0; i < currentScope->count; i++)
        if (strcmp(currentScope->names[i], name) == 0)
            return -1;                 /* duplicate declaration */
    currentScope->names[currentScope->count++] = strdup(name);
    return 0;
}
```

**Looking up a name** searches from the innermost scope outward and stops at
the first match, so the nearest declaration wins. This is static scoping:

```c
static int isVarDeclaredInScope(char* name) {
    for (int depth = scopeDepth - 1; depth >= 0; depth--)
        for (int i = 0; i < scopes[depth].count; i++)
            if (strcmp(scopes[depth].names[i], name) == 0)
                return 1;             /* visible */
    return 0;                         /* undeclared */
}
```

The analyzer walks the AST in source order. A `DECL` node adds its name to
the stack. An `ASSIGN` node looks up its target and then every variable on
its right-hand side, and a `PRINT` node looks up the variables in its
expression. Because a name is added only when its declaration is reached, a
use that comes before its declaration is correctly reported as undeclared.
Errors are counted rather than fatal, so one run reports every problem.

### The storage map (`symtab.c`)

Each entry records where a name lives at run time:

```c
typedef struct {
    char* name;
    char* type;
    int   offset;      /* locals: byte offset from $sp */
    int   isArray;
    int   arraySize;
    int   isGlobal;    /* 1 = lives in .data, addressed by label */
    int   isParamArray;
} Symbol;
```

There are two tables of these: `locals`, which is cleared at the start of
every function because each call gets a fresh activation record, and
`globals`, which lives in the `.data` section.

**Declaring a local** gives it the next free word of the stack frame, so
offsets are assigned 0, 4, 8, … in declaration order:

```c
int addVar(char* name, char* type) {
    if (findIn(&locals, name)) return -1;
    Symbol* s = appendTo(&locals, name, type);
    s->offset = locals.nextOffset;
    locals.nextOffset += 4;            /* one word per int */
    return s->offset;
}
```

**Looking up a name** searches the local table first and the global table
second. That order is what lets a local declaration hide a global of the same
name:

```c
Symbol* lookupSymbol(const char* name) {
    Symbol* s = findIn(&locals, name);
    if (s) return s;
    return globalsReady ? findIn(&globals, name) : NULL;
}
```

The code generator fills this table in `layoutFrame()` before emitting any
instructions. It walks the function's TAC once, calls `addVar` for every
`DECL` (and for every compiler temporary), and uses the total to size the
activation record. Every later load or store asks `lookupSymbol` for the
name's address.

## 3. The tables in action

The test program is the one from the Topic 2 activity *Predict the Symbol
Table*, saved as `tests/t2_11_symbol_tables.cm`:

```c
int a;          /* line 6  */
int b;          /* line 7  */
int sum;        /* line 8  */
a = 3;          /* line 9  */
b = 4;          /* line 10 */
sum = a + b;    /* line 11 */
print(sum);     /* line 12 */
```

It was compiled with the full trace:

```bash
./minicompiler tests/t2_11_symbol_tables.cm out.s
```

### Phase 3: the scope stack

The global scope is pushed empty. Each declaration adds a name, and each use
looks one up:

```
┌─────────────────────────────────────────────────────────┐
│ SEMANTIC SCOPE STACK (Depth: 1)
├─────────────────────────────────────────────────────────┤
│ Scope[0] GLOBAL (0 variables)
│   (empty)
└─────────────────────────────────────────────────────────┘

  [SCOPE]   declare 'a' (line 6) -> added to scope[0]
  [SCOPE]   declare 'b' (line 7) -> added to scope[0]
  [SCOPE]   declare 'sum' (line 8) -> added to scope[0]
  [SCOPE]   lookup  'a' -> found in scope[0] (searched 1 scope)
  [SCOPE]   lookup  'b' -> found in scope[0] (searched 1 scope)
  [SCOPE]   lookup  'sum' -> found in scope[0] (searched 1 scope)
  [SCOPE]   lookup  'a' -> found in scope[0] (searched 1 scope)
  [SCOPE]   lookup  'b' -> found in scope[0] (searched 1 scope)
  [SCOPE]   lookup  'sum' -> found in scope[0] (searched 1 scope)

Scope stack after analysis (discarded next):
┌─────────────────────────────────────────────────────────┐
│ SEMANTIC SCOPE STACK (Depth: 1)
├─────────────────────────────────────────────────────────┤
│ Scope[0] GLOBAL (3 variables)
│   Variables: a, b, sum
└─────────────────────────────────────────────────────────┘
```

The six lookups come from the statements in order:

- `a = 3` looks up `a`.
- `b = 4` looks up `b`.
- `sum = a + b` looks up `sum`, then `a` and `b`. The target is checked
  before the right-hand side.
- `print(sum)` looks up `sum`.

The table only ever holds names. No addresses exist yet.

### Phase 6: the storage map

The same three names now receive addresses. The table is created fresh for
`main`'s activation record:

```
  [STORAGE] new activation record: local table cleared
  [STORAGE] addVar  'a' -> 0($sp)   (frame now 4 bytes)
  [STORAGE] addVar  'b' -> 4($sp)   (frame now 8 bytes)
  [STORAGE] addVar  'sum' -> 8($sp)   (frame now 12 bytes)

  Activation record for 'main': 24 bytes

  ┌─ SYMBOL TABLE ─────────────────────────────────────────────┐
  │ GLOBALS (.data)                                            │
    (none)
  │ LOCALS (current activation record,  12 bytes)              │
    a              int        0($sp)
    b              int        4($sp)
    sum            int        8($sp)
  └────────────────────────────────────────────────────────────┘

  [STORAGE] lookup  'a' -> 0($sp)   (local)
  [STORAGE] lookup  'b' -> 4($sp)   (local)
  [STORAGE] lookup  'sum' -> 8($sp)   (local)
```

The variables take 12 bytes. The activation record is 24 bytes because it
also holds the saved return address `$ra` and 4 bytes of alignment padding
(20 bytes), rounded up to a multiple of 8.

The code generator records each lookup result in the assembly as a comment:

```
main:
    addi $sp, $sp, -24        # build activation record
    sw   $ra, 20($sp)        # save return address
    # int a lives at 0($sp)
    # int b lives at 4($sp)
    # int sum lives at 8($sp)
```

The program runs in SPIM and prints `7`.

Only three storage lookups happen because the optimizer computed `sum = 7`
at compile time, so no instruction needed to load `a` or `b` from memory.
In a program where values are not known in advance, every load and store
would produce a `[STORAGE] lookup` line.

### When the scope stack rejects a program

Compiling `tests/t2_07_errors_semantic_multi.cm` shows the scope stack
catching two errors:

```
  [SCOPE]   declare 'x' (line 7) -> added to scope[0]
  [SCOPE]   declare 'total' (line 8) -> added to scope[0]
  [SCOPE]   declare 'x' (line 9) -> REFUSED, already in scope[0]
  [SCOPE]   lookup  'total' -> found in scope[0] (searched 1 scope)
  [SCOPE]   lookup  'totl' -> NOT FOUND in any of 1 scope
```

The refused declaration becomes a *duplicate declaration* error on line 9,
and the failed lookup becomes an *undeclared variable* error on line 11 with
the suggestion "did you mean 'total'?". Because semantic analysis failed,
compilation stops, and the storage map is never built for this program.
That is the division of labor in action: illegal programs never reach the
table that assigns addresses.

## 4. Known limitation

The storage map keeps one flat table of locals per function, while the scope
stack supports nested scopes. Topic 2 has only one scope, so this makes no
difference yet. If a later topic allowed a nested block to declare a name
that an enclosing block also declares, the scope stack would correctly treat
them as two variables, but `addVar` would refuse the second one, and both
would share one stack slot. Supporting that would mean giving the storage
map block scopes too.

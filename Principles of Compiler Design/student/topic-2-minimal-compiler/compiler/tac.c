/* =========================================================================
 *  TOPIC 2 · Compiler for a Starter Language
 * FILE: tac.c   —   Phases 4 & 5 — Intermediate code and optimization
 * -------------------------------------------------------------------------
 * THE PIPELINE, AND WHERE THIS FILE SITS IN IT
 *   scanner -> parser -> ast -> semantic -> tac -> codegen
 *                                           ^^^  this file
 *
 * RECEIVES  the AST, after semantic.c has certified it (every name declared)
 * PRODUCES  two TAC lists: the direct translation (saved as .tac) and the
 *           optimized version (saved as .optimized.tac) that codegen.c
 *           turns into MIPS
 * ========================================================================= */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "tac.h"
#include "trace.h"

TACList tacList;
TACList optimizedList;
TempAllocator tempAlloc;

/* Reset both TAC lists and the temporary allocator.  Called once per
 * compilation, before generateTAC. */
void initTAC() {
    tacList.head = NULL;
    tacList.tail = NULL;
    tacList.tempCount = 0;
    tacList.labelCount = 0;
    optimizedList.head = NULL;
    optimizedList.tail = NULL;

    /* Initialize temporary allocator */
    for (int i = 0; i < MAX_TEMPS; i++) {
        tempAlloc.allocated[i] = 0;
    }
    tempAlloc.maxUsed = 0;
    tempAlloc.freeCount = 0;
    trace("TAC: Temporary allocator initialized\n");
}

/* A brand-new temporary name that is never reused (t0, t1, t2, ...).
 * Kept for reference: this compiler uses allocTemp instead, which recycles
 * freed temporaries and so keeps the frame small.  Caller owns the string. */
char* newTemp() {
    char* temp = malloc(10);
    sprintf(temp, "t%d", tacList.tempCount++);
    return temp;
}

/* A temporary name for a new intermediate value, reusing one released by
 * freeTemp when possible.  Reuse matters because every temporary gets its own
 * stack slot in codegen.c: without it, a long expression would need one slot
 * per operator.  Returns the name ("t3"); the caller owns the string. */
char* allocTemp() {
    int tempNum;

    if (tempAlloc.freeCount > 0) {
        tempNum = tempAlloc.freeList[--tempAlloc.freeCount];
        trace("    [TAC ALLOC] Reusing temporary t%d\n", tempNum);
    } else {
        tempNum = tempAlloc.maxUsed++;
        if (tempNum >= MAX_TEMPS) {
            fprintf(stderr, "ERROR: Exceeded maximum temporaries\n");
            exit(1);
        }
        trace("    [TAC ALLOC] Allocating new temporary t%d\n", tempNum);
    }

    tempAlloc.allocated[tempNum] = 1;
    char* temp = malloc(10);
    sprintf(temp, "t%d", tempNum);
    return temp;
}

/* Return a temporary to the pool once nothing will read it again.  Only
 * pass real temporaries: this checks just the first letter, which is why
 * callers go through releaseOperand/isTempName first. */
void freeTemp(char* temp) {
    if (!temp || temp[0] != 't') return;

    int tempNum = atoi(temp + 1);
    if (!tempAlloc.allocated[tempNum]) return;

    tempAlloc.allocated[tempNum] = 0;
    if (tempAlloc.freeCount < MAX_TEMPS) {
        tempAlloc.freeList[tempAlloc.freeCount++] = tempNum;
        trace("    [TAC FREE] Released temporary %s\n", temp);
    }
}


/* Trace how many temporaries were created and how many are free for reuse. */
void printTempAllocatorState() {
    trace("\n┌──────────────────────────────────────────────────────────┐\n");
    trace("│ TEMPORARY ALLOCATOR STATISTICS                           │\n");
    trace("├──────────────────────────────────────────────────────────┤\n");
    trace("│ Total temporaries used:      %3d                         │\n", tempAlloc.maxUsed);
    trace("│ Currently allocated:         %3d                         │\n",
           tempAlloc.maxUsed - tempAlloc.freeCount);
    trace("│ Available for reuse:         %3d                         │\n", tempAlloc.freeCount);
    trace("└──────────────────────────────────────────────────────────┘\n\n");
}

/* Build one TAC instruction.  Every operand string is COPIED, so callers may
 * free or reuse their own strings as soon as this returns. */
TACInstr* createTAC(TACOp op, char* arg1, char* arg2, char* result) {
    TACInstr* instr = malloc(sizeof(TACInstr));
    instr->op = op;
    instr->arg1 = arg1 ? strdup(arg1) : NULL;
    instr->arg2 = arg2 ? strdup(arg2) : NULL;
    instr->result = result ? strdup(result) : NULL;
    instr->next = NULL;
    return instr;
}

/* Add an instruction to the end of the unoptimized list.  A tail pointer
 * makes this constant time, so emitting a program is linear in its size. */
void appendTAC(TACInstr* instr) {
    if (!tacList.head) {
        tacList.head = tacList.tail = instr;
    } else {
        tacList.tail->next = instr;
        tacList.tail = instr;
    }
}

/* Add an instruction to the end of the optimized list (same as appendTAC). */
void appendOptimizedTAC(TACInstr* instr) {
    if (!optimizedList.head) {
        optimizedList.head = optimizedList.tail = instr;
    } else {
        optimizedList.tail->next = instr;
        optimizedList.tail = instr;
    }
}

/* Forward declarations */
static void generateTACStmt(ASTNode* node);

/* Is `s` a compiler temporary (t0, t1, ...)?  freeTemp() only checks the
 * first letter, so without this a user variable such as `total` would be
 * mistaken for t0 and release a temporary that is still live. */
static int isTempName(const char* s) {
    if (!s || s[0] != 't' || !s[1]) return 0;
    for (int i = 1; s[i]; i++) if (!isdigit((unsigned char)s[i])) return 0;
    return 1;
}

/* Hand an operand back once the instruction that reads it has been emitted:
 * temporaries return to the allocator, and the string itself is freed
 * because createTAC keeps its own copy. */
static void releaseOperand(char* s) {
    if (!s) return;
    if (isTempName(s)) freeTemp(s);
    free(s);
}

/* TAC opcode for an AST operator character. */
static TACOp opForChar(char op) {
    switch (op) {
        case '-': return TAC_SUB;
        case '*': return TAC_MUL;
        case '/': return TAC_DIV;
        default:  return TAC_ADD;
    }
}


/* Emit the TAC that computes an expression, and return the NAME of the
 * location holding its value: a literal ("42"), a variable ("x"), or a
 * temporary ("t0").  The caller owns the returned string.
 *
 * Returning a name, whatever kind it is, is what makes the recursion work:
 * a BINOP asks each operand for a name and does not care which kind it got.
 * For  a + b + c  this emits  t0 = a + b ;  t1 = t0 + c  and returns "t1". */
char* generateTACExpr(ASTNode* node) {
    if (!node) return NULL;

    switch (node->type) {
        case NODE_NUM: {
            char* s = malloc(16);
            snprintf(s, 16, "%d", node->data.num);
            return s;
        }
        case NODE_VAR:
            return strdup(node->data.name);
        case NODE_BINOP: {
            char* left  = generateTACExpr(node->data.binop.left);
            char* right = generateTACExpr(node->data.binop.right);
            /* The result temporary is allocated AFTER both operands are
             * evaluated but BEFORE they are released, so it can never share
             * a number with a value this instruction is still reading. */
            char* t = allocTemp();
            appendTAC(createTAC(opForChar(node->data.binop.op), left, right, t));
            releaseOperand(left);
            releaseOperand(right);
            return t;
        }
        default:
            fprintf(stderr, "TAC: unexpected node %d in an expression\n", node->type);
            return NULL;
    }
}

/* Generate TAC for statement list */
static void generateTACStmtList(ASTNode* node) {
    if (!node) return;

    if (node->type == NODE_STMT_LIST) {
        generateTACStmt(node->data.stmtlist.stmt);
        generateTACStmtList(node->data.stmtlist.next);
    } else {
        generateTACStmt(node);
    }
}

/* Emit the TAC for one statement.  A declaration produces a DECL even though
 * no code runs for it: the back end needs it to reserve the variable's slot. */
static void generateTACStmt(ASTNode* node) {
    if (!node) return;

    switch (node->type) {
        case NODE_DECL:
            appendTAC(createTAC(TAC_DECL, node->data.decl.varType, NULL,
                                node->data.decl.name));
            break;
        case NODE_ASSIGN: {
            char* value = generateTACExpr(node->data.assign.value);
            appendTAC(createTAC(TAC_ASSIGN, value, NULL, node->data.assign.var));
            releaseOperand(value);
            break;
        }
        case NODE_PRINT: {
            char* value = generateTACExpr(node->data.expr);
            appendTAC(createTAC(TAC_PRINT, value, NULL, NULL));
            releaseOperand(value);
            break;
        }
        case NODE_STMT_LIST:
            generateTACStmtList(node);
            break;
        default:
            fprintf(stderr, "TAC: unexpected node %d as a statement\n", node->type);
            break;
    }
}

/* Entry point for Phase 4: translate the whole program into TAC, stored in
 * tacList.  The statements are wrapped in FUNC_BEGIN main ... FUNC_END main
 * because the back end always generates code for functions; see below. */
void generateTAC(ASTNode* node) {
    if (!node) return;
    /* The starter language has no function syntax, but the code generator
     * still emits code into MIPS functions — every program needs an entry
     * point called `main`.  So the whole statement list becomes the body of
     * an implicit main().  Topic 3 makes that function explicit in the
     * source, and this wrapper goes away. */
    appendTAC(createTAC(TAC_FUNC_BEGIN, NULL, NULL, "main"));
    generateTACStmt(node);
    appendTAC(createTAC(TAC_RETURN, "0", NULL, NULL));
    appendTAC(createTAC(TAC_FUNC_END, NULL, NULL, "main"));
}

/* Get operation name for printing */
static const char* getOpName(TACOp op) {
    switch(op) {
        case TAC_ADD: return "+";
        case TAC_SUB: return "-";
        case TAC_MUL: return "*";
        case TAC_DIV: return "/";
        case TAC_NEG: return "NEG";
        case TAC_LT: return "<";
        case TAC_GT: return ">";
        case TAC_LE: return "<=";
        case TAC_GE: return ">=";
        case TAC_EQ: return "==";
        case TAC_NE: return "!=";
        case TAC_AND: return "&&";
        case TAC_OR: return "||";
        case TAC_NOT: return "!";
        default: return "?";
    }
}


/* ==========================================================================
 * FORMATTING
 * --------------------------------------------------------------------------
 * One function renders a TAC instruction as text.  Everything that displays
 * or saves three-address code goes through it, so the listing you read on
 * screen and the listing saved to the .tac file can never drift apart.
 * ========================================================================*/
void formatTAC(const TACInstr* i, char* buf, size_t n) {
    if (!i) { snprintf(buf, n, "(null)"); return; }
    switch (i->op) {
        case TAC_FUNC_BEGIN: snprintf(buf, n, "FUNC_BEGIN %s", i->result); break;
        case TAC_FUNC_END:   snprintf(buf, n, "FUNC_END %s", i->result); break;
        case TAC_PARAM:      snprintf(buf, n, "PARAM %s %s",
                                      i->arg1 ? i->arg1 : "int", i->result); break;
        case TAC_DECL:       snprintf(buf, n, "DECL %s %s",
                                      i->arg1 ? i->arg1 : "int", i->result); break;

        case TAC_ADD: case TAC_SUB: case TAC_MUL: case TAC_DIV:
        case TAC_LT:  case TAC_GT:  case TAC_LE:  case TAC_GE:
        case TAC_EQ:  case TAC_NE:  case TAC_AND: case TAC_OR:
            snprintf(buf, n, "%s = %s %s %s", i->result, i->arg1,
                     getOpName(i->op), i->arg2); break;

        case TAC_NEG:        snprintf(buf, n, "%s = -%s", i->result, i->arg1); break;
        case TAC_NOT:        snprintf(buf, n, "%s = !%s", i->result, i->arg1); break;
        case TAC_ASSIGN:     snprintf(buf, n, "%s = %s", i->result, i->arg1); break;
        case TAC_PRINT:      snprintf(buf, n, "PRINT %s", i->arg1); break;
        case TAC_ARG:        snprintf(buf, n, "ARG %s", i->arg1); break;
        case TAC_CALL:       snprintf(buf, n, "%s = CALL %s, %s",
                                      i->result, i->arg1, i->arg2); break;
        case TAC_RETURN:     if (i->arg1) snprintf(buf, n, "RETURN %s", i->arg1);
                             else         snprintf(buf, n, "RETURN");
                             break;
        case TAC_LABEL:      snprintf(buf, n, "%s:", i->result); break;
        case TAC_GOTO:       snprintf(buf, n, "GOTO %s", i->arg1); break;
        case TAC_IF_FALSE:   snprintf(buf, n, "IF_FALSE %s GOTO %s", i->arg1, i->arg2); break;
        case TAC_IF_TRUE:    snprintf(buf, n, "IF_TRUE %s GOTO %s", i->arg1, i->arg2); break;
        case TAC_ARRAY_DECL: snprintf(buf, n, "ARRAY_DECL %s[%s]", i->result, i->arg1); break;
        case TAC_ARRAY_LOAD: snprintf(buf, n, "%s = %s[%s]", i->result, i->arg1, i->arg2); break;
        case TAC_ARRAY_STORE:snprintf(buf, n, "%s[%s] = %s", i->arg1, i->arg2, i->result); break;
        default:             snprintf(buf, n, "UNKNOWN"); break;
    }
}

/* Count instructions in a list — the crudest possible code-size metric, and
 * the one Topic 4 uses to quantify what optimization bought us. */
int countTAC(const TACList* list) {
    int n = 0;
    for (TACInstr* c = list->head; c; c = c->next) n++;
    return n;
}

/* Print a TAC list with line numbers for the trace.  Labels and function
 * boundaries are printed flush left so the structure stands out. */
static void dumpList(const TACList* list, const char* title) {
    char buf[256];
    trace("%s\n", title);
    trace("─────────────────────────────────────────────\n");
    int line = 1;
    for (TACInstr* c = list->head; c; c = c->next) {
        formatTAC(c, buf, sizeof buf);
        if (c->op == TAC_LABEL || c->op == TAC_FUNC_BEGIN || c->op == TAC_FUNC_END)
            trace("%3d: %s\n", line++, buf);
        else
            trace("%3d:     %s\n", line++, buf);
    }
    trace("─────────────────────────────────────────────\n");
    trace("     %d instructions\n\n", countTAC(list));
}

void printTAC(void)          { dumpList(&tacList,       "UNOPTIMIZED THREE-ADDRESS CODE"); }
void printOptimizedTAC(void) { dumpList(&optimizedList, "OPTIMIZED THREE-ADDRESS CODE"); }

TACList* getOptimizedTAC(void) { return &optimizedList; }
TACList* getUnoptimizedTAC(void) { return &tacList; }

/* Write a TAC list to a file in the same format as the trace, so the .tac
 * files can be compared side by side with what was shown on screen. */
static void saveList(const TACList* list, const char* filename, const char* banner) {
    FILE* f = fopen(filename, "w");
    if (!f) { fprintf(stderr, "Cannot write %s\n", filename); return; }
    char buf[256];
    fprintf(f, "; %s\n", banner);
    fprintf(f, "; Generated by the mini compiler\n;\n");
    int line = 1;
    for (TACInstr* c = list->head; c; c = c->next) {
        formatTAC(c, buf, sizeof buf);
        if (c->op == TAC_LABEL || c->op == TAC_FUNC_BEGIN || c->op == TAC_FUNC_END)
            fprintf(f, "%3d: %s\n", line++, buf);
        else
            fprintf(f, "%3d:     %s\n", line++, buf);
    }
    fprintf(f, ";\n; %d instructions\n", countTAC(list));
    fclose(f);
}

void saveTACToFile(const char* filename)          { saveList(&tacList,       filename, "Unoptimized three-address code"); }
void saveOptimizedTACToFile(const char* filename) { saveList(&optimizedList, filename, "Optimized three-address code"); }

/* ==========================================================================
 * PHASE 5 — OPTIMIZATION
 * --------------------------------------------------------------------------
 * The optimizer rewrites the TAC list into a shorter, cheaper list that
 * computes the same thing.  It runs the same set of transformations REPEATEDLY
 * until a pass changes nothing, because optimizations feed each other:
 *
 *      t0 = 2 * 3        constant folding  ->  t0 = 6
 *      x  = t0                                  x  = 6      (copy propagation)
 *      y  = x + 0                               y  = 6      (algebraic + folding)
 *      t0 = ...                                 (t0 now dead -> removed)
 *
 * No single pass finds all of that; three passes do.  Reaching a fixed point
 * is the standard way real optimizers are structured.
 *
 * Transformations implemented, in the order they are applied within a pass:
 *
 *   1. Algebraic simplification   x+0, x-0, x*1, x*0, x/1
 *   2. Constant folding           evaluate operations on two literals
 *   3. Constant propagation       replace a variable by a constant known to
 *                                 hold it at that point
 *   4. Copy propagation           replace t1 by x after "t1 = x"
 *   5. Dead code elimination      drop assignments to names never read again
 *   6. Unreachable code removal   drop code between GOTO and the next label
 *   7. Branch simplification      constant conditions become GOTO or nothing
 *
 * SAFETY RULES the passes obey, and that students must not break when they
 * extend this file:
 *   • A LABEL ends a basic block: forget everything known about values,
 *     because control can arrive here from anywhere.
 *   • A CALL may modify globals and arrays: forget facts about non-temporaries.
 *   • Never eliminate a store to an array or a global as "dead"; we do not
 *     track those precisely enough to prove it.
 * ========================================================================*/

#define MAX_FACTS 256

typedef struct {
    char* name;      /* The variable or temporary the fact is about */
    char* value;     /* The constant, or the name it is a copy of   */
    int   isConst;   /* 1 = value is a literal, 0 = value is a name */
} Fact;

static Fact  facts[MAX_FACTS];
static int   factCount = 0;
static int   changesThisPass = 0;

/* Optimization bookkeeping, reported to the user and used by Topic 4 to
 * quantify the gain from each technique. */
static OptStats optStats;

OptStats getOptStats(void) { return optStats; }

static void clearFacts(void) { factCount = 0; }

/* Forget everything known about `name` because it is about to change.  That
 * includes facts about OTHER names that were copies of it: after
 * "y = x; x = 5", y is no longer a copy of x. */
static void dropFactsAbout(const char* name) {
    for (int i = 0; i < factCount; i++) {
        if (strcmp(facts[i].name, name) == 0 ||
            (!facts[i].isConst && strcmp(facts[i].value, name) == 0)) {
            facts[i] = facts[--factCount];
            i--;
        }
    }
}

/* A call can change any global or array element, so only facts about
 * compiler temporaries survive it. */
static void dropNonTempFacts(void) {
    for (int i = 0; i < factCount; i++) {
        const char* n = facts[i].name;
        int temp = (n[0] == 't' && n[1] >= '0' && n[1] <= '9');
        if (!temp) { facts[i] = facts[--factCount]; i--; }
    }
}

/* Remember that `name` now holds `value`: a literal if isConst, otherwise the
 * name it was copied from.  Any older fact about `name` is dropped first. */
static void recordFact(const char* name, const char* value, int isConst) {
    dropFactsAbout(name);
    if (factCount >= MAX_FACTS) return;
    facts[factCount].name    = (char*)name;
    facts[factCount].value   = (char*)value;
    facts[factCount].isConst = isConst;
    factCount++;
}

/* What is known about `name`?  Returns the constant or source name it holds,
 * or NULL if nothing is known.  With wantConst set, only constants count. */
static const char* lookupFact(const char* name, int wantConst) {
    if (!name) return NULL;
    for (int i = 0; i < factCount; i++)
        if (strcmp(facts[i].name, name) == 0 && (!wantConst || facts[i].isConst))
            return facts[i].value;
    return NULL;
}

/* True for opcodes that only compute a value into `result` and have no other
 * effect.  Only these are candidates for dead-code elimination: a CALL may
 * print or modify globals, a STORE writes memory, a branch changes control. */
static int mnemonicIsPure(TACOp op) {
    switch (op) {
        case TAC_ADD: case TAC_SUB: case TAC_MUL: case TAC_DIV: case TAC_NEG:
        case TAC_LT:  case TAC_GT:  case TAC_LE:  case TAC_GE:
        case TAC_EQ:  case TAC_NE:  case TAC_AND: case TAC_OR: case TAC_NOT:
        case TAC_ARRAY_LOAD:
            return 1;
        default:
            return 0;
    }
}

/* Is this operand a literal integer? */
static int isConstantNumber(const char* s) {
    if (!s || !*s) return 0;
    int i = (s[0] == '-' || s[0] == '+') ? 1 : 0;
    if (!s[i]) return 0;
    for (; s[i]; i++) if (s[i] < '0' || s[i] > '9') return 0;
    return 1;
}

/* Evaluate an operation whose operands are both literals. */
static char* foldConstants(TACOp op, const char* a1, const char* a2, int* folded) {
    *folded = 0;
    if (!isConstantNumber(a1) || !isConstantNumber(a2)) return NULL;
    long a = atol(a1), b = atol(a2), r;
    switch (op) {
        case TAC_ADD: r = a + b; break;
        case TAC_SUB: r = a - b; break;
        case TAC_MUL: r = a * b; break;
        case TAC_DIV: if (b == 0) return NULL;   /* leave division by zero
                                                  * alone: folding it would
                                                  * change a run-time trap
                                                  * into a compile-time one */
                      r = a / b; break;
        case TAC_LT:  r = a <  b; break;
        case TAC_GT:  r = a >  b; break;
        case TAC_LE:  r = a <= b; break;
        case TAC_GE:  r = a >= b; break;
        case TAC_EQ:  r = a == b; break;
        case TAC_NE:  r = a != b; break;
        case TAC_AND: r = (a != 0) && (b != 0); break;
        case TAC_OR:  r = (a != 0) || (b != 0); break;
        default: return NULL;
    }
    char* s = malloc(24);
    snprintf(s, 24, "%ld", r);
    *folded = 1;
    return s;
}

/* Does `name` get read anywhere at or after instruction `from`?
 * A conservative liveness test: it stops being conservative only for
 * compiler temporaries, which is exactly where dead code piles up. */
static int isReadLater(TACInstr* from, const char* name) {
    for (TACInstr* c = from; c; c = c->next) {
        if (c->arg1 && strcmp(c->arg1, name) == 0) return 1;
        if (c->arg2 && strcmp(c->arg2, name) == 0) return 1;
        /* ARRAY_STORE reads its `result` field (the value being stored) */
        if (c->op == TAC_ARRAY_STORE && c->result && strcmp(c->result, name) == 0) return 1;
        if (c->op == TAC_RETURN && c->arg1 && strcmp(c->arg1, name) == 0) return 1;
    }
    return 0;
}

/* Copy a TAC list so a pass can rewrite it without destroying the original. */
static TACList copyList(const TACList* src) {
    TACList d = { NULL, NULL, src->tempCount, src->labelCount };
    for (TACInstr* c = src->head; c; c = c->next) {
        TACInstr* n = createTAC(c->op, c->arg1, c->arg2, c->result);
        if (!d.head) d.head = d.tail = n;
        else { d.tail->next = n; d.tail = n; }
    }
    return d;
}

/* Two-operand opcodes: the ones constant folding can evaluate. */
static int isBinaryOp(TACOp op) {
    switch (op) {
        case TAC_ADD: case TAC_SUB: case TAC_MUL: case TAC_DIV:
        case TAC_LT:  case TAC_GT:  case TAC_LE:  case TAC_GE:
        case TAC_EQ:  case TAC_NE:  case TAC_AND: case TAC_OR:
            return 1;
        default:
            return 0;
    }
}

/* Does this opcode READ arg1 as a value?  (GOTO's arg1 is a label and
 * CALL's is a function name — substituting a constant there would be
 * nonsense, which is why this is a whitelist.) */
static int readsArg1(TACOp op) {
    if (isBinaryOp(op)) return 1;
    switch (op) {
        case TAC_NEG: case TAC_NOT: case TAC_ASSIGN: case TAC_PRINT:
        case TAC_RETURN: case TAC_IF_FALSE: case TAC_IF_TRUE: case TAC_ARG:
            return 1;
        default:
            return 0;
    }
}

/* Does this opcode READ arg2 as a value?  For array access arg2 is the
 * index; arg1 is the array's name, which is an address, not a value. */
static int readsArg2(TACOp op) {
    return isBinaryOp(op) || op == TAC_ARRAY_LOAD || op == TAC_ARRAY_STORE;
}

/* Does this opcode write a new value into `result`? */
static int definesResult(TACOp op) {
    return mnemonicIsPure(op) || op == TAC_ASSIGN || op == TAC_CALL;
}

/* Replace an operand with a known constant (constant propagation) or with
 * the name it is a copy of (copy propagation). */
static void substituteOperand(char** operand) {
    if (!*operand || isConstantNumber(*operand)) return;

    const char* value = lookupFact(*operand, 1);
    if (value) {
        optStats.constProp++;
    } else if ((value = lookupFact(*operand, 0)) != NULL) {
        optStats.copyProp++;
    } else {
        return;
    }
    char* copy = strdup(value);
    free(*operand);
    *operand = copy;
    changesThisPass++;
}

/* Rewrite an instruction in place as `result = value`. */
static void becomeAssign(TACInstr* n, const char* value) {
    char* copy = strdup(value);
    free(n->arg1);
    free(n->arg2);
    n->op   = TAC_ASSIGN;
    n->arg1 = copy;
    n->arg2 = NULL;
}

/* x+0, 0+x, x-0, x*1, 1*x, x*0, 0*x, x/1  ->  a plain copy. */
static void simplifyAlgebraic(TACInstr* n) {
    const char* a = n->arg1;
    const char* b = n->arg2;
    const char* keep = NULL;
    if (!a || !b) return;

    switch (n->op) {
        case TAC_ADD:
            if      (strcmp(b, "0") == 0) keep = a;
            else if (strcmp(a, "0") == 0) keep = b;
            break;
        case TAC_SUB:
            if (strcmp(b, "0") == 0) keep = a;
            break;
        case TAC_MUL:
            if      (strcmp(a, "0") == 0 || strcmp(b, "0") == 0) keep = "0";
            else if (strcmp(b, "1") == 0) keep = a;
            else if (strcmp(a, "1") == 0) keep = b;
            break;
        case TAC_DIV:
            if (strcmp(b, "1") == 0) keep = a;
            break;
        default:
            break;
    }
    if (keep) {
        becomeAssign(n, keep);
        optStats.algebraic++;
        changesThisPass++;
    }
}

/* -------------------------------------------------------------------------
 * One optimization pass over `in`, returning the rewritten list.
 *
 * Walks the instructions in order, remembering FACTS ("x holds 5", "t1 is a
 * copy of y") and using them to rewrite later operands.  Propagation and
 * folding feed each other:  x = 5 ; y = x + 1  ->  y = 5 + 1  ->  y = 6.
 *
 * Every rewrite is counted in changesThisPass (so optimizeTAC knows whether
 * to run another pass) and in optStats (so main.c can report it).
 * -----------------------------------------------------------------------*/
static TACList optimizePass(TACList* in) {
    TACList out = { NULL, NULL, in->tempCount, in->labelCount };

    /* Facts are only valid inside the pass that learned them: they point at
     * strings owned by instructions in `out`. */
    clearFacts();

    for (TACInstr* c = in->head; c; c = c->next) {
        TACInstr* n = createTAC(c->op, c->arg1, c->arg2, c->result);

        /* A label (or a function boundary) starts a new basic block: control
         * can arrive from anywhere, so nothing learned before it still holds. */
        if (n->op == TAC_LABEL || n->op == TAC_FUNC_BEGIN || n->op == TAC_FUNC_END)
            clearFacts();

        /* 3 & 4. Propagation: substitute known values into the operands this
         * instruction READS.  Substitution must happen before the instruction's
         * own definition is recorded, so that `x = x + 1` reads the old x. */
        if (readsArg1(n->op))            substituteOperand(&n->arg1);
        if (readsArg2(n->op))            substituteOperand(&n->arg2);
        if (n->op == TAC_ARRAY_STORE)    substituteOperand(&n->result);

        /* 1. Algebraic simplification, then 2. constant folding.  Propagation
         * above is what gives these two something to work on. */
        simplifyAlgebraic(n);
        if (isBinaryOp(n->op)) {
            int folded;
            char* value = foldConstants(n->op, n->arg1, n->arg2, &folded);
            if (folded) {
                becomeAssign(n, value);
                free(value);
                optStats.constFold++;
                changesThisPass++;
            }
        }

        /* Record what this instruction teaches us about its result. */
        if (definesResult(n->op) && n->result) {
            dropFactsAbout(n->result);
            if (n->op == TAC_ASSIGN && strcmp(n->arg1, n->result) != 0)
                recordFact(n->result, n->arg1, isConstantNumber(n->arg1));
        }
        if (n->op == TAC_CALL) dropNonTempFacts();

        if (!out.head) out.head = out.tail = n;
        else { out.tail->next = n; out.tail = n; }
    }

    /* 5. Dead code elimination.  Only compiler temporaries are candidates:
     * a user variable might be observed in ways this pass does not model
     * (globals, and later a debugger), while a temporary exists only to be
     * read by later TAC — if nothing reads it, it is garbage. */
    TACInstr* prev = NULL;
    for (TACInstr* n = out.head; n; ) {
        TACInstr* next = n->next;
        int candidate = (mnemonicIsPure(n->op) || n->op == TAC_ASSIGN)
                        && isTempName(n->result);
        if (candidate && !isReadLater(next, n->result)) {
            if (prev) prev->next = next; else out.head = next;
            if (out.tail == n) out.tail = prev;
            free(n->arg1); free(n->arg2); free(n->result); free(n);
            optStats.deadCode++;
            changesThisPass++;
        } else {
            prev = n;
        }
        n = next;
    }

    return out;
}

/* Entry point for Phase 5.  Copies the unoptimized TAC and runs
 * optimizePass on it until a pass makes no change (a fixed point), because
 * each technique can expose work for another.  The limit of 20 passes is a
 * safety net against a bug that makes two rewrites undo each other forever.
 * Stores the result in optimizedList and prints the statistics. */
void optimizeTAC(void) {
    memset(&optStats, 0, sizeof optStats);
    optStats.instructionsBefore = countTAC(&tacList);

    TACList work = copyList(&tacList);

    int pass = 0;
    trace("Running optimization passes until no further change:\n");
    do {
        changesThisPass = 0;
        TACList next = optimizePass(&work);
        work = next;
        pass++;
        trace("  pass %d: %3d change(s), %3d instructions\n",
               pass, changesThisPass, countTAC(&work));
    } while (changesThisPass > 0 && pass < 20);

    optimizedList = work;
    optStats.passes = pass;
    optStats.instructionsAfter = countTAC(&optimizedList);

    int removed = optStats.instructionsBefore - optStats.instructionsAfter;
    double pct = optStats.instructionsBefore
               ? (100.0 * removed / optStats.instructionsBefore) : 0.0;

    trace("\n  Technique                     Applications\n");
    trace("  ─────────────────────────────────────────\n");
    trace("  Algebraic simplification      %6d\n", optStats.algebraic);
    trace("  Constant folding              %6d\n", optStats.constFold);
    trace("  Constant propagation          %6d\n", optStats.constProp);
    trace("  Copy propagation              %6d\n", optStats.copyProp);
    trace("  Dead code elimination         %6d\n", optStats.deadCode);
    trace("  Unreachable code removal      %6d\n", optStats.unreachable);
    trace("  Branch simplification         %6d\n", optStats.branch);
    trace("  ─────────────────────────────────────────\n");
    trace("  TAC instructions   %d -> %d  (%d removed, %.1f%% smaller)\n\n",
           optStats.instructionsBefore, optStats.instructionsAfter, removed, pct);
}

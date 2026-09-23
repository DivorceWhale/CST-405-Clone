/* =========================================================================
 *  TOPIC 2 · Compiler for a Starter Language
 * FILE: symtab.c   —   Phases 3 & 6 — Symbol table / storage map
 * -------------------------------------------------------------------------
 * THE PIPELINE, AND WHERE THIS FILE SITS IN IT
 *   scanner -> parser -> ast -> semantic -> tac -> codegen
 *
 * RECEIVES  the DECL instructions of a function, from codegen.c's frame
 *           layout pass
 * PRODUCES  the storage map: for every declared name, its home in memory
 *           (a byte offset from $sp, or a .data label for globals), which
 *           codegen.c consults for every load and store
 * NOTE      this table only assigns storage; legality of names is checked
 *           earlier, in semantic.c
 * ========================================================================= */

/* ============================================================================
 * SYMBOL TABLE IMPLEMENTATION  —  the storage map
 * ----------------------------------------------------------------------------
 * Every identifier the code generator meets has to be turned into an address.
 * This file is the only place that decides what that address is.
 *
 *   int total;          (at file scope)  ->  label  total   in .data
 *   int i;              (inside main)    ->  0($sp)
 *   int scores[10];     (inside main)    ->  4($sp) .. 40($sp)   (40 bytes)
 *   int sum(int a[])    (parameter)      ->  0($sp) holds a POINTER to a[0]
 *
 * The last line is the one students trip over: an array parameter does not
 * copy the array. It receives the caller's base address, so the callee's slot
 * is one word wide no matter how large the array is. That is why
 * `isParamArray` exists and why addArrayParam reserves 4 bytes, not size*4.
 * ==========================================================================*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "symtab.h"
#include "trace.h"

/* The current function's activation record, and the program's globals. */
static SymbolTable locals;
static SymbolTable globals;

/* Set to 1 once initGlobalScope has run, so a stray lookup before that is
 * reported clearly instead of reading uninitialized memory. */
static int globalsReady = 0;

/* --------------------------------------------------------------------------
 * Internal: find a name in one specific table.
 * -------------------------------------------------------------------------*/
static Symbol* findIn(SymbolTable* t, const char* name) {
    for (int i = 0; i < t->count; i++) {
        if (strcmp(t->vars[i].name, name) == 0) return &t->vars[i];
    }
    return NULL;
}

/* --------------------------------------------------------------------------
 * Internal: append a symbol to a table, or return NULL if the table is full.
 * -------------------------------------------------------------------------*/
static Symbol* appendTo(SymbolTable* t, const char* name, const char* type) {
    if (t->count >= MAX_VARS) {
        fprintf(stderr,
                "Symbol table overflow: more than %d names in one scope "
                "(while declaring '%s')\n", MAX_VARS, name);
        exit(1);
    }
    Symbol* s      = &t->vars[t->count++];
    s->name        = strdup(name);
    s->type        = strdup(type ? type : "int");
    s->offset      = 0;
    s->isArray     = 0;
    s->arraySize   = 0;
    s->isGlobal    = 0;
    s->isParamArray= 0;
    return s;
}

/* ==========================================================================
 * LOCAL SCOPE — one activation record
 * ========================================================================*/

void initSymTab(void) {
    locals.count      = 0;
    locals.nextOffset = 0;   /* Offsets grow upward from $sp */
}

/* Give a local scalar the next free word in the frame.  Returns its byte
 * offset from $sp (0, 4, 8, ... in declaration order), or -1 if the name is
 * already in this frame.  semantic.c has already rejected duplicates, so -1
 * is a safety net, not the way duplicates are reported. */
int addVar(char* name, char* type) {
    if (findIn(&locals, name)) return -1;

    Symbol* s = appendTo(&locals, name, type);
    s->offset = locals.nextOffset;
    locals.nextOffset += 4;                        /* one word per int      */
    return s->offset;
}

/* Declare a local array of `size` ints.  The elements are laid out
 * contiguously in the frame, so the array takes size*4 bytes starting at the
 * returned offset (element i lives at offset + 4*i).  Returns the offset of
 * element 0, or -1 if the name is already declared in this frame.
 * Not reachable from Topic 2 source (the language has no arrays yet); it is
 * here so the storage map does not change shape in Topic 3. */
int addArray(char* name, int size) {
    if (findIn(&locals, name)) return -1;
    if (size <= 0) size = 1;                       /* Defensive: never 0    */

    Symbol* s    = appendTo(&locals, name, "int");
    s->offset    = locals.nextOffset;
    s->isArray   = 1;
    s->arraySize = size;
    locals.nextOffset += size * 4;                 /* size words, contiguous */
    return s->offset;
}

/* Declare an array PARAMETER.  Arrays are passed by reference, so the slot
 * holds the caller's base ADDRESS, not the elements: one word, whatever the
 * array's length.  Returns the slot's offset, or -1 if already declared. */
int addArrayParam(char* name) {
    if (findIn(&locals, name)) return -1;

    Symbol* s        = appendTo(&locals, name, "int");
    s->offset        = locals.nextOffset;
    s->isArray       = 1;
    s->arraySize     = 0;      /* Size is unknown to the callee — by design */
    s->isParamArray  = 1;      /* Slot holds an ADDRESS, not the elements   */
    locals.nextOffset += 4;    /* A pointer is one word, whatever it points to */
    return s->offset;
}

/* Bytes of frame space handed out so far in the current function: the sum
 * of every local's slot.  codegen.c adds room for the saved return address
 * and rounds up to size the whole activation record. */
int getLocalBytes(void) {
    return locals.nextOffset;
}

/* ==========================================================================
 * GLOBAL SCOPE — the .data section
 * ========================================================================*/

void initGlobalScope(void) {
    globals.count      = 0;
    globals.nextOffset = 0;    /* Globals are label-addressed; no offsets */
    globalsReady       = 1;
}

/* Declare a global scalar.  Globals live in the .data section and are
 * addressed by label, so no offset is assigned.  Returns 0 on success, or -1
 * if a global of that name already exists. */
int addGlobalVar(char* name, char* type) {
    if (findIn(&globals, name)) return -1;
    Symbol* s   = appendTo(&globals, name, type);
    s->isGlobal = 1;
    return 0;
}

/* Declare a global array of `size` ints in the .data section.  Returns 0 on
 * success, or -1 if a global of that name already exists. */
int addGlobalArray(char* name, int size) {
    if (findIn(&globals, name)) return -1;
    if (size <= 0) size = 1;
    Symbol* s    = appendTo(&globals, name, "int");
    s->isGlobal  = 1;
    s->isArray   = 1;
    s->arraySize = size;
    return 0;
}

/* ==========================================================================
 * LOOKUP — locals shadow globals, exactly as the language rules require
 * ========================================================================*/

/* Searching locals before globals is the whole implementation of scoping
 * here: it is what lets a local declaration hide a global of the same name.
 *
 * KNOWN LIMITATION: there is one flat local table per function, with no
 * nested block scopes.  If a later topic allows `int i;` inside a nested
 * block while an outer `i` exists, addVar will refuse the second one and
 * both will share the outer slot, even though semantic.c accepts it. */
Symbol* lookupSymbol(const char* name) {
    Symbol* s = findIn(&locals, name);
    if (s) return s;
    /* Before initGlobalScope has run the global table holds garbage, so
     * treat it as empty rather than searching it. */
    if (!globalsReady) return NULL;
    return findIn(&globals, name);
}

/* Frame offset of a LOCAL name, or -1 if the name is global (globals have a
 * label, not an offset) or not declared at all. */
int getVarOffset(char* name) {
    Symbol* s = lookupSymbol(name);
    if (!s || s->isGlobal) return -1;             /* Globals have no offset */
    return s->offset;
}

/* Element count of an array, or -1 if the name is not an array.  Returns 0
 * for an array parameter, whose length the callee cannot know. */
int getArraySize(char* name) {
    Symbol* s = lookupSymbol(name);
    if (!s || !s->isArray) return -1;
    return s->arraySize;
}

/* 1 if the name has storage in the local or global table, 0 otherwise. */
int isVarDeclared(char* name) {
    return lookupSymbol(name) != NULL;
}

/* 1 if the name resolves to an array (local, global or parameter). */
int isArray(char* name) {
    Symbol* s = lookupSymbol(name);
    return s && s->isArray;
}

/* 1 if the name resolves to a global.  A local of the same name hides the
 * global, so this answers for whichever declaration is actually visible. */
int isGlobalSymbol(char* name) {
    Symbol* s = lookupSymbol(name);
    return s && s->isGlobal;
}

/* ==========================================================================
 * TRACING — shown while compiling (pass -q to silence it)
 * ========================================================================*/

static void printOne(const Symbol* s) {
    if (s->isParamArray) {
        trace("    %-14s int[]   %4d($sp)   (by reference: slot holds base address)\n",
               s->name, s->offset);
    } else if (s->isArray && s->isGlobal) {
        trace("    %-14s int[%d]%*s .data label   (%d bytes)\n",
               s->name, s->arraySize, 6, "", s->arraySize * 4);
    } else if (s->isArray) {
        trace("    %-14s int[%d]%*s %4d($sp)   (%d bytes)\n",
               s->name, s->arraySize, 6, "", s->offset, s->arraySize * 4);
    } else if (s->isGlobal) {
        trace("    %-14s %-7s .data label\n", s->name, s->type);
    } else {
        trace("    %-14s %-7s %4d($sp)\n", s->name, s->type, s->offset);
    }
}

/* Print both tables with every name's storage location.  codegen.c calls
 * this once per function, after the frame has been laid out, which is what
 * makes the symbol table viewable as its own phase output in the trace. */
void printSymTab(void) {
    trace("\n  ┌─ SYMBOL TABLE ─────────────────────────────────────────────┐\n");

    trace("  │ GLOBALS (.data)                                            │\n");
    if (globals.count == 0) {
        trace("    (none)\n");
    } else {
        for (int i = 0; i < globals.count; i++) printOne(&globals.vars[i]);
    }

    trace("  │ LOCALS (current activation record, %3d bytes)              │\n",
           locals.nextOffset);
    if (locals.count == 0) {
        trace("    (none)\n");
    } else {
        for (int i = 0; i < locals.count; i++) printOne(&locals.vars[i]);
    }
    trace("  └────────────────────────────────────────────────────────────┘\n\n");
}

/* =========================================================================
 *  TOPIC 2 · Compiler for a Starter Language
 * FILE: semantic.c   —   Phase 3 — Semantic analysis
 * -------------------------------------------------------------------------
 * THE PIPELINE, AND WHERE THIS FILE SITS IN IT
 *   scanner -> parser -> ast -> semantic -> tac -> codegen
 *                               ^^^^^^^^  this file
 *
 * RECEIVES  the AST from the parser
 * PRODUCES  a verdict: 0 when every name is declared exactly once and before
 *           use, so tac.c may proceed; otherwise every error is reported
 *           with its line and name, and main.c stops compilation
 * ========================================================================= */

/* SEMANTIC ANALYSIS IMPLEMENTATION
 * Walks the AST once, keeping a stack of scopes (only the global scope is
 * used in Topic 2).  This stack answers "is this name visible here?" and is
 * thrown away afterwards; where a name LIVES is symtab.c's job. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "semantic.h"
#include "symtab.h"
#include "trace.h"

#define MAX_FUNCTIONS 100
#define MAX_PARAMS 20
#define MAX_SCOPE_DEPTH 10


/* Scope for variables */
typedef struct {
    char* names[MAX_VARS];
    int count;
} Scope;

/* Global semantic information */
static SemanticInfo semInfo;
static Scope scopes[MAX_SCOPE_DEPTH];
static int scopeDepth = 0;

/* Initialize semantic analyzer */
void initSemantic() {
    semInfo.errorCount = 0;
    semInfo.warningCount = 0;
    scopeDepth = 0;

    trace("SEMANTIC ANALYZER: initialized\n\n");
}

/* Scope management */
static void enterScope() {
    if (scopeDepth >= MAX_SCOPE_DEPTH) {
        fprintf(stderr, "SEMANTIC ERROR: Maximum scope depth exceeded\n");
        semInfo.errorCount++;
        return;
    }
    scopes[scopeDepth].count = 0;
    scopeDepth++;
}

/* Leave the innermost scope, freeing the names declared in it: once a scope
 * closes, its names must stop being visible to the checks that follow. */
static void exitScope() {
    if (scopeDepth > 0) {
        /* Free variable names in this scope */
        for (int i = 0; i < scopes[scopeDepth - 1].count; i++) {
            free(scopes[scopeDepth - 1].names[i]);
        }
        scopeDepth--;
    }
}

/* Print current semantic scopes for debugging */
static void printSemanticScopes() {
    trace("\n┌─────────────────────────────────────────────────────────┐\n");
    trace("│ SEMANTIC SCOPE STACK (Depth: %d)                        \n", scopeDepth);
    trace("├─────────────────────────────────────────────────────────┤\n");

    if (scopeDepth == 0) {
        trace("│ (no active scopes)                                      │\n");
    } else {
        for (int depth = 0; depth < scopeDepth; depth++) {
            if (depth == 0) {
                trace("│ Scope[%d] GLOBAL (%d variables)                        \n", depth, scopes[depth].count);
            } else {
                trace("│ Scope[%d] LOCAL (%d variables)                         \n", depth, scopes[depth].count);
            }

            if (scopes[depth].count > 0) {
                trace("│   Variables: ");
                for (int i = 0; i < scopes[depth].count; i++) {
                    trace("%s", scopes[depth].names[i]);
                    if (i < scopes[depth].count - 1) trace(", ");
                }
                trace("\n");
            } else {
                trace("│   (empty)\n");
            }
        }
    }
    trace("└─────────────────────────────────────────────────────────┘\n\n");
}

/* Add variable to current scope */
/* RESERVED IDENTIFIERS
 * The intermediate-code generator invents names of its own: temporaries
 * t0, t1, ... and labels L0, L1, ...  If a user variable had one of those
 * shapes the back end could not tell them apart, so the language reserves
 * that namespace.  Reporting it here — in the semantic analyzer, with a line
 * number — is far kinder than a mysterious wrong answer at run time. */
static int isReservedName(const char* name) {
    if (!name || !name[0]) return 0;
    if (strncmp(name, "__sw", 4) == 0) return 1;          /* switch temporaries */
    if (name[0] != 't' && name[0] != 'L') return 0;
    if (!name[1]) return 0;
    for (int i = 1; name[i]; i++)
        if (name[i] < '0' || name[i] > '9') return 0;
    return 1;
}

/* Print the error for a variable named like a compiler temporary or label.
 * The caller counts the error; this only formats the message. */
static void reportReserved(const char* name, int lineno) {
    fprintf(stderr, "\n╔════════════════════════════════════════════════════════════╗\n");
    fprintf(stderr, "║ SEMANTIC ERROR - Reserved Identifier                      ║\n");
    fprintf(stderr, "╚════════════════════════════════════════════════════════════╝\n");
    fprintf(stderr, "  📍 Location: Line %d\n", lineno);
    fprintf(stderr, "  ❌ Error: '%s' is reserved for the compiler's own use\n", name);
    fprintf(stderr, "  📖 Note: names of the form t0, t1, ... are three-address-code\n");
    fprintf(stderr, "           temporaries and L0, L1, ... are generated labels.\n");
    fprintf(stderr, "  💡 Suggestion: rename the variable, for example '%s_' or 'total'\n\n", name);
}

/* Record `name` as declared in the innermost scope.  Returns 0 on success,
 * or -1 if the same scope already declares it: a duplicate declaration.
 * Only the CURRENT scope is searched, so an inner scope may reuse an outer
 * name (shadowing) without that counting as a duplicate. */
static int addVarToScope(char* name) {
    if (scopeDepth == 0) {
        fprintf(stderr, "SEMANTIC ERROR: No scope to add variable to\n");
        return -1;
    }

    Scope* currentScope = &scopes[scopeDepth - 1];

    /* Check if already declared in current scope */
    for (int i = 0; i < currentScope->count; i++) {
        if (strcmp(currentScope->names[i], name) == 0) {
            return -1;  /* Already declared in this scope */
        }
    }

    /* Add to current scope */
    if (currentScope->count >= MAX_VARS) {
        fprintf(stderr, "SEMANTIC ERROR: Too many variables in scope\n");
        return -1;
    }

    currentScope->names[currentScope->count] = strdup(name);
    currentScope->count++;
    return 0;
}

/* Check if variable is declared in any visible scope */
static int isVarDeclaredInScope(char* name) {
    /* Search from innermost to outermost scope */
    for (int depth = scopeDepth - 1; depth >= 0; depth--) {
        for (int i = 0; i < scopes[depth].count; i++) {
            if (strcmp(scopes[depth].names[i], name) == 0) {
                return 1;
            }
        }
    }
    return 0;
}

/* Edit distance between two names (insertions, deletions, substitutions).
 * Used only to suggest a correction for an undeclared identifier. */
static int editDistance(const char* a, const char* b) {
    int la = (int)strlen(a), lb = (int)strlen(b);
    if (la > 63 || lb > 63) return 99;
    int d[64][64];
    for (int i = 0; i <= la; i++) d[i][0] = i;
    for (int j = 0; j <= lb; j++) d[0][j] = j;
    for (int i = 1; i <= la; i++) {
        for (int j = 1; j <= lb; j++) {
            int best = d[i - 1][j - 1] + (a[i - 1] != b[j - 1]);
            if (d[i - 1][j] + 1 < best) best = d[i - 1][j] + 1;
            if (d[i][j - 1] + 1 < best) best = d[i][j - 1] + 1;
            d[i][j] = best;
        }
    }
    return d[la][lb];
}

/* The visible name closest to `name`, or NULL if nothing is close enough
 * to be a plausible typo (distance 2 or less, and less than the name's own
 * length so that `x` is never "corrected" to `y`). */
static const char* closestVisibleName(const char* name) {
    const char* best = NULL;
    int bestDist = 3;
    for (int depth = scopeDepth - 1; depth >= 0; depth--) {
        for (int i = 0; i < scopes[depth].count; i++) {
            int dist = editDistance(name, scopes[depth].names[i]);
            if (dist < bestDist && dist < (int)strlen(name)) {
                bestDist = dist;
                best = scopes[depth].names[i];
            }
        }
    }
    return best;
}

/* Report a use of a name that is not declared.  `context` says what the
 * program was trying to do with it ("used", "assigned to"). */
static void reportUndeclared(const char* name, int lineno, const char* context) {
    const char* guess = closestVisibleName(name);
    fprintf(stderr, "\n╔════════════════════════════════════════════════════════════╗\n");
    fprintf(stderr, "║ SEMANTIC ERROR - Undeclared Variable                       ║\n");
    fprintf(stderr, "╚════════════════════════════════════════════════════════════╝\n");
    fprintf(stderr, "  📍 Location: Line %d\n", lineno);
    fprintf(stderr, "  ❌ Error: '%s' is %s but was never declared\n", name, context);
    if (guess)
        fprintf(stderr, "  💡 Suggestion: did you mean '%s'?\n\n", guess);
    else
        fprintf(stderr, "  💡 Suggestion: declare it first, e.g.  int %s;\n\n", name);
    semInfo.errorCount++;
}

/* Forward declaration: checkStmt and checkStmtList are mutually recursive,
 * which is exactly what you want when the thing being checked is a tree. */
static void checkStmtList(ASTNode* node);

/* Check an expression: every variable it reads must be declared and visible.
 * Errors are counted, not fatal, so one run reports every problem in the
 * program instead of making the programmer fix them one compile at a time. */
static void checkExpr(ASTNode* node) {
    if (!node) return;

    switch (node->type) {
        case NODE_NUM:
            break;
        case NODE_VAR:
            if (!isVarDeclaredInScope(node->data.name))
                reportUndeclared(node->data.name, node->lineno, "used");
            break;
        case NODE_BINOP:
            checkExpr(node->data.binop.left);
            checkExpr(node->data.binop.right);
            break;
        default:
            fprintf(stderr, "SEMANTIC ERROR (line %d): unexpected node %d in an expression\n",
                    node->lineno, node->type);
            semInfo.errorCount++;
            break;
    }
}

/* Check one statement.  A declaration adds its name to the current scope
 * only when it is reached, so statements are checked in source order and a
 * use before the declaration is reported as undeclared — the same rule as C. */
static void checkStmt(ASTNode* node) {
    if (!node) return;

    switch (node->type) {
        case NODE_DECL: {
            char* name = node->data.decl.name;
            if (isReservedName(name)) {
                reportReserved(name, node->lineno);
                semInfo.errorCount++;
                break;
            }
            if (addVarToScope(name) != 0) {
                fprintf(stderr, "\n╔════════════════════════════════════════════════════════════╗\n");
                fprintf(stderr, "║ SEMANTIC ERROR - Duplicate Declaration                     ║\n");
                fprintf(stderr, "╚════════════════════════════════════════════════════════════╝\n");
                fprintf(stderr, "  📍 Location: Line %d\n", node->lineno);
                fprintf(stderr, "  ❌ Error: '%s' is already declared in this scope\n", name);
                fprintf(stderr, "  💡 Suggestion: remove this declaration, or pick a new name\n\n");
                semInfo.errorCount++;
            } else {
                trace("  line %d: declared '%s'\n", node->lineno, name);
            }
            break;
        }
        case NODE_ASSIGN:
            /* A declaration is its own statement, so by the time we reach any
             * assignment its target is either already in scope or never will
             * be.  Checking the target before the right-hand side means an
             * error in both is reported in left-to-right source order. */
            if (!isVarDeclaredInScope(node->data.assign.var))
                reportUndeclared(node->data.assign.var, node->lineno, "assigned to");
            checkExpr(node->data.assign.value);
            break;
        case NODE_PRINT:
            checkExpr(node->data.expr);
            break;
        case NODE_STMT_LIST:
            checkStmtList(node);
            break;
        default:
            fprintf(stderr, "SEMANTIC ERROR (line %d): unexpected node %d as a statement\n",
                    node->lineno, node->type);
            semInfo.errorCount++;
            break;
    }
}

/* Check statement list */
static void checkStmtList(ASTNode* node) {
    if (!node) return;

    if (node->type == NODE_STMT_LIST) {
        checkStmt(node->data.stmtlist.stmt);
        checkStmtList(node->data.stmtlist.next);
    } else {
        checkStmt(node);
    }
}

/* Entry point for Phase 3.  Opens the global scope, checks every statement
 * in source order, then closes the scope.  Returns 0 if the program is
 * semantically valid, or -1 if any error was reported, which tells main.c
 * to stop before generating code for a program that means nothing. */
int performSemanticAnalysis(ASTNode* root) {
    if (!root) {
        fprintf(stderr, "SEMANTIC ERROR: No AST to analyze\n");
        return -1;
    }

    trace("Running semantic analysis...\n\n");

    /* Enter global scope */
    enterScope();
    trace("Entered global scope\n");
    printSemanticScopes();

    /* The starter language has no functions, so one walk over the statement
     * list is the whole analysis.  Topic 3 replaces this with two passes. */
    checkStmtList(root);

    /* Exit global scope */
    exitScope();

    return semInfo.errorCount > 0 ? -1 : 0;
}

/* Print semantic analysis summary */
void printSemanticSummary() {
    trace("═══════════════════════════════════════════\n");
    trace("SEMANTIC ANALYSIS SUMMARY\n");
    trace("═══════════════════════════════════════════\n");
    trace("Errors found:       %d\n", semInfo.errorCount);
    trace("Warnings found:     %d\n", semInfo.warningCount);
    trace("\n");

    if (semInfo.errorCount == 0) {
        trace("✓ Semantic analysis passed - program is semantically correct!\n");
    } else {
        trace("✗ Semantic analysis failed - fix errors before proceeding\n");
    }
    trace("═══════════════════════════════════════════\n\n");
}

/* =========================================================================
 *  TOPIC 2 · Compiler for a Starter Language
 * FILE: ast.c   —   Phase 2 — Syntax analysis (the tree it builds)
 * -------------------------------------------------------------------------
 * THE PIPELINE, AND WHERE THIS FILE SITS IN IT
 *   scanner -> parser -> ast -> semantic -> tac -> codegen
 *                        ^^^  this file
 *
 * RECEIVES  one call per grammar rule the parser reduces (parser.y actions)
 * PRODUCES  the abstract syntax tree rooted at `root`, which semantic.c
 *           checks and tac.c translates; printAST shows it in the trace
 * ========================================================================= */

/* AST IMPLEMENTATION
 * Functions to create and manipulate Abstract Syntax Tree nodes
 * The AST is built during parsing and used for all subsequent phases
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ast.h"

/* External line number from scanner */
extern int yylineno;

/* Create a number literal node */
ASTNode* createNum(int value) {
    ASTNode* node = malloc(sizeof(ASTNode));
    node->type = NODE_NUM;
    node->lineno = yylineno;
    node->data.num = value;  /* Store the integer value */
    return node;
}

/* Create a variable reference node */
ASTNode* createVar(char* name) {
    ASTNode* node = malloc(sizeof(ASTNode));
    node->type = NODE_VAR;
    node->lineno = yylineno;
    node->data.name = strdup(name);  /* Copy the variable name */
    return node;
}

/* Create a binary operation node.  `op` is the operator character ('+'). */
ASTNode* createBinOp(char op, ASTNode* left, ASTNode* right) {
    ASTNode* node = malloc(sizeof(ASTNode));
    node->type = NODE_BINOP;
    node->lineno = yylineno;
    node->data.binop.op = op;
    node->data.binop.left = left;
    node->data.binop.right = right;
    return node;
}

/* Create a declaration node: `int name;` */
ASTNode* createDecl(char* type, char* name) {
    ASTNode* node = malloc(sizeof(ASTNode));
    node->type = NODE_DECL;
    node->lineno = yylineno;
    node->data.decl.varType = strdup(type);
    node->data.decl.name = strdup(name);
    return node;
}

/* Create an assignment node: `var = value;` */
ASTNode* createAssign(char* var, ASTNode* value) {
    ASTNode* node = malloc(sizeof(ASTNode));
    node->type = NODE_ASSIGN;
    node->lineno = yylineno;
    node->data.assign.var = strdup(var);
    node->data.assign.value = value;
    return node;
}

/* Create a print node: `print(expr);` */
ASTNode* createPrint(ASTNode* expr) {
    ASTNode* node = malloc(sizeof(ASTNode));
    node->type = NODE_PRINT;
    node->lineno = yylineno;
    node->data.expr = expr;
    return node;
}

/* Link two statements into a list.
 * The grammar is left recursive (stmt_list -> stmt_list stmt), so stmt1 is
 * the list built so far and stmt2 is the statement just parsed.  We store
 * them in that order rather than appending stmt2 to the tail: appending
 * would walk the whole list on every statement (quadratic), while this is
 * constant time.  The cost is that the tree leans LEFT, so every walker
 * (semantic.c, tac.c, printAST) must treat a NODE_STMT_LIST found in the
 * `stmt` slot as "recurse into it first" — which keeps source order. */
ASTNode* createStmtList(ASTNode* stmt1, ASTNode* stmt2) {
    ASTNode* node = malloc(sizeof(ASTNode));
    node->type = NODE_STMT_LIST;
    node->lineno = yylineno;
    node->data.stmtlist.stmt = stmt1;
    node->data.stmtlist.next = stmt2;
    return node;
}

/* Text form of an operator code.  Operators are stored as a single char so
 * the AST node stays small; this turns one back into something printable. */
const char* opText(char op) {
    switch (op) {
        case '+': return "+";
        case '-': return "-";
        case '*': return "*";
        case '/': return "/";
        default:  return "?";
    }
}

/* Display the AST structure (for debugging and education) */
void printAST(ASTNode* node, int level) {
    if (!node) return;

    /* A statement list is a sequence, not a nesting: print both halves at
     * the caller's level, and do it before indenting, because the list node
     * itself gets no line of its own. */
    if (node->type == NODE_STMT_LIST) {
        printAST(node->data.stmtlist.stmt, level);
        printAST(node->data.stmtlist.next, level);
        return;
    }

    for (int i = 0; i < level; i++) printf("  ");

    switch (node->type) {
        case NODE_NUM:
            printf("NUM %d\n", node->data.num);
            break;
        case NODE_VAR:
            printf("VAR %s\n", node->data.name);
            break;
        case NODE_BINOP:
            printf("BINOP %s\n", opText(node->data.binop.op));
            printAST(node->data.binop.left, level + 1);
            printAST(node->data.binop.right, level + 1);
            break;
        case NODE_DECL:
            printf("DECL %s %s   (line %d)\n",
                   node->data.decl.varType, node->data.decl.name, node->lineno);
            break;
        case NODE_ASSIGN:
            printf("ASSIGN %s   (line %d)\n", node->data.assign.var, node->lineno);
            printAST(node->data.assign.value, level + 1);
            break;
        case NODE_PRINT:
            printf("PRINT   (line %d)\n", node->lineno);
            printAST(node->data.expr, level + 1);
            break;
        default:
            printf("UNKNOWN NODE %d\n", node->type);
            break;
    }
}

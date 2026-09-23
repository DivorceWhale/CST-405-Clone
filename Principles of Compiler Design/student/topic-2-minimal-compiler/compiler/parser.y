/* =========================================================================
 *  TOPIC 2 · Compiler for a Starter Language
 * FILE: parser.y   —   Phase 2 — Syntax analysis
 * -------------------------------------------------------------------------
 * THE PIPELINE, AND WHERE THIS FILE SITS IN IT
 *   scanner -> parser -> ast -> semantic -> tac -> codegen
 *              ^^^^^^  this file
 *
 * RECEIVES  tokens from yylex() (scanner.l), one at a time
 * PRODUCES  the AST, rooted at `root`, for semantic.c; or, for a program
 *           that does not match the grammar, a message naming the line and
 *           what was expected, and root = NULL so main.c stops
 * ========================================================================= */

%{
/* PHASE 2 — SYNTAX ANALYSIS
 *
 * The parser answers one question: do these tokens form a legal program?
 * Bison builds an LALR(1) bottom-up parser from the grammar below.  It reads
 * tokens left to right, shifts them onto a stack, and REDUCES whenever the
 * top of the stack matches the right-hand side of a rule.
 *
 * Answering "yes" is not enough, though — the rest of the compiler needs the
 * program's STRUCTURE.  So each rule carries a semantic action in { } that
 * builds one node of the abstract syntax tree.  $1, $2, ... are the values of
 * the symbols on the right-hand side; $$ is the value this rule hands back.
 *
 *      expr '+' expr   { $$ = createBinOp('+', $1, $3); }
 *       │        │                              │   └── right operand
 *       │        └── $3                         └────── left operand
 *       └── $1
 *
 * By the time yyparse() returns, the tree has been built bottom-up beneath us
 * and `root` points at the whole program.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ast.h"

/* External declarations for lexer interface */
extern int yylex();
extern int yyparse();
extern FILE* yyin;
extern int yylineno;  /* Line number from scanner */

void yyerror(const char* s);
ASTNode* root = NULL;

/* Syntax errors seen so far.  Error productions let the parser recover and
 * keep going, which makes yyparse() return 0 even though the program was
 * wrong; this count is how the `program` rule knows not to hand a tree to
 * the later phases anyway. */
static int syntaxErrors = 0;
%}

/* Report WHAT was expected ("unexpected ID, expecting ';' or '+'") instead
 * of a bare "syntax error". */
%define parse.error verbose

/* SEMANTIC VALUES UNION */
%union {
    int num;
    char* str;
    struct ASTNode* node;
}

/* TOKEN DECLARATIONS */
%token <num> NUM
%token <str> ID
%token INT PRINT

/* Every non-terminal that yields an AST node */
%type <node> program stmt_list stmt decl assign expr print_stmt

/* Track token locations so an error production can name the line of a
 * token it matched (the scanner fills in yylloc for every token). */
%locations

/* OPERATOR PRECEDENCE AND ASSOCIATIVITY, lowest first.
 * `expr '+' expr` alone is ambiguous: a + b + c could group either way.
 * %left resolves it as (a + b) + c, which is what makes the AST lean left.
 * Only '+' is in the Topic 2 grammar; the rest are declared ahead of
 * Topic 3 and bison warns (harmlessly, with -Wall) that they are unused. */
%left '+' '-'
%left '*' '/'

%%

/* THE GRAMMAR
 *
 *     program     ->  stmt_list
 *     stmt_list   ->  stmt  |  stmt_list stmt
 *     stmt        ->  decl  |  assign  |  print_stmt
 *     decl        ->  'int' ID ';'
 *     assign      ->  ID '=' expr ';'
 *     expr        ->  NUM  |  ID  |  expr '+' expr
 *     print_stmt  ->  'print' '(' expr ')' ';'
 *
 * MEMORY: the scanner strdup's every identifier, and every AST constructor
 * strdup's its own copy, so each rule that receives an ID frees it once the
 * node is built. */

/* The whole program.  Sets `root`, the only handle main.c has on the tree.
 * A program with any syntax error produces no tree at all: the recovered
 * tree has holes where the bad statements were, and no later phase should
 * have to cope with that. */
program:
    stmt_list                  { root = syntaxErrors ? NULL : $1; $$ = root; }
    ;

/* A sequence of statements -> NODE_STMT_LIST.  Left recursive, so bison's
 * stack stays shallow however long the program is; see createStmtList for
 * the shape of the list this builds. */
stmt_list:
    stmt                       { $$ = $1; }
    | stmt_list stmt           { $$ = createStmtList($1, $2); }
    ;

/* A statement is just whichever node its alternative built. */
stmt:
    decl                       { $$ = $1; }
    | assign                   { $$ = $1; }
    | print_stmt                { $$ = $1; }
    ;

/* ERROR PRODUCTIONS
 * Each `error` alternative below matches a statement that is correct up to
 * the point where its ';' should be.  Bison reports the error through
 * yyerror, discards the bad input, and resumes here — so we can add a
 * message that names the statement, and parsing continues with the next
 * one so that a single run reports every missing ';'.
 *
 * The line number comes from the last node built before the error, not from
 * yylineno: by the time bison notices a missing ';' it has already read the
 * NEXT token, which is usually on the following line. */
stmt:
    error ';'                  { $$ = NULL; yyerrok; }
    ;

/* `int x;` -> NODE_DECL */
decl:
    INT ID ';'                 { $$ = createDecl("int", $2); free($2); }
    | INT ID error             { fprintf(stderr, "  -> line %d: missing ';' after declaration of '%s'\n",
                                         @2.first_line, $2);
                                 free($2); $$ = NULL; yyerrok; }
    ;

/* `x = expr;` -> NODE_ASSIGN holding the expression's tree */
assign:
    ID '=' expr ';'            { $$ = createAssign($1, $3); free($1); }
    | ID '=' expr error        { fprintf(stderr, "  -> line %d: missing ';' after assignment to '%s'\n",
                                         $3->lineno, $1);
                                 free($1); $$ = NULL; yyerrok; }
    ;

/* An expression -> NODE_NUM, NODE_VAR, or NODE_BINOP with two subtrees */
expr:
    NUM                        { $$ = createNum($1); }
    | ID                       { $$ = createVar($1); free($1); }
    | expr '+' expr            { $$ = createBinOp('+', $1, $3); }
    ;

/* `print(expr);` -> NODE_PRINT holding the expression's tree */
print_stmt:
    PRINT '(' expr ')' ';'     { $$ = createPrint($3); }
    | PRINT '(' expr ')' error { fprintf(stderr, "  -> line %d: missing ';' after print(...)\n",
                                         $3->lineno);
                                 $$ = NULL; yyerrok; }
    ;


%%

/* ERROR HANDLING
 * yylineno is the line of the token bison was looking at when it gave up,
 * which is where the error was DETECTED; the error productions above add
 * where it was most likely MADE. */
void yyerror(const char* s) {
    syntaxErrors++;
    fprintf(stderr, "Syntax Error at line %d: %s\n", yylineno, s);
}

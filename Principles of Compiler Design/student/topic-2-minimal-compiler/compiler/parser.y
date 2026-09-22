/* =========================================================================
 *  TOPIC 2 · Compiler for a Starter Language
 * FILE: parser.y   —   Phase 2 — Syntax analysis
 * -------------------------------------------------------------------------
 * THE PIPELINE, AND WHERE THIS FILE SITS IN IT
 *   scanner -> parser -> ast -> semantic -> tac -> codegen
 *              ^^^^^^  this file
 *
 * WHAT IS NEW IN TOPIC 2
 *   • The starter grammar: a program is a list of statements
 *   • Declaration, assignment, addition, and print
 *   • Error productions that name the mistake instead of just saying "syntax error"
 *
 * WHAT COMES NEXT
 *   Topic 3 adds functions, arrays and the rest of arithmetic — and with them, real activation records.
 *
 * YOUR TASK
 *   This is Project 2: the first compiler you build end to end.  Sections
 *   marked  TODO (Topic 2)  are yours.  Everything else — the headers, the
 *   scanner, the driver, the register allocator — is given, because the
 *   point of this project is the six PHASES, not the plumbing between them.
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

/* NON-TERMINAL TYPES */
/* Every non-terminal that yields an AST node must be declared here.
 * Add to this line as you add rules:
 *   %type <node> program stmt_list stmt decl assign expr print_stmt
 * (bison rejects a %type for a non-terminal that has no rules yet,
 *  which is why only `program` is listed to begin with) */
%type <node> program stmt_list stmt decl assign expr print_stmt

/* OPERATOR PRECEDENCE AND ASSOCIATIVITY
 * Listed from lowest to highest precedence.
 *
 * DANGLING-ELSE RESOLUTION:
 *   The grammar has an ambiguity: in  if (c) if (c) s  else s
 *   the "else" can bind to either "if".  The standard resolution
 *   is that else binds to the nearest (innermost) if.
 *
 *   We express this by giving the if-without-else rule a lower
 *   precedence (%prec LOWER_THAN_ELSE) than the ELSE token, so
 *   when bison sees ELSE it shifts (attaches it to the inner if)
 *   rather than reducing (closing the outer if first).
 */
%left '+' '-'
%left '*' '/'

%%

/* --------------------------------------------------------------------
 * TODO (Topic 2) — THE GRAMMAR
 * Everything above this line is given: the token declarations, the
 * precedence table, and the union that lets a rule hand back an ASTNode*.
 * Below it, write the rules.  Each rule needs a semantic action in { }
 * that builds one AST node and assigns it to $$.
 *
 * THE GRAMMAR YOU ARE IMPLEMENTING
 *
 *     program     ->  stmt_list
 *     stmt_list   ->  stmt  |  stmt_list stmt
 *     stmt        ->  decl  |  assign  |  print_stmt
 *     decl        ->  'int' ID ';'
 *     assign      ->  ID '=' expr ';'
 *     expr        ->  NUM  |  ID  |  expr '+' expr
 *     print_stmt  ->  'print' '(' expr ')' ';'
 *
 * THE ACTIONS YOU NEED
 *
 *     program    : stmt_list          { root = $1; }
 *     stmt_list  : stmt_list stmt     { $$ = createStmtList($1, $2); }
 *     decl       : INT ID ';'         { $$ = createDecl("int", $2); free($2); }
 *     assign     : ID '=' expr ';'    { $$ = createAssign($1, $3); free($1); }
 *     expr       : expr '+' expr      { $$ = createBinOp('+', $1, $3); }
 *     print_stmt : PRINT '(' expr ')' ';'  { $$ = createPrint($3); }
 *
 * TWO THINGS THAT WILL BITE YOU
 *
 *   free($2) on an ID.  The scanner strdup'd the identifier text; the AST
 *   constructor strdup's it again.  Whoever received it from the scanner
 *   has to free it, and that is this rule.  Skip it and the compiler
 *   leaks — small here, embarrassing under valgrind in Topic 6.
 *
 *   `root` is a global declared in the prologue above.  The `program` rule
 *   is the ONLY place that assigns it.  If root stays NULL after a
 *   successful parse, that assignment is missing and every later phase
 *   will silently do nothing.
 *
 * GOING FURTHER (worth doing, and worth talking about in your video)
 *
 *   Add ERROR PRODUCTIONS so that a missing semicolon produces
 *     "line 4: missing ';' after assignment to x"
 *   instead of "syntax error".  The shape is:
 *     | ID '=' expr error  { report it; $$ = NULL; yyerrok; }
 *   Error messages are most of what people judge a compiler by.
 * -------------------------------------------------------------------- */

/* A program with any syntax error produces no tree at all: the recovered
 * tree has holes where the bad statements were, and no later phase should
 * have to cope with that. */
program:
    stmt_list                  { root = syntaxErrors ? NULL : $1; $$ = root; }
    ;

stmt_list:
    stmt                       { $$ = $1; }
    | stmt_list stmt           { $$ = createStmtList($1, $2); }
    ;

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

decl:
    INT ID ';'                 { $$ = createDecl("int", $2); free($2); }
    | INT ID error             { fprintf(stderr, "  -> missing ';' after declaration of '%s'\n", $2);
                                 free($2); $$ = NULL; yyerrok; }
    ;

assign:
    ID '=' expr ';'            { $$ = createAssign($1, $3); free($1); }
    | ID '=' expr error        { fprintf(stderr, "  -> line %d: missing ';' after assignment to '%s'\n",
                                         $3->lineno, $1);
                                 free($1); $$ = NULL; yyerrok; }
    ;

expr:
    NUM                        { $$ = createNum($1); }
    | ID                       { $$ = createVar($1); free($1); }
    | expr '+' expr            { $$ = createBinOp('+', $1, $3); }
    ;

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

/* =========================================================================
 *  TOPIC 2 · Compiler for a Starter Language
 * FILE: scanner.l   —   Phase 1 — Lexical analysis
 * -------------------------------------------------------------------------
 * THE PIPELINE, AND WHERE THIS FILE SITS IN IT
 *   scanner -> parser -> ast -> semantic -> tac -> codegen
 *   ^^^^^^^  this file
 *
 * RECEIVES  the characters of the source file (yyin, opened by main.c)
 * PRODUCES  one token per call to yylex(), consumed by the bison parser;
 *           ID and NUM carry their text/value in yylval.  Also counts
 *           lexical errors in lexErrorCount, which main.c checks.
 *
 * TOKEN SET   `int`, `print`, identifiers, numbers, `+ = ; ( )`,
 *             and both comment forms
 * ========================================================================= */

%{
/* PHASE 1 — LEXICAL ANALYSIS
 *
 * The scanner turns a stream of CHARACTERS into a stream of TOKENS.
 * It is the only part of the compiler that ever sees whitespace or comments.
 *
 *      int  sum  =  a + 10 ;
 *      └─┬┘ └─┬┘ │  │ │ └┬┘ │
 *       INT  ID  '=' ID '+' NUM ';'
 *
 * Flex turns the rules below into a finite automaton.  Two rules decide
 * everything you need to know about how it behaves:
 *
 *   LONGEST MATCH   the rule matching the most characters wins.  This is
 *                   why a well-formed comment beats the unterminated-comment
 *                   rule below: it matches one character more (the final /).
 *   FIRST MATCH     when two rules match the SAME length, the one written
 *                   first wins.  This is why the keyword rules sit above
 *                   the identifier rule: without that order, "int" would
 *                   come back as an ID.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "parser.tab.h"  /* Token definitions from Bison */

int lexErrorCount = 0;   /* Characters the language does not admit        */
int showTokens    = 0;   /* Set by main.c's -t flag: print the token stream */

/* Flex tracks lines for us (%option yylineno) but not columns.  YY_USER_ACTION
 * runs before every rule's action, so it is the one place that sees every
 * character exactly once: tokCol is where the current match STARTED, colNo
 * is where the next one will start. */
static int colNo  = 1;
static int tokCol = 1;
#define YY_USER_ACTION                                              \
    tokCol = colNo;                                                 \
    yylloc.first_line = yylloc.last_line = yylineno;                \
    yylloc.first_column = tokCol;                                   \
    for (int i_ = 0; i_ < yyleng; i_++)                             \
        colNo = (yytext[i_] == '\n') ? 1 : colNo + 1;

/* Print one token when -t is given, then hand it to the parser.  Every
 * token goes through here, so the listing is exactly what bison sees. */
#define TOKEN(kind, name)                                           \
    do {                                                            \
        if (showTokens)                                             \
            printf("  %4d  %3d  %-6s %s\n", yylineno, tokCol, name, yytext); \
        return kind;                                                \
    } while (0)
%}

%option nounput
%option noinput
%option yylineno

%%

 /* ---- keywords: must precede the identifier rule (first match) ---- */
"int"           { TOKEN(INT, "INT"); }
"print"         { TOKEN(PRINT, "PRINT"); }

 /* ---- identifiers and numbers: the only tokens that carry a value ---- */
[a-zA-Z_][a-zA-Z0-9_]* {
    yylval.str = strdup(yytext);   /* the parser frees this after use */
    TOKEN(ID, "ID");
}

[0-9]+          {
    yylval.num = atoi(yytext);
    TOKEN(NUM, "NUM");
}

 /* ---- operators and delimiters: the character IS the token number ---- */
"+"             { TOKEN('+', "'+'"); }
"="             { TOKEN('=', "'='"); }
";"             { TOKEN(';', "';'"); }
"("             { TOKEN('(', "'('"); }
")"             { TOKEN(')', "')'"); }

 /* ---- comments: no token, but %option yylineno still counts the newlines
  *      inside them, so line numbers after a comment stay correct ---- */
"//".*          { }
"/*"([^*]|\*+[^*/])*\*+"/"  { }

 /* An opening comment that never closes.  This pattern cannot cross a "*" "/"
  * pair, so for a well-formed comment it is always one character shorter
  * than the rule above and loses; it wins only when the comment runs to
  * end of file.  Without it, "/" "*" would scan as two stray characters and
  * the rest of the comment as code. */
"/*"([^*]|\*+[^*/])*\**     {
    /* yylineno has already advanced past every newline in the match, so
     * count them back off to report where the comment STARTED. */
    int startLine = yylineno;
    for (int i = 0; i < yyleng; i++) if (yytext[i] == '\n') startLine--;
    fprintf(stderr, "Lexical Error at line %d, column %d: unterminated comment\n",
            startLine, tokCol);
    lexErrorCount++;
}

 /* \r is whitespace so that files checked out with Windows line endings
  * (git core.autocrlf) scan the same as files written on Linux. */
[ \t\r\n]       { }

 /* Anything else is not part of the language.  Report it with its exact
  * position and KEEP SCANNING, so one run reports every bad character;
  * main.c stops compilation afterwards because lexErrorCount > 0. */
.               {
    fprintf(stderr, "Lexical Error at line %d, column %d: unexpected character '%c'\n",
            yylineno, tokCol, *yytext);
    lexErrorCount++;
}

%%

int yywrap() {
    return 1;
}

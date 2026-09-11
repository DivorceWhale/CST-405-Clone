%{
/* ============================================================================
 *  TOPIC 1 · Compiler Design Phases
 * FILE: scanner.l   —   Phase 1: Lexical analysis
 * ----------------------------------------------------------------------------
 * Flex reads the rules below and writes a finite automaton that recognizes
 * all of them at once.  You never write the automaton; you write the
 * vocabulary, and two rules decide everything about how it behaves:
 *
 *   LONGEST MATCH   "<=" beats "<" because it consumes more characters.
 *                   Position in the file is irrelevant for this.
 *   FIRST MATCH     when two rules match the SAME number of characters, the
 *                   one written EARLIER wins.  This is the only reason the
 *                   keyword rules must appear above the identifier rule:
 *                   "int" matches both, both match three characters, and
 *                   whichever is written first decides the outcome.
 *
 * Delete a keyword rule and watch that keyword come back as an ID.  That one
 * experiment explains rule order better than any paragraph can.
 *
 * WHAT COMES NEXT
 *   In Topic 2 this same file feeds bison instead of main.c.  The actions
 *   change from "record a token for printing" to "return a token number";
 *   the patterns do not change at all.
 * ==========================================================================*/
#include <stdio.h>
#include <string.h>
#include "tokens.h"

int lexErrorCount = 0;
int tokenCount    = 0;
int lineNo        = 1;
int colNo         = 1;
static int commentStartLine;
static int commentStartCol;

char lastLexeme[256];
int  lastLine;
int  lastCol;

/* Record where the token that is being returned started, then advance the
 * column past it.  Flex tracks lines for us (%option yylineno) but not
 * columns, so we keep the column by hand — and a column number is half of
 * what makes an error message actually findable. */
static int tok(TokenKind k) {
    lastLine = lineNo;
    lastCol  = colNo;
    snprintf(lastLexeme, sizeof lastLexeme, "%s", yytext);
    colNo += (int)yyleng;
    tokenCount++;
    return (int)k;
}

/* Advance the position counters over text that produces no token at all:
 * whitespace, newlines, and the insides of comments. */
static void skip(const char* s, int n) {
    for (int i = 0; i < n; i++) {
        if (s[i] == '\n') { lineNo++; colNo = 1; }
        else if (s[i] == '\t') colNo += 4;
        else colNo++;
    }
}
%}

%option noyywrap
%option nounput
%option noinput
%x COMMENT

/* Named patterns keep the rules below readable.  A definition is pure
 * substitution — {digit}+ literally becomes [0-9]+ before flex sees it. */
digit   [0-9]
letter  [a-zA-Z_]
ident   {letter}({letter}|{digit})*
number  {digit}+

%%

 /* ══════════════════════════════════════════════════════════════════════
  * YOUR TASK STARTS HERE.
  *
  * Below this line, write one flex rule per row of the token table.  A rule
  * is a PATTERN, whitespace, then an ACTION in braces:
  *
  *      "int"       { return tok(TOK_INT); }
  *      {ident}     { return tok(TOK_ID);  }
  *
  * tok() is written for you above: it records the lexeme and position, keeps
  * the column counter honest, and returns the kind.  Always return through
  * it — never `return TOK_X;` directly, or your line and column numbers
  * will be wrong and you will not notice until Topic 2.
  *
  * WORK IN THIS ORDER.  Build and run ./lexer after each step; a scanner is
  * one of the few programs you can test after five minutes of writing.
  *
  *  1. WHITESPACE AND COMMENTS  (produce no token)
  *       - [ \t\r\n]+
  *       - a line comment:  "//" to end of line
  *       - a block comment: /* ... * / , which may span lines
  *     Call skip(yytext, yyleng) in each action, NOT nothing at all.  skip()
  *     is what keeps lineNo and colNo correct across a multi-line comment.
  *     Test with tests/04_comments.cm: `x = 1;` must report line 6.
  *
  *  2. KEYWORDS — int print return if else while for switch case default
  *     break.  These must be written ABOVE the identifier rule.  Write them
  *     below it instead, run tests/03_keywords_vs_ids.cm, and see what
  *     happens; then move them and understand why it changed.
  *
  *  3. IDENTIFIERS AND NUMBERS — use the {ident} and {number} definitions
  *     already declared above.
  *
  *  4. TWO-CHARACTER OPERATORS — <= >= == != && ||
  *     Then the one-character ones: + - * / < > ! =
  *     Predict what happens if you omit the "<=" rule entirely, then try it.
  *
  *  5. DELIMITERS — ; : , ( ) { } [ ]
  *
  *  6. THE ERROR RULE — a single `.` pattern, written LAST.
  *     It must: report the offending character WITH ITS LINE AND COLUMN,
  *     increment lexErrorCount, advance colNo, and then CONTINUE SCANNING.
  *     Do not exit.  tests/05_lexical_errors.cm contains four bad characters
  *     and expects four messages from one run.
  *
  *  7. UNTERMINATED BLOCK COMMENT — a `/*` that reaches end of file.
  *     tests/06_unterminated_comment.cm expects exactly one error for this.
  *     Hint: a second pattern for "an opening /* followed by anything that
  *     never closes" will match only when the well-formed pattern cannot.
  *
  * WHEN YOU ARE DONE
  *   make test           must report 0 errors for tests 01-04
  *   ./lexer tests/05_lexical_errors.cm      must report 4 errors, exit 1
  *   ./lexer tests/06_unterminated_comment.cm must report 1 error,  exit 1
  * ══════════════════════════════════════════════════════════════════════ */

 /* ---- whitespace ---- */
[ \t\r]+                { skip(yytext, yyleng); }
\n                       { skip(yytext, yyleng); }

 /* ---- line comment ---- */
"//".*                   { skip(yytext, yyleng); }

 /* ---- block comment: enter/exit a state so EOF-inside-comment is easy ---- */
"/*"                     { commentStartLine = lineNo; commentStartCol = colNo;
                           skip(yytext, yyleng); BEGIN(COMMENT); }
<COMMENT>"*/"            { skip(yytext, yyleng); BEGIN(INITIAL); }
<COMMENT>\n              { skip(yytext, yyleng); }
<COMMENT>.               { skip(yytext, yyleng); }
<COMMENT><<EOF>>         {
                             fprintf(stderr,
                                 "Lexical error: unterminated comment starting at line %d, column %d\n",
                                 commentStartLine, commentStartCol);
                             lexErrorCount++;
                             yyterminate();
                         }

 /* ---- keywords (must precede {ident}) ---- */
"int"                    { return tok(TOK_INT); }
"print"                  { return tok(TOK_PRINT); }
"return"                 { return tok(TOK_RETURN); }
"if"                     { return tok(TOK_IF); }
"else"                   { return tok(TOK_ELSE); }
"while"                  { return tok(TOK_WHILE); }
"for"                    { return tok(TOK_FOR); }
"switch"                 { return tok(TOK_SWITCH); }
"case"                   { return tok(TOK_CASE); }
"default"                { return tok(TOK_DEFAULT); }
"break"                  { return tok(TOK_BREAK); }

 /* ---- identifiers and numbers ---- */
{ident}                  { return tok(TOK_ID); }
{number}                 { return tok(TOK_NUM); }

 /* ---- two-character operators (before the one-char ones) ---- */
"<="                     { return tok(TOK_LE); }
">="                     { return tok(TOK_GE); }
"=="                     { return tok(TOK_EQ); }
"!="                     { return tok(TOK_NE); }
"&&"                     { return tok(TOK_AND); }
"||"                     { return tok(TOK_OR); }

 /* ---- one-character operators ---- */
"+"                      { return tok(TOK_PLUS); }
"-"                      { return tok(TOK_MINUS); }
"*"                      { return tok(TOK_STAR); }
"/"                      { return tok(TOK_SLASH); }
"<"                      { return tok(TOK_LT); }
">"                      { return tok(TOK_GT); }
"!"                      { return tok(TOK_NOT); }
"="                      { return tok(TOK_ASSIGN); }

 /* ---- delimiters ---- */
";"                      { return tok(TOK_SEMI); }
":"                      { return tok(TOK_COLON); }
","                      { return tok(TOK_COMMA); }
"("                      { return tok(TOK_LPAREN); }
")"                      { return tok(TOK_RPAREN); }
"{"                      { return tok(TOK_LBRACE); }
"}"                      { return tok(TOK_RBRACE); }
"["                      { return tok(TOK_LBRACKET); }
"]"                      { return tok(TOK_RBRACKET); }

 /* ---- catch-all error rule, LAST, keeps scanning ---- */
.                        {
                             fprintf(stderr,
                                 "Lexical error at line %d, column %d: unexpected character '%s'\n",
                                 lineNo, colNo, yytext);
                             lexErrorCount++;
                             skip(yytext, yyleng);
                         }


%%

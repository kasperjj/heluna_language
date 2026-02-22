/*
 * test_lexer: basic sanity checks for the Heluna lexer.
 */

#include "heluna/lexer.h"
#include "heluna/arena.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int tests_run = 0;
static int tests_passed = 0;

#define ASSERT(cond, msg) do { \
    tests_run++; \
    if (!(cond)) { \
        fprintf(stderr, "  FAIL: %s (line %d)\n", msg, __LINE__); \
    } else { \
        tests_passed++; \
    } \
} while (0)

static void test_keywords(void) {
    Arena *a = arena_create(4096);
    Lexer lex;
    lexer_init(&lex, "contract define end let be result if then else match when", "test", a);

    ASSERT(lexer_next(&lex).kind == TOK_CONTRACT, "contract keyword");
    ASSERT(lexer_next(&lex).kind == TOK_DEFINE,   "define keyword");
    ASSERT(lexer_next(&lex).kind == TOK_END,      "end keyword");
    ASSERT(lexer_next(&lex).kind == TOK_LET,      "let keyword");
    ASSERT(lexer_next(&lex).kind == TOK_BE,       "be keyword");
    ASSERT(lexer_next(&lex).kind == TOK_RESULT,   "result keyword");
    ASSERT(lexer_next(&lex).kind == TOK_IF,       "if keyword");
    ASSERT(lexer_next(&lex).kind == TOK_THEN,     "then keyword");
    ASSERT(lexer_next(&lex).kind == TOK_ELSE,     "else keyword");
    ASSERT(lexer_next(&lex).kind == TOK_MATCH,    "match keyword");
    ASSERT(lexer_next(&lex).kind == TOK_WHEN,     "when keyword");
    ASSERT(lexer_next(&lex).kind == TOK_EOF,      "EOF after keywords");

    arena_destroy(a);
}

static void test_literals(void) {
    Arena *a = arena_create(4096);
    Lexer lex;
    lexer_init(&lex, "42 3.14 \"hello\" true false nothing", "test", a);

    ASSERT(lexer_next(&lex).kind == TOK_INTEGER, "integer literal");
    ASSERT(lexer_next(&lex).kind == TOK_FLOAT,   "float literal");
    ASSERT(lexer_next(&lex).kind == TOK_STRING,  "string literal");
    ASSERT(lexer_next(&lex).kind == TOK_TRUE,    "true literal");
    ASSERT(lexer_next(&lex).kind == TOK_FALSE,   "false literal");
    ASSERT(lexer_next(&lex).kind == TOK_NOTHING, "nothing literal");
    ASSERT(lexer_next(&lex).kind == TOK_EOF,     "EOF after literals");

    arena_destroy(a);
}

static void test_input_refs(void) {
    Arena *a = arena_create(4096);
    Lexer lex;
    lexer_init(&lex, "$first-name $age", "test", a);

    Token t1 = lexer_next(&lex);
    ASSERT(t1.kind == TOK_INPUT_REF, "input ref kind");
    ASSERT(t1.length == 11,          "input ref length");

    Token t2 = lexer_next(&lex);
    ASSERT(t2.kind == TOK_INPUT_REF, "second input ref");

    arena_destroy(a);
}

static void test_punctuation(void) {
    Arena *a = arena_create(4096);
    Lexer lex;
    lexer_init(&lex, "{ } [ ] ( ) , . : + - * / % = != < > <= >=", "test", a);

    ASSERT(lexer_next(&lex).kind == TOK_LBRACE,   "{");
    ASSERT(lexer_next(&lex).kind == TOK_RBRACE,   "}");
    ASSERT(lexer_next(&lex).kind == TOK_LBRACKET, "[");
    ASSERT(lexer_next(&lex).kind == TOK_RBRACKET, "]");
    ASSERT(lexer_next(&lex).kind == TOK_LPAREN,   "(");
    ASSERT(lexer_next(&lex).kind == TOK_RPAREN,   ")");
    ASSERT(lexer_next(&lex).kind == TOK_COMMA,    ",");
    ASSERT(lexer_next(&lex).kind == TOK_DOT,      ".");
    ASSERT(lexer_next(&lex).kind == TOK_COLON,    ":");
    ASSERT(lexer_next(&lex).kind == TOK_PLUS,     "+");
    ASSERT(lexer_next(&lex).kind == TOK_MINUS,    "-");
    ASSERT(lexer_next(&lex).kind == TOK_STAR,     "*");
    ASSERT(lexer_next(&lex).kind == TOK_SLASH,    "/");
    ASSERT(lexer_next(&lex).kind == TOK_PERCENT,  "%");
    ASSERT(lexer_next(&lex).kind == TOK_EQ,       "=");
    ASSERT(lexer_next(&lex).kind == TOK_NEQ,      "!=");
    ASSERT(lexer_next(&lex).kind == TOK_LT,       "<");
    ASSERT(lexer_next(&lex).kind == TOK_GT,       ">");
    ASSERT(lexer_next(&lex).kind == TOK_LTE,      "<=");
    ASSERT(lexer_next(&lex).kind == TOK_GTE,      ">=");

    arena_destroy(a);
}

int main(void) {
    printf("test_lexer:\n");

    test_keywords();
    test_literals();
    test_input_refs();
    test_punctuation();

    printf("  %d/%d passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}

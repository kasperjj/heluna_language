#ifndef HELUNA_TOKEN_H
#define HELUNA_TOKEN_H

#include "heluna/errors.h"

typedef enum {
    /* Literals */
    TOK_INTEGER,
    TOK_FLOAT,
    TOK_STRING,
    TOK_TRUE,
    TOK_FALSE,
    TOK_NOTHING,

    /* Identifiers and references */
    TOK_IDENT,          /* foo, full-name, bracket-age */
    TOK_INPUT_REF,      /* $field-name */

    /* Keywords — contracts */
    TOK_CONTRACT,
    TOK_USES,
    TOK_TAGS,
    TOK_TAGGED,
    TOK_SANITIZERS,
    TOK_STRIPS,
    TOK_INPUT,
    TOK_OUTPUT,
    TOK_RULES,
    TOK_FORBID,
    TOK_REQUIRE,
    TOK_REJECT,
    TOK_TESTS,
    TOK_TEST,
    TOK_GIVEN,
    TOK_EXPECT,

    /* Keywords — types */
    TOK_AS,
    TOK_STRING_TYPE,
    TOK_INTEGER_TYPE,
    TOK_FLOAT_TYPE,
    TOK_BOOLEAN_TYPE,
    TOK_MAYBE,
    TOK_LIST,
    TOK_OF,
    TOK_RECORD,

    /* Keywords — expressions */
    TOK_DEFINE,
    TOK_WITH,
    TOK_LET,
    TOK_BE,
    TOK_RESULT,
    TOK_IF,
    TOK_THEN,
    TOK_ELSE,
    TOK_END,
    TOK_MATCH,
    TOK_WHEN,
    TOK_BETWEEN,
    TOK_AND,
    TOK_OR,
    TOK_NOT,
    TOK_IN,
    TOK_THROUGH,
    TOK_MAP,
    TOK_DO,
    TOK_FILTER,
    TOK_WHERE,

    /* Punctuation */
    TOK_LPAREN,         /* ( */
    TOK_RPAREN,         /* ) */
    TOK_LBRACKET,       /* [ */
    TOK_RBRACKET,       /* ] */
    TOK_LBRACE,         /* { */
    TOK_RBRACE,         /* } */
    TOK_COMMA,          /* , */
    TOK_DOT,            /* . */
    TOK_DOTDOT,         /* .. */
    TOK_COLON,          /* : */
    TOK_DOLLAR,         /* $ */
    TOK_UNDERSCORE,     /* _ */

    /* Operators */
    TOK_PLUS,           /* + */
    TOK_MINUS,          /* - */
    TOK_STAR,           /* * */
    TOK_SLASH,          /* / */
    TOK_PERCENT,        /* % */
    TOK_EQ,             /* = */
    TOK_NEQ,            /* != */
    TOK_LT,             /* < */
    TOK_GT,             /* > */
    TOK_LTE,            /* <= */
    TOK_GTE,            /* >= */

    /* Special */
    TOK_COMMENT,
    TOK_EOF,
    TOK_ERROR,
} TokenKind;

typedef struct {
    TokenKind   kind;
    SrcLoc      loc;
    const char *start;      /* pointer into source buffer */
    int         length;     /* length of raw lexeme */
} Token;

/* Return a human-readable name for a token kind (for debugging). */
const char *token_kind_name(TokenKind kind);

#endif /* HELUNA_TOKEN_H */

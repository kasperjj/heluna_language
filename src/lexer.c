#include "heluna/lexer.h"
#include <string.h>
#include <ctype.h>

/* ── Keyword table ───────────────────────────────────────── */

typedef struct {
    const char *word;
    TokenKind   kind;
} Keyword;

static const Keyword keywords[] = {
    { "and",        TOK_AND },
    { "as",         TOK_AS },
    { "be",         TOK_BE },
    { "between",    TOK_BETWEEN },
    { "boolean",    TOK_BOOLEAN_TYPE },
    { "contract",   TOK_CONTRACT },
    { "define",     TOK_DEFINE },
    { "do",         TOK_DO },
    { "else",       TOK_ELSE },
    { "end",        TOK_END },
    { "expect",     TOK_EXPECT },
    { "false",      TOK_FALSE },
    { "filter",     TOK_FILTER },
    { "float",      TOK_FLOAT_TYPE },
    { "forbid",     TOK_FORBID },
    { "given",      TOK_GIVEN },
    { "if",         TOK_IF },
    { "in",         TOK_IN },
    { "input",      TOK_INPUT },
    { "integer",    TOK_INTEGER_TYPE },
    { "let",        TOK_LET },
    { "list",       TOK_LIST },
    { "map",        TOK_MAP },
    { "match",      TOK_MATCH },
    { "maybe",      TOK_MAYBE },
    { "not",        TOK_NOT },
    { "nothing",    TOK_NOTHING },
    { "of",         TOK_OF },
    { "or",         TOK_OR },
    { "output",     TOK_OUTPUT },
    { "record",     TOK_RECORD },
    { "reject",     TOK_REJECT },
    { "require",    TOK_REQUIRE },
    { "result",     TOK_RESULT },
    { "rules",      TOK_RULES },
    { "sanitizers", TOK_SANITIZERS },
    { "string",     TOK_STRING_TYPE },
    { "strips",     TOK_STRIPS },
    { "tagged",     TOK_TAGGED },
    { "tags",       TOK_TAGS },
    { "test",       TOK_TEST },
    { "tests",      TOK_TESTS },
    { "then",       TOK_THEN },
    { "through",    TOK_THROUGH },
    { "true",       TOK_TRUE },
    { "uses",       TOK_USES },
    { "when",       TOK_WHEN },
    { "where",      TOK_WHERE },
    { "with",       TOK_WITH },
    { NULL,         TOK_ERROR },
};

static TokenKind lookup_keyword(const char *start, int length) {
    for (const Keyword *kw = keywords; kw->word; kw++) {
        if ((int)strlen(kw->word) == length &&
            memcmp(kw->word, start, (size_t)length) == 0) {
            return kw->kind;
        }
    }
    return TOK_IDENT;
}

/* ── Helpers ─────────────────────────────────────────────── */

void lexer_init(Lexer *lex, const char *source, const char *filename, Arena *arena) {
    lex->source   = source;
    lex->current  = source;
    lex->filename = filename;
    lex->line     = 1;
    lex->col      = 1;
    lex->arena    = arena;
}

static char peek(Lexer *lex) {
    return *lex->current;
}

static char advance(Lexer *lex) {
    char c = *lex->current++;
    if (c == '\n') {
        lex->line++;
        lex->col = 1;
    } else {
        lex->col++;
    }
    return c;
}

static Token make_token(Lexer *lex, TokenKind kind, const char *start, int length, SrcLoc loc) {
    (void)lex;
    return (Token){ .kind = kind, .loc = loc, .start = start, .length = length };
}

static Token error_token(Lexer *lex, const char *msg, SrcLoc loc) {
    return (Token){ .kind = TOK_ERROR, .loc = loc, .start = msg, .length = (int)strlen(msg) };
    (void)lex;
}

static void skip_whitespace(Lexer *lex) {
    while (peek(lex) == ' ' || peek(lex) == '\t' ||
           peek(lex) == '\r' || peek(lex) == '\n') {
        advance(lex);
    }
}

/* ── Scanning ────────────────────────────────────────────── */

static Token scan_string(Lexer *lex, SrcLoc loc) {
    const char *start = lex->current - 1;  /* include opening quote */

    while (peek(lex) != '"' && peek(lex) != '\0') {
        if (peek(lex) == '\\') advance(lex);  /* skip escape */
        advance(lex);
    }

    if (peek(lex) == '\0') {
        return error_token(lex, "unterminated string", loc);
    }

    advance(lex);  /* consume closing quote */
    int length = (int)(lex->current - start);
    return make_token(lex, TOK_STRING, start, length, loc);
}

static Token scan_number(Lexer *lex, SrcLoc loc) {
    const char *start = lex->current - 1;
    TokenKind kind = TOK_INTEGER;

    while (isdigit((unsigned char)peek(lex))) advance(lex);

    /* Fractional part */
    if (peek(lex) == '.' && isdigit((unsigned char)lex->current[1])) {
        kind = TOK_FLOAT;
        advance(lex);  /* consume '.' */
        while (isdigit((unsigned char)peek(lex))) advance(lex);
    }

    /* Exponent */
    if (peek(lex) == 'e' || peek(lex) == 'E') {
        kind = TOK_FLOAT;
        advance(lex);
        if (peek(lex) == '+' || peek(lex) == '-') advance(lex);
        while (isdigit((unsigned char)peek(lex))) advance(lex);
    }

    int length = (int)(lex->current - start);
    return make_token(lex, kind, start, length, loc);
}

static Token scan_identifier(Lexer *lex, SrcLoc loc) {
    const char *start = lex->current - 1;

    while (isalpha((unsigned char)peek(lex)) ||
           isdigit((unsigned char)peek(lex)) ||
           peek(lex) == '-') {
        advance(lex);
    }

    int length = (int)(lex->current - start);
    TokenKind kind = lookup_keyword(start, length);
    return make_token(lex, kind, start, length, loc);
}

static Token scan_comment(Lexer *lex, SrcLoc loc) {
    const char *start = lex->current - 1;

    while (peek(lex) != '\n' && peek(lex) != '\0') {
        advance(lex);
    }

    int length = (int)(lex->current - start);
    return make_token(lex, TOK_COMMENT, start, length, loc);
}

/* ── Public API ──────────────────────────────────────────── */

Token lexer_next(Lexer *lex) {
    skip_whitespace(lex);

    SrcLoc loc = { .filename = lex->filename, .line = lex->line, .col = lex->col };

    if (peek(lex) == '\0') {
        return make_token(lex, TOK_EOF, lex->current, 0, loc);
    }

    char c = advance(lex);

    /* Comments */
    if (c == '#')  return scan_comment(lex, loc);

    /* Strings */
    if (c == '"')  return scan_string(lex, loc);

    /* Numbers */
    if (isdigit((unsigned char)c)) return scan_number(lex, loc);

    /* Identifiers and keywords */
    if (isalpha((unsigned char)c)) return scan_identifier(lex, loc);

    /* Input references: $identifier */
    if (c == '$') {
        if (isalpha((unsigned char)peek(lex))) {
            const char *start = lex->current - 1;
            advance(lex);  /* consume first letter */
            while (isalpha((unsigned char)peek(lex)) ||
                   isdigit((unsigned char)peek(lex)) ||
                   peek(lex) == '-') {
                advance(lex);
            }
            int length = (int)(lex->current - start);
            return make_token(lex, TOK_INPUT_REF, start, length, loc);
        }
        return make_token(lex, TOK_DOLLAR, lex->current - 1, 1, loc);
    }

    /* Two-character tokens */
    switch (c) {
    case '!':
        if (peek(lex) == '=') { advance(lex); return make_token(lex, TOK_NEQ, lex->current - 2, 2, loc); }
        return error_token(lex, "unexpected '!'", loc);
    case '<':
        if (peek(lex) == '=') { advance(lex); return make_token(lex, TOK_LTE, lex->current - 2, 2, loc); }
        return make_token(lex, TOK_LT, lex->current - 1, 1, loc);
    case '>':
        if (peek(lex) == '=') { advance(lex); return make_token(lex, TOK_GTE, lex->current - 2, 2, loc); }
        return make_token(lex, TOK_GT, lex->current - 1, 1, loc);
    case '.':
        if (peek(lex) == '.') { advance(lex); return make_token(lex, TOK_DOTDOT, lex->current - 2, 2, loc); }
        return make_token(lex, TOK_DOT, lex->current - 1, 1, loc);
    default:
        break;
    }

    /* Single-character tokens */
    switch (c) {
    case '(':  return make_token(lex, TOK_LPAREN,     lex->current - 1, 1, loc);
    case ')':  return make_token(lex, TOK_RPAREN,     lex->current - 1, 1, loc);
    case '[':  return make_token(lex, TOK_LBRACKET,   lex->current - 1, 1, loc);
    case ']':  return make_token(lex, TOK_RBRACKET,   lex->current - 1, 1, loc);
    case '{':  return make_token(lex, TOK_LBRACE,     lex->current - 1, 1, loc);
    case '}':  return make_token(lex, TOK_RBRACE,     lex->current - 1, 1, loc);
    case ',':  return make_token(lex, TOK_COMMA,      lex->current - 1, 1, loc);
    case ':':  return make_token(lex, TOK_COLON,      lex->current - 1, 1, loc);
    case '_':  return make_token(lex, TOK_UNDERSCORE, lex->current - 1, 1, loc);
    case '+':  return make_token(lex, TOK_PLUS,       lex->current - 1, 1, loc);
    case '-':
        /* Could be negative number or minus operator */
        if (isdigit((unsigned char)peek(lex))) return scan_number(lex, loc);
        return make_token(lex, TOK_MINUS, lex->current - 1, 1, loc);
    case '*':  return make_token(lex, TOK_STAR,       lex->current - 1, 1, loc);
    case '/':  return make_token(lex, TOK_SLASH,      lex->current - 1, 1, loc);
    case '%':  return make_token(lex, TOK_PERCENT,    lex->current - 1, 1, loc);
    case '=':  return make_token(lex, TOK_EQ,         lex->current - 1, 1, loc);
    default:
        break;
    }

    return error_token(lex, "unexpected character", loc);
}

Token lexer_peek(Lexer *lex) {
    /* Save state */
    const char *saved_current = lex->current;
    int saved_line = lex->line;
    int saved_col  = lex->col;

    Token tok = lexer_next(lex);

    /* Restore state */
    lex->current = saved_current;
    lex->line    = saved_line;
    lex->col     = saved_col;

    return tok;
}

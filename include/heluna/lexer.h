#ifndef HELUNA_LEXER_H
#define HELUNA_LEXER_H

#include "heluna/token.h"
#include "heluna/arena.h"

/*
 * Lexer: scans a source buffer and produces tokens one at a time.
 *
 * Usage:
 *   Lexer lex;
 *   lexer_init(&lex, source, filename, arena);
 *   Token tok;
 *   do {
 *       tok = lexer_next(&lex);
 *       // ... process tok ...
 *   } while (tok.kind != TOK_EOF);
 */

typedef struct {
    const char *source;     /* full source buffer (null-terminated) */
    const char *current;    /* current read position */
    const char *filename;   /* for error reporting */
    int         line;
    int         col;
    Arena      *arena;      /* for string duplication if needed */
} Lexer;

/* Initialize a lexer over a source buffer. */
void  lexer_init(Lexer *lex, const char *source, const char *filename, Arena *arena);

/* Produce the next token. Returns TOK_EOF at end, TOK_ERROR on bad input. */
Token lexer_next(Lexer *lex);

/* Peek at the next token without consuming it. */
Token lexer_peek(Lexer *lex);

#endif /* HELUNA_LEXER_H */

#ifndef HELUNA_PARSER_H
#define HELUNA_PARSER_H

#include "heluna/lexer.h"
#include "heluna/ast.h"
#include "heluna/arena.h"
#include "heluna/errors.h"

typedef struct {
    Lexer      *lex;
    Token       current;    /* most recently consumed token */
    Token       next;       /* lookahead token */
    Arena      *arena;
    HelunaError error;
    int         had_error;
} Parser;

/* Initialize a parser over a lexer. */
void        parser_init(Parser *p, Lexer *lex, Arena *arena);

/* Parse a complete Heluna program (contract + define). Returns NULL on error. */
AstProgram *parser_parse(Parser *p);

#endif /* HELUNA_PARSER_H */

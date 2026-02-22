/*
 * heluna-lex: tokenize a Heluna source file and print each token.
 *
 * Usage: heluna-lex <file.heluna>
 *
 * Reads the file, runs the lexer, and prints one token per line:
 *   <file>:<line>:<col>  <KIND>  <lexeme>
 */

#include "heluna/lexer.h"
#include "heluna/arena.h"
#include <stdio.h>
#include <stdlib.h>

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "heluna-lex: cannot open '%s'\n", path);
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *buf = malloc((size_t)size + 1);
    if (!buf) {
        fclose(f);
        fprintf(stderr, "heluna-lex: out of memory\n");
        return NULL;
    }

    size_t read = fread(buf, 1, (size_t)size, f);
    buf[read] = '\0';
    fclose(f);
    return buf;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: heluna-lex <file.heluna>\n");
        return 1;
    }

    char *source = read_file(argv[1]);
    if (!source) return 1;

    Arena *arena = arena_create(16 * 1024);
    Lexer lex;
    lexer_init(&lex, source, argv[1], arena);

    Token tok;
    do {
        tok = lexer_next(&lex);

        /* Skip comments for cleaner output */
        if (tok.kind == TOK_COMMENT) continue;

        printf("%s:%d:%d\t%-16s\t%.*s\n",
               tok.loc.filename ? tok.loc.filename : "<input>",
               tok.loc.line,
               tok.loc.col,
               token_kind_name(tok.kind),
               tok.length,
               tok.start);

    } while (tok.kind != TOK_EOF && tok.kind != TOK_ERROR);

    arena_destroy(arena);
    free(source);

    return tok.kind == TOK_ERROR ? 1 : 0;
}

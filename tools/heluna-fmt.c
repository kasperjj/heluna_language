/*
 * heluna-fmt: format a Heluna source file to canonical style.
 *
 * Usage: heluna-fmt <file.heluna>
 *
 * Reads the file, parses it to an AST, and emits canonically-formatted
 * source to stdout.  Redirect to overwrite:
 *     heluna-fmt file.heluna > file.heluna.tmp && mv file.heluna.tmp file.heluna
 *
 * Known limitation: comments are discarded by the parser and therefore
 * cannot be preserved in the formatted output.
 */

#include "heluna/formatter.h"
#include "heluna/parser.h"
#include "heluna/lexer.h"
#include "heluna/arena.h"
#include "heluna/errors.h"
#include <stdio.h>
#include <stdlib.h>

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "heluna-fmt: cannot open '%s'\n", path);
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *buf = malloc((size_t)size + 1);
    if (!buf) {
        fclose(f);
        fprintf(stderr, "heluna-fmt: out of memory\n");
        return NULL;
    }

    size_t nread = fread(buf, 1, (size_t)size, f);
    buf[nread] = '\0';
    fclose(f);
    return buf;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: heluna-fmt <file.heluna>\n");
        return 1;
    }

    char *source = read_file(argv[1]);
    if (!source) return 1;

    Arena *arena = arena_create(64 * 1024);
    Lexer lex;
    lexer_init(&lex, source, argv[1], arena);

    Parser parser;
    parser_init(&parser, &lex, arena);

    AstProgram *prog = parser_parse(&parser);

    if (parser.had_error) {
        heluna_error_print(&parser.error);
        arena_destroy(arena);
        free(source);
        return 1;
    }

    heluna_format(prog, stdout);

    arena_destroy(arena);
    free(source);
    return 0;
}

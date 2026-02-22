/*
 * heluna-parse: parse a Heluna source file and dump the AST.
 *
 * Usage: heluna-parse <file.heluna>
 */

#include "heluna/parser.h"
#include "heluna/lexer.h"
#include "heluna/ast.h"
#include "heluna/arena.h"
#include "heluna/errors.h"
#include <stdio.h>
#include <stdlib.h>

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "heluna-parse: cannot open '%s'\n", path);
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *buf = malloc((size_t)size + 1);
    if (!buf) {
        fclose(f);
        fprintf(stderr, "heluna-parse: out of memory\n");
        return NULL;
    }

    size_t nread = fread(buf, 1, (size_t)size, f);
    buf[nread] = '\0';
    fclose(f);
    return buf;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: heluna-parse <file.heluna>\n");
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

    ast_print(prog, stdout);

    arena_destroy(arena);
    free(source);
    return 0;
}

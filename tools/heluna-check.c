/*
 * heluna-check: validate a Heluna contract and function without running it.
 *
 * Usage: heluna-check <file.heluna>
 *
 * Runs static analysis: contract structure, name resolution, function call
 * validation, tag/sanitizer coherence, and rule validation.
 */

#include "heluna/checker.h"
#include "heluna/parser.h"
#include "heluna/lexer.h"
#include "heluna/arena.h"
#include "heluna/errors.h"
#include <stdio.h>
#include <stdlib.h>

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "heluna-check: cannot open '%s'\n", path);
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *buf = malloc((size_t)size + 1);
    if (!buf) {
        fclose(f);
        fprintf(stderr, "heluna-check: out of memory\n");
        return NULL;
    }

    size_t nread = fread(buf, 1, (size_t)size, f);
    buf[nread] = '\0';
    fclose(f);
    return buf;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: heluna-check <file.heluna>\n");
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

    Checker checker;
    checker_init(&checker, prog, arena);
    int nerrs = checker_check(&checker);

    if (nerrs > 0) {
        for (int i = 0; i < checker.errors.count; i++) {
            heluna_error_print(&checker.errors.errors[i]);
        }
        fprintf(stderr, "%d error%s found\n", nerrs, nerrs == 1 ? "" : "s");
        arena_destroy(arena);
        free(source);
        return 1;
    }

    printf("ok\n");

    arena_destroy(arena);
    free(source);
    return 0;
}

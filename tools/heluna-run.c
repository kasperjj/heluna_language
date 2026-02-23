/*
 * heluna-run: execute a Heluna function with JSON input.
 *
 * Usage: heluna-run <file.heluna> [input.json]
 *
 * If input.json is omitted, reads from stdin.
 */

#include "heluna/evaluator.h"
#include "heluna/json.h"
#include "heluna/checker.h"
#include "heluna/parser.h"
#include "heluna/lexer.h"
#include "heluna/arena.h"
#include "heluna/errors.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "heluna-run: cannot open '%s'\n", path);
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *buf = malloc((size_t)size + 1);
    if (!buf) {
        fclose(f);
        fprintf(stderr, "heluna-run: out of memory\n");
        return NULL;
    }

    size_t nread = fread(buf, 1, (size_t)size, f);
    buf[nread] = '\0';
    fclose(f);
    return buf;
}

static char *read_stdin(void) {
    size_t capacity = 4096;
    size_t length = 0;
    char *buf = malloc(capacity);
    if (!buf) {
        fprintf(stderr, "heluna-run: out of memory\n");
        return NULL;
    }

    while (1) {
        size_t nread = fread(buf + length, 1, capacity - length - 1, stdin);
        length += nread;
        if (nread == 0) break;
        if (length + 1 >= capacity) {
            capacity *= 2;
            char *new_buf = realloc(buf, capacity);
            if (!new_buf) {
                free(buf);
                fprintf(stderr, "heluna-run: out of memory\n");
                return NULL;
            }
            buf = new_buf;
        }
    }

    buf[length] = '\0';
    return buf;
}

int main(int argc, char **argv) {
    if (argc < 2 || argc > 3) {
        fprintf(stderr, "usage: heluna-run <file.heluna> [input.json]\n");
        return 1;
    }

    /* Read Heluna source */
    char *source = read_file(argv[1]);
    if (!source) return 1;

    /* Read JSON input */
    char *json_input = NULL;
    if (argc == 3) {
        json_input = read_file(argv[2]);
    } else {
        json_input = read_stdin();
    }
    if (!json_input) {
        free(source);
        return 1;
    }

    Arena *arena = arena_create(64 * 1024);

    /* Lex */
    Lexer lex;
    lexer_init(&lex, source, argv[1], arena);

    /* Parse */
    Parser parser;
    parser_init(&parser, &lex, arena);
    AstProgram *prog = parser_parse(&parser);

    if (parser.had_error) {
        heluna_error_print(&parser.error);
        arena_destroy(arena);
        free(source);
        free(json_input);
        return 1;
    }

    /* Check */
    Checker checker;
    checker_init(&checker, prog, arena);
    int nerrs = checker_check(&checker);
    if (nerrs > 0) {
        for (int i = 0; i < checker.errors.count; i++) {
            heluna_error_print(&checker.errors.errors[i]);
        }
        arena_destroy(arena);
        free(source);
        free(json_input);
        return 1;
    }

    if (prog->contract->kind != CONTRACT_FUNCTION) {
        fprintf(stderr, "heluna-run: not a function contract\n");
        arena_destroy(arena);
        free(source);
        free(json_input);
        return 1;
    }

    /* Parse JSON input */
    HelunaError json_err = {0};
    HVal *input = json_parse(arena, json_input, &json_err);
    if (!input) {
        heluna_error_print(&json_err);
        arena_destroy(arena);
        free(source);
        free(json_input);
        return 1;
    }

    /* Evaluate */
    Evaluator ev;
    evaluator_init(&ev, prog, arena);
    HVal *result = evaluator_eval(&ev, input);

    if (ev.had_error) {
        heluna_error_print(&ev.error);
        arena_destroy(arena);
        free(source);
        free(json_input);
        return 1;
    }

    /* Emit JSON output */
    json_emit(result, stdout);
    printf("\n");

    arena_destroy(arena);
    free(source);
    free(json_input);
    return 0;
}

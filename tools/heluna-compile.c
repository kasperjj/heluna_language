/*
 * heluna-compile: compile a Heluna contract to a VM packet (.hlna).
 *
 * Usage: heluna-compile <file.heluna> [-o output.hlna]
 *
 * Pipeline: read → lex → parse → check → compile → write binary packet.
 * Only function contracts can be compiled.
 */

#include "heluna/compiler.h"
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
        fprintf(stderr, "heluna-compile: cannot open '%s'\n", path);
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *buf = malloc((size_t)size + 1);
    if (!buf) {
        fclose(f);
        fprintf(stderr, "heluna-compile: out of memory\n");
        return NULL;
    }

    size_t nread = fread(buf, 1, (size_t)size, f);
    buf[nread] = '\0';
    fclose(f);
    return buf;
}

static char *default_output_path(const char *input_path) {
    size_t len = strlen(input_path);
    const char *dot = strrchr(input_path, '.');

    size_t base_len = dot ? (size_t)(dot - input_path) : len;
    char *out = malloc(base_len + 6); /* .hlna + \0 */
    if (!out) return NULL;

    memcpy(out, input_path, base_len);
    memcpy(out + base_len, ".hlna", 6);
    return out;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: heluna-compile <file.heluna> [-o output.hlna]\n");
        return 1;
    }

    const char *input_path = argv[1];
    const char *output_path = NULL;

    /* Parse -o flag */
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            output_path = argv[++i];
        }
    }

    char *source = read_file(input_path);
    if (!source) return 1;

    Arena *arena = arena_create(64 * 1024);

    /* Lex */
    Lexer lex;
    lexer_init(&lex, source, input_path, arena);

    /* Parse */
    Parser parser;
    parser_init(&parser, &lex, arena);
    AstProgram *prog = parser_parse(&parser);

    if (parser.had_error) {
        heluna_error_print(&parser.error);
        arena_destroy(arena);
        free(source);
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
        fprintf(stderr, "%d error%s found\n", nerrs, nerrs == 1 ? "" : "s");
        arena_destroy(arena);
        free(source);
        return 1;
    }

    /* Compile */
    Compiler compiler;
    compiler_init(&compiler, prog, arena);
    PacketResult packet = compiler_compile(&compiler);

    if (compiler.errors.count > 0) {
        for (int i = 0; i < compiler.errors.count; i++) {
            heluna_error_print(&compiler.errors.errors[i]);
        }
        fprintf(stderr, "%d compile error%s\n",
                compiler.errors.count,
                compiler.errors.count == 1 ? "" : "s");
        arena_destroy(arena);
        free(source);
        return 1;
    }

    if (!packet.data || packet.size == 0) {
        fprintf(stderr, "heluna-compile: compilation produced no output\n");
        arena_destroy(arena);
        free(source);
        return 1;
    }

    /* Determine output path */
    char *default_out = NULL;
    if (!output_path) {
        default_out = default_output_path(input_path);
        output_path = default_out;
    }

    /* Write packet */
    FILE *out = fopen(output_path, "wb");
    if (!out) {
        fprintf(stderr, "heluna-compile: cannot write '%s'\n", output_path);
        free(default_out);
        arena_destroy(arena);
        free(source);
        return 1;
    }

    size_t written = fwrite(packet.data, 1, packet.size, out);
    fclose(out);

    if (written != packet.size) {
        fprintf(stderr, "heluna-compile: write error\n");
        free(default_out);
        arena_destroy(arena);
        free(source);
        return 1;
    }

    printf("%s → %s (%zu bytes)\n", input_path, output_path, packet.size);

    free(default_out);
    arena_destroy(arena);
    free(source);
    return 0;
}

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
#include <libgen.h>

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

/* ── Stdlib name check ──────────────────────────────────── */

static const char *stdlib_names[] = {
    "upper","lower","trim","trim-start","trim-end","substring","replace",
    "split","join","starts-with","ends-with","contains","length",
    "pad-left","pad-right","regex-match","regex-replace",
    "abs","ceil","floor","round","min","max","clamp",
    "sort","sort-by","reverse","unique","flatten","zip","range","slice",
    "keys","values","merge","pick","omit",
    "parse-date","format-date","date-diff","date-add","now-date",
    "base64-encode","base64-decode","url-encode","url-decode",
    "json-encode","json-parse","sha256","hmac-sha256","uuid",
    NULL
};

static int is_stdlib_name(const char *name) {
    for (const char **s = stdlib_names; *s; s++) {
        if (strcmp(*s, name) == 0) return 1;
    }
    return 0;
}

/* ── Dependency resolution ──────────────────────────────── */

static char *dep_sources[64];
static int   dep_source_count = 0;

static int resolve_deps(const char *base_dir, Arena *arena,
                        const AstProgram *prog,
                        CompilerDep *deps, int *dep_count, int max_deps) {
    const AstContract *ct = prog->contract;

    for (int ui = 0; ui < ct->uses_count; ui++) {
        const char *uname = ct->uses[ui];
        int found = 0;
        for (int i = 0; i < *dep_count; i++) {
            if (strcmp(deps[i].name, uname) == 0) { found = 1; break; }
        }
        if (found || *dep_count >= max_deps) continue;

        char dep_path[512];
        snprintf(dep_path, sizeof dep_path, "%s/%s.heluna", base_dir, uname);
        char *src = read_file(dep_path);
        if (!src) continue;
        dep_sources[dep_source_count++] = src;

        Lexer lex;
        lexer_init(&lex, src, dep_path, arena);
        Parser parser;
        parser_init(&parser, &lex, arena);
        AstProgram *dep_prog = parser_parse(&parser);
        if (parser.had_error) continue;

        Checker checker;
        checker_init(&checker, dep_prog, arena);
        if (checker_check(&checker) > 0) continue;

        deps[*dep_count].name = uname;
        deps[*dep_count].prog = dep_prog;
        (*dep_count)++;

        resolve_deps(base_dir, arena, dep_prog, deps, dep_count, max_deps);
    }

    for (const AstSanitizerDef *s = ct->sanitizers; s; s = s->next) {
        if (!s->impl_name || is_stdlib_name(s->impl_name)) continue;

        int found = 0;
        for (int i = 0; i < *dep_count; i++) {
            if (strcmp(deps[i].name, s->impl_name) == 0) { found = 1; break; }
        }
        if (found || *dep_count >= max_deps) continue;

        char dep_path[512];
        snprintf(dep_path, sizeof dep_path, "%s/%s.heluna",
                 base_dir, s->impl_name);
        char *src = read_file(dep_path);
        if (!src) continue;
        dep_sources[dep_source_count++] = src;

        Lexer lex;
        lexer_init(&lex, src, dep_path, arena);
        Parser parser;
        parser_init(&parser, &lex, arena);
        AstProgram *dep_prog = parser_parse(&parser);
        if (parser.had_error) continue;

        Checker checker;
        checker_init(&checker, dep_prog, arena);
        if (checker_check(&checker) > 0) continue;

        deps[*dep_count].name = s->impl_name;
        deps[*dep_count].prog = dep_prog;
        (*dep_count)++;

        resolve_deps(base_dir, arena, dep_prog, deps, dep_count, max_deps);
    }

    return 0;
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

    /* Resolve dependencies */
    char *input_copy = strdup(input_path);
    const char *base_dir = dirname(input_copy);
    CompilerDep deps[32];
    int dep_count = 0;
    resolve_deps(base_dir, arena, prog, deps, &dep_count, 32);
    free(input_copy);

    /* Compile */
    Compiler compiler;
    if (dep_count > 0) {
        compiler_init_with_deps(&compiler, prog, arena, deps, dep_count);
    } else {
        compiler_init(&compiler, prog, arena);
    }
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

    for (int i = 0; i < dep_source_count; i++) free(dep_sources[i]);
    free(default_out);
    arena_destroy(arena);
    free(source);
    return 0;
}

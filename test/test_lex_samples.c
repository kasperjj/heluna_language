/*
 * test_lex_samples: verify the lexer produces expected token streams
 * for every .heluna sample file.
 *
 * Each sample in test/samples/<name>.heluna has a corresponding golden
 * file test/expected/<name>.tokens containing one token kind per line.
 * This test lexes each sample and compares against its golden file.
 *
 * Run from the project root (make test does this automatically).
 */

#include "heluna/lexer.h"
#include "heluna/arena.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int tests_run    = 0;
static int tests_passed = 0;

/* ── File I/O helper ────────────────────────────────────── */

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *buf = malloc((size_t)size + 1);
    if (!buf) { fclose(f); return NULL; }

    size_t n = fread(buf, 1, (size_t)size, f);
    buf[n] = '\0';
    fclose(f);
    return buf;
}

/* ── Test one sample ────────────────────────────────────── */

static void test_sample(const char *name) {
    char heluna_path[256];
    char tokens_path[256];
    snprintf(heluna_path, sizeof heluna_path, "test/samples/%s.heluna", name);
    snprintf(tokens_path, sizeof tokens_path, "test/expected/%s.tokens", name);

    char *source = read_file(heluna_path);
    if (!source) {
        tests_run++;
        fprintf(stderr, "  FAIL: %s — cannot open %s\n", name, heluna_path);
        return;
    }

    char *expected = read_file(tokens_path);
    if (!expected) {
        tests_run++;
        fprintf(stderr, "  FAIL: %s — cannot open %s\n", name, tokens_path);
        free(source);
        return;
    }

    Arena *arena = arena_create(64 * 1024);
    Lexer lex;
    lexer_init(&lex, source, heluna_path, arena);

    char *cursor = expected;
    int index = 0;
    int ok = 1;

    for (;;) {
        /* Get next non-comment token from the lexer */
        Token tok;
        do {
            tok = lexer_next(&lex);
        } while (tok.kind == TOK_COMMENT);

        /* Read next non-empty line from the golden file */
        char exp_kind[64] = {0};
        while (*cursor) {
            char *eol = strchr(cursor, '\n');
            int len = eol ? (int)(eol - cursor) : (int)strlen(cursor);

            while (len > 0 && (cursor[len - 1] == '\r' || cursor[len - 1] == ' '))
                len--;

            char *next = eol ? eol + 1 : cursor + strlen(cursor);

            if (len > 0) {
                if (len >= (int)sizeof exp_kind) len = (int)sizeof exp_kind - 1;
                memcpy(exp_kind, cursor, (size_t)len);
                exp_kind[len] = '\0';
                cursor = next;
                break;
            }
            cursor = next;
        }

        /* No more expected tokens */
        if (exp_kind[0] == '\0') {
            if (tok.kind != TOK_EOF) {
                fprintf(stderr, "  FAIL: %s[%d] — expected end of tokens, got %s\n",
                        name, index, token_kind_name(tok.kind));
                ok = 0;
            }
            break;
        }

        /* Compare */
        const char *actual = token_kind_name(tok.kind);
        if (strcmp(actual, exp_kind) != 0) {
            fprintf(stderr, "  FAIL: %s[%d] — expected %s, got %s\n",
                    name, index, exp_kind, actual);
            ok = 0;
            break;
        }

        index++;
        if (tok.kind == TOK_EOF || tok.kind == TOK_ERROR) break;
    }

    tests_run++;
    if (ok) tests_passed++;

    arena_destroy(arena);
    free(source);
    free(expected);
}

/* ── Sample list ────────────────────────────────────────── */

static const char *samples[] = {
    "boolean-logic",
    "comments",
    "complex-types",
    "create-order",
    "describe-value",
    "empty-and-nothing",
    "float-arithmetic",
    "format-names",
    "full-name",
    "integer-arithmetic",
    "list-pipeline",
    "match-list-pattern",
    "match-literals",
    "match-record-pattern",
    "match-rule",
    "minimal-contract",
    "multiline-records",
    "nested-conditional",
    "nested-lists",
    "normalize-email",
    "operator-precedence",
    "patient-summary",
    "process-scores",
    "rectangle-area",
    "string-escapes",
    "string-operations",
    "tag-propagation",
    "ticket-price",
    "validate-user",
    NULL,
};

int main(void) {
    printf("test_lex_samples:\n");

    for (const char **s = samples; *s; s++) {
        test_sample(*s);
    }

    printf("  %d/%d passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}

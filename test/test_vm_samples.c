/*
 * test_vm_samples: compile each sample .heluna file, execute with
 * embedded test data, and verify output matches expected.
 *
 * For function contracts with test sections: parse the test case
 * input/output from the AST, compile → load → execute, compare result.
 *
 * Tag/source contracts: skip gracefully.
 */

#include "heluna/vm.h"
#include "heluna/compiler.h"
#include "heluna/checker.h"
#include "heluna/parser.h"
#include "heluna/json.h"
#include "heluna/arena.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int tests_run    = 0;
static int tests_passed = 0;

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

/* ── Helpers ─────────────────────────────────────────────── */

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

/* ── Dependency resolution ──────────────────────────────── */

static char *dep_sources[64];
static int   dep_source_count = 0;

static int resolve_deps(Arena *arena, const AstProgram *prog,
                        CompilerDep *deps, int *dep_count, int max_deps) {
    const AstContract *ct = prog->contract;

    /* Collect dep names from uses declarations */
    for (int ui = 0; ui < ct->uses_count; ui++) {
        const char *uname = ct->uses[ui];
        int found = 0;
        for (int i = 0; i < *dep_count; i++) {
            if (strcmp(deps[i].name, uname) == 0) { found = 1; break; }
        }
        if (found || *dep_count >= max_deps) continue;

        char dep_path[256];
        snprintf(dep_path, sizeof dep_path, "test/samples/%s.heluna", uname);
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

        resolve_deps(arena, dep_prog, deps, dep_count, max_deps);
    }

    /* Collect from sanitizer impl_name (non-stdlib) */
    for (const AstSanitizerDef *s = ct->sanitizers; s; s = s->next) {
        if (!s->impl_name || is_stdlib_name(s->impl_name)) continue;

        int found = 0;
        for (int i = 0; i < *dep_count; i++) {
            if (strcmp(deps[i].name, s->impl_name) == 0) { found = 1; break; }
        }
        if (found || *dep_count >= max_deps) continue;

        char dep_path[256];
        snprintf(dep_path, sizeof dep_path, "test/samples/%s.heluna", s->impl_name);
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

        resolve_deps(arena, dep_prog, deps, dep_count, max_deps);
    }

    return 0;
}

/* ── Source resolution ─────────────────────────────────────── */

/* Serialize an AST config (EXPR_RECORD with literal values) to JSON string */
static const char *config_to_json(Arena *arena, const AstExpr *config) {
    if (!config || config->kind != EXPR_RECORD) return NULL;

    /* Build JSON string manually from literals */
    char buf[2048];
    int pos = 0;
    buf[pos++] = '{';

    for (const AstLabel *l = config->as.record.labels; l; l = l->next) {
        if (pos > 1) buf[pos++] = ',';
        pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos,
                        "\"%s\":", l->name);
        if (l->value) {
            switch (l->value->kind) {
            case EXPR_STRING:
                /* Already includes quotes */
                pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos,
                                "%.*s", l->value->as.string_val.length,
                                l->value->as.string_val.value);
                break;
            case EXPR_INTEGER:
                pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos,
                                "%lld", l->value->as.integer_val);
                break;
            case EXPR_FLOAT:
                pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos,
                                "%g", l->value->as.float_val);
                break;
            case EXPR_TRUE:
                pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos, "true");
                break;
            case EXPR_FALSE:
                pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos, "false");
                break;
            default:
                pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos, "null");
                break;
            }
        }
    }
    buf[pos++] = '}';
    buf[pos] = '\0';
    return arena_strndup(arena, buf, (size_t)pos);
}

static int resolve_sources(Arena *arena, const AstProgram *prog,
                           CompilerSource *sources, int *source_count,
                           int max_sources) {
    const AstContract *ct = prog->contract;
    if (!ct->sources_refs) return 0;

    for (int si = 0; si < ct->sources_count; si++) {
        const char *sname = ct->sources_refs[si];
        /* Skip if already resolved */
        int found = 0;
        for (int i = 0; i < *source_count; i++) {
            if (strcmp(sources[i].name, sname) == 0) { found = 1; break; }
        }
        if (found || *source_count >= max_sources) continue;

        /* Parse the source contract file */
        char dep_path[256];
        snprintf(dep_path, sizeof dep_path, "test/samples/%s.heluna", sname);
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

        if (dep_prog->contract->kind != CONTRACT_SOURCE) continue;

        /* Extract config JSON */
        const char *config_json = config_to_json(arena, dep_prog->contract->config);

        /* Extract keyed-by field name */
        const char *keyed_by = NULL;
        if (dep_prog->contract->keyed_by) {
            keyed_by = dep_prog->contract->keyed_by->name;
        }

        sources[*source_count].name = sname;
        sources[*source_count].config_json = config_json;
        sources[*source_count].keyed_by = keyed_by;
        (*source_count)++;
    }

    return 0;
}

/* Convert an AST literal expression to an HVal for test case data */
static HVal *ast_literal_to_hval(Arena *arena, const AstExpr *e);

static HVal *ast_literal_to_hval(Arena *arena, const AstExpr *e) {
    if (!e) return NULL;

    HVal *v;
    switch (e->kind) {
    case EXPR_INTEGER:
        v = arena_calloc(arena, sizeof(HVal));
        v->kind = VAL_INTEGER;
        v->as.integer_val = e->as.integer_val;
        return v;

    case EXPR_FLOAT:
        v = arena_calloc(arena, sizeof(HVal));
        v->kind = VAL_FLOAT;
        v->as.float_val = e->as.float_val;
        return v;

    case EXPR_STRING: {
        v = arena_calloc(arena, sizeof(HVal));
        v->kind = VAL_STRING;
        /* Strip surrounding quotes and resolve escapes */
        const char *raw = e->as.string_val.value;
        int len = e->as.string_val.length;
        if (len >= 2 && raw[0] == '"' && raw[len-1] == '"') {
            /* Remove quotes, resolve basic escapes */
            char *buf = arena_alloc(arena, (size_t)len);
            int j = 0;
            for (int i = 1; i < len - 1; i++) {
                if (raw[i] == '\\' && i + 1 < len - 1) {
                    i++;
                    switch (raw[i]) {
                    case 'n': buf[j++] = '\n'; break;
                    case 't': buf[j++] = '\t'; break;
                    case '\\': buf[j++] = '\\'; break;
                    case '"': buf[j++] = '"'; break;
                    default: buf[j++] = raw[i]; break;
                    }
                } else {
                    buf[j++] = raw[i];
                }
            }
            buf[j] = '\0';
            v->as.string_val = buf;
        } else {
            v->as.string_val = arena_strndup(arena, raw, (size_t)len);
        }
        return v;
    }

    case EXPR_TRUE:
        v = arena_calloc(arena, sizeof(HVal));
        v->kind = VAL_BOOLEAN;
        v->as.boolean_val = 1;
        return v;

    case EXPR_FALSE:
        v = arena_calloc(arena, sizeof(HVal));
        v->kind = VAL_BOOLEAN;
        v->as.boolean_val = 0;
        return v;

    case EXPR_NOTHING:
        v = arena_calloc(arena, sizeof(HVal));
        v->kind = VAL_NOTHING;
        return v;

    case EXPR_LIST: {
        HVal *head = NULL;
        HVal **tail = &head;
        for (const AstExpr *elem = e->as.list.elements; elem; elem = elem->next) {
            HVal *item = ast_literal_to_hval(arena, elem);
            if (!item) continue;
            HVal *copy = arena_calloc(arena, sizeof(HVal));
            *copy = *item;
            copy->next = NULL;
            *tail = copy;
            tail = &copy->next;
        }
        v = arena_calloc(arena, sizeof(HVal));
        v->kind = VAL_LIST;
        v->as.list_head = head;
        return v;
    }

    case EXPR_RECORD: {
        HField *fields = NULL;
        HField **tail = &fields;
        for (const AstLabel *l = e->as.record.labels; l; l = l->next) {
            HField *f = arena_calloc(arena, sizeof(HField));
            f->name = l->name;
            f->value = ast_literal_to_hval(arena, l->value);
            *tail = f;
            tail = &f->next;
        }
        v = arena_calloc(arena, sizeof(HVal));
        v->kind = VAL_RECORD;
        v->as.record_fields = fields;
        return v;
    }

    case EXPR_UNARY_NEG: {
        HVal *inner = ast_literal_to_hval(arena, e->as.unary.operand);
        if (!inner) return NULL;
        v = arena_calloc(arena, sizeof(HVal));
        if (inner->kind == VAL_INTEGER) {
            v->kind = VAL_INTEGER;
            v->as.integer_val = -inner->as.integer_val;
        } else if (inner->kind == VAL_FLOAT) {
            v->kind = VAL_FLOAT;
            v->as.float_val = -inner->as.float_val;
        } else {
            return inner;
        }
        return v;
    }

    default:
        return NULL;
    }
}

/* Deep compare two HVals, tolerating float precision */
static int values_match(const HVal *actual, const HVal *expected) {
    if (!actual && !expected) return 1;
    if (!actual || !expected) return 0;

    /* Allow integer/float cross-comparison for test cases like price: 5.0 */
    if (actual->kind == VAL_INTEGER && expected->kind == VAL_FLOAT) {
        double diff = (double)actual->as.integer_val - expected->as.float_val;
        return diff > -1e-9 && diff < 1e-9;
    }
    if (actual->kind == VAL_FLOAT && expected->kind == VAL_INTEGER) {
        double diff = actual->as.float_val - (double)expected->as.integer_val;
        return diff > -1e-9 && diff < 1e-9;
    }

    if (actual->kind != expected->kind) return 0;

    switch (actual->kind) {
    case VAL_INTEGER:
        return actual->as.integer_val == expected->as.integer_val;
    case VAL_FLOAT: {
        double diff = actual->as.float_val - expected->as.float_val;
        return diff > -1e-9 && diff < 1e-9;
    }
    case VAL_STRING:
        return strcmp(actual->as.string_val, expected->as.string_val) == 0;
    case VAL_BOOLEAN:
        return actual->as.boolean_val == expected->as.boolean_val;
    case VAL_NOTHING:
        return 1;
    case VAL_LIST: {
        const HVal *a = actual->as.list_head;
        const HVal *e = expected->as.list_head;
        while (a && e) {
            if (!values_match(a, e)) return 0;
            a = a->next;
            e = e->next;
        }
        return !a && !e;
    }
    case VAL_RECORD: {
        /* Check that all expected fields exist and match */
        for (const HField *ef = expected->as.record_fields; ef; ef = ef->next) {
            int found = 0;
            for (const HField *af = actual->as.record_fields; af; af = af->next) {
                if (strcmp(af->name, ef->name) == 0) {
                    if (!values_match(af->value, ef->value)) return 0;
                    found = 1;
                    break;
                }
            }
            if (!found) return 0;
        }
        return 1;
    }
    }
    return 0;
}

/* ── Emit HVal to string for diagnostics ─────────────────── */

static void emit_val_to_file(const HVal *val, FILE *f);

static void emit_val_to_file(const HVal *val, FILE *f) {
    if (!val) { fputs("null", f); return; }
    json_emit(val, f);
}

/* ── Test one sample ─────────────────────────────────────── */

static void test_sample(const char *name) {
    char path[256];
    snprintf(path, sizeof path, "test/samples/%s.heluna", name);

    char *source = read_file(path);
    if (!source) {
        tests_run++;
        fprintf(stderr, "  FAIL: %s — cannot open %s\n", name, path);
        return;
    }

    Arena *arena = arena_create(128 * 1024);

    /* Parse */
    Lexer lex;
    lexer_init(&lex, source, path, arena);
    Parser parser;
    parser_init(&parser, &lex, arena);
    AstProgram *prog = parser_parse(&parser);

    if (parser.had_error) {
        tests_run++;
        fprintf(stderr, "  FAIL: %s — parse error\n", name);
        arena_destroy(arena);
        free(source);
        return;
    }

    /* Skip non-function contracts */
    if (prog->contract->kind != CONTRACT_FUNCTION) {
        tests_run++;
        tests_passed++;
        arena_destroy(arena);
        free(source);
        return;
    }

    /* Check */
    Checker checker;
    checker_init(&checker, prog, arena);
    if (checker_check(&checker) > 0) {
        tests_run++;
        fprintf(stderr, "  FAIL: %s — checker errors\n", name);
        arena_destroy(arena);
        free(source);
        return;
    }

    /* Resolve dependencies */
    CompilerDep deps[32];
    int dep_count = 0;
    dep_source_count = 0;
    resolve_deps(arena, prog, deps, &dep_count, 32);

    /* Resolve source dependencies */
    CompilerSource sources[16];
    int source_count = 0;
    resolve_sources(arena, prog, sources, &source_count, 16);

    /* Compile */
    Compiler compiler;
    if (source_count > 0) {
        compiler_init_with_sources(&compiler, prog, arena,
                                   deps, dep_count, sources, source_count);
    } else if (dep_count > 0) {
        compiler_init_with_deps(&compiler, prog, arena, deps, dep_count);
    } else {
        compiler_init(&compiler, prog, arena);
    }
    PacketResult packet = compiler_compile(&compiler);

    if (!packet.data || packet.size == 0) {
        tests_run++;
        fprintf(stderr, "  FAIL: %s — compilation failed\n", name);
        for (int i = 0; i < dep_source_count; i++) free(dep_sources[i]);
        arena_destroy(arena);
        free(source);
        return;
    }

    /* Load packet */
    HelunaError load_err = {0};
    VmPacket *vmpkt = vm_load_packet(packet.data, packet.size,
                                     arena, &load_err);
    if (!vmpkt) {
        tests_run++;
        fprintf(stderr, "  FAIL: %s — packet load: %s\n",
                name, load_err.message);
        arena_destroy(arena);
        free(source);
        return;
    }

    /* Run test cases from the contract's test section */
    const AstContract *ct = prog->contract;
    int has_tests = 0;

    for (const AstTestCase *tc = ct->tests; tc; tc = tc->next) {
        has_tests = 1;
        tests_run++;

        /* Convert AST given/expect records to HVal */
        HVal *input = ast_literal_to_hval(arena, tc->given);
        HVal *expected = ast_literal_to_hval(arena, tc->expect);

        if (!input) {
            fprintf(stderr, "  FAIL: %s test \"%s\" — cannot convert input\n",
                    name, tc->name);
            continue;
        }

        /* Execute */
        Vm vm;
        vm_init(&vm, vmpkt, arena);
        HVal *actual = vm_execute(&vm, input);

        if (vm.had_error || !actual) {
            fprintf(stderr, "  FAIL: %s test \"%s\" — runtime error: %s\n",
                    name, tc->name, vm.error.message);
            continue;
        }

        /* Compare */
        if (values_match(actual, expected)) {
            tests_passed++;
        } else {
            fprintf(stderr, "  FAIL: %s test \"%s\" — output mismatch\n",
                    name, tc->name);
            fprintf(stderr, "    expected: ");
            emit_val_to_file(expected, stderr);
            fprintf(stderr, "\n    actual:   ");
            emit_val_to_file(actual, stderr);
            fprintf(stderr, "\n");
        }
    }

    /* If no test cases, just verify it runs without error */
    if (!has_tests) {
        tests_run++;

        /* Build a minimal input record with default values */
        HField *fields = NULL;
        HField **tail = &fields;
        for (const AstFieldDecl *f = ct->input; f; f = f->next) {
            HField *hf = arena_calloc(arena, sizeof(HField));
            hf->name = f->name;
            /* Default values based on type */
            HVal *def = arena_calloc(arena, sizeof(HVal));
            if (f->type) {
                switch (f->type->kind) {
                case TYPE_INTEGER:
                    def->kind = VAL_INTEGER;
                    def->as.integer_val = 0;
                    break;
                case TYPE_FLOAT:
                    def->kind = VAL_FLOAT;
                    def->as.float_val = 0.0;
                    break;
                case TYPE_STRING:
                    def->kind = VAL_STRING;
                    def->as.string_val = "";
                    break;
                case TYPE_BOOLEAN:
                    def->kind = VAL_BOOLEAN;
                    def->as.boolean_val = 0;
                    break;
                default:
                    def->kind = VAL_NOTHING;
                    break;
                }
            } else {
                def->kind = VAL_STRING;
                def->as.string_val = "";
            }
            hf->value = def;
            *tail = hf;
            tail = &hf->next;
        }

        HVal *input = arena_calloc(arena, sizeof(HVal));
        input->kind = VAL_RECORD;
        input->as.record_fields = fields;

        Vm vm;
        vm_init(&vm, vmpkt, arena);
        HVal *result = vm_execute(&vm, input);

        if (result && !vm.had_error) {
            tests_passed++;
        } else {
            fprintf(stderr, "  FAIL: %s — runtime error with defaults: %s\n",
                    name, vm.error.message);
        }
    }

    for (int i = 0; i < dep_source_count; i++) free(dep_sources[i]);
    arena_destroy(arena);
    free(source);
}

/* ── Sample list ─────────────────────────────────────────── */

static const char *samples[] = {
    "boolean-logic",
    "bracket-age",
    "comments",
    "company-security",
    "complex-types",
    "customers-source",
    "create-order",
    "describe-value",
    "empty-and-nothing",
    "enrich-order",
    "float-arithmetic",
    "forbid-field-rule",
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
    printf("test_vm_samples:\n");

    for (const char **s = samples; *s; s++) {
        test_sample(*s);
    }

    printf("  %d/%d passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}

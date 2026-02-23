#include "heluna/checker.h"
#include <string.h>
#include <stdarg.h>
#include <stdio.h>

/* ── Stdlib function table ───────────────────────────────── */

static const char *stdlib_functions[] = {
    /* string */
    "upper", "lower", "trim", "trim-start", "trim-end",
    "substring", "replace", "split", "join",
    "starts-with", "ends-with", "contains", "length",
    "pad-left", "pad-right",
    "regex-match", "regex-replace",
    /* numeric */
    "abs", "ceil", "floor", "round", "min", "max", "clamp",
    /* list */
    "sort", "sort-by", "reverse", "unique", "flatten",
    "zip", "range", "slice", "fold",
    /* record */
    "keys", "values", "merge", "pick", "omit",
    /* date */
    "parse-date", "format-date", "date-diff", "date-add", "now-date",
    /* encoding */
    "base64-encode", "base64-decode",
    "url-encode", "url-decode",
    "json-encode", "json-parse",
    /* crypto */
    "sha256", "hmac-sha256", "uuid",
    /* conversion */
    "to-string", "to-float", "to-integer",
    NULL,
};

static int is_stdlib(const char *name) {
    for (const char **p = stdlib_functions; *p; p++) {
        if (strcmp(name, *p) == 0) return 1;
    }
    return 0;
}

/* ── Error helpers ───────────────────────────────────────── */

static void add_error(Checker *c, HelunaErrorKind kind, SrcLoc loc,
                      const char *fmt, ...) {
    CheckerErrors *e = &c->errors;
    if (e->count == e->capacity) {
        int new_cap = e->capacity ? e->capacity * 2 : 16;
        HelunaError *new_arr = arena_alloc(e->arena, (size_t)new_cap * sizeof(HelunaError));
        if (e->count > 0) {
            memcpy(new_arr, e->errors, (size_t)e->count * sizeof(HelunaError));
        }
        e->errors = new_arr;
        e->capacity = new_cap;
    }

    HelunaError *err = &e->errors[e->count++];
    err->kind = kind;
    err->loc = loc;

    va_list ap;
    va_start(ap, fmt);
    vsnprintf(err->message, sizeof(err->message), fmt, ap);
    va_end(ap);
}

/* ── Scope helpers ───────────────────────────────────────── */

static void scope_push(Checker *c, const char *name, ScopeEntryKind kind,
                        SrcLoc loc) {
    if (c->scope_count == c->scope_capacity) {
        int new_cap = c->scope_capacity ? c->scope_capacity * 2 : 32;
        ScopeEntry *new_arr = arena_alloc(c->arena,
                                          (size_t)new_cap * sizeof(ScopeEntry));
        if (c->scope_count > 0) {
            memcpy(new_arr, c->scope, (size_t)c->scope_count * sizeof(ScopeEntry));
        }
        c->scope = new_arr;
        c->scope_capacity = new_cap;
    }

    ScopeEntry *entry = &c->scope[c->scope_count++];
    entry->name = name;
    entry->kind = kind;
    entry->loc = loc;
}

static const ScopeEntry *scope_lookup(const Checker *c, const char *name) {
    for (int i = c->scope_count - 1; i >= 0; i--) {
        if (strcmp(c->scope[i].name, name) == 0) {
            return &c->scope[i];
        }
    }
    return NULL;
}

/* ── Contract field lookup ───────────────────────────────── */

static int has_input_field(const Checker *c, const char *name) {
    for (const AstFieldDecl *f = c->prog->contract->input; f; f = f->next) {
        if (strcmp(f->name, name) == 0) return 1;
    }
    return 0;
}

static int has_output_field(const Checker *c, const char *name) {
    for (const AstFieldDecl *f = c->prog->contract->output; f; f = f->next) {
        if (strcmp(f->name, name) == 0) return 1;
    }
    return 0;
}

static int has_tag(const Checker *c, const char *name) {
    for (const AstTagDef *t = c->prog->contract->tags; t; t = t->next) {
        if (strcmp(t->name, name) == 0) return 1;
    }
    return 0;
}

static int is_uses(const Checker *c, const char *name) {
    const AstContract *ct = c->prog->contract;
    if (!ct->uses) return 0;
    for (int i = 0; i < ct->uses_count; i++) {
        if (strcmp(ct->uses[i], name) == 0) return 1;
    }
    return 0;
}

static int is_sanitizer(const Checker *c, const char *name) {
    for (const AstSanitizerDef *s = c->prog->contract->sanitizers; s; s = s->next) {
        if (strcmp(s->name, name) == 0) return 1;
    }
    return 0;
}

static int is_source(const Checker *c, const char *name) {
    const AstContract *ct = c->prog->contract;
    if (!ct->sources_refs) return 0;
    for (int i = 0; i < ct->sources_count; i++) {
        if (strcmp(ct->sources_refs[i], name) == 0) return 1;
    }
    return 0;
}

/* ── Forward declarations ────────────────────────────────── */

static void check_contract(Checker *c);
static void check_tags(Checker *c);
static void check_sanitizers(Checker *c);
static void check_rules(Checker *c);
static void check_tests(Checker *c);
static void check_function(Checker *c);
static void check_expr(Checker *c, AstExpr *e);
static void check_pattern_bindings(Checker *c, AstPattern *p);
static void check_acyclicity(Checker *c);

/* ── Contract structure checks ───────────────────────────── */

static void check_contract(Checker *c) {
    const AstContract    *ct = c->prog->contract;
    const AstFunctionDef *fn = c->prog->function;

    /* Tag and source contracts have no function */
    if (ct->kind == CONTRACT_TAG || ct->kind == CONTRACT_SOURCE) {
        /* No duplicate tag definitions */
        for (const AstTagDef *t = ct->tags; t; t = t->next) {
            for (const AstTagDef *u = t->next; u; u = u->next) {
                if (strcmp(t->name, u->name) == 0) {
                    add_error(c, HELUNA_ERR_CONTRACT, u->loc,
                              "duplicate tag definition '%s'", u->name);
                }
            }
        }

        /* Source contracts: check keyed-by and returns exist */
        if (ct->kind == CONTRACT_SOURCE) {
            if (!ct->source_name) {
                add_error(c, HELUNA_ERR_CONTRACT, ct->loc,
                          "source contract '%s' has no source name", ct->name);
            }
            if (!ct->keyed_by) {
                add_error(c, HELUNA_ERR_CONTRACT, ct->loc,
                          "source contract '%s' has no keyed-by fields", ct->name);
            }
            if (!ct->returns_type) {
                add_error(c, HELUNA_ERR_CONTRACT, ct->loc,
                          "source contract '%s' has no returns type", ct->name);
            }
        }

        /* Tag contracts: must have at least one tag */
        if (ct->kind == CONTRACT_TAG && !ct->tags) {
            add_error(c, HELUNA_ERR_CONTRACT, ct->loc,
                      "tag contract '%s' has no tags", ct->name);
        }

        return;
    }

    /* Function contract checks below */

    /* Contract name must match function name */
    if (fn && strcmp(ct->name, fn->name) != 0) {
        add_error(c, HELUNA_ERR_CONTRACT, fn->loc,
                  "function name '%s' does not match contract name '%s'",
                  fn->name, ct->name);
    }

    /* Must have at least one input field */
    if (!ct->input) {
        add_error(c, HELUNA_ERR_CONTRACT, ct->loc,
                  "contract '%s' has no input fields", ct->name);
    }

    /* Must have at least one output field */
    if (!ct->output) {
        add_error(c, HELUNA_ERR_CONTRACT, ct->loc,
                  "contract '%s' has no output fields", ct->name);
    }

    /* No duplicate input field names */
    for (const AstFieldDecl *f = ct->input; f; f = f->next) {
        for (const AstFieldDecl *g = f->next; g; g = g->next) {
            if (strcmp(f->name, g->name) == 0) {
                add_error(c, HELUNA_ERR_CONTRACT, g->loc,
                          "duplicate input field '%s'", g->name);
            }
        }
    }

    /* No duplicate output field names */
    for (const AstFieldDecl *f = ct->output; f; f = f->next) {
        for (const AstFieldDecl *g = f->next; g; g = g->next) {
            if (strcmp(f->name, g->name) == 0) {
                add_error(c, HELUNA_ERR_CONTRACT, g->loc,
                          "duplicate output field '%s'", g->name);
            }
        }
    }

    /* No duplicate tag definitions */
    for (const AstTagDef *t = ct->tags; t; t = t->next) {
        for (const AstTagDef *u = t->next; u; u = u->next) {
            if (strcmp(t->name, u->name) == 0) {
                add_error(c, HELUNA_ERR_CONTRACT, u->loc,
                          "duplicate tag definition '%s'", u->name);
            }
        }
    }

    /* No duplicate sanitizer definitions */
    for (const AstSanitizerDef *s = ct->sanitizers; s; s = s->next) {
        for (const AstSanitizerDef *t = s->next; t; t = t->next) {
            if (strcmp(s->name, t->name) == 0) {
                add_error(c, HELUNA_ERR_CONTRACT, t->loc,
                          "duplicate sanitizer definition '%s'", t->name);
            }
        }
    }

    /* No duplicate test case names */
    for (const AstTestCase *t = ct->tests; t; t = t->next) {
        for (const AstTestCase *u = t->next; u; u = u->next) {
            if (strcmp(t->name, u->name) == 0) {
                add_error(c, HELUNA_ERR_CONTRACT, u->loc,
                          "duplicate test case '%s'", u->name);
            }
        }
    }
}

/* ── Tag coherence ───────────────────────────────────────── */

static void check_tags(Checker *c) {
    const AstContract *ct = c->prog->contract;

    /* Tags used in field annotations must be declared */
    for (const AstFieldDecl *f = ct->input; f; f = f->next) {
        for (int i = 0; i < f->tag_count; i++) {
            if (!has_tag(c, f->tags[i])) {
                add_error(c, HELUNA_ERR_TAG, f->loc,
                          "undeclared tag '%s' on input field '%s'",
                          f->tags[i], f->name);
            }
        }
    }
    for (const AstFieldDecl *f = ct->output; f; f = f->next) {
        for (int i = 0; i < f->tag_count; i++) {
            if (!has_tag(c, f->tags[i])) {
                add_error(c, HELUNA_ERR_TAG, f->loc,
                          "undeclared tag '%s' on output field '%s'",
                          f->tags[i], f->name);
            }
        }
    }
}

/* ── Sanitizer coherence ─────────────────────────────────── */

static void check_sanitizers(Checker *c) {
    const AstContract *ct = c->prog->contract;

    /* Tags referenced in sanitizer strips lists must be declared */
    for (const AstSanitizerDef *s = ct->sanitizers; s; s = s->next) {
        for (int i = 0; i < s->stripped_count; i++) {
            if (!has_tag(c, s->stripped_tags[i])) {
                add_error(c, HELUNA_ERR_TAG, s->loc,
                          "sanitizer '%s' strips undeclared tag '%s'",
                          s->name, s->stripped_tags[i]);
            }
        }

        /* Validate using clause: impl must be stdlib or uses function */
        if (s->impl_name) {
            if (!is_stdlib(s->impl_name) && !is_uses(c, s->impl_name)) {
                add_error(c, HELUNA_ERR_CONTRACT, s->loc,
                          "sanitizer '%s' using '%s': not a stdlib or uses function",
                          s->name, s->impl_name);
            }
        }
    }
}

/* ── Rule validation ─────────────────────────────────────── */

static void check_field_ref(Checker *c, const AstFieldRef *ref) {
    if (ref->accessor_count < 1) return;

    const char *field_name = ref->accessors[0];
    if (ref->is_output) {
        if (!has_output_field(c, field_name)) {
            add_error(c, HELUNA_ERR_CONTRACT, ref->loc,
                      "rule references unknown output field '%s'", field_name);
        }
    } else {
        if (!has_input_field(c, field_name)) {
            add_error(c, HELUNA_ERR_CONTRACT, ref->loc,
                      "rule references unknown input field '%s'", field_name);
        }
    }
}

static void check_rules(Checker *c) {
    const AstContract *ct = c->prog->contract;

    for (const AstRule *r = ct->rules; r; r = r->next) {
        switch (r->kind) {
        case RULE_FORBID_FIELD:
            check_field_ref(c, r->as.forbid_field.field_ref);
            break;

        case RULE_FORBID_TAGGED:
            if (!has_tag(c, r->as.forbid_tagged.tag_name)) {
                add_error(c, HELUNA_ERR_TAG, r->loc,
                          "forbid rule references undeclared tag '%s'",
                          r->as.forbid_tagged.tag_name);
            }
            break;

        case RULE_REQUIRE:
            check_field_ref(c, r->as.require.field_ref);
            break;

        case RULE_MATCH:
            check_field_ref(c, r->as.match.field_ref);
            break;
        }
    }
}

/* ── Test case field validation ──────────────────────────── */

static void check_tests(Checker *c) {
    const AstContract *ct = c->prog->contract;

    for (const AstTestCase *tc = ct->tests; tc; tc = tc->next) {
        /* Validate given record fields against input schema */
        if (tc->given && tc->given->kind == EXPR_RECORD) {
            for (const AstLabel *l = tc->given->as.record.labels; l; l = l->next) {
                if (!has_input_field(c, l->name)) {
                    add_error(c, HELUNA_ERR_CONTRACT, l->loc,
                              "test '%s' given field '%s' is not in contract input",
                              tc->name, l->name);
                }
            }
        }

        /* Validate expect record fields against output schema */
        if (tc->expect && tc->expect->kind == EXPR_RECORD) {
            for (const AstLabel *l = tc->expect->as.record.labels; l; l = l->next) {
                if (!has_output_field(c, l->name)) {
                    add_error(c, HELUNA_ERR_CONTRACT, l->loc,
                              "test '%s' expect field '%s' is not in contract output",
                              tc->name, l->name);
                }
            }
        }
    }
}

/* ── Pattern binding collection ──────────────────────────── */

static void check_pattern_bindings(Checker *c, AstPattern *p) {
    if (!p) return;

    switch (p->kind) {
    case PAT_LITERAL:
    case PAT_WILDCARD:
    case PAT_RANGE:
        /* No bindings */
        break;

    case PAT_BINDING: {
        const char *name = p->as.binding.name;
        const ScopeEntry *existing = scope_lookup(c, name);
        if (existing) {
            add_error(c, HELUNA_ERR_CONTRACT, p->loc,
                      "match binding '%s' shadows existing '%s'",
                      name, name);
        } else {
            scope_push(c, name, SCOPE_MATCH_BINDING, p->loc);
        }
        break;
    }

    case PAT_LIST:
        for (AstPatternElem *el = p->as.list.elements; el; el = el->next) {
            check_pattern_bindings(c, el->pattern);
        }
        if (p->as.list.rest_name && p->as.list.rest_name[0] != '\0') {
            const char *name = p->as.list.rest_name;
            const ScopeEntry *existing = scope_lookup(c, name);
            if (existing) {
                add_error(c, HELUNA_ERR_CONTRACT, p->loc,
                          "rest binding '%s' shadows existing '%s'",
                          name, name);
            } else {
                scope_push(c, name, SCOPE_MATCH_BINDING, p->loc);
            }
        }
        break;

    case PAT_RECORD:
        for (AstFieldPattern *fp = p->as.record.fields; fp; fp = fp->next) {
            check_pattern_bindings(c, fp->pattern);
        }
        break;
    }
}

/* ── Expression walker ───────────────────────────────────── */

static void check_expr(Checker *c, AstExpr *e) {
    if (!e) return;

    switch (e->kind) {
    case EXPR_INTEGER:
    case EXPR_FLOAT:
    case EXPR_STRING:
    case EXPR_TRUE:
    case EXPR_FALSE:
    case EXPR_NOTHING:
        /* Literals — nothing to check */
        break;

    case EXPR_IDENT: {
        const char *name = e->as.ident.name;
        if (!scope_lookup(c, name)) {
            add_error(c, HELUNA_ERR_CONTRACT, e->loc,
                      "undefined identifier '%s'", name);
        }
        break;
    }

    case EXPR_INPUT_REF: {
        const char *name = e->as.input_ref.name;
        if (!has_input_field(c, name)) {
            add_error(c, HELUNA_ERR_CONTRACT, e->loc,
                      "input reference '$%s' does not match any input field",
                      name);
        }
        break;
    }

    case EXPR_BINARY:
        check_expr(c, e->as.binary.left);
        check_expr(c, e->as.binary.right);
        break;

    case EXPR_UNARY_NEG:
        check_expr(c, e->as.unary.operand);
        break;

    case EXPR_NOT:
        check_expr(c, e->as.not_expr.operand);
        break;

    case EXPR_IF:
        for (AstIfBranch *b = e->as.if_expr.branches; b; b = b->next) {
            if (b->condition) check_expr(c, b->condition);
            check_expr(c, b->body);
        }
        break;

    case EXPR_MATCH: {
        check_expr(c, e->as.match.subject);
        for (AstWhenClause *cl = e->as.match.clauses; cl; cl = cl->next) {
            int mark = c->scope_count;
            if (cl->pattern) check_pattern_bindings(c, cl->pattern);
            if (cl->guard)   check_expr(c, cl->guard);
            check_expr(c, cl->body);
            c->scope_count = mark;
        }
        if (e->as.match.else_body) {
            check_expr(c, e->as.match.else_body);
        }
        break;
    }

    case EXPR_LET: {
        /* Check binding expression (name NOT yet in scope) */
        check_expr(c, e->as.let.binding);

        /* Check for shadowing */
        const char *name = e->as.let.name;
        const ScopeEntry *existing = scope_lookup(c, name);
        if (existing) {
            add_error(c, HELUNA_ERR_CONTRACT, e->loc,
                      "let binding '%s' shadows existing '%s'", name, name);
        }

        /* Push name and check body */
        int mark = c->scope_count;
        scope_push(c, name, SCOPE_LET, e->loc);
        check_expr(c, e->as.let.body);
        c->scope_count = mark;
        break;
    }

    case EXPR_FILTER: {
        /* Check list expression */
        check_expr(c, e->as.filter.list);

        /* Check for shadowing */
        const char *var = e->as.filter.var_name;
        const ScopeEntry *existing = scope_lookup(c, var);
        if (existing) {
            add_error(c, HELUNA_ERR_CONTRACT, e->loc,
                      "filter variable '%s' shadows existing '%s'", var, var);
        }

        /* Push variable and check predicate */
        int mark = c->scope_count;
        scope_push(c, var, SCOPE_FILTER_VAR, e->loc);
        check_expr(c, e->as.filter.predicate);
        c->scope_count = mark;
        break;
    }

    case EXPR_MAP: {
        /* Check list expression */
        check_expr(c, e->as.map.list);

        /* Check for shadowing */
        const char *var = e->as.map.var_name;
        const ScopeEntry *existing = scope_lookup(c, var);
        if (existing) {
            add_error(c, HELUNA_ERR_CONTRACT, e->loc,
                      "map variable '%s' shadows existing '%s'", var, var);
        }

        /* Push variable and check body */
        int mark = c->scope_count;
        scope_push(c, var, SCOPE_MAP_VAR, e->loc);
        check_expr(c, e->as.map.body);
        c->scope_count = mark;
        break;
    }

    case EXPR_THROUGH:
        check_expr(c, e->as.through.left);
        check_expr(c, e->as.through.right);
        break;

    case EXPR_CALL: {
        const char *name = e->as.call.name;

        /* Check for self-recursion */
        if (strcmp(name, c->prog->function->name) == 0) {
            add_error(c, HELUNA_ERR_CONTRACT, e->loc,
                      "function '%s' calls itself (recursion not allowed)",
                      name);
        }
        /* Validate call target */
        else if (!is_stdlib(name) && !is_uses(c, name) &&
                 !is_sanitizer(c, name)) {
            add_error(c, HELUNA_ERR_CONTRACT, e->loc,
                      "unknown function '%s'", name);
        }

        /* Check argument expression */
        check_expr(c, e->as.call.arg);
        break;
    }

    case EXPR_RECORD:
        for (AstLabel *l = e->as.record.labels; l; l = l->next) {
            check_expr(c, l->value);
        }
        break;

    case EXPR_LIST:
        for (AstExpr *el = e->as.list.elements; el; el = el->next) {
            check_expr(c, el);
        }
        break;

    case EXPR_ACCESS:
        check_expr(c, e->as.access.object);
        break;

    case EXPR_PAREN:
        check_expr(c, e->as.paren.inner);
        break;

    case EXPR_LOOKUP: {
        const char *source = e->as.lookup.source_name;
        if (!is_source(c, source)) {
            add_error(c, HELUNA_ERR_CONTRACT, e->loc,
                      "lookup references undeclared source '%s'", source);
        }
        for (AstLookupKey *lk = e->as.lookup.keys; lk; lk = lk->next) {
            check_expr(c, lk->value);
        }
        break;
    }
    }
}

/* ── Function body check ─────────────────────────────────── */

static void check_function(Checker *c) {
    /* Push all input field names into scope */
    for (const AstFieldDecl *f = c->prog->contract->input; f; f = f->next) {
        scope_push(c, f->name, SCOPE_INPUT, f->loc);
    }

    /* Walk the function body */
    check_expr(c, c->prog->function->body);
}

/* ── Acyclicity check (DFS) ──────────────────────────────── */

#define DFS_UNVISITED  0
#define DFS_IN_PROGRESS 1
#define DFS_DONE       2

static int find_dep_node(const DepGraph *g, const char *name) {
    for (int i = 0; i < g->count; i++) {
        if (strcmp(g->nodes[i].name, name) == 0) return i;
    }
    return -1;
}

static int dfs_visit(Checker *c, const DepGraph *g, int node_idx,
                     int *state, int *path, int path_len) {
    state[node_idx] = DFS_IN_PROGRESS;
    path[path_len] = node_idx;
    path_len++;

    const DepGraphNode *n = &g->nodes[node_idx];
    for (int d = 0; d < n->dep_count; d++) {
        int dep_idx = find_dep_node(g, n->deps[d]);
        if (dep_idx < 0) continue; /* external dep — skip */

        if (state[dep_idx] == DFS_IN_PROGRESS) {
            /* Cycle detected — build path string */
            char msg[512];
            int pos = 0;
            pos += snprintf(msg + pos, sizeof(msg) - (size_t)pos,
                            "dependency cycle: ");
            /* Find where the cycle starts in the path */
            int start = 0;
            for (int i = 0; i < path_len; i++) {
                if (path[i] == dep_idx) { start = i; break; }
            }
            for (int i = start; i < path_len && pos < (int)sizeof(msg) - 20; i++) {
                if (i > start) pos += snprintf(msg + pos, sizeof(msg) - (size_t)pos, " -> ");
                pos += snprintf(msg + pos, sizeof(msg) - (size_t)pos,
                                "%s", g->nodes[path[i]].name);
            }
            snprintf(msg + pos, sizeof(msg) - (size_t)pos,
                     " -> %s", g->nodes[dep_idx].name);
            add_error(c, HELUNA_ERR_CONTRACT, c->prog->contract->loc,
                      "%s", msg);
            return 1;
        }
        if (state[dep_idx] == DFS_UNVISITED) {
            if (dfs_visit(c, g, dep_idx, state, path, path_len))
                return 1;
        }
    }

    state[node_idx] = DFS_DONE;
    return 0;
}

static void check_acyclicity(Checker *c) {
    if (!c->deps || c->deps->count == 0) return;

    const DepGraph *g = c->deps;
    int *state = arena_calloc(c->arena, (size_t)g->count * sizeof(int));
    int *path = arena_alloc(c->arena, (size_t)g->count * sizeof(int));

    for (int i = 0; i < g->count; i++) {
        if (state[i] == DFS_UNVISITED) {
            if (dfs_visit(c, g, i, state, path, 0))
                return; /* stop at first cycle */
        }
    }
}

/* ── Public API ──────────────────────────────────────────── */

void checker_init(Checker *c, const AstProgram *prog, Arena *arena) {
    c->prog = prog;
    c->arena = arena;
    c->errors.errors = NULL;
    c->errors.count = 0;
    c->errors.capacity = 0;
    c->errors.arena = arena;
    c->scope = NULL;
    c->scope_count = 0;
    c->scope_capacity = 0;
    c->deps = NULL;
}

void checker_init_with_deps(Checker *c, const AstProgram *prog,
                            Arena *arena, const DepGraph *deps) {
    checker_init(c, prog, arena);
    c->deps = deps;
}

int checker_check(Checker *c) {
    check_contract(c);

    /* Acyclicity check (if dependency graph provided) */
    if (c->deps) check_acyclicity(c);

    /* Tag and source contracts have no function body to check */
    if (c->prog->contract->kind != CONTRACT_FUNCTION)
        return c->errors.count;

    check_tags(c);
    check_sanitizers(c);
    check_rules(c);
    check_tests(c);
    check_function(c);
    return c->errors.count;
}

#include "heluna/evaluator.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <math.h>

/* ── Value constructors ──────────────────────────────────── */

static HVal *make_val(Arena *a, HValKind kind) {
    HVal *v = arena_calloc(a, sizeof(HVal));
    v->kind = kind;
    return v;
}

static HVal *make_integer(Arena *a, long long n) {
    HVal *v = make_val(a, VAL_INTEGER);
    v->as.integer_val = n;
    return v;
}

static HVal *make_float(Arena *a, double d) {
    HVal *v = make_val(a, VAL_FLOAT);
    v->as.float_val = d;
    return v;
}

static HVal *make_string(Arena *a, const char *s) {
    HVal *v = make_val(a, VAL_STRING);
    v->as.string_val = s;
    return v;
}

static HVal *make_boolean(Arena *a, int b) {
    HVal *v = make_val(a, VAL_BOOLEAN);
    v->as.boolean_val = b ? 1 : 0;
    return v;
}

static HVal *make_nothing(Arena *a) {
    return make_val(a, VAL_NOTHING);
}

static HVal *make_record(Arena *a, HField *fields) {
    HVal *v = make_val(a, VAL_RECORD);
    v->as.record_fields = fields;
    return v;
}

static HVal *make_list(Arena *a, HVal *head) {
    HVal *v = make_val(a, VAL_LIST);
    v->as.list_head = head;
    return v;
}

/* ── String helpers ──────────────────────────────────────── */

/* Strip surrounding quotes and resolve escape sequences from an AST string. */
static const char *resolve_string(Arena *a, const char *raw, int len) {
    /* Skip surrounding quotes */
    if (len >= 2 && raw[0] == '"' && raw[len - 1] == '"') {
        raw++;
        len -= 2;
    }

    /* First pass: compute output length */
    int out_len = 0;
    for (int i = 0; i < len; i++) {
        if (raw[i] == '\\' && i + 1 < len) {
            i++; /* skip escaped char */
        }
        out_len++;
    }

    char *buf = arena_alloc(a, (size_t)out_len + 1);
    int j = 0;
    for (int i = 0; i < len; i++) {
        if (raw[i] == '\\' && i + 1 < len) {
            i++;
            switch (raw[i]) {
            case 'n':  buf[j++] = '\n'; break;
            case 't':  buf[j++] = '\t'; break;
            case '\\': buf[j++] = '\\'; break;
            case '"':  buf[j++] = '"';  break;
            default:   buf[j++] = raw[i]; break;
            }
        } else {
            buf[j++] = raw[i];
        }
    }
    buf[j] = '\0';
    return buf;
}

/* Concatenate two strings. */
static const char *string_concat(Arena *a, const char *s1, const char *s2) {
    size_t len1 = strlen(s1);
    size_t len2 = strlen(s2);
    char *buf = arena_alloc(a, len1 + len2 + 1);
    memcpy(buf, s1, len1);
    memcpy(buf + len1, s2, len2);
    buf[len1 + len2] = '\0';
    return buf;
}

/* ── Environment ─────────────────────────────────────────── */

static void env_push(Evaluator *ev, const char *name, HVal *value) {
    if (ev->env_count == ev->env_capacity) {
        int new_cap = ev->env_capacity ? ev->env_capacity * 2 : 32;
        EnvEntry *new_arr = arena_alloc(ev->arena,
                                        (size_t)new_cap * sizeof(EnvEntry));
        if (ev->env_count > 0) {
            memcpy(new_arr, ev->env,
                   (size_t)ev->env_count * sizeof(EnvEntry));
        }
        ev->env = new_arr;
        ev->env_capacity = new_cap;
    }
    ev->env[ev->env_count].name = name;
    ev->env[ev->env_count].value = value;
    ev->env_count++;
}

static HVal *env_lookup(const Evaluator *ev, const char *name) {
    for (int i = ev->env_count - 1; i >= 0; i--) {
        if (strcmp(ev->env[i].name, name) == 0) {
            return ev->env[i].value;
        }
    }
    return NULL;
}

/* ── Record field lookup ─────────────────────────────────── */

static HVal *record_get(const HVal *rec, const char *field) {
    if (rec->kind != VAL_RECORD) return NULL;
    for (HField *f = rec->as.record_fields; f; f = f->next) {
        if (strcmp(f->name, field) == 0) return f->value;
    }
    return NULL;
}

/* ── Error helpers ───────────────────────────────────────── */

static void eval_error(Evaluator *ev, SrcLoc loc, const char *fmt, ...) {
    if (ev->had_error) return;
    ev->had_error = 1;
    ev->error.kind = HELUNA_ERR_RUNTIME;
    ev->error.loc = loc;

    va_list ap;
    va_start(ap, fmt);
    vsnprintf(ev->error.message, sizeof(ev->error.message), fmt, ap);
    va_end(ap);
}

/* ── Forward declarations ────────────────────────────────── */

static HVal *eval_expr(Evaluator *ev, const AstExpr *e);
static int match_pattern(Evaluator *ev, const AstPattern *pat, const HVal *val);

/* ── Expression evaluation ───────────────────────────────── */

static HVal *eval_binary(Evaluator *ev, const AstExpr *e) {
    HVal *left = eval_expr(ev, e->as.binary.left);
    if (!left) return NULL;
    HVal *right = eval_expr(ev, e->as.binary.right);
    if (!right) return NULL;

    AstBinOp op = e->as.binary.op;

    /* String concatenation */
    if (op == BIN_ADD && left->kind == VAL_STRING && right->kind == VAL_STRING) {
        return make_string(ev->arena,
                           string_concat(ev->arena,
                                         left->as.string_val,
                                         right->as.string_val));
    }

    /* Boolean operators */
    if (op == BIN_AND) {
        if (left->kind != VAL_BOOLEAN || right->kind != VAL_BOOLEAN) {
            eval_error(ev, e->loc, "'and' requires boolean operands");
            return NULL;
        }
        return make_boolean(ev->arena,
                            left->as.boolean_val && right->as.boolean_val);
    }
    if (op == BIN_OR) {
        if (left->kind != VAL_BOOLEAN || right->kind != VAL_BOOLEAN) {
            eval_error(ev, e->loc, "'or' requires boolean operands");
            return NULL;
        }
        return make_boolean(ev->arena,
                            left->as.boolean_val || right->as.boolean_val);
    }

    /* Equality/inequality on any type */
    if (op == BIN_EQ) {
        return make_boolean(ev->arena, hval_equal(left, right));
    }
    if (op == BIN_NEQ) {
        return make_boolean(ev->arena, !hval_equal(left, right));
    }

    /* Numeric arithmetic and comparison */
    if (left->kind == VAL_INTEGER && right->kind == VAL_INTEGER) {
        long long a = left->as.integer_val;
        long long b = right->as.integer_val;
        switch (op) {
        case BIN_ADD: return make_integer(ev->arena, a + b);
        case BIN_SUB: return make_integer(ev->arena, a - b);
        case BIN_MUL: return make_integer(ev->arena, a * b);
        case BIN_DIV:
            if (b == 0) {
                eval_error(ev, e->loc, "division by zero");
                return NULL;
            }
            return make_integer(ev->arena, a / b);
        case BIN_MOD:
            if (b == 0) {
                eval_error(ev, e->loc, "modulo by zero");
                return NULL;
            }
            return make_integer(ev->arena, a % b);
        case BIN_LT:  return make_boolean(ev->arena, a < b);
        case BIN_GT:  return make_boolean(ev->arena, a > b);
        case BIN_LTE: return make_boolean(ev->arena, a <= b);
        case BIN_GTE: return make_boolean(ev->arena, a >= b);
        default: break;
        }
    }

    /* Float arithmetic (or mixed int+float) */
    if ((left->kind == VAL_FLOAT || left->kind == VAL_INTEGER) &&
        (right->kind == VAL_FLOAT || right->kind == VAL_INTEGER)) {
        double a = left->kind == VAL_FLOAT
                       ? left->as.float_val
                       : (double)left->as.integer_val;
        double b = right->kind == VAL_FLOAT
                       ? right->as.float_val
                       : (double)right->as.integer_val;
        switch (op) {
        case BIN_ADD: return make_float(ev->arena, a + b);
        case BIN_SUB: return make_float(ev->arena, a - b);
        case BIN_MUL: return make_float(ev->arena, a * b);
        case BIN_DIV:
            if (b == 0.0) {
                eval_error(ev, e->loc, "division by zero");
                return NULL;
            }
            return make_float(ev->arena, a / b);
        case BIN_MOD:
            eval_error(ev, e->loc, "modulo on float values");
            return NULL;
        case BIN_LT:  return make_boolean(ev->arena, a < b);
        case BIN_GT:  return make_boolean(ev->arena, a > b);
        case BIN_LTE: return make_boolean(ev->arena, a <= b);
        case BIN_GTE: return make_boolean(ev->arena, a >= b);
        default: break;
        }
    }

    /* String comparison */
    if (left->kind == VAL_STRING && right->kind == VAL_STRING) {
        int cmp = strcmp(left->as.string_val, right->as.string_val);
        switch (op) {
        case BIN_LT:  return make_boolean(ev->arena, cmp < 0);
        case BIN_GT:  return make_boolean(ev->arena, cmp > 0);
        case BIN_LTE: return make_boolean(ev->arena, cmp <= 0);
        case BIN_GTE: return make_boolean(ev->arena, cmp >= 0);
        default: break;
        }
    }

    eval_error(ev, e->loc, "unsupported operand types for binary operator");
    return NULL;
}

static HVal *eval_through(Evaluator *ev, const AstExpr *e) {
    HVal *left = eval_expr(ev, e->as.through.left);
    if (!left) return NULL;

    const AstExpr *right = e->as.through.right;

    /* through + CALL: inject left as "value" field in arg record */
    if (right->kind == EXPR_CALL) {
        /* Evaluate the call's argument */
        HVal *arg = eval_expr(ev, right->as.call.arg);
        if (!arg) return NULL;

        /* Inject "value" field into the argument record */
        if (arg->kind == VAL_RECORD) {
            HField *vf = arena_calloc(ev->arena, sizeof(HField));
            vf->name = "value";
            vf->value = left;
            vf->next = arg->as.record_fields;
            arg = make_record(ev->arena, vf);
        }

        return stdlib_call(right->as.call.name, arg, ev->arena, &ev->error);
    }

    /* through + FILTER: use left as the list */
    if (right->kind == EXPR_FILTER) {
        if (left->kind != VAL_LIST) {
            eval_error(ev, e->loc, "through filter requires a list");
            return NULL;
        }
        HVal *result_head = NULL;
        HVal **tail = &result_head;
        int mark = ev->env_count;
        for (HVal *item = left->as.list_head; item; item = item->next) {
            ev->env_count = mark;
            env_push(ev, right->as.filter.var_name, item);
            HVal *pred = eval_expr(ev, right->as.filter.predicate);
            if (!pred) return NULL;
            if (pred->kind == VAL_BOOLEAN && pred->as.boolean_val) {
                HVal *copy = arena_calloc(ev->arena, sizeof(HVal));
                *copy = *item;
                copy->next = NULL;
                *tail = copy;
                tail = &copy->next;
            }
        }
        ev->env_count = mark;
        return make_list(ev->arena, result_head);
    }

    /* through + MAP: use left as the list */
    if (right->kind == EXPR_MAP) {
        if (left->kind != VAL_LIST) {
            eval_error(ev, e->loc, "through map requires a list");
            return NULL;
        }
        HVal *result_head = NULL;
        HVal **tail = &result_head;
        int mark = ev->env_count;
        for (HVal *item = left->as.list_head; item; item = item->next) {
            ev->env_count = mark;
            env_push(ev, right->as.map.var_name, item);
            HVal *val = eval_expr(ev, right->as.map.body);
            if (!val) return NULL;
            HVal *copy = arena_calloc(ev->arena, sizeof(HVal));
            *copy = *val;
            copy->next = NULL;
            *tail = copy;
            tail = &copy->next;
        }
        ev->env_count = mark;
        return make_list(ev->arena, result_head);
    }

    eval_error(ev, e->loc, "unsupported right-hand side of 'through'");
    return NULL;
}

static HVal *eval_expr(Evaluator *ev, const AstExpr *e) {
    if (!e || ev->had_error) return NULL;

    switch (e->kind) {
    case EXPR_INTEGER:
        return make_integer(ev->arena, e->as.integer_val);

    case EXPR_FLOAT:
        return make_float(ev->arena, e->as.float_val);

    case EXPR_STRING:
        return make_string(ev->arena,
                           resolve_string(ev->arena,
                                          e->as.string_val.value,
                                          e->as.string_val.length));

    case EXPR_TRUE:
        return make_boolean(ev->arena, 1);

    case EXPR_FALSE:
        return make_boolean(ev->arena, 0);

    case EXPR_NOTHING:
        return make_nothing(ev->arena);

    case EXPR_IDENT: {
        HVal *v = env_lookup(ev, e->as.ident.name);
        if (!v) {
            eval_error(ev, e->loc, "undefined identifier '%s'",
                       e->as.ident.name);
            return NULL;
        }
        return v;
    }

    case EXPR_INPUT_REF: {
        if (!ev->input_record || ev->input_record->kind != VAL_RECORD) {
            eval_error(ev, e->loc, "no input record for '$%s'",
                       e->as.input_ref.name);
            return NULL;
        }
        HVal *v = record_get(ev->input_record, e->as.input_ref.name);
        if (!v) {
            eval_error(ev, e->loc, "input field '$%s' not found",
                       e->as.input_ref.name);
            return NULL;
        }
        return v;
    }

    case EXPR_BINARY:
        return eval_binary(ev, e);

    case EXPR_UNARY_NEG: {
        HVal *operand = eval_expr(ev, e->as.unary.operand);
        if (!operand) return NULL;
        if (operand->kind == VAL_INTEGER)
            return make_integer(ev->arena, -operand->as.integer_val);
        if (operand->kind == VAL_FLOAT)
            return make_float(ev->arena, -operand->as.float_val);
        eval_error(ev, e->loc, "cannot negate non-numeric value");
        return NULL;
    }

    case EXPR_NOT: {
        HVal *operand = eval_expr(ev, e->as.not_expr.operand);
        if (!operand) return NULL;
        if (operand->kind != VAL_BOOLEAN) {
            eval_error(ev, e->loc, "'not' requires boolean operand");
            return NULL;
        }
        return make_boolean(ev->arena, !operand->as.boolean_val);
    }

    case EXPR_IF: {
        for (AstIfBranch *b = e->as.if_expr.branches; b; b = b->next) {
            if (!b->condition) {
                /* else branch */
                return eval_expr(ev, b->body);
            }
            HVal *cond = eval_expr(ev, b->condition);
            if (!cond) return NULL;
            if (cond->kind == VAL_BOOLEAN && cond->as.boolean_val) {
                return eval_expr(ev, b->body);
            }
        }
        /* No branch taken, no else → nothing */
        return make_nothing(ev->arena);
    }

    case EXPR_MATCH: {
        HVal *subject = eval_expr(ev, e->as.match.subject);
        if (!subject) return NULL;

        for (AstWhenClause *cl = e->as.match.clauses; cl; cl = cl->next) {
            int mark = ev->env_count;
            if (match_pattern(ev, cl->pattern, subject)) {
                /* Check guard if present */
                if (cl->guard) {
                    HVal *guard_val = eval_expr(ev, cl->guard);
                    if (!guard_val) return NULL;
                    if (guard_val->kind != VAL_BOOLEAN ||
                        !guard_val->as.boolean_val) {
                        ev->env_count = mark;
                        continue;
                    }
                }
                HVal *result = eval_expr(ev, cl->body);
                ev->env_count = mark;
                return result;
            }
            ev->env_count = mark;
        }

        /* else branch */
        if (e->as.match.else_body) {
            return eval_expr(ev, e->as.match.else_body);
        }

        return make_nothing(ev->arena);
    }

    case EXPR_LET: {
        HVal *binding = eval_expr(ev, e->as.let.binding);
        if (!binding) return NULL;
        int mark = ev->env_count;
        env_push(ev, e->as.let.name, binding);
        HVal *result = eval_expr(ev, e->as.let.body);
        ev->env_count = mark;
        return result;
    }

    case EXPR_RECORD: {
        HField *head = NULL;
        HField **tail = &head;
        for (AstLabel *l = e->as.record.labels; l; l = l->next) {
            HVal *val = eval_expr(ev, l->value);
            if (!val) return NULL;
            HField *f = arena_calloc(ev->arena, sizeof(HField));
            f->name = l->name;
            f->value = val;
            *tail = f;
            tail = &f->next;
        }
        return make_record(ev->arena, head);
    }

    case EXPR_LIST: {
        HVal *head = NULL;
        HVal **tail = &head;
        for (AstExpr *el = e->as.list.elements; el; el = el->next) {
            HVal *val = eval_expr(ev, el);
            if (!val) return NULL;
            HVal *item = arena_calloc(ev->arena, sizeof(HVal));
            *item = *val;
            item->next = NULL;
            *tail = item;
            tail = &item->next;
        }
        return make_list(ev->arena, head);
    }

    case EXPR_ACCESS: {
        HVal *obj = eval_expr(ev, e->as.access.object);
        if (!obj) return NULL;
        if (obj->kind != VAL_RECORD) {
            eval_error(ev, e->loc, "field access on non-record value");
            return NULL;
        }
        HVal *val = record_get(obj, e->as.access.field);
        if (!val) {
            eval_error(ev, e->loc, "field '%s' not found in record",
                       e->as.access.field);
            return NULL;
        }
        return val;
    }

    case EXPR_PAREN:
        return eval_expr(ev, e->as.paren.inner);

    case EXPR_CALL: {
        HVal *arg = eval_expr(ev, e->as.call.arg);
        if (!arg) return NULL;

        HVal *result = stdlib_call(e->as.call.name, arg, ev->arena,
                                   &ev->error);
        if (!result) {
            ev->had_error = 1;
            return NULL;
        }
        return result;
    }

    case EXPR_FILTER: {
        HVal *list = eval_expr(ev, e->as.filter.list);
        if (!list) return NULL;
        if (list->kind != VAL_LIST) {
            eval_error(ev, e->loc, "filter requires a list");
            return NULL;
        }
        HVal *result_head = NULL;
        HVal **tail = &result_head;
        int mark = ev->env_count;
        for (HVal *item = list->as.list_head; item; item = item->next) {
            ev->env_count = mark;
            env_push(ev, e->as.filter.var_name, item);
            HVal *pred = eval_expr(ev, e->as.filter.predicate);
            if (!pred) return NULL;
            if (pred->kind == VAL_BOOLEAN && pred->as.boolean_val) {
                HVal *copy = arena_calloc(ev->arena, sizeof(HVal));
                *copy = *item;
                copy->next = NULL;
                *tail = copy;
                tail = &copy->next;
            }
        }
        ev->env_count = mark;
        return make_list(ev->arena, result_head);
    }

    case EXPR_MAP: {
        HVal *list = eval_expr(ev, e->as.map.list);
        if (!list) return NULL;
        if (list->kind != VAL_LIST) {
            eval_error(ev, e->loc, "map requires a list");
            return NULL;
        }
        HVal *result_head = NULL;
        HVal **tail = &result_head;
        int mark = ev->env_count;
        for (HVal *item = list->as.list_head; item; item = item->next) {
            ev->env_count = mark;
            env_push(ev, e->as.map.var_name, item);
            HVal *val = eval_expr(ev, e->as.map.body);
            if (!val) return NULL;
            HVal *copy = arena_calloc(ev->arena, sizeof(HVal));
            *copy = *val;
            copy->next = NULL;
            *tail = copy;
            tail = &copy->next;
        }
        ev->env_count = mark;
        return make_list(ev->arena, result_head);
    }

    case EXPR_THROUGH:
        return eval_through(ev, e);

    case EXPR_LOOKUP:
        /* Phase 1: not supported */
        return make_nothing(ev->arena);
    }

    eval_error(ev, e->loc, "unhandled expression kind %d", e->kind);
    return NULL;
}

/* ── Pattern matching ────────────────────────────────────── */

static int match_pattern(Evaluator *ev, const AstPattern *pat,
                         const HVal *val) {
    if (!pat) return 1; /* NULL pattern always matches */

    switch (pat->kind) {
    case PAT_WILDCARD:
        return 1;

    case PAT_BINDING:
        env_push(ev, pat->as.binding.name, (HVal *)val);
        return 1;

    case PAT_LITERAL: {
        /* Evaluate the literal expression to get a value, then compare */
        HVal *lit = eval_expr(ev, pat->as.literal.value);
        if (!lit) return 0;
        return hval_equal(lit, val);
    }

    case PAT_RANGE: {
        HVal *low = eval_expr(ev, pat->as.range.low);
        HVal *high = eval_expr(ev, pat->as.range.high);
        if (!low || !high) return 0;

        if (val->kind == VAL_INTEGER && low->kind == VAL_INTEGER &&
            high->kind == VAL_INTEGER) {
            return val->as.integer_val >= low->as.integer_val &&
                   val->as.integer_val <= high->as.integer_val;
        }
        if ((val->kind == VAL_FLOAT || val->kind == VAL_INTEGER) &&
            (low->kind == VAL_FLOAT || low->kind == VAL_INTEGER) &&
            (high->kind == VAL_FLOAT || high->kind == VAL_INTEGER)) {
            double v = val->kind == VAL_FLOAT
                           ? val->as.float_val
                           : (double)val->as.integer_val;
            double lo = low->kind == VAL_FLOAT
                            ? low->as.float_val
                            : (double)low->as.integer_val;
            double hi = high->kind == VAL_FLOAT
                            ? high->as.float_val
                            : (double)high->as.integer_val;
            return v >= lo && v <= hi;
        }
        return 0;
    }

    case PAT_LIST: {
        if (val->kind != VAL_LIST) return 0;

        /* Count value elements */
        int val_len = 0;
        for (const HVal *v = val->as.list_head; v; v = v->next) val_len++;

        /* Count pattern elements */
        int pat_len = 0;
        for (AstPatternElem *el = pat->as.list.elements; el; el = el->next)
            pat_len++;

        int has_rest = (pat->as.list.rest_name != NULL);

        if (has_rest) {
            if (val_len < pat_len) return 0;
        } else {
            if (val_len != pat_len) return 0;
        }

        /* Match each element pattern */
        const HVal *v = val->as.list_head;
        for (AstPatternElem *el = pat->as.list.elements; el; el = el->next) {
            if (!match_pattern(ev, el->pattern, v)) return 0;
            v = v->next;
        }

        /* Bind rest if present */
        if (has_rest && pat->as.list.rest_name[0] != '\0') {
            /* Build rest list from remaining elements */
            HVal *rest_head = NULL;
            HVal **tail = &rest_head;
            for (const HVal *r = v; r; r = r->next) {
                HVal *copy = arena_calloc(ev->arena, sizeof(HVal));
                *copy = *r;
                copy->next = NULL;
                *tail = copy;
                tail = &copy->next;
            }
            HVal *rest_list = make_list(ev->arena, rest_head);
            env_push(ev, pat->as.list.rest_name, rest_list);
        }

        return 1;
    }

    case PAT_RECORD: {
        if (val->kind != VAL_RECORD) return 0;

        for (AstFieldPattern *fp = pat->as.record.fields; fp; fp = fp->next) {
            HVal *field_val = record_get(val, fp->name);
            if (!field_val) return 0;
            if (!match_pattern(ev, fp->pattern, field_val)) return 0;
        }
        return 1;
    }
    }

    return 0;
}

/* ── Deep equality ───────────────────────────────────────── */

int hval_equal(const HVal *a, const HVal *b) {
    if (a == b) return 1;
    if (!a || !b) return 0;
    if (a->kind != b->kind) return 0;

    switch (a->kind) {
    case VAL_INTEGER:
        return a->as.integer_val == b->as.integer_val;
    case VAL_FLOAT: {
        double da = a->as.float_val;
        double db = b->as.float_val;
        if (da == db) return 1;
        double diff = fabs(da - db);
        double mag = fmax(fabs(da), fabs(db));
        return diff <= mag * 1e-9;
    }
    case VAL_STRING:
        return strcmp(a->as.string_val, b->as.string_val) == 0;
    case VAL_BOOLEAN:
        return a->as.boolean_val == b->as.boolean_val;
    case VAL_NOTHING:
        return 1;
    case VAL_RECORD: {
        /* Both must have same fields with equal values */
        int a_count = 0, b_count = 0;
        for (const HField *f = a->as.record_fields; f; f = f->next)
            a_count++;
        for (const HField *f = b->as.record_fields; f; f = f->next)
            b_count++;
        if (a_count != b_count) return 0;

        for (const HField *fa = a->as.record_fields; fa; fa = fa->next) {
            const HVal *bv = record_get(b, fa->name);
            if (!bv || !hval_equal(fa->value, bv)) return 0;
        }
        return 1;
    }
    case VAL_LIST: {
        const HVal *va = a->as.list_head;
        const HVal *vb = b->as.list_head;
        while (va && vb) {
            if (!hval_equal(va, vb)) return 0;
            va = va->next;
            vb = vb->next;
        }
        return va == NULL && vb == NULL;
    }
    }

    return 0;
}

/* ── Public API ──────────────────────────────────────────── */

void evaluator_init(Evaluator *ev, const AstProgram *prog, Arena *arena) {
    ev->arena = arena;
    ev->prog = prog;
    ev->env = NULL;
    ev->env_count = 0;
    ev->env_capacity = 0;
    ev->input_record = NULL;
    ev->had_error = 0;
    memset(&ev->error, 0, sizeof(ev->error));
}

HVal *evaluator_eval(Evaluator *ev, HVal *input) {
    ev->input_record = input;
    ev->had_error = 0;
    ev->env_count = 0;

    if (!ev->prog->function || !ev->prog->function->body) {
        eval_error(ev, ev->prog->loc, "no function body to evaluate");
        return NULL;
    }

    return eval_expr(ev, ev->prog->function->body);
}

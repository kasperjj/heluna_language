#include "heluna/evaluator.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

/* ── Helpers ─────────────────────────────────────────────── */

static HVal *make_val(Arena *a, HValKind kind) {
    HVal *v = arena_calloc(a, sizeof(HVal));
    v->kind = kind;
    return v;
}

static HVal *get_field(HVal *rec, const char *name) {
    if (!rec || rec->kind != VAL_RECORD) return NULL;
    for (HField *f = rec->as.record_fields; f; f = f->next) {
        if (strcmp(f->name, name) == 0) return f->value;
    }
    return NULL;
}

/* ── to-string ───────────────────────────────────────────── */

static HVal *fn_to_string(Arena *arena, HVal *arg, HelunaError *err) {
    HVal *val = get_field(arg, "value");
    if (!val) {
        heluna_error_set(err, HELUNA_ERR_RUNTIME, (SrcLoc){0},
                         "to-string: missing 'value' field");
        return NULL;
    }

    char buf[256];
    switch (val->kind) {
    case VAL_INTEGER:
        snprintf(buf, sizeof buf, "%lld", val->as.integer_val);
        break;
    case VAL_FLOAT: {
        snprintf(buf, sizeof buf, "%g", val->as.float_val);
        /* Ensure float strings have a decimal point (e.g. "5" → "5.0") */
        if (!strchr(buf, '.') && !strchr(buf, 'e') && !strchr(buf, 'E')) {
            size_t len = strlen(buf);
            if (len + 2 < sizeof buf) {
                buf[len] = '.';
                buf[len + 1] = '0';
                buf[len + 2] = '\0';
            }
        }
        break;
    }
    case VAL_STRING:
        /* Already a string — return as-is wrapped in record */
        goto wrap;
    case VAL_BOOLEAN:
        snprintf(buf, sizeof buf, "%s",
                 val->as.boolean_val ? "true" : "false");
        break;
    case VAL_NOTHING:
        snprintf(buf, sizeof buf, "nothing");
        break;
    default:
        heluna_error_set(err, HELUNA_ERR_RUNTIME, (SrcLoc){0},
                         "to-string: unsupported value type");
        return NULL;
    }

    val = make_val(arena, VAL_STRING);
    val->as.string_val = arena_strdup(arena, buf);

wrap: {
    /* Wrap in { value: "..." } record */
    HField *f = arena_calloc(arena, sizeof(HField));
    f->name = "value";
    f->value = val;

    HVal *rec = make_val(arena, VAL_RECORD);
    rec->as.record_fields = f;
    return rec;
    }
}

/* ── to-float ────────────────────────────────────────────── */

static HVal *fn_to_float(Arena *arena, HVal *arg, HelunaError *err) {
    HVal *val = get_field(arg, "value");
    if (!val) {
        heluna_error_set(err, HELUNA_ERR_RUNTIME, (SrcLoc){0},
                         "to-float: missing 'value' field");
        return NULL;
    }

    double d;
    switch (val->kind) {
    case VAL_INTEGER: d = (double)val->as.integer_val; break;
    case VAL_FLOAT:   d = val->as.float_val; break;
    case VAL_STRING:  d = strtod(val->as.string_val, NULL); break;
    default:
        heluna_error_set(err, HELUNA_ERR_RUNTIME, (SrcLoc){0},
                         "to-float: cannot convert value to float");
        return NULL;
    }

    HVal *result = make_val(arena, VAL_FLOAT);
    result->as.float_val = d;
    return result;
}

/* ── to-integer ──────────────────────────────────────────── */

static HVal *fn_to_integer(Arena *arena, HVal *arg, HelunaError *err) {
    HVal *val = get_field(arg, "value");
    if (!val) {
        heluna_error_set(err, HELUNA_ERR_RUNTIME, (SrcLoc){0},
                         "to-integer: missing 'value' field");
        return NULL;
    }

    long long n;
    switch (val->kind) {
    case VAL_INTEGER: n = val->as.integer_val; break;
    case VAL_FLOAT:   n = (long long)val->as.float_val; break;
    case VAL_STRING:  n = strtoll(val->as.string_val, NULL, 10); break;
    default:
        heluna_error_set(err, HELUNA_ERR_RUNTIME, (SrcLoc){0},
                         "to-integer: cannot convert value to integer");
        return NULL;
    }

    HVal *result = make_val(arena, VAL_INTEGER);
    result->as.integer_val = n;
    return result;
}

/* ── length ──────────────────────────────────────────────── */

static HVal *fn_length(Arena *arena, HVal *arg, HelunaError *err) {
    HVal *list = get_field(arg, "list");
    if (!list) {
        /* Also try "value" field for string length */
        HVal *val = get_field(arg, "value");
        if (val && val->kind == VAL_STRING) {
            HVal *result = make_val(arena, VAL_INTEGER);
            result->as.integer_val = (long long)strlen(val->as.string_val);
            return result;
        }
        heluna_error_set(err, HELUNA_ERR_RUNTIME, (SrcLoc){0},
                         "length: missing 'list' field");
        return NULL;
    }

    if (list->kind != VAL_LIST) {
        heluna_error_set(err, HELUNA_ERR_RUNTIME, (SrcLoc){0},
                         "length: 'list' field is not a list");
        return NULL;
    }

    long long count = 0;
    for (HVal *v = list->as.list_head; v; v = v->next) count++;

    HVal *result = make_val(arena, VAL_INTEGER);
    result->as.integer_val = count;
    return result;
}

/* ── fold ────────────────────────────────────────────────── */

static HVal *fn_fold(Arena *arena, HVal *arg, HelunaError *err) {
    HVal *list = get_field(arg, "list");
    HVal *initial = get_field(arg, "initial");
    HVal *fn = get_field(arg, "fn");

    if (!list || !initial || !fn) {
        heluna_error_set(err, HELUNA_ERR_RUNTIME, (SrcLoc){0},
                         "fold: missing required fields (list, initial, fn)");
        return NULL;
    }
    if (list->kind != VAL_LIST) {
        heluna_error_set(err, HELUNA_ERR_RUNTIME, (SrcLoc){0},
                         "fold: 'list' is not a list");
        return NULL;
    }
    if (fn->kind != VAL_STRING) {
        heluna_error_set(err, HELUNA_ERR_RUNTIME, (SrcLoc){0},
                         "fold: 'fn' must be a string");
        return NULL;
    }

    const char *fn_name = fn->as.string_val;
    HVal *acc = initial;

    for (HVal *item = list->as.list_head; item; item = item->next) {
        if (strcmp(fn_name, "add") == 0) {
            if (acc->kind == VAL_INTEGER && item->kind == VAL_INTEGER) {
                HVal *r = make_val(arena, VAL_INTEGER);
                r->as.integer_val = acc->as.integer_val + item->as.integer_val;
                acc = r;
            } else {
                double a = acc->kind == VAL_FLOAT
                               ? acc->as.float_val
                               : (double)acc->as.integer_val;
                double b = item->kind == VAL_FLOAT
                               ? item->as.float_val
                               : (double)item->as.integer_val;
                HVal *r = make_val(arena, VAL_FLOAT);
                r->as.float_val = a + b;
                acc = r;
            }
        } else if (strcmp(fn_name, "multiply") == 0) {
            if (acc->kind == VAL_INTEGER && item->kind == VAL_INTEGER) {
                HVal *r = make_val(arena, VAL_INTEGER);
                r->as.integer_val = acc->as.integer_val * item->as.integer_val;
                acc = r;
            } else {
                double a = acc->kind == VAL_FLOAT
                               ? acc->as.float_val
                               : (double)acc->as.integer_val;
                double b = item->kind == VAL_FLOAT
                               ? item->as.float_val
                               : (double)item->as.integer_val;
                HVal *r = make_val(arena, VAL_FLOAT);
                r->as.float_val = a * b;
                acc = r;
            }
        } else {
            heluna_error_set(err, HELUNA_ERR_RUNTIME, (SrcLoc){0},
                             "fold: unsupported fn '%s'", fn_name);
            return NULL;
        }
    }

    return acc;
}

/* ── trim ────────────────────────────────────────────────── */

static HVal *fn_trim(Arena *arena, HVal *arg, HelunaError *err) {
    HVal *val = get_field(arg, "value");
    if (!val || val->kind != VAL_STRING) {
        heluna_error_set(err, HELUNA_ERR_RUNTIME, (SrcLoc){0},
                         "trim: missing or non-string 'value' field");
        return NULL;
    }

    const char *s = val->as.string_val;
    size_t len = strlen(s);

    /* Trim leading whitespace */
    size_t start = 0;
    while (start < len && isspace((unsigned char)s[start])) start++;

    /* Trim trailing whitespace */
    size_t end = len;
    while (end > start && isspace((unsigned char)s[end - 1])) end--;

    HVal *result = make_val(arena, VAL_STRING);
    result->as.string_val = arena_strndup(arena, s + start, end - start);
    return result;
}

/* ── lower ───────────────────────────────────────────────── */

static HVal *fn_lower(Arena *arena, HVal *arg, HelunaError *err) {
    HVal *val = get_field(arg, "value");
    if (!val || val->kind != VAL_STRING) {
        heluna_error_set(err, HELUNA_ERR_RUNTIME, (SrcLoc){0},
                         "lower: missing or non-string 'value' field");
        return NULL;
    }

    size_t len = strlen(val->as.string_val);
    char *buf = arena_alloc(arena, len + 1);
    for (size_t i = 0; i < len; i++) {
        buf[i] = (char)tolower((unsigned char)val->as.string_val[i]);
    }
    buf[len] = '\0';

    HVal *result = make_val(arena, VAL_STRING);
    result->as.string_val = buf;
    return result;
}

/* ── upper ───────────────────────────────────────────────── */

static HVal *fn_upper(Arena *arena, HVal *arg, HelunaError *err) {
    HVal *val = get_field(arg, "value");
    if (!val || val->kind != VAL_STRING) {
        heluna_error_set(err, HELUNA_ERR_RUNTIME, (SrcLoc){0},
                         "upper: missing or non-string 'value' field");
        return NULL;
    }

    size_t len = strlen(val->as.string_val);
    char *buf = arena_alloc(arena, len + 1);
    for (size_t i = 0; i < len; i++) {
        buf[i] = (char)toupper((unsigned char)val->as.string_val[i]);
    }
    buf[len] = '\0';

    HVal *result = make_val(arena, VAL_STRING);
    result->as.string_val = buf;
    return result;
}

/* ── Dispatch table ──────────────────────────────────────── */

typedef HVal *(*StdlibFn)(Arena *arena, HVal *arg, HelunaError *err);

typedef struct {
    const char *name;
    StdlibFn    fn;
} StdlibEntry;

static const StdlibEntry stdlib_table[] = {
    { "to-string",  fn_to_string },
    { "to-float",   fn_to_float },
    { "to-integer", fn_to_integer },
    { "length",     fn_length },
    { "fold",       fn_fold },
    { "trim",       fn_trim },
    { "lower",      fn_lower },
    { "upper",      fn_upper },
    { NULL,         NULL },
};

HVal *stdlib_call(const char *name, HVal *arg, Arena *arena,
                  HelunaError *err) {
    for (const StdlibEntry *e = stdlib_table; e->name; e++) {
        if (strcmp(e->name, name) == 0) {
            return e->fn(arena, arg, err);
        }
    }

    /* Not a Phase 1 stdlib function — report as unsupported */
    heluna_error_set(err, HELUNA_ERR_RUNTIME, (SrcLoc){0},
                     "function '%s' is not yet implemented", name);
    return NULL;
}

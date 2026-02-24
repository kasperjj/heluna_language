#include "heluna/vm.h"
#include "heluna/json.h"
#include "vendor/sha256.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <math.h>
#include <time.h>
#include <regex.h>

/* ── Value constructors ──────────────────────────────────── */

static HVal *mk_val(Arena *a, HValKind kind) {
    HVal *v = arena_calloc(a, sizeof(HVal));
    v->kind = kind;
    return v;
}

static HVal *mk_integer(Arena *a, long long n) {
    HVal *v = mk_val(a, VAL_INTEGER);
    v->as.integer_val = n;
    return v;
}

static HVal *mk_float(Arena *a, double d) {
    HVal *v = mk_val(a, VAL_FLOAT);
    v->as.float_val = d;
    return v;
}

static HVal *mk_string(Arena *a, const char *s) {
    HVal *v = mk_val(a, VAL_STRING);
    v->as.string_val = s;
    return v;
}

static HVal *mk_boolean(Arena *a, int b) {
    HVal *v = mk_val(a, VAL_BOOLEAN);
    v->as.boolean_val = b ? 1 : 0;
    return v;
}

static HVal *mk_list(Arena *a, HVal *head) {
    HVal *v = mk_val(a, VAL_LIST);
    v->as.list_head = head;
    return v;
}

static HVal *mk_record(Arena *a, HField *fields) {
    HVal *v = mk_val(a, VAL_RECORD);
    v->as.record_fields = fields;
    return v;
}

/* ── Field access helper ─────────────────────────────────── */

static HVal *get_field(HVal *rec, const char *name) {
    if (!rec || rec->kind != VAL_RECORD) return NULL;
    for (HField *f = rec->as.record_fields; f; f = f->next) {
        if (strcmp(f->name, name) == 0) return f->value;
    }
    return NULL;
}

static const char *get_string(HVal *rec, const char *name) {
    HVal *v = get_field(rec, name);
    if (v && v->kind == VAL_STRING) return v->as.string_val;
    return NULL;
}

static int get_integer(HVal *rec, const char *name, long long *out) {
    HVal *v = get_field(rec, name);
    if (v && v->kind == VAL_INTEGER) { *out = v->as.integer_val; return 1; }
    return 0;
}

/* ── String functions (0x0001–0x0011) ────────────────────── */

/* upper (0x0001) */
static HVal *fn_upper(Arena *a, HVal *arg, HelunaError *err) {
    const char *s = get_string(arg, "value");
    if (!s) {
        heluna_error_set(err, HELUNA_ERR_RUNTIME, (SrcLoc){0},
                         "upper: missing or non-string 'value'");
        return NULL;
    }
    size_t len = strlen(s);
    char *buf = arena_alloc(a, len + 1);
    for (size_t i = 0; i < len; i++)
        buf[i] = (char)toupper((unsigned char)s[i]);
    buf[len] = '\0';
    return mk_string(a, buf);
}

/* lower (0x0002) */
static HVal *fn_lower(Arena *a, HVal *arg, HelunaError *err) {
    const char *s = get_string(arg, "value");
    if (!s) {
        heluna_error_set(err, HELUNA_ERR_RUNTIME, (SrcLoc){0},
                         "lower: missing or non-string 'value'");
        return NULL;
    }
    size_t len = strlen(s);
    char *buf = arena_alloc(a, len + 1);
    for (size_t i = 0; i < len; i++)
        buf[i] = (char)tolower((unsigned char)s[i]);
    buf[len] = '\0';
    return mk_string(a, buf);
}

/* trim (0x0003) */
static HVal *fn_trim(Arena *a, HVal *arg, HelunaError *err) {
    const char *s = get_string(arg, "value");
    if (!s) {
        heluna_error_set(err, HELUNA_ERR_RUNTIME, (SrcLoc){0},
                         "trim: missing or non-string 'value'");
        return NULL;
    }
    size_t len = strlen(s);
    size_t start = 0;
    while (start < len && isspace((unsigned char)s[start])) start++;
    size_t end = len;
    while (end > start && isspace((unsigned char)s[end - 1])) end--;
    return mk_string(a, arena_strndup(a, s + start, end - start));
}

/* trim-start (0x0004) */
static HVal *fn_trim_start(Arena *a, HVal *arg, HelunaError *err) {
    const char *s = get_string(arg, "value");
    if (!s) {
        heluna_error_set(err, HELUNA_ERR_RUNTIME, (SrcLoc){0},
                         "trim-start: missing or non-string 'value'");
        return NULL;
    }
    size_t len = strlen(s);
    size_t start = 0;
    while (start < len && isspace((unsigned char)s[start])) start++;
    return mk_string(a, arena_strdup(a, s + start));
}

/* trim-end (0x0005) */
static HVal *fn_trim_end(Arena *a, HVal *arg, HelunaError *err) {
    const char *s = get_string(arg, "value");
    if (!s) {
        heluna_error_set(err, HELUNA_ERR_RUNTIME, (SrcLoc){0},
                         "trim-end: missing or non-string 'value'");
        return NULL;
    }
    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) len--;
    return mk_string(a, arena_strndup(a, s, len));
}

/* substring (0x0006) */
static HVal *fn_substring(Arena *a, HVal *arg, HelunaError *err) {
    const char *s = get_string(arg, "value");
    long long start_val = 0, end_val = 0;
    if (!s || !get_integer(arg, "start", &start_val) ||
        !get_integer(arg, "end", &end_val)) {
        heluna_error_set(err, HELUNA_ERR_RUNTIME, (SrcLoc){0},
                         "substring: missing fields");
        return NULL;
    }
    size_t len = strlen(s);
    if (start_val < 0) start_val = 0;
    if ((size_t)end_val > len) end_val = (long long)len;
    if (start_val > end_val) start_val = end_val;
    return mk_string(a, arena_strndup(a, s + start_val,
                                      (size_t)(end_val - start_val)));
}

/* replace (0x0007) */
static HVal *fn_replace(Arena *a, HVal *arg, HelunaError *err) {
    const char *s = get_string(arg, "value");
    const char *find = get_string(arg, "find");
    const char *repl = get_string(arg, "replacement");
    if (!s || !find || !repl) {
        heluna_error_set(err, HELUNA_ERR_RUNTIME, (SrcLoc){0},
                         "replace: missing fields");
        return NULL;
    }
    size_t flen = strlen(find);
    if (flen == 0) return mk_string(a, arena_strdup(a, s));

    /* Build result with all occurrences replaced */
    size_t slen = strlen(s), rlen = strlen(repl);
    size_t cap = slen * 2 + rlen + 1;
    char *buf = arena_alloc(a, cap);
    size_t out = 0;

    const char *p = s;
    while (*p) {
        const char *found = strstr(p, find);
        if (!found) {
            size_t remaining = strlen(p);
            if (out + remaining >= cap) {
                cap = cap * 2 + remaining;
                char *nb = arena_alloc(a, cap);
                memcpy(nb, buf, out);
                buf = nb;
            }
            memcpy(buf + out, p, remaining);
            out += remaining;
            break;
        }
        size_t chunk = (size_t)(found - p);
        if (out + chunk + rlen >= cap) {
            cap = cap * 2 + chunk + rlen;
            char *nb = arena_alloc(a, cap);
            memcpy(nb, buf, out);
            buf = nb;
        }
        memcpy(buf + out, p, chunk);
        out += chunk;
        memcpy(buf + out, repl, rlen);
        out += rlen;
        p = found + flen;
    }
    buf[out] = '\0';
    return mk_string(a, buf);
}

/* split (0x0008) */
static HVal *fn_split(Arena *a, HVal *arg, HelunaError *err) {
    const char *s = get_string(arg, "value");
    const char *delim = get_string(arg, "delimiter");
    if (!s || !delim) {
        heluna_error_set(err, HELUNA_ERR_RUNTIME, (SrcLoc){0},
                         "split: missing fields");
        return NULL;
    }
    size_t dlen = strlen(delim);
    HVal *head = NULL;
    HVal **tail = &head;

    if (dlen == 0) {
        /* Split each character */
        for (const char *p = s; *p; p++) {
            HVal *item = mk_string(a, arena_strndup(a, p, 1));
            item->next = NULL;
            *tail = item;
            tail = &item->next;
        }
    } else {
        const char *p = s;
        while (1) {
            const char *found = strstr(p, delim);
            size_t chunk = found ? (size_t)(found - p) : strlen(p);
            HVal *item = mk_string(a, arena_strndup(a, p, chunk));
            item->next = NULL;
            *tail = item;
            tail = &item->next;
            if (!found) break;
            p = found + dlen;
        }
    }

    return mk_list(a, head);
}

/* join (0x0009) */
static HVal *fn_join(Arena *a, HVal *arg, HelunaError *err) {
    HVal *list = get_field(arg, "list");
    const char *delim = get_string(arg, "delimiter");
    if (!list || list->kind != VAL_LIST || !delim) {
        heluna_error_set(err, HELUNA_ERR_RUNTIME, (SrcLoc){0},
                         "join: missing fields");
        return NULL;
    }
    size_t dlen = strlen(delim);
    size_t total = 0;
    int count = 0;
    for (HVal *v = list->as.list_head; v; v = v->next) {
        if (v->kind == VAL_STRING) total += strlen(v->as.string_val);
        count++;
    }
    if (count > 1) total += dlen * (size_t)(count - 1);

    char *buf = arena_alloc(a, total + 1);
    size_t pos = 0;
    int first = 1;
    for (HVal *v = list->as.list_head; v; v = v->next) {
        if (!first) {
            memcpy(buf + pos, delim, dlen);
            pos += dlen;
        }
        first = 0;
        if (v->kind == VAL_STRING) {
            size_t l = strlen(v->as.string_val);
            memcpy(buf + pos, v->as.string_val, l);
            pos += l;
        }
    }
    buf[pos] = '\0';
    return mk_string(a, buf);
}

/* starts-with (0x000A) */
static HVal *fn_starts_with(Arena *a, HVal *arg, HelunaError *err) {
    const char *s = get_string(arg, "value");
    const char *prefix = get_string(arg, "prefix");
    if (!s || !prefix) {
        heluna_error_set(err, HELUNA_ERR_RUNTIME, (SrcLoc){0},
                         "starts-with: missing fields");
        return NULL;
    }
    size_t plen = strlen(prefix);
    return mk_boolean(a, strncmp(s, prefix, plen) == 0);
}

/* ends-with (0x000B) */
static HVal *fn_ends_with(Arena *a, HVal *arg, HelunaError *err) {
    const char *s = get_string(arg, "value");
    const char *suffix = get_string(arg, "suffix");
    if (!s || !suffix) {
        heluna_error_set(err, HELUNA_ERR_RUNTIME, (SrcLoc){0},
                         "ends-with: missing fields");
        return NULL;
    }
    size_t slen = strlen(s), xlen = strlen(suffix);
    if (xlen > slen) return mk_boolean(a, 0);
    return mk_boolean(a, strcmp(s + slen - xlen, suffix) == 0);
}

/* contains (0x000C) */
static HVal *fn_contains(Arena *a, HVal *arg, HelunaError *err) {
    const char *s = get_string(arg, "value");
    const char *sub = get_string(arg, "substring");
    if (!s || !sub) {
        heluna_error_set(err, HELUNA_ERR_RUNTIME, (SrcLoc){0},
                         "contains: missing fields");
        return NULL;
    }
    return mk_boolean(a, strstr(s, sub) != NULL);
}

/* length (0x000D) */
static HVal *fn_length(Arena *a, HVal *arg, HelunaError *err) {
    /* Try list first, then string */
    HVal *list = get_field(arg, "list");
    if (list && list->kind == VAL_LIST) {
        long long n = 0;
        for (HVal *v = list->as.list_head; v; v = v->next) n++;
        return mk_integer(a, n);
    }
    HVal *val = get_field(arg, "value");
    if (val && val->kind == VAL_STRING) {
        return mk_integer(a, (long long)strlen(val->as.string_val));
    }
    heluna_error_set(err, HELUNA_ERR_RUNTIME, (SrcLoc){0},
                     "length: missing 'list' or string 'value'");
    return NULL;
}

/* pad-left (0x000E) */
static HVal *fn_pad_left(Arena *a, HVal *arg, HelunaError *err) {
    const char *s = get_string(arg, "value");
    const char *fill = get_string(arg, "fill");
    long long width = 0;
    if (!s || !fill || !get_integer(arg, "width", &width)) {
        heluna_error_set(err, HELUNA_ERR_RUNTIME, (SrcLoc){0},
                         "pad-left: missing fields");
        return NULL;
    }
    size_t slen = strlen(s);
    if ((long long)slen >= width) return mk_string(a, arena_strdup(a, s));

    size_t pad = (size_t)(width - (long long)slen);
    char *buf = arena_alloc(a, (size_t)width + 1);
    size_t flen = strlen(fill);
    if (flen == 0) flen = 1;
    for (size_t i = 0; i < pad; i++) buf[i] = fill[i % flen];
    memcpy(buf + pad, s, slen);
    buf[(size_t)width] = '\0';
    return mk_string(a, buf);
}

/* pad-right (0x000F) */
static HVal *fn_pad_right(Arena *a, HVal *arg, HelunaError *err) {
    const char *s = get_string(arg, "value");
    const char *fill = get_string(arg, "fill");
    long long width = 0;
    if (!s || !fill || !get_integer(arg, "width", &width)) {
        heluna_error_set(err, HELUNA_ERR_RUNTIME, (SrcLoc){0},
                         "pad-right: missing fields");
        return NULL;
    }
    size_t slen = strlen(s);
    if ((long long)slen >= width) return mk_string(a, arena_strdup(a, s));

    char *buf = arena_alloc(a, (size_t)width + 1);
    memcpy(buf, s, slen);
    size_t flen = strlen(fill);
    if (flen == 0) flen = 1;
    for (size_t i = slen; i < (size_t)width; i++) buf[i] = fill[(i - slen) % flen];
    buf[(size_t)width] = '\0';
    return mk_string(a, buf);
}

/* regex-match (0x0010) */
static HVal *fn_regex_match(Arena *a, HVal *arg, HelunaError *err) {
    const char *s = get_string(arg, "value");
    const char *pat = get_string(arg, "pattern");
    if (!s || !pat) {
        heluna_error_set(err, HELUNA_ERR_RUNTIME, (SrcLoc){0},
                         "regex-match: missing fields");
        return NULL;
    }
    regex_t re;
    if (regcomp(&re, pat, REG_EXTENDED | REG_NOSUB) != 0) {
        heluna_error_set(err, HELUNA_ERR_RUNTIME, (SrcLoc){0},
                         "regex-match: invalid pattern");
        return NULL;
    }
    int match = regexec(&re, s, 0, NULL, 0) == 0;
    regfree(&re);
    return mk_boolean(a, match);
}

/* regex-replace (0x0011) */
static HVal *fn_regex_replace(Arena *a, HVal *arg, HelunaError *err) {
    const char *s = get_string(arg, "value");
    const char *pat = get_string(arg, "pattern");
    const char *repl = get_string(arg, "replacement");
    if (!s || !pat || !repl) {
        heluna_error_set(err, HELUNA_ERR_RUNTIME, (SrcLoc){0},
                         "regex-replace: missing fields");
        return NULL;
    }
    regex_t re;
    if (regcomp(&re, pat, REG_EXTENDED) != 0) {
        heluna_error_set(err, HELUNA_ERR_RUNTIME, (SrcLoc){0},
                         "regex-replace: invalid pattern");
        return NULL;
    }
    regmatch_t match;
    size_t slen = strlen(s), rlen = strlen(repl);
    size_t cap = slen * 2 + rlen + 1;
    char *buf = arena_alloc(a, cap);
    size_t out = 0;
    const char *p = s;

    while (regexec(&re, p, 1, &match, 0) == 0) {
        size_t prefix = (size_t)match.rm_so;
        if (out + prefix + rlen >= cap) {
            cap = cap * 2 + prefix + rlen;
            char *nb = arena_alloc(a, cap);
            memcpy(nb, buf, out);
            buf = nb;
        }
        memcpy(buf + out, p, prefix);
        out += prefix;
        memcpy(buf + out, repl, rlen);
        out += rlen;
        p += match.rm_eo;
        if (match.rm_so == match.rm_eo) {
            if (*p) buf[out++] = *p++;
            else break;
        }
    }
    size_t remaining = strlen(p);
    if (out + remaining >= cap) {
        cap = out + remaining + 1;
        char *nb = arena_alloc(a, cap);
        memcpy(nb, buf, out);
        buf = nb;
    }
    memcpy(buf + out, p, remaining);
    out += remaining;
    buf[out] = '\0';
    regfree(&re);
    return mk_string(a, buf);
}

/* ── Numeric functions (0x0020–0x0026) ───────────────────── */

/* abs (0x0020) */
static HVal *fn_abs(Arena *a, HVal *arg, HelunaError *err) {
    HVal *v = get_field(arg, "value");
    if (!v) { heluna_error_set(err, HELUNA_ERR_RUNTIME, (SrcLoc){0},
              "abs: missing 'value'"); return NULL; }
    if (v->kind == VAL_INTEGER)
        return mk_integer(a, v->as.integer_val < 0 ? -v->as.integer_val
                                                    : v->as.integer_val);
    if (v->kind == VAL_FLOAT)
        return mk_float(a, fabs(v->as.float_val));
    heluna_error_set(err, HELUNA_ERR_RUNTIME, (SrcLoc){0},
                     "abs: non-numeric value");
    return NULL;
}

/* ceil (0x0021) */
static HVal *fn_ceil(Arena *a, HVal *arg, HelunaError *err) {
    HVal *v = get_field(arg, "value");
    if (!v) { heluna_error_set(err, HELUNA_ERR_RUNTIME, (SrcLoc){0},
              "ceil: missing 'value'"); return NULL; }
    if (v->kind == VAL_INTEGER) return mk_integer(a, v->as.integer_val);
    if (v->kind == VAL_FLOAT)
        return mk_integer(a, (long long)ceil(v->as.float_val));
    heluna_error_set(err, HELUNA_ERR_RUNTIME, (SrcLoc){0},
                     "ceil: non-numeric value");
    return NULL;
}

/* floor (0x0022) */
static HVal *fn_floor(Arena *a, HVal *arg, HelunaError *err) {
    HVal *v = get_field(arg, "value");
    if (!v) { heluna_error_set(err, HELUNA_ERR_RUNTIME, (SrcLoc){0},
              "floor: missing 'value'"); return NULL; }
    if (v->kind == VAL_INTEGER) return mk_integer(a, v->as.integer_val);
    if (v->kind == VAL_FLOAT)
        return mk_integer(a, (long long)floor(v->as.float_val));
    heluna_error_set(err, HELUNA_ERR_RUNTIME, (SrcLoc){0},
                     "floor: non-numeric value");
    return NULL;
}

/* round (0x0023) */
static HVal *fn_round(Arena *a, HVal *arg, HelunaError *err) {
    HVal *v = get_field(arg, "value");
    if (!v) { heluna_error_set(err, HELUNA_ERR_RUNTIME, (SrcLoc){0},
              "round: missing 'value'"); return NULL; }
    if (v->kind == VAL_INTEGER) return mk_integer(a, v->as.integer_val);
    if (v->kind == VAL_FLOAT)
        return mk_integer(a, (long long)round(v->as.float_val));
    heluna_error_set(err, HELUNA_ERR_RUNTIME, (SrcLoc){0},
                     "round: non-numeric value");
    return NULL;
}

/* min (0x0024) */
static HVal *fn_min(Arena *a, HVal *arg, HelunaError *err) {
    HVal *va = get_field(arg, "a");
    HVal *vb = get_field(arg, "b");
    if (!va || !vb) { heluna_error_set(err, HELUNA_ERR_RUNTIME, (SrcLoc){0},
                      "min: missing fields"); return NULL; }
    double da = va->kind == VAL_FLOAT ? va->as.float_val
                                      : (double)va->as.integer_val;
    double db = vb->kind == VAL_FLOAT ? vb->as.float_val
                                      : (double)vb->as.integer_val;
    if (da <= db) {
        if (va->kind == VAL_INTEGER) return mk_integer(a, va->as.integer_val);
        return mk_float(a, va->as.float_val);
    }
    if (vb->kind == VAL_INTEGER) return mk_integer(a, vb->as.integer_val);
    return mk_float(a, vb->as.float_val);
}

/* max (0x0025) */
static HVal *fn_max(Arena *a, HVal *arg, HelunaError *err) {
    HVal *va = get_field(arg, "a");
    HVal *vb = get_field(arg, "b");
    if (!va || !vb) { heluna_error_set(err, HELUNA_ERR_RUNTIME, (SrcLoc){0},
                      "max: missing fields"); return NULL; }
    double da = va->kind == VAL_FLOAT ? va->as.float_val
                                      : (double)va->as.integer_val;
    double db = vb->kind == VAL_FLOAT ? vb->as.float_val
                                      : (double)vb->as.integer_val;
    if (da >= db) {
        if (va->kind == VAL_INTEGER) return mk_integer(a, va->as.integer_val);
        return mk_float(a, va->as.float_val);
    }
    if (vb->kind == VAL_INTEGER) return mk_integer(a, vb->as.integer_val);
    return mk_float(a, vb->as.float_val);
}

/* clamp (0x0026) */
static HVal *fn_clamp(Arena *a, HVal *arg, HelunaError *err) {
    HVal *val = get_field(arg, "value");
    HVal *lo  = get_field(arg, "min");
    HVal *hi  = get_field(arg, "max");
    if (!val || !lo || !hi) {
        heluna_error_set(err, HELUNA_ERR_RUNTIME, (SrcLoc){0},
                         "clamp: missing fields");
        return NULL;
    }
    double dv = val->kind == VAL_FLOAT ? val->as.float_val
                                       : (double)val->as.integer_val;
    double dl = lo->kind == VAL_FLOAT ? lo->as.float_val
                                      : (double)lo->as.integer_val;
    double dh = hi->kind == VAL_FLOAT ? hi->as.float_val
                                      : (double)hi->as.integer_val;
    if (dv < dl) dv = dl;
    if (dv > dh) dv = dh;

    if (val->kind == VAL_INTEGER && lo->kind == VAL_INTEGER &&
        hi->kind == VAL_INTEGER)
        return mk_integer(a, (long long)dv);
    return mk_float(a, dv);
}

/* ── List functions (0x0030–0x0037) ──────────────────────── */

/* Comparison for qsort */
static int hval_compare(const HVal *a, const HVal *b) {
    if (a->kind != b->kind) return (int)a->kind - (int)b->kind;
    switch (a->kind) {
    case VAL_INTEGER:
        if (a->as.integer_val < b->as.integer_val) return -1;
        if (a->as.integer_val > b->as.integer_val) return 1;
        return 0;
    case VAL_FLOAT:
        if (a->as.float_val < b->as.float_val) return -1;
        if (a->as.float_val > b->as.float_val) return 1;
        return 0;
    case VAL_STRING:
        return strcmp(a->as.string_val, b->as.string_val);
    case VAL_BOOLEAN:
        return a->as.boolean_val - b->as.boolean_val;
    default:
        return 0;
    }
}

typedef struct {
    HVal val_copy;
} SortEntry;

static int sort_cmp(const void *ap, const void *bp) {
    return hval_compare(&((const SortEntry *)ap)->val_copy,
                        &((const SortEntry *)bp)->val_copy);
}

/* sort (0x0030) */
static HVal *fn_sort(Arena *a, HVal *arg, HelunaError *err) {
    HVal *list = get_field(arg, "list");
    if (!list || list->kind != VAL_LIST) {
        heluna_error_set(err, HELUNA_ERR_RUNTIME, (SrcLoc){0},
                         "sort: missing or non-list 'list'");
        return NULL;
    }
    int n = 0;
    for (HVal *v = list->as.list_head; v; v = v->next) n++;
    if (n == 0) return mk_list(a, NULL);

    SortEntry *entries = arena_alloc(a, (size_t)n * sizeof(SortEntry));
    int i = 0;
    for (HVal *v = list->as.list_head; v; v = v->next) {
        entries[i].val_copy = *v;
        i++;
    }
    qsort(entries, (size_t)n, sizeof(SortEntry), sort_cmp);

    HVal *head = NULL;
    HVal **tail = &head;
    for (i = 0; i < n; i++) {
        HVal *item = arena_calloc(a, sizeof(HVal));
        *item = entries[i].val_copy;
        item->next = NULL;
        *tail = item;
        tail = &item->next;
    }
    return mk_list(a, head);
}

/* sort-by (0x0031) */
static const char *sort_by_field_name;
static int sort_by_cmp(const void *ap, const void *bp) {
    const HVal *a = &((const SortEntry *)ap)->val_copy;
    const HVal *b = &((const SortEntry *)bp)->val_copy;
    HVal *fa = NULL, *fb = NULL;
    if (a->kind == VAL_RECORD) {
        for (HField *f = a->as.record_fields; f; f = f->next)
            if (strcmp(f->name, sort_by_field_name) == 0) { fa = f->value; break; }
    }
    if (b->kind == VAL_RECORD) {
        for (HField *f = b->as.record_fields; f; f = f->next)
            if (strcmp(f->name, sort_by_field_name) == 0) { fb = f->value; break; }
    }
    if (!fa || !fb) return 0;
    return hval_compare(fa, fb);
}

static HVal *fn_sort_by(Arena *a, HVal *arg, HelunaError *err) {
    HVal *list = get_field(arg, "list");
    const char *field = get_string(arg, "field");
    if (!list || list->kind != VAL_LIST || !field) {
        heluna_error_set(err, HELUNA_ERR_RUNTIME, (SrcLoc){0},
                         "sort-by: missing fields");
        return NULL;
    }
    int n = 0;
    for (HVal *v = list->as.list_head; v; v = v->next) n++;
    if (n == 0) return mk_list(a, NULL);

    SortEntry *entries = arena_alloc(a, (size_t)n * sizeof(SortEntry));
    int i = 0;
    for (HVal *v = list->as.list_head; v; v = v->next) {
        entries[i].val_copy = *v;
        i++;
    }
    sort_by_field_name = field;
    qsort(entries, (size_t)n, sizeof(SortEntry), sort_by_cmp);

    HVal *head = NULL;
    HVal **tail = &head;
    for (i = 0; i < n; i++) {
        HVal *item = arena_calloc(a, sizeof(HVal));
        *item = entries[i].val_copy;
        item->next = NULL;
        *tail = item;
        tail = &item->next;
    }
    return mk_list(a, head);
}

/* reverse (0x0032) */
static HVal *fn_reverse(Arena *a, HVal *arg, HelunaError *err) {
    HVal *list = get_field(arg, "list");
    if (!list || list->kind != VAL_LIST) {
        heluna_error_set(err, HELUNA_ERR_RUNTIME, (SrcLoc){0},
                         "reverse: missing or non-list 'list'");
        return NULL;
    }
    HVal *head = NULL;
    for (HVal *v = list->as.list_head; v; v = v->next) {
        HVal *item = arena_calloc(a, sizeof(HVal));
        *item = *v;
        item->next = head;
        head = item;
    }
    return mk_list(a, head);
}

/* unique (0x0033) */
static HVal *fn_unique(Arena *a, HVal *arg, HelunaError *err) {
    HVal *list = get_field(arg, "list");
    if (!list || list->kind != VAL_LIST) {
        heluna_error_set(err, HELUNA_ERR_RUNTIME, (SrcLoc){0},
                         "unique: missing or non-list 'list'");
        return NULL;
    }
    HVal *head = NULL;
    HVal **tail = &head;
    for (HVal *v = list->as.list_head; v; v = v->next) {
        /* O(n^2) dedup */
        int found = 0;
        for (HVal *u = head; u; u = u->next) {
            if (hval_equal(u, v)) { found = 1; break; }
        }
        if (!found) {
            HVal *item = arena_calloc(a, sizeof(HVal));
            *item = *v;
            item->next = NULL;
            *tail = item;
            tail = &item->next;
        }
    }
    return mk_list(a, head);
}

/* flatten (0x0034) */
static HVal *fn_flatten(Arena *a, HVal *arg, HelunaError *err) {
    HVal *list = get_field(arg, "list");
    if (!list || list->kind != VAL_LIST) {
        heluna_error_set(err, HELUNA_ERR_RUNTIME, (SrcLoc){0},
                         "flatten: missing or non-list 'list'");
        return NULL;
    }
    HVal *head = NULL;
    HVal **tail = &head;
    for (HVal *v = list->as.list_head; v; v = v->next) {
        if (v->kind == VAL_LIST) {
            for (HVal *sub = v->as.list_head; sub; sub = sub->next) {
                HVal *item = arena_calloc(a, sizeof(HVal));
                *item = *sub;
                item->next = NULL;
                *tail = item;
                tail = &item->next;
            }
        } else {
            HVal *item = arena_calloc(a, sizeof(HVal));
            *item = *v;
            item->next = NULL;
            *tail = item;
            tail = &item->next;
        }
    }
    return mk_list(a, head);
}

/* zip (0x0035) */
static HVal *fn_zip(Arena *a, HVal *arg, HelunaError *err) {
    HVal *la = get_field(arg, "a");
    HVal *lb = get_field(arg, "b");
    if (!la || la->kind != VAL_LIST || !lb || lb->kind != VAL_LIST) {
        heluna_error_set(err, HELUNA_ERR_RUNTIME, (SrcLoc){0},
                         "zip: missing or non-list fields");
        return NULL;
    }
    HVal *head = NULL;
    HVal **tail = &head;
    HVal *va = la->as.list_head, *vb = lb->as.list_head;
    while (va && vb) {
        HField *fb = arena_calloc(a, sizeof(HField));
        fb->name = "b";
        fb->value = vb;

        HField *fa = arena_calloc(a, sizeof(HField));
        fa->name = "a";
        fa->value = va;
        fa->next = fb;

        HVal *rec = mk_record(a, fa);
        rec->next = NULL;
        *tail = rec;
        tail = &rec->next;

        va = va->next;
        vb = vb->next;
    }
    return mk_list(a, head);
}

/* range (0x0036) */
static HVal *fn_range(Arena *a, HVal *arg, HelunaError *err) {
    long long start = 0, end = 0;
    if (!get_integer(arg, "start", &start) || !get_integer(arg, "end", &end)) {
        heluna_error_set(err, HELUNA_ERR_RUNTIME, (SrcLoc){0},
                         "range: missing 'start' or 'end'");
        return NULL;
    }
    HVal *head = NULL;
    HVal **tail = &head;
    long long step = start <= end ? 1 : -1;
    for (long long i = start; step > 0 ? i <= end : i >= end; i += step) {
        HVal *item = mk_integer(a, i);
        item->next = NULL;
        *tail = item;
        tail = &item->next;
    }
    return mk_list(a, head);
}

/* slice (0x0037) */
static HVal *fn_slice(Arena *a, HVal *arg, HelunaError *err) {
    HVal *list = get_field(arg, "list");
    long long start = 0, end_val = 0;
    if (!list || list->kind != VAL_LIST ||
        !get_integer(arg, "start", &start) ||
        !get_integer(arg, "end", &end_val)) {
        heluna_error_set(err, HELUNA_ERR_RUNTIME, (SrcLoc){0},
                         "slice: missing fields");
        return NULL;
    }
    HVal *head = NULL;
    HVal **tail = &head;
    int i = 0;
    for (HVal *v = list->as.list_head; v; v = v->next, i++) {
        if (i >= start && i < end_val) {
            HVal *item = arena_calloc(a, sizeof(HVal));
            *item = *v;
            item->next = NULL;
            *tail = item;
            tail = &item->next;
        }
    }
    return mk_list(a, head);
}

/* ── Record functions (0x0040–0x0044) ────────────────────── */

/* keys (0x0040) */
static HVal *fn_keys(Arena *a, HVal *arg, HelunaError *err) {
    HVal *rec = get_field(arg, "record");
    if (!rec || rec->kind != VAL_RECORD) {
        /* If arg itself is a record and has no "record" field, use arg */
        heluna_error_set(err, HELUNA_ERR_RUNTIME, (SrcLoc){0},
                         "keys: missing or non-record 'record'");
        return NULL;
    }
    HVal *head = NULL;
    HVal **tail = &head;
    for (HField *f = rec->as.record_fields; f; f = f->next) {
        HVal *item = mk_string(a, f->name);
        item->next = NULL;
        *tail = item;
        tail = &item->next;
    }
    return mk_list(a, head);
}

/* values (0x0041) */
static HVal *fn_values(Arena *a, HVal *arg, HelunaError *err) {
    HVal *rec = get_field(arg, "record");
    if (!rec || rec->kind != VAL_RECORD) {
        heluna_error_set(err, HELUNA_ERR_RUNTIME, (SrcLoc){0},
                         "values: missing or non-record 'record'");
        return NULL;
    }
    HVal *head = NULL;
    HVal **tail = &head;
    for (HField *f = rec->as.record_fields; f; f = f->next) {
        HVal *item = arena_calloc(a, sizeof(HVal));
        if (f->value) *item = *f->value;
        else item->kind = VAL_NOTHING;
        item->next = NULL;
        *tail = item;
        tail = &item->next;
    }
    return mk_list(a, head);
}

/* merge (0x0042) */
static HVal *fn_merge(Arena *a, HVal *arg, HelunaError *err) {
    HVal *ra = get_field(arg, "a");
    HVal *rb = get_field(arg, "b");
    if (!ra || ra->kind != VAL_RECORD || !rb || rb->kind != VAL_RECORD) {
        heluna_error_set(err, HELUNA_ERR_RUNTIME, (SrcLoc){0},
                         "merge: missing or non-record fields");
        return NULL;
    }
    /* Copy all fields from a, then overlay from b */
    HField *head = NULL;
    HField **tail = &head;
    for (HField *f = ra->as.record_fields; f; f = f->next) {
        HField *nf = arena_calloc(a, sizeof(HField));
        nf->name = f->name;
        nf->value = f->value;
        *tail = nf;
        tail = &nf->next;
    }
    for (HField *f = rb->as.record_fields; f; f = f->next) {
        /* Check if already exists */
        int found = 0;
        for (HField *e = head; e; e = e->next) {
            if (strcmp(e->name, f->name) == 0) {
                e->value = f->value;
                found = 1;
                break;
            }
        }
        if (!found) {
            HField *nf = arena_calloc(a, sizeof(HField));
            nf->name = f->name;
            nf->value = f->value;
            *tail = nf;
            tail = &nf->next;
        }
    }
    return mk_record(a, head);
}

/* pick (0x0043) */
static HVal *fn_pick(Arena *a, HVal *arg, HelunaError *err) {
    HVal *rec = get_field(arg, "record");
    HVal *fields = get_field(arg, "fields");
    if (!rec || rec->kind != VAL_RECORD || !fields || fields->kind != VAL_LIST) {
        heluna_error_set(err, HELUNA_ERR_RUNTIME, (SrcLoc){0},
                         "pick: missing fields");
        return NULL;
    }
    HField *head = NULL;
    HField **tail = &head;
    for (HVal *name_val = fields->as.list_head; name_val; name_val = name_val->next) {
        if (name_val->kind != VAL_STRING) continue;
        for (HField *f = rec->as.record_fields; f; f = f->next) {
            if (strcmp(f->name, name_val->as.string_val) == 0) {
                HField *nf = arena_calloc(a, sizeof(HField));
                nf->name = f->name;
                nf->value = f->value;
                *tail = nf;
                tail = &nf->next;
                break;
            }
        }
    }
    return mk_record(a, head);
}

/* omit (0x0044) */
static HVal *fn_omit(Arena *a, HVal *arg, HelunaError *err) {
    HVal *rec = get_field(arg, "record");
    HVal *fields = get_field(arg, "fields");
    if (!rec || rec->kind != VAL_RECORD || !fields || fields->kind != VAL_LIST) {
        heluna_error_set(err, HELUNA_ERR_RUNTIME, (SrcLoc){0},
                         "omit: missing fields");
        return NULL;
    }
    HField *head = NULL;
    HField **tail = &head;
    for (HField *f = rec->as.record_fields; f; f = f->next) {
        int omit = 0;
        for (HVal *name_val = fields->as.list_head; name_val;
             name_val = name_val->next) {
            if (name_val->kind == VAL_STRING &&
                strcmp(f->name, name_val->as.string_val) == 0) {
                omit = 1;
                break;
            }
        }
        if (!omit) {
            HField *nf = arena_calloc(a, sizeof(HField));
            nf->name = f->name;
            nf->value = f->value;
            *tail = nf;
            tail = &nf->next;
        }
    }
    return mk_record(a, head);
}

/* ── Date/Time functions (0x0050–0x0054) ─────────────────── */

/* parse-date (0x0050) */
static HVal *fn_parse_date(Arena *a, HVal *arg, HelunaError *err) {
    const char *s = get_string(arg, "value");
    const char *fmt = get_string(arg, "format");
    if (!s || !fmt) {
        heluna_error_set(err, HELUNA_ERR_RUNTIME, (SrcLoc){0},
                         "parse-date: missing fields");
        return NULL;
    }
    struct tm tm;
    memset(&tm, 0, sizeof(tm));
    if (!strptime(s, fmt, &tm)) {
        heluna_error_set(err, HELUNA_ERR_RUNTIME, (SrcLoc){0},
                         "parse-date: parse failed");
        return NULL;
    }

    /* Build record with date components */
    HField *sec_f = arena_calloc(a, sizeof(HField));
    sec_f->name = "second"; sec_f->value = mk_integer(a, tm.tm_sec);

    HField *min_f = arena_calloc(a, sizeof(HField));
    min_f->name = "minute"; min_f->value = mk_integer(a, tm.tm_min);
    min_f->next = sec_f;

    HField *hr_f = arena_calloc(a, sizeof(HField));
    hr_f->name = "hour"; hr_f->value = mk_integer(a, tm.tm_hour);
    hr_f->next = min_f;

    HField *day_f = arena_calloc(a, sizeof(HField));
    day_f->name = "day"; day_f->value = mk_integer(a, tm.tm_mday);
    day_f->next = hr_f;

    HField *mon_f = arena_calloc(a, sizeof(HField));
    mon_f->name = "month"; mon_f->value = mk_integer(a, tm.tm_mon + 1);
    mon_f->next = day_f;

    HField *yr_f = arena_calloc(a, sizeof(HField));
    yr_f->name = "year"; yr_f->value = mk_integer(a, tm.tm_year + 1900);
    yr_f->next = mon_f;

    return mk_record(a, yr_f);
}

/* format-date (0x0051) */
static HVal *fn_format_date(Arena *a, HVal *arg, HelunaError *err) {
    const char *fmt = get_string(arg, "format");
    long long year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
    HVal *date_rec = get_field(arg, "date");

    if (!fmt || !date_rec || date_rec->kind != VAL_RECORD) {
        heluna_error_set(err, HELUNA_ERR_RUNTIME, (SrcLoc){0},
                         "format-date: missing fields");
        return NULL;
    }
    get_integer(date_rec, "year", &year);
    get_integer(date_rec, "month", &month);
    get_integer(date_rec, "day", &day);
    get_integer(date_rec, "hour", &hour);
    get_integer(date_rec, "minute", &minute);
    get_integer(date_rec, "second", &second);

    struct tm tm;
    memset(&tm, 0, sizeof(tm));
    tm.tm_year = (int)(year - 1900);
    tm.tm_mon  = (int)(month - 1);
    tm.tm_mday = (int)day;
    tm.tm_hour = (int)hour;
    tm.tm_min  = (int)minute;
    tm.tm_sec  = (int)second;

    char buf[256];
    strftime(buf, sizeof buf, fmt, &tm);
    return mk_string(a, arena_strdup(a, buf));
}

/* date-diff (0x0052) */
static HVal *fn_date_diff(Arena *a, HVal *arg, HelunaError *err) {
    const char *from = get_string(arg, "from");
    const char *to = get_string(arg, "to");
    const char *unit = get_string(arg, "unit");
    if (!from || !to || !unit) {
        heluna_error_set(err, HELUNA_ERR_RUNTIME, (SrcLoc){0},
                         "date-diff: missing fields");
        return NULL;
    }
    struct tm tm_from, tm_to;
    memset(&tm_from, 0, sizeof(tm_from));
    memset(&tm_to, 0, sizeof(tm_to));
    strptime(from, "%Y-%m-%dT%H:%M:%S", &tm_from);
    strptime(to, "%Y-%m-%dT%H:%M:%S", &tm_to);

    time_t t_from = mktime(&tm_from);
    time_t t_to   = mktime(&tm_to);
    double diff = difftime(t_to, t_from);

    if (strcmp(unit, "seconds") == 0) return mk_integer(a, (long long)diff);
    if (strcmp(unit, "minutes") == 0) return mk_integer(a, (long long)(diff / 60));
    if (strcmp(unit, "hours") == 0)   return mk_integer(a, (long long)(diff / 3600));
    if (strcmp(unit, "days") == 0)    return mk_integer(a, (long long)(diff / 86400));
    return mk_integer(a, (long long)diff);
}

/* date-add (0x0053) */
static HVal *fn_date_add(Arena *a, HVal *arg, HelunaError *err) {
    const char *date = get_string(arg, "date");
    long long amount = 0;
    const char *unit = get_string(arg, "unit");
    if (!date || !unit || !get_integer(arg, "amount", &amount)) {
        heluna_error_set(err, HELUNA_ERR_RUNTIME, (SrcLoc){0},
                         "date-add: missing fields");
        return NULL;
    }
    struct tm tm;
    memset(&tm, 0, sizeof(tm));
    strptime(date, "%Y-%m-%dT%H:%M:%S", &tm);

    if (strcmp(unit, "seconds") == 0) tm.tm_sec  += (int)amount;
    else if (strcmp(unit, "minutes") == 0) tm.tm_min  += (int)amount;
    else if (strcmp(unit, "hours") == 0)   tm.tm_hour += (int)amount;
    else if (strcmp(unit, "days") == 0)    tm.tm_mday += (int)amount;
    else if (strcmp(unit, "months") == 0)  tm.tm_mon  += (int)amount;
    else if (strcmp(unit, "years") == 0)   tm.tm_year += (int)amount;

    mktime(&tm);

    char buf[64];
    strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", &tm);
    return mk_string(a, arena_strdup(a, buf));
}

/* now-date (0x0054) */
static HVal *fn_now_date(Arena *a, HVal *arg, HelunaError *err) {
    (void)arg; (void)err;
    return mk_string(a, "2024-01-01T00:00:00Z");
}

/* ── Encoding functions (0x0060–0x0065) ──────────────────── */

static const char b64_enc[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/* base64-encode (0x0060) */
static HVal *fn_base64_encode(Arena *a, HVal *arg, HelunaError *err) {
    const char *s = get_string(arg, "value");
    if (!s) {
        heluna_error_set(err, HELUNA_ERR_RUNTIME, (SrcLoc){0},
                         "base64-encode: missing 'value'");
        return NULL;
    }
    size_t len = strlen(s);
    size_t out_len = ((len + 2) / 3) * 4;
    char *buf = arena_alloc(a, out_len + 1);

    size_t j = 0;
    for (size_t i = 0; i < len; i += 3) {
        uint32_t n = ((uint32_t)(unsigned char)s[i]) << 16;
        if (i + 1 < len) n |= ((uint32_t)(unsigned char)s[i+1]) << 8;
        if (i + 2 < len) n |= ((uint32_t)(unsigned char)s[i+2]);

        buf[j++] = b64_enc[(n >> 18) & 0x3F];
        buf[j++] = b64_enc[(n >> 12) & 0x3F];
        buf[j++] = (i + 1 < len) ? b64_enc[(n >> 6) & 0x3F] : '=';
        buf[j++] = (i + 2 < len) ? b64_enc[n & 0x3F] : '=';
    }
    buf[j] = '\0';
    return mk_string(a, buf);
}

/* base64-decode (0x0061) */
static int b64_decode_char(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

static HVal *fn_base64_decode(Arena *a, HVal *arg, HelunaError *err) {
    const char *s = get_string(arg, "value");
    if (!s) {
        heluna_error_set(err, HELUNA_ERR_RUNTIME, (SrcLoc){0},
                         "base64-decode: missing 'value'");
        return NULL;
    }
    size_t len = strlen(s);
    size_t out_len = (len / 4) * 3;
    char *buf = arena_alloc(a, out_len + 1);

    size_t j = 0;
    for (size_t i = 0; i < len; i += 4) {
        int a0 = b64_decode_char(s[i]);
        int a1 = (i+1 < len) ? b64_decode_char(s[i+1]) : 0;
        int a2 = (i+2 < len && s[i+2] != '=') ? b64_decode_char(s[i+2]) : -1;
        int a3 = (i+3 < len && s[i+3] != '=') ? b64_decode_char(s[i+3]) : -1;
        if (a0 < 0 || a1 < 0) continue;

        uint32_t n = ((uint32_t)a0 << 18) | ((uint32_t)a1 << 12);
        buf[j++] = (char)((n >> 16) & 0xFF);
        if (a2 >= 0) {
            n |= ((uint32_t)a2 << 6);
            buf[j++] = (char)((n >> 8) & 0xFF);
        }
        if (a3 >= 0) {
            n |= (uint32_t)a3;
            buf[j++] = (char)(n & 0xFF);
        }
    }
    buf[j] = '\0';
    return mk_string(a, buf);
}

/* url-encode (0x0062) */
static HVal *fn_url_encode(Arena *a, HVal *arg, HelunaError *err) {
    const char *s = get_string(arg, "value");
    if (!s) {
        heluna_error_set(err, HELUNA_ERR_RUNTIME, (SrcLoc){0},
                         "url-encode: missing 'value'");
        return NULL;
    }
    size_t len = strlen(s);
    char *buf = arena_alloc(a, len * 3 + 1);
    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            buf[j++] = (char)c;
        } else {
            buf[j++] = '%';
            buf[j++] = "0123456789ABCDEF"[c >> 4];
            buf[j++] = "0123456789ABCDEF"[c & 0x0F];
        }
    }
    buf[j] = '\0';
    return mk_string(a, buf);
}

/* url-decode (0x0063) */
static HVal *fn_url_decode(Arena *a, HVal *arg, HelunaError *err) {
    const char *s = get_string(arg, "value");
    if (!s) {
        heluna_error_set(err, HELUNA_ERR_RUNTIME, (SrcLoc){0},
                         "url-decode: missing 'value'");
        return NULL;
    }
    size_t len = strlen(s);
    char *buf = arena_alloc(a, len + 1);
    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        if (s[i] == '%' && i + 2 < len) {
            int hi = 0, lo = 0;
            if (isxdigit((unsigned char)s[i+1]) &&
                isxdigit((unsigned char)s[i+2])) {
                hi = isdigit((unsigned char)s[i+1]) ? s[i+1] - '0'
                     : toupper((unsigned char)s[i+1]) - 'A' + 10;
                lo = isdigit((unsigned char)s[i+2]) ? s[i+2] - '0'
                     : toupper((unsigned char)s[i+2]) - 'A' + 10;
                buf[j++] = (char)(hi * 16 + lo);
                i += 2;
                continue;
            }
        }
        if (s[i] == '+') buf[j++] = ' ';
        else buf[j++] = s[i];
    }
    buf[j] = '\0';
    return mk_string(a, buf);
}

/* json-encode (0x0064) */
static HVal *fn_json_encode(Arena *a, HVal *arg, HelunaError *err) {
    HVal *val = get_field(arg, "value");
    if (!val) {
        heluna_error_set(err, HELUNA_ERR_RUNTIME, (SrcLoc){0},
                         "json-encode: missing 'value'");
        return NULL;
    }
    /* Emit to a temp buffer */
    char *buf = NULL;
    size_t buf_len = 0;
    FILE *f = open_memstream(&buf, &buf_len);
    if (!f) {
        heluna_error_set(err, HELUNA_ERR_RUNTIME, (SrcLoc){0},
                         "json-encode: open_memstream failed");
        return NULL;
    }
    json_emit(val, f);
    fclose(f);
    HVal *result = mk_string(a, arena_strdup(a, buf));
    free(buf);
    return result;
}

/* json-parse (0x0065) */
static HVal *fn_json_parse_fn(Arena *a, HVal *arg, HelunaError *err) {
    const char *s = get_string(arg, "value");
    if (!s) {
        heluna_error_set(err, HELUNA_ERR_RUNTIME, (SrcLoc){0},
                         "json-parse: missing 'value'");
        return NULL;
    }
    return json_parse(a, s, err);
}

/* ── Crypto functions (0x0070–0x0072) ────────────────────── */

/* sha256 (0x0070) */
static HVal *fn_sha256(Arena *a, HVal *arg, HelunaError *err) {
    const char *s = get_string(arg, "value");
    if (!s) {
        heluna_error_set(err, HELUNA_ERR_RUNTIME, (SrcLoc){0},
                         "sha256: missing 'value'");
        return NULL;
    }
    SHA256_CTX ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, (const uint8_t *)s, strlen(s));
    uint8_t hash[32];
    sha256_final(&ctx, hash);

    char *hex = arena_alloc(a, 65);
    for (int i = 0; i < 32; i++) {
        hex[i*2]     = "0123456789abcdef"[hash[i] >> 4];
        hex[i*2 + 1] = "0123456789abcdef"[hash[i] & 0x0F];
    }
    hex[64] = '\0';
    return mk_string(a, hex);
}

/* hmac-sha256 (0x0071) */
static HVal *fn_hmac_sha256(Arena *a, HVal *arg, HelunaError *err) {
    const char *key = get_string(arg, "key");
    const char *msg = get_string(arg, "value");
    if (!key || !msg) {
        heluna_error_set(err, HELUNA_ERR_RUNTIME, (SrcLoc){0},
                         "hmac-sha256: missing fields");
        return NULL;
    }

    /* HMAC per RFC 2104 */
    uint8_t key_pad[64];
    memset(key_pad, 0, 64);
    size_t klen = strlen(key);
    if (klen > 64) {
        SHA256_CTX hctx;
        sha256_init(&hctx);
        sha256_update(&hctx, (const uint8_t *)key, klen);
        sha256_final(&hctx, key_pad);
        klen = 32;
    } else {
        memcpy(key_pad, key, klen);
    }

    uint8_t i_pad[64], o_pad[64];
    for (int i = 0; i < 64; i++) {
        i_pad[i] = key_pad[i] ^ 0x36;
        o_pad[i] = key_pad[i] ^ 0x5C;
    }

    /* Inner hash */
    SHA256_CTX inner;
    sha256_init(&inner);
    sha256_update(&inner, i_pad, 64);
    sha256_update(&inner, (const uint8_t *)msg, strlen(msg));
    uint8_t inner_hash[32];
    sha256_final(&inner, inner_hash);

    /* Outer hash */
    SHA256_CTX outer;
    sha256_init(&outer);
    sha256_update(&outer, o_pad, 64);
    sha256_update(&outer, inner_hash, 32);
    uint8_t final_hash[32];
    sha256_final(&outer, final_hash);

    char *hex = arena_alloc(a, 65);
    for (int i = 0; i < 32; i++) {
        hex[i*2]     = "0123456789abcdef"[final_hash[i] >> 4];
        hex[i*2 + 1] = "0123456789abcdef"[final_hash[i] & 0x0F];
    }
    hex[64] = '\0';
    return mk_string(a, hex);
}

/* uuid (0x0072) */
static HVal *fn_uuid(Arena *a, HVal *arg, HelunaError *err) {
    (void)arg; (void)err;
    uint8_t bytes[16];

    /* Use arc4random_buf on macOS, /dev/urandom fallback */
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__)
    arc4random_buf(bytes, sizeof(bytes));
#else
    FILE *f = fopen("/dev/urandom", "rb");
    if (f) {
        size_t n = fread(bytes, 1, sizeof(bytes), f);
        (void)n;
        fclose(f);
    } else {
        /* Fallback: seed from time (not cryptographically secure) */
        srand((unsigned)time(NULL));
        for (int i = 0; i < 16; i++) bytes[i] = (uint8_t)(rand() & 0xFF);
    }
#endif

    /* Set version 4 and variant bits */
    bytes[6] = (bytes[6] & 0x0F) | 0x40;
    bytes[8] = (bytes[8] & 0x3F) | 0x80;

    char *buf = arena_alloc(a, 37);
    snprintf(buf, 37, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             bytes[0], bytes[1], bytes[2], bytes[3],
             bytes[4], bytes[5], bytes[6], bytes[7],
             bytes[8], bytes[9], bytes[10], bytes[11],
             bytes[12], bytes[13], bytes[14], bytes[15]);
    return mk_string(a, buf);
}

/* ── Conversion functions (used by evaluator, dispatched via VM) ── */

/* to-string */
static HVal *fn_to_string(Arena *a, HVal *arg, HelunaError *err) {
    HVal *val = get_field(arg, "value");
    if (!val) {
        heluna_error_set(err, HELUNA_ERR_RUNTIME, (SrcLoc){0},
                         "to-string: missing 'value'");
        return NULL;
    }
    char buf[256];
    switch (val->kind) {
    case VAL_INTEGER:
        snprintf(buf, sizeof buf, "%lld", val->as.integer_val);
        break;
    case VAL_FLOAT:
        snprintf(buf, sizeof buf, "%g", val->as.float_val);
        if (!strchr(buf, '.') && !strchr(buf, 'e') && !strchr(buf, 'E')) {
            size_t len = strlen(buf);
            if (len + 2 < sizeof(buf)) { buf[len] = '.'; buf[len+1] = '0'; buf[len+2] = '\0'; }
        }
        break;
    case VAL_STRING:
        return val;
    case VAL_BOOLEAN:
        snprintf(buf, sizeof buf, "%s", val->as.boolean_val ? "true" : "false");
        break;
    case VAL_NOTHING:
        snprintf(buf, sizeof buf, "nothing");
        break;
    default:
        heluna_error_set(err, HELUNA_ERR_RUNTIME, (SrcLoc){0},
                         "to-string: unsupported type");
        return NULL;
    }
    return mk_string(a, arena_strdup(a, buf));
}

/* to-float */
static HVal *fn_to_float(Arena *a, HVal *arg, HelunaError *err) {
    HVal *val = get_field(arg, "value");
    if (!val) {
        heluna_error_set(err, HELUNA_ERR_RUNTIME, (SrcLoc){0},
                         "to-float: missing 'value'");
        return NULL;
    }
    double d;
    switch (val->kind) {
    case VAL_INTEGER: d = (double)val->as.integer_val; break;
    case VAL_FLOAT:   d = val->as.float_val; break;
    case VAL_STRING:  d = strtod(val->as.string_val, NULL); break;
    default:
        heluna_error_set(err, HELUNA_ERR_RUNTIME, (SrcLoc){0},
                         "to-float: cannot convert");
        return NULL;
    }
    return mk_float(a, d);
}

/* to-integer */
static HVal *fn_to_integer(Arena *a, HVal *arg, HelunaError *err) {
    HVal *val = get_field(arg, "value");
    if (!val) {
        heluna_error_set(err, HELUNA_ERR_RUNTIME, (SrcLoc){0},
                         "to-integer: missing 'value'");
        return NULL;
    }
    long long n;
    switch (val->kind) {
    case VAL_INTEGER: n = val->as.integer_val; break;
    case VAL_FLOAT:   n = (long long)val->as.float_val; break;
    case VAL_STRING:  n = strtoll(val->as.string_val, NULL, 10); break;
    default:
        heluna_error_set(err, HELUNA_ERR_RUNTIME, (SrcLoc){0},
                         "to-integer: cannot convert");
        return NULL;
    }
    return mk_integer(a, n);
}

/* fold */
static HVal *fn_fold(Arena *a, HVal *arg, HelunaError *err) {
    HVal *list = get_field(arg, "list");
    HVal *initial = get_field(arg, "initial");
    HVal *fn = get_field(arg, "fn");

    if (!list || !initial || !fn) {
        heluna_error_set(err, HELUNA_ERR_RUNTIME, (SrcLoc){0},
                         "fold: missing required fields");
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
                acc = mk_integer(a, acc->as.integer_val + item->as.integer_val);
            } else {
                double da = acc->kind == VAL_FLOAT ? acc->as.float_val
                                                   : (double)acc->as.integer_val;
                double db = item->kind == VAL_FLOAT ? item->as.float_val
                                                    : (double)item->as.integer_val;
                acc = mk_float(a, da + db);
            }
        } else if (strcmp(fn_name, "multiply") == 0) {
            if (acc->kind == VAL_INTEGER && item->kind == VAL_INTEGER) {
                acc = mk_integer(a, acc->as.integer_val * item->as.integer_val);
            } else {
                double da = acc->kind == VAL_FLOAT ? acc->as.float_val
                                                   : (double)acc->as.integer_val;
                double db = item->kind == VAL_FLOAT ? item->as.float_val
                                                    : (double)item->as.integer_val;
                acc = mk_float(a, da * db);
            }
        } else {
            heluna_error_set(err, HELUNA_ERR_RUNTIME, (SrcLoc){0},
                             "fold: unsupported fn '%s'", fn_name);
            return NULL;
        }
    }

    return acc;
}

/* ── Dispatch table ──────────────────────────────────────── */

typedef HVal *(*VmStdlibFn)(Arena *a, HVal *arg, HelunaError *err);

typedef struct {
    uint16_t    func_id;
    VmStdlibFn  fn;
} VmStdlibEntry;

static const VmStdlibEntry vm_stdlib_table[] = {
    /* String */
    { 0x0001, fn_upper },
    { 0x0002, fn_lower },
    { 0x0003, fn_trim },
    { 0x0004, fn_trim_start },
    { 0x0005, fn_trim_end },
    { 0x0006, fn_substring },
    { 0x0007, fn_replace },
    { 0x0008, fn_split },
    { 0x0009, fn_join },
    { 0x000A, fn_starts_with },
    { 0x000B, fn_ends_with },
    { 0x000C, fn_contains },
    { 0x000D, fn_length },
    { 0x000E, fn_pad_left },
    { 0x000F, fn_pad_right },
    { 0x0010, fn_regex_match },
    { 0x0011, fn_regex_replace },
    /* Numeric */
    { 0x0020, fn_abs },
    { 0x0021, fn_ceil },
    { 0x0022, fn_floor },
    { 0x0023, fn_round },
    { 0x0024, fn_min },
    { 0x0025, fn_max },
    { 0x0026, fn_clamp },
    /* List */
    { 0x0030, fn_sort },
    { 0x0031, fn_sort_by },
    { 0x0032, fn_reverse },
    { 0x0033, fn_unique },
    { 0x0034, fn_flatten },
    { 0x0035, fn_zip },
    { 0x0036, fn_range },
    { 0x0037, fn_slice },
    /* Record */
    { 0x0040, fn_keys },
    { 0x0041, fn_values },
    { 0x0042, fn_merge },
    { 0x0043, fn_pick },
    { 0x0044, fn_omit },
    /* Date/Time */
    { 0x0050, fn_parse_date },
    { 0x0051, fn_format_date },
    { 0x0052, fn_date_diff },
    { 0x0053, fn_date_add },
    { 0x0054, fn_now_date },
    /* Encoding */
    { 0x0060, fn_base64_encode },
    { 0x0061, fn_base64_decode },
    { 0x0062, fn_url_encode },
    { 0x0063, fn_url_decode },
    { 0x0064, fn_json_encode },
    { 0x0065, fn_json_parse_fn },
    /* Crypto */
    { 0x0070, fn_sha256 },
    { 0x0071, fn_hmac_sha256 },
    { 0x0072, fn_uuid },
    /* Conversion (evaluator stdlib IDs from compiler) */
    { 0x00F0, fn_to_string },
    { 0x00F1, fn_to_float },
    { 0x00F2, fn_to_integer },
    { 0x00F3, fn_fold },
    { 0, NULL },
};

/* ── Direct dispatch table (O(1) lookup by func_id) ──────── */

#define STDLIB_DISPATCH_SIZE 244  /* max func_id 0x00F3 = 243, +1 */

static VmStdlibFn stdlib_dispatch[STDLIB_DISPATCH_SIZE];
static int stdlib_dispatch_init = 0;

static void stdlib_init_dispatch(void) {
    if (stdlib_dispatch_init) return;
    memset(stdlib_dispatch, 0, sizeof(stdlib_dispatch));
    for (const VmStdlibEntry *e = vm_stdlib_table; e->fn; e++) {
        if (e->func_id < STDLIB_DISPATCH_SIZE) {
            stdlib_dispatch[e->func_id] = e->fn;
        }
    }
    stdlib_dispatch_init = 1;
}

HVal *vm_stdlib_call(uint16_t func_id, HVal *args,
                     Arena *arena, HelunaError *err) {
    if (!stdlib_dispatch_init) stdlib_init_dispatch();

    if (func_id < STDLIB_DISPATCH_SIZE && stdlib_dispatch[func_id]) {
        return stdlib_dispatch[func_id](arena, args, err);
    }

    heluna_error_set(err, HELUNA_ERR_RUNTIME, (SrcLoc){0},
                     "unknown stdlib function ID 0x%04X", func_id);
    return NULL;
}

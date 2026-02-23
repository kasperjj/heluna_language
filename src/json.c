#include "heluna/json.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

/* ── JSON Parser ─────────────────────────────────────────── */

typedef struct {
    const char *src;
    int         pos;
    Arena      *arena;
    HelunaError *err;
} JsonParser;

static void skip_ws(JsonParser *p) {
    while (p->src[p->pos] &&
           (p->src[p->pos] == ' ' || p->src[p->pos] == '\t' ||
            p->src[p->pos] == '\n' || p->src[p->pos] == '\r')) {
        p->pos++;
    }
}

static char peek(JsonParser *p) {
    skip_ws(p);
    return p->src[p->pos];
}

static int expect_char(JsonParser *p, char c) {
    skip_ws(p);
    if (p->src[p->pos] == c) {
        p->pos++;
        return 1;
    }
    heluna_error_set(p->err, HELUNA_ERR_RUNTIME, (SrcLoc){0},
                     "JSON: expected '%c' at position %d", c, p->pos);
    return 0;
}

static int match_str(JsonParser *p, const char *s) {
    size_t len = strlen(s);
    if (strncmp(p->src + p->pos, s, len) == 0) {
        p->pos += (int)len;
        return 1;
    }
    return 0;
}

static HVal *parse_value(JsonParser *p);

static HVal *parse_string(JsonParser *p) {
    p->pos++; /* skip opening " */

    /* Find end, accounting for escapes */
    int start = p->pos;
    int out_len = 0;
    while (p->src[p->pos] && p->src[p->pos] != '"') {
        if (p->src[p->pos] == '\\') {
            p->pos++;
            if (!p->src[p->pos]) break;
        }
        p->pos++;
        out_len++;
    }
    int end = p->pos;
    if (p->src[p->pos] == '"') p->pos++;

    /* Resolve escapes */
    char *buf = arena_alloc(p->arena, (size_t)out_len + 1);
    int j = 0;
    for (int i = start; i < end; i++) {
        if (p->src[i] == '\\' && i + 1 < end) {
            i++;
            switch (p->src[i]) {
            case 'n':  buf[j++] = '\n'; break;
            case 't':  buf[j++] = '\t'; break;
            case 'r':  buf[j++] = '\r'; break;
            case '\\': buf[j++] = '\\'; break;
            case '"':  buf[j++] = '"'; break;
            case '/':  buf[j++] = '/'; break;
            default:   buf[j++] = p->src[i]; break;
            }
        } else {
            buf[j++] = p->src[i];
        }
    }
    buf[j] = '\0';

    HVal *v = arena_calloc(p->arena, sizeof(HVal));
    v->kind = VAL_STRING;
    v->as.string_val = buf;
    return v;
}

static HVal *parse_number(JsonParser *p) {
    int start = p->pos;
    int is_float = 0;

    if (p->src[p->pos] == '-') p->pos++;

    while (isdigit((unsigned char)p->src[p->pos])) p->pos++;

    if (p->src[p->pos] == '.') {
        is_float = 1;
        p->pos++;
        while (isdigit((unsigned char)p->src[p->pos])) p->pos++;
    }

    if (p->src[p->pos] == 'e' || p->src[p->pos] == 'E') {
        is_float = 1;
        p->pos++;
        if (p->src[p->pos] == '+' || p->src[p->pos] == '-') p->pos++;
        while (isdigit((unsigned char)p->src[p->pos])) p->pos++;
    }

    char *numstr = arena_strndup(p->arena, p->src + start,
                                 (size_t)(p->pos - start));

    HVal *v = arena_calloc(p->arena, sizeof(HVal));
    if (is_float) {
        v->kind = VAL_FLOAT;
        v->as.float_val = strtod(numstr, NULL);
    } else {
        v->kind = VAL_INTEGER;
        v->as.integer_val = strtoll(numstr, NULL, 10);
    }
    return v;
}

static HVal *parse_array(JsonParser *p) {
    p->pos++; /* skip [ */
    HVal *head = NULL;
    HVal **tail = &head;

    if (peek(p) == ']') {
        p->pos++;
        HVal *v = arena_calloc(p->arena, sizeof(HVal));
        v->kind = VAL_LIST;
        v->as.list_head = NULL;
        return v;
    }

    while (1) {
        HVal *elem = parse_value(p);
        if (!elem) return NULL;

        HVal *item = arena_calloc(p->arena, sizeof(HVal));
        *item = *elem;
        item->next = NULL;
        *tail = item;
        tail = &item->next;

        if (peek(p) == ',') {
            p->pos++;
        } else {
            break;
        }
    }

    if (!expect_char(p, ']')) return NULL;

    HVal *v = arena_calloc(p->arena, sizeof(HVal));
    v->kind = VAL_LIST;
    v->as.list_head = head;
    return v;
}

static HVal *parse_object(JsonParser *p) {
    p->pos++; /* skip { */
    HField *head = NULL;
    HField **tail = &head;

    if (peek(p) == '}') {
        p->pos++;
        HVal *v = arena_calloc(p->arena, sizeof(HVal));
        v->kind = VAL_RECORD;
        v->as.record_fields = NULL;
        return v;
    }

    while (1) {
        skip_ws(p);
        if (p->src[p->pos] != '"') {
            heluna_error_set(p->err, HELUNA_ERR_RUNTIME, (SrcLoc){0},
                             "JSON: expected string key at position %d",
                             p->pos);
            return NULL;
        }

        HVal *key = parse_string(p);
        if (!key) return NULL;

        if (!expect_char(p, ':')) return NULL;

        HVal *val = parse_value(p);
        if (!val) return NULL;

        HField *f = arena_calloc(p->arena, sizeof(HField));
        f->name = key->as.string_val;
        f->value = val;
        *tail = f;
        tail = &f->next;

        if (peek(p) == ',') {
            p->pos++;
        } else {
            break;
        }
    }

    if (!expect_char(p, '}')) return NULL;

    HVal *v = arena_calloc(p->arena, sizeof(HVal));
    v->kind = VAL_RECORD;
    v->as.record_fields = head;
    return v;
}

static HVal *parse_value(JsonParser *p) {
    char c = peek(p);

    if (c == '"') return parse_string(p);
    if (c == '{') return parse_object(p);
    if (c == '[') return parse_array(p);

    if (c == '-' || isdigit((unsigned char)c)) return parse_number(p);

    if (match_str(p, "true")) {
        HVal *v = arena_calloc(p->arena, sizeof(HVal));
        v->kind = VAL_BOOLEAN;
        v->as.boolean_val = 1;
        return v;
    }
    if (match_str(p, "false")) {
        HVal *v = arena_calloc(p->arena, sizeof(HVal));
        v->kind = VAL_BOOLEAN;
        v->as.boolean_val = 0;
        return v;
    }
    if (match_str(p, "null")) {
        HVal *v = arena_calloc(p->arena, sizeof(HVal));
        v->kind = VAL_NOTHING;
        return v;
    }

    heluna_error_set(p->err, HELUNA_ERR_RUNTIME, (SrcLoc){0},
                     "JSON: unexpected character '%c' at position %d",
                     c, p->pos);
    return NULL;
}

HVal *json_parse(Arena *arena, const char *input, HelunaError *err) {
    JsonParser p = { .src = input, .pos = 0, .arena = arena, .err = err };
    return parse_value(&p);
}

/* ── JSON Emitter ────────────────────────────────────────── */

static void emit_string(const char *s, FILE *out) {
    fputc('"', out);
    for (const char *p = s; *p; p++) {
        switch (*p) {
        case '"':  fputs("\\\"", out); break;
        case '\\': fputs("\\\\", out); break;
        case '\n': fputs("\\n", out); break;
        case '\t': fputs("\\t", out); break;
        case '\r': fputs("\\r", out); break;
        default:
            if ((unsigned char)*p < 0x20) {
                fprintf(out, "\\u%04x", (unsigned char)*p);
            } else {
                fputc(*p, out);
            }
            break;
        }
    }
    fputc('"', out);
}

void json_emit(const HVal *val, FILE *out) {
    if (!val) {
        fputs("null", out);
        return;
    }

    switch (val->kind) {
    case VAL_INTEGER:
        fprintf(out, "%lld", val->as.integer_val);
        break;

    case VAL_FLOAT: {
        /* Emit with enough precision, but remove trailing zeros */
        char buf[64];
        snprintf(buf, sizeof buf, "%.17g", val->as.float_val);
        fputs(buf, out);
        break;
    }

    case VAL_STRING:
        emit_string(val->as.string_val, out);
        break;

    case VAL_BOOLEAN:
        fputs(val->as.boolean_val ? "true" : "false", out);
        break;

    case VAL_NOTHING:
        fputs("null", out);
        break;

    case VAL_RECORD: {
        fputc('{', out);
        int first = 1;
        for (const HField *f = val->as.record_fields; f; f = f->next) {
            if (!first) fputc(',', out);
            first = 0;
            emit_string(f->name, out);
            fputc(':', out);
            json_emit(f->value, out);
        }
        fputc('}', out);
        break;
    }

    case VAL_LIST: {
        fputc('[', out);
        int first = 1;
        for (const HVal *v = val->as.list_head; v; v = v->next) {
            if (!first) fputc(',', out);
            first = 0;
            json_emit(v, out);
        }
        fputc(']', out);
        break;
    }
    }
}

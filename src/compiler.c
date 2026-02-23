#include "heluna/compiler.h"
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

/* ── Stdlib name-to-ID table (§6 of VM spec) ────────────── */

typedef struct {
    const char *name;
    uint16_t    func_id;
} StdlibEntry;

static const StdlibEntry stdlib_table[] = {
    /* String */
    { "upper",         0x0001 },
    { "lower",         0x0002 },
    { "trim",          0x0003 },
    { "trim-start",    0x0004 },
    { "trim-end",      0x0005 },
    { "substring",     0x0006 },
    { "replace",       0x0007 },
    { "split",         0x0008 },
    { "join",          0x0009 },
    { "starts-with",   0x000A },
    { "ends-with",     0x000B },
    { "contains",      0x000C },
    { "length",        0x000D },
    { "pad-left",      0x000E },
    { "pad-right",     0x000F },
    { "regex-match",   0x0010 },
    { "regex-replace", 0x0011 },
    /* Numeric */
    { "abs",           0x0020 },
    { "ceil",          0x0021 },
    { "floor",         0x0022 },
    { "round",         0x0023 },
    { "min",           0x0024 },
    { "max",           0x0025 },
    { "clamp",         0x0026 },
    /* List */
    { "sort",          0x0030 },
    { "sort-by",       0x0031 },
    { "reverse",       0x0032 },
    { "unique",        0x0033 },
    { "flatten",       0x0034 },
    { "zip",           0x0035 },
    { "range",         0x0036 },
    { "slice",         0x0037 },
    /* Record */
    { "keys",          0x0040 },
    { "values",        0x0041 },
    { "merge",         0x0042 },
    { "pick",          0x0043 },
    { "omit",          0x0044 },
    /* Date/Time */
    { "parse-date",    0x0050 },
    { "format-date",   0x0051 },
    { "date-diff",     0x0052 },
    { "date-add",      0x0053 },
    { "now-date",      0x0054 },
    /* Encoding */
    { "base64-encode", 0x0060 },
    { "base64-decode", 0x0061 },
    { "url-encode",    0x0062 },
    { "url-decode",    0x0063 },
    { "json-encode",   0x0064 },
    { "json-parse",    0x0065 },
    /* Crypto */
    { "sha256",        0x0070 },
    { "hmac-sha256",   0x0071 },
    { "uuid",          0x0072 },
    { NULL, 0 },
};

static int stdlib_lookup(const char *name, uint16_t *out_id) {
    for (const StdlibEntry *e = stdlib_table; e->name; e++) {
        if (strcmp(e->name, name) == 0) {
            *out_id = e->func_id;
            return 1;
        }
    }
    return 0;
}

/* ── Error helpers ───────────────────────────────────────── */

static void add_error(Compiler *c, HelunaErrorKind kind, SrcLoc loc,
                      const char *fmt, ...) {
    CompilerErrors *e = &c->errors;
    if (e->count == e->capacity) {
        int new_cap = e->capacity ? e->capacity * 2 : 16;
        HelunaError *new_arr = arena_alloc(e->arena,
                                           (size_t)new_cap * sizeof(HelunaError));
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

/* ── Scratchpad allocation ───────────────────────────────── */

static uint16_t scratch_alloc(Compiler *c) {
    return (uint16_t)c->scratch_next++;
}

/* ── Instruction buffer ──────────────────────────────────── */

static int emit(Compiler *c, uint8_t opcode, uint8_t flags,
                uint16_t dest, uint16_t op1, uint16_t op2) {
    if (c->instr_count == c->instr_capacity) {
        int new_cap = c->instr_capacity ? c->instr_capacity * 2 : 64;
        Instruction *new_arr = arena_alloc(c->arena,
                                           (size_t)new_cap * sizeof(Instruction));
        if (c->instr_count > 0) {
            memcpy(new_arr, c->instructions,
                   (size_t)c->instr_count * sizeof(Instruction));
        }
        c->instructions = new_arr;
        c->instr_capacity = new_cap;
    }

    int idx = c->instr_count++;
    Instruction *ins = &c->instructions[idx];
    ins->opcode   = opcode;
    ins->flags    = flags;
    ins->dest     = dest;
    ins->operand1 = op1;
    ins->operand2 = op2;
    return idx;
}

static void patch_jump(Compiler *c, int instr_idx, uint16_t target) {
    c->instructions[instr_idx].dest = target;
}

static uint8_t make_flags(uint8_t type_hint, uint8_t tag_mode) {
    return (uint8_t)((type_hint & 0x07) | ((tag_mode & 0x03) << 3));
}

static uint8_t make_iter_flags(uint8_t mode, uint8_t parallel, uint8_t complexity) {
    return (uint8_t)((mode & 0x03) | ((parallel & 0x01) << 2) |
                     ((complexity & 0x03) << 3));
}

/* ── Constant pool ───────────────────────────────────────── */

static void const_pool_grow_data(Compiler *c, int needed) {
    if (c->const_data_len + needed <= c->const_data_cap) return;
    int new_cap = c->const_data_cap ? c->const_data_cap * 2 : 1024;
    while (new_cap < c->const_data_len + needed) new_cap *= 2;
    uint8_t *new_data = arena_alloc(c->arena, (size_t)new_cap);
    if (c->const_data_len > 0) {
        memcpy(new_data, c->const_data, (size_t)c->const_data_len);
    }
    c->const_data = new_data;
    c->const_data_cap = new_cap;
}

static void const_pool_grow_index(Compiler *c) {
    if (c->const_count < c->const_index_cap) return;
    int new_cap = c->const_index_cap ? c->const_index_cap * 2 : 64;
    uint8_t *new_types = arena_alloc(c->arena, (size_t)new_cap);
    int *new_offsets = arena_alloc(c->arena, (size_t)new_cap * sizeof(int));
    int *new_lengths = arena_alloc(c->arena, (size_t)new_cap * sizeof(int));
    if (c->const_count > 0) {
        memcpy(new_types, c->const_types, (size_t)c->const_count);
        memcpy(new_offsets, c->const_offsets, (size_t)c->const_count * sizeof(int));
        memcpy(new_lengths, c->const_lengths, (size_t)c->const_count * sizeof(int));
    }
    c->const_types   = new_types;
    c->const_offsets  = new_offsets;
    c->const_lengths  = new_lengths;
    c->const_index_cap = new_cap;
}

static uint16_t add_const_string(Compiler *c, const char *str, int len) {
    /* Dedup: check for existing identical string constant */
    for (int i = 0; i < c->const_count; i++) {
        if (c->const_types[i] == FTYPE_STRING &&
            c->const_lengths[i] == len &&
            memcmp(c->const_data + c->const_offsets[i], str, (size_t)len) == 0) {
            return (uint16_t)i;
        }
    }

    const_pool_grow_index(c);
    const_pool_grow_data(c, len);

    int idx = c->const_count++;
    c->const_types[idx]   = FTYPE_STRING;
    c->const_offsets[idx]  = c->const_data_len;
    c->const_lengths[idx]  = len;
    memcpy(c->const_data + c->const_data_len, str, (size_t)len);
    c->const_data_len += len;
    return (uint16_t)idx;
}

static uint16_t add_const_integer(Compiler *c, long long val) {
    /* Dedup */
    for (int i = 0; i < c->const_count; i++) {
        if (c->const_types[i] == FTYPE_INTEGER && c->const_lengths[i] == 8) {
            long long existing;
            memcpy(&existing, c->const_data + c->const_offsets[i], 8);
            if (existing == val) return (uint16_t)i;
        }
    }

    const_pool_grow_index(c);
    const_pool_grow_data(c, 8);

    int idx = c->const_count++;
    c->const_types[idx]   = FTYPE_INTEGER;
    c->const_offsets[idx]  = c->const_data_len;
    c->const_lengths[idx]  = 8;
    memcpy(c->const_data + c->const_data_len, &val, 8);
    c->const_data_len += 8;
    return (uint16_t)idx;
}

static uint16_t add_const_float(Compiler *c, double val) {
    /* Dedup */
    for (int i = 0; i < c->const_count; i++) {
        if (c->const_types[i] == FTYPE_FLOAT && c->const_lengths[i] == 8) {
            double existing;
            memcpy(&existing, c->const_data + c->const_offsets[i], 8);
            if (existing == val) return (uint16_t)i;
        }
    }

    const_pool_grow_index(c);
    const_pool_grow_data(c, 8);

    int idx = c->const_count++;
    c->const_types[idx]   = FTYPE_FLOAT;
    c->const_offsets[idx]  = c->const_data_len;
    c->const_lengths[idx]  = 8;
    memcpy(c->const_data + c->const_data_len, &val, 8);
    c->const_data_len += 8;
    return (uint16_t)idx;
}

static uint16_t add_const_boolean(Compiler *c, int val) {
    uint8_t b = val ? 1 : 0;
    /* Dedup */
    for (int i = 0; i < c->const_count; i++) {
        if (c->const_types[i] == FTYPE_BOOLEAN && c->const_lengths[i] == 1) {
            if (c->const_data[c->const_offsets[i]] == b) return (uint16_t)i;
        }
    }

    const_pool_grow_index(c);
    const_pool_grow_data(c, 1);

    int idx = c->const_count++;
    c->const_types[idx]   = FTYPE_BOOLEAN;
    c->const_offsets[idx]  = c->const_data_len;
    c->const_lengths[idx]  = 1;
    c->const_data[c->const_data_len] = b;
    c->const_data_len += 1;
    return (uint16_t)idx;
}

/* ── Scope management ────────────────────────────────────── */

static void scope_push(Compiler *c, const char *name, uint16_t slot,
                       CompileType type) {
    if (c->scope_count == c->scope_capacity) {
        int new_cap = c->scope_capacity ? c->scope_capacity * 2 : 32;
        size_t entry_size = sizeof(*c->scope);
        void *new_arr = arena_alloc(c->arena, (size_t)new_cap * entry_size);
        if (c->scope_count > 0) {
            memcpy(new_arr, c->scope, (size_t)c->scope_count * entry_size);
        }
        c->scope = new_arr;
        c->scope_capacity = new_cap;
    }
    c->scope[c->scope_count].name = name;
    c->scope[c->scope_count].slot = slot;
    c->scope[c->scope_count].type = type;
    c->scope_count++;
}

static int scope_lookup(const Compiler *c, const char *name,
                        uint16_t *out_slot, CompileType *out_type) {
    for (int i = c->scope_count - 1; i >= 0; i--) {
        if (strcmp(c->scope[i].name, name) == 0) {
            *out_slot = c->scope[i].slot;
            *out_type = c->scope[i].type;
            return 1;
        }
    }
    return 0;
}

/* ── Stdlib dependency tracking ──────────────────────────── */

static void track_stdlib(Compiler *c, uint16_t func_id) {
    /* Dedup */
    for (int i = 0; i < c->stdlib_dep_count; i++) {
        if (c->stdlib_deps[i] == func_id) return;
    }
    if (c->stdlib_dep_count == c->stdlib_dep_cap) {
        int new_cap = c->stdlib_dep_cap ? c->stdlib_dep_cap * 2 : 16;
        uint16_t *new_arr = arena_alloc(c->arena,
                                        (size_t)new_cap * sizeof(uint16_t));
        if (c->stdlib_dep_count > 0) {
            memcpy(new_arr, c->stdlib_deps,
                   (size_t)c->stdlib_dep_count * sizeof(uint16_t));
        }
        c->stdlib_deps = new_arr;
        c->stdlib_dep_cap = new_cap;
    }
    c->stdlib_deps[c->stdlib_dep_count++] = func_id;
}

/* ── Tag map ─────────────────────────────────────────────── */

static int tag_bit_index(const Compiler *c, const char *name) {
    for (int i = 0; i < c->tag_map_count; i++) {
        if (strcmp(c->tag_map[i].name, name) == 0) {
            return c->tag_map[i].bit_index;
        }
    }
    return -1;
}

static uint64_t compute_tag_bits(const Compiler *c, const char **tags,
                                 int tag_count) {
    uint64_t bits = 0;
    for (int i = 0; i < tag_count; i++) {
        int idx = tag_bit_index(c, tags[i]);
        if (idx >= 0) bits |= (1ULL << idx);
    }
    return bits;
}

/* ── String resolution (duplicated from evaluator.c) ─────── */

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
            i++;
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

/* ── Type helpers ────────────────────────────────────────── */

static uint8_t compile_type_to_hint(CompileType t) {
    switch (t) {
    case CTYPE_STRING:  return THINT_STRING;
    case CTYPE_INTEGER: return THINT_INTEGER;
    case CTYPE_FLOAT:   return THINT_FLOAT;
    case CTYPE_BOOLEAN: return THINT_BOOLEAN;
    case CTYPE_LIST:    return THINT_LIST;
    case CTYPE_RECORD:  return THINT_RECORD;
    case CTYPE_NOTHING: return THINT_NOTHING;
    default:            return THINT_UNSPECIFIED;
    }
}

static CompileType ast_type_to_ctype(const AstType *t) {
    if (!t) return CTYPE_UNKNOWN;
    switch (t->kind) {
    case TYPE_STRING:  return CTYPE_STRING;
    case TYPE_INTEGER: return CTYPE_INTEGER;
    case TYPE_FLOAT:   return CTYPE_FLOAT;
    case TYPE_BOOLEAN: return CTYPE_BOOLEAN;
    case TYPE_MAYBE:   return CTYPE_UNKNOWN;
    case TYPE_LIST:    return CTYPE_LIST;
    case TYPE_RECORD:  return CTYPE_RECORD;
    }
    return CTYPE_UNKNOWN;
}

static uint8_t ast_type_to_ftype(const AstType *t) {
    if (!t) return FTYPE_STRING;
    switch (t->kind) {
    case TYPE_STRING:  return FTYPE_STRING;
    case TYPE_INTEGER: return FTYPE_INTEGER;
    case TYPE_FLOAT:   return FTYPE_FLOAT;
    case TYPE_BOOLEAN: return FTYPE_BOOLEAN;
    case TYPE_MAYBE:   return FTYPE_MAYBE;
    case TYPE_LIST:    return FTYPE_LIST;
    case TYPE_RECORD:  return FTYPE_RECORD;
    }
    return FTYPE_STRING;
}

/* ── Forward declarations ────────────────────────────────── */

static CompileResult compile_expr(Compiler *c, const AstExpr *e, int target);
static void compile_pattern_test(Compiler *c, const AstPattern *p,
                                 uint16_t subject_slot, uint16_t result_slot);
static void compile_pattern_bindings(Compiler *c, const AstPattern *p,
                                     uint16_t subject_slot);

/* ── Expression compilation ──────────────────────────────── */

static CompileResult compile_expr(Compiler *c, const AstExpr *e, int target) {
    CompileResult r = { 0, CTYPE_UNKNOWN };

    if (!e) {
        add_error(c, HELUNA_ERR_SYNTAX, (SrcLoc){NULL, 0, 0},
                  "null expression in compiler");
        return r;
    }

    uint16_t dest = (target >= 0) ? (uint16_t)target : scratch_alloc(c);

    switch (e->kind) {

    case EXPR_INTEGER: {
        uint16_t ci = add_const_integer(c, e->as.integer_val);
        emit(c, OP_LOAD_CONST, make_flags(THINT_INTEGER, TMODE_PROPAGATE),
             dest, ci, 0);
        r.slot = dest;
        r.type = CTYPE_INTEGER;
        break;
    }

    case EXPR_FLOAT: {
        uint16_t ci = add_const_float(c, e->as.float_val);
        emit(c, OP_LOAD_CONST, make_flags(THINT_FLOAT, TMODE_PROPAGATE),
             dest, ci, 0);
        r.slot = dest;
        r.type = CTYPE_FLOAT;
        break;
    }

    case EXPR_STRING: {
        const char *resolved = resolve_string(c->arena, e->as.string_val.value,
                                              e->as.string_val.length);
        int slen = (int)strlen(resolved);
        uint16_t ci = add_const_string(c, resolved, slen);
        emit(c, OP_LOAD_CONST, make_flags(THINT_STRING, TMODE_PROPAGATE),
             dest, ci, 0);
        r.slot = dest;
        r.type = CTYPE_STRING;
        break;
    }

    case EXPR_TRUE: {
        uint16_t ci = add_const_boolean(c, 1);
        emit(c, OP_LOAD_CONST, make_flags(THINT_BOOLEAN, TMODE_PROPAGATE),
             dest, ci, 0);
        r.slot = dest;
        r.type = CTYPE_BOOLEAN;
        break;
    }

    case EXPR_FALSE: {
        uint16_t ci = add_const_boolean(c, 0);
        emit(c, OP_LOAD_CONST, make_flags(THINT_BOOLEAN, TMODE_PROPAGATE),
             dest, ci, 0);
        r.slot = dest;
        r.type = CTYPE_BOOLEAN;
        break;
    }

    case EXPR_NOTHING: {
        emit(c, OP_LOAD_NOTHING, make_flags(THINT_NOTHING, TMODE_PROPAGATE),
             dest, 0, 0);
        r.slot = dest;
        r.type = CTYPE_NOTHING;
        break;
    }

    case EXPR_IDENT: {
        uint16_t src_slot;
        CompileType src_type;
        if (scope_lookup(c, e->as.ident.name, &src_slot, &src_type)) {
            if (src_slot != dest) {
                emit(c, OP_COPY, make_flags(compile_type_to_hint(src_type),
                     TMODE_PROPAGATE), dest, src_slot, 0);
            }
            r.slot = (src_slot == dest) ? dest : dest;
            r.type = src_type;
        } else {
            add_error(c, HELUNA_ERR_CONTRACT, e->loc,
                      "undefined identifier '%s' in compiler", e->as.ident.name);
            r.slot = dest;
        }
        break;
    }

    case EXPR_INPUT_REF: {
        uint16_t src_slot;
        CompileType src_type;
        if (scope_lookup(c, e->as.input_ref.name, &src_slot, &src_type)) {
            if (src_slot != dest) {
                emit(c, OP_COPY, make_flags(compile_type_to_hint(src_type),
                     TMODE_PROPAGATE), dest, src_slot, 0);
            }
            r.slot = dest;
            r.type = src_type;
        } else {
            add_error(c, HELUNA_ERR_CONTRACT, e->loc,
                      "unknown input ref '$%s'", e->as.input_ref.name);
            r.slot = dest;
        }
        break;
    }

    case EXPR_BINARY: {
        CompileResult left  = compile_expr(c, e->as.binary.left, -1);
        CompileResult right = compile_expr(c, e->as.binary.right, -1);

        switch (e->as.binary.op) {
        case BIN_ADD:
            if (left.type == CTYPE_STRING || right.type == CTYPE_STRING) {
                emit(c, OP_STR_CONCAT, make_flags(THINT_STRING, TMODE_PROPAGATE),
                     dest, left.slot, right.slot);
                r.type = CTYPE_STRING;
            } else {
                uint8_t hint = (left.type == CTYPE_FLOAT || right.type == CTYPE_FLOAT)
                               ? THINT_FLOAT : THINT_INTEGER;
                emit(c, OP_ADD, make_flags(hint, TMODE_PROPAGATE),
                     dest, left.slot, right.slot);
                r.type = (hint == THINT_FLOAT) ? CTYPE_FLOAT : CTYPE_INTEGER;
            }
            break;
        case BIN_SUB: {
            uint8_t hint = (left.type == CTYPE_FLOAT || right.type == CTYPE_FLOAT)
                           ? THINT_FLOAT : THINT_INTEGER;
            emit(c, OP_SUB, make_flags(hint, TMODE_PROPAGATE),
                 dest, left.slot, right.slot);
            r.type = (hint == THINT_FLOAT) ? CTYPE_FLOAT : CTYPE_INTEGER;
            break;
        }
        case BIN_MUL: {
            uint8_t hint = (left.type == CTYPE_FLOAT || right.type == CTYPE_FLOAT)
                           ? THINT_FLOAT : THINT_INTEGER;
            emit(c, OP_MUL, make_flags(hint, TMODE_PROPAGATE),
                 dest, left.slot, right.slot);
            r.type = (hint == THINT_FLOAT) ? CTYPE_FLOAT : CTYPE_INTEGER;
            break;
        }
        case BIN_DIV: {
            uint8_t hint = (left.type == CTYPE_FLOAT || right.type == CTYPE_FLOAT)
                           ? THINT_FLOAT : THINT_INTEGER;
            emit(c, OP_DIV, make_flags(hint, TMODE_PROPAGATE),
                 dest, left.slot, right.slot);
            r.type = (hint == THINT_FLOAT) ? CTYPE_FLOAT : CTYPE_INTEGER;
            break;
        }
        case BIN_MOD:
            emit(c, OP_MOD, make_flags(THINT_INTEGER, TMODE_PROPAGATE),
                 dest, left.slot, right.slot);
            r.type = CTYPE_INTEGER;
            break;

        case BIN_EQ:
            emit(c, OP_EQ, make_flags(THINT_BOOLEAN, TMODE_PROPAGATE),
                 dest, left.slot, right.slot);
            r.type = CTYPE_BOOLEAN;
            break;
        case BIN_NEQ:
            emit(c, OP_NEQ, make_flags(THINT_BOOLEAN, TMODE_PROPAGATE),
                 dest, left.slot, right.slot);
            r.type = CTYPE_BOOLEAN;
            break;
        case BIN_LT:
            emit(c, OP_LT, make_flags(THINT_BOOLEAN, TMODE_PROPAGATE),
                 dest, left.slot, right.slot);
            r.type = CTYPE_BOOLEAN;
            break;
        case BIN_GT:
            emit(c, OP_GT, make_flags(THINT_BOOLEAN, TMODE_PROPAGATE),
                 dest, left.slot, right.slot);
            r.type = CTYPE_BOOLEAN;
            break;
        case BIN_LTE:
            emit(c, OP_LTE, make_flags(THINT_BOOLEAN, TMODE_PROPAGATE),
                 dest, left.slot, right.slot);
            r.type = CTYPE_BOOLEAN;
            break;
        case BIN_GTE:
            emit(c, OP_GTE, make_flags(THINT_BOOLEAN, TMODE_PROPAGATE),
                 dest, left.slot, right.slot);
            r.type = CTYPE_BOOLEAN;
            break;

        case BIN_AND:
            emit(c, OP_AND, make_flags(THINT_BOOLEAN, TMODE_PROPAGATE),
                 dest, left.slot, right.slot);
            r.type = CTYPE_BOOLEAN;
            break;
        case BIN_OR:
            emit(c, OP_OR, make_flags(THINT_BOOLEAN, TMODE_PROPAGATE),
                 dest, left.slot, right.slot);
            r.type = CTYPE_BOOLEAN;
            break;
        }
        r.slot = dest;
        break;
    }

    case EXPR_UNARY_NEG: {
        CompileResult operand = compile_expr(c, e->as.unary.operand, -1);
        uint8_t hint = (operand.type == CTYPE_FLOAT) ? THINT_FLOAT : THINT_INTEGER;
        emit(c, OP_NEGATE, make_flags(hint, TMODE_PROPAGATE),
             dest, operand.slot, 0);
        r.slot = dest;
        r.type = operand.type;
        break;
    }

    case EXPR_NOT: {
        CompileResult operand = compile_expr(c, e->as.not_expr.operand, -1);
        emit(c, OP_NOT, make_flags(THINT_BOOLEAN, TMODE_PROPAGATE),
             dest, operand.slot, 0);
        r.slot = dest;
        r.type = CTYPE_BOOLEAN;
        break;
    }

    case EXPR_IF: {
        /* Collect all jump-to-end indices for patching */
        int end_jumps[64];
        int end_jump_count = 0;

        for (AstIfBranch *b = e->as.if_expr.branches; b; b = b->next) {
            if (b->condition) {
                /* if/else-if branch */
                CompileResult cond = compile_expr(c, b->condition, -1);
                int jmp_false = emit(c, OP_JUMP_IF_NOT, 0, 0, cond.slot, 0);

                CompileResult body = compile_expr(c, b->body, dest);
                r.type = body.type;

                if (b->next) {
                    int jmp_end = emit(c, OP_JUMP, 0, 0, 0, 0);
                    if (end_jump_count < 64) {
                        end_jumps[end_jump_count++] = jmp_end;
                    }
                }

                patch_jump(c, jmp_false, (uint16_t)c->instr_count);
            } else {
                /* else branch */
                CompileResult body = compile_expr(c, b->body, dest);
                r.type = body.type;
            }
        }

        /* Patch all end jumps */
        for (int i = 0; i < end_jump_count; i++) {
            patch_jump(c, end_jumps[i], (uint16_t)c->instr_count);
        }
        r.slot = dest;
        break;
    }

    case EXPR_MATCH: {
        /* Compile subject once */
        CompileResult subject = compile_expr(c, e->as.match.subject, -1);

        int end_jumps[64];
        int end_jump_count = 0;

        for (AstWhenClause *cl = e->as.match.clauses; cl; cl = cl->next) {
            int scope_mark = c->scope_count;

            /* Pattern test */
            uint16_t test_slot = scratch_alloc(c);
            if (cl->pattern) {
                compile_pattern_test(c, cl->pattern, subject.slot, test_slot);
            } else {
                /* No pattern = always true */
                uint16_t ci = add_const_boolean(c, 1);
                emit(c, OP_LOAD_CONST, make_flags(THINT_BOOLEAN, TMODE_PROPAGATE),
                     test_slot, ci, 0);
            }

            /* Bind pattern variables into scope (before guard, which may
             * reference them — e.g. `when n and n > 100`) */
            if (cl->pattern) {
                compile_pattern_bindings(c, cl->pattern, subject.slot);
            }

            /* Guard */
            if (cl->guard) {
                CompileResult guard = compile_expr(c, cl->guard, -1);
                emit(c, OP_AND, make_flags(THINT_BOOLEAN, TMODE_PROPAGATE),
                     test_slot, test_slot, guard.slot);
            }

            int jmp_next = emit(c, OP_JUMP_IF_NOT, 0, 0, test_slot, 0);

            /* Body */
            CompileResult body = compile_expr(c, cl->body, dest);
            r.type = body.type;

            int jmp_end = emit(c, OP_JUMP, 0, 0, 0, 0);
            if (end_jump_count < 64) {
                end_jumps[end_jump_count++] = jmp_end;
            }

            patch_jump(c, jmp_next, (uint16_t)c->instr_count);
            c->scope_count = scope_mark;
        }

        /* Else body */
        if (e->as.match.else_body) {
            CompileResult body = compile_expr(c, e->as.match.else_body, dest);
            r.type = body.type;
        } else {
            /* Default: load nothing */
            emit(c, OP_LOAD_NOTHING,
                 make_flags(THINT_NOTHING, TMODE_PROPAGATE), dest, 0, 0);
            r.type = CTYPE_NOTHING;
        }

        for (int i = 0; i < end_jump_count; i++) {
            patch_jump(c, end_jumps[i], (uint16_t)c->instr_count);
        }
        r.slot = dest;
        break;
    }

    case EXPR_LET: {
        /* Compile binding into a fresh slot */
        uint16_t bind_slot = scratch_alloc(c);
        CompileResult binding = compile_expr(c, e->as.let.binding, (int)bind_slot);

        /* Push scope */
        int scope_mark = c->scope_count;
        scope_push(c, e->as.let.name, bind_slot, binding.type);

        /* Compile body */
        CompileResult body = compile_expr(c, e->as.let.body, dest);
        r.slot = dest;
        r.type = body.type;

        c->scope_count = scope_mark;
        break;
    }

    case EXPR_RECORD: {
        emit(c, OP_RECORD_NEW, make_flags(THINT_RECORD, TMODE_PROPAGATE),
             dest, 0, 0);

        for (AstLabel *l = e->as.record.labels; l; l = l->next) {
            /* Load key string constant */
            int klen = (int)strlen(l->name);
            uint16_t key_ci = add_const_string(c, l->name, klen);
            uint16_t key_slot = scratch_alloc(c);
            emit(c, OP_LOAD_CONST, make_flags(THINT_STRING, TMODE_PROPAGATE),
                 key_slot, key_ci, 0);

            /* Compile value */
            CompileResult val = compile_expr(c, l->value, -1);

            /* Set field */
            emit(c, OP_RECORD_SET, make_flags(THINT_RECORD, TMODE_PROPAGATE),
                 dest, key_slot, val.slot);
        }
        r.slot = dest;
        r.type = CTYPE_RECORD;
        break;
    }

    case EXPR_LIST: {
        emit(c, OP_LIST_NEW, make_flags(THINT_LIST, TMODE_PROPAGATE),
             dest, 0, 0);

        for (AstExpr *el = e->as.list.elements; el; el = el->next) {
            CompileResult val = compile_expr(c, el, -1);
            emit(c, OP_LIST_APPEND, make_flags(THINT_LIST, TMODE_PROPAGATE),
                 dest, val.slot, 0);
        }
        r.slot = dest;
        r.type = CTYPE_LIST;
        break;
    }

    case EXPR_ACCESS: {
        CompileResult obj = compile_expr(c, e->as.access.object, -1);
        int flen = (int)strlen(e->as.access.field);
        uint16_t key_ci = add_const_string(c, e->as.access.field, flen);
        uint16_t key_slot = scratch_alloc(c);
        emit(c, OP_LOAD_CONST, make_flags(THINT_STRING, TMODE_PROPAGATE),
             key_slot, key_ci, 0);
        emit(c, OP_RECORD_GET, make_flags(THINT_UNSPECIFIED, TMODE_PROPAGATE),
             dest, obj.slot, key_slot);
        r.slot = dest;
        r.type = CTYPE_UNKNOWN;
        break;
    }

    case EXPR_PAREN: {
        CompileResult inner = compile_expr(c, e->as.paren.inner, dest);
        r = inner;
        r.slot = dest;
        break;
    }

    case EXPR_CALL: {
        const char *name = e->as.call.name;

        /* Conversion function opcodes: to-string, to-float, to-integer.
         * These take { value: X } and return { value: converted }.
         * We extract the value field, apply the opcode, wrap result. */
        if (strcmp(name, "to-string") == 0 ||
            strcmp(name, "to-float") == 0 ||
            strcmp(name, "to-integer") == 0) {
            /* Compile arg record */
            CompileResult arg = compile_expr(c, e->as.call.arg, -1);

            /* Extract "value" field from arg */
            uint16_t vkey_ci = add_const_string(c, "value", 5);
            uint16_t vkey_slot = scratch_alloc(c);
            emit(c, OP_LOAD_CONST, make_flags(THINT_STRING, TMODE_PROPAGATE),
                 vkey_slot, vkey_ci, 0);
            uint16_t val_slot = scratch_alloc(c);
            emit(c, OP_RECORD_GET, make_flags(THINT_UNSPECIFIED, TMODE_PROPAGATE),
                 val_slot, arg.slot, vkey_slot);

            /* Apply conversion opcode */
            uint16_t conv_slot = scratch_alloc(c);
            uint8_t conv_op, conv_hint;
            CompileType conv_type;
            if (strcmp(name, "to-string") == 0) {
                conv_op = OP_TO_STRING;
                conv_hint = THINT_STRING;
                conv_type = CTYPE_STRING;
            } else if (strcmp(name, "to-float") == 0) {
                conv_op = OP_TO_FLOAT;
                conv_hint = THINT_FLOAT;
                conv_type = CTYPE_FLOAT;
            } else {
                conv_op = OP_TO_INT;
                conv_hint = THINT_INTEGER;
                conv_type = CTYPE_INTEGER;
            }
            emit(c, conv_op, make_flags(conv_hint, TMODE_PROPAGATE),
                 conv_slot, val_slot, 0);

            /* Wrap in { value: result } record */
            emit(c, OP_RECORD_NEW, make_flags(THINT_RECORD, TMODE_PROPAGATE),
                 dest, 0, 0);
            emit(c, OP_RECORD_SET, make_flags(THINT_RECORD, TMODE_PROPAGATE),
                 dest, vkey_slot, conv_slot);

            r.slot = dest;
            r.type = CTYPE_RECORD;
            (void)conv_type;
            break;
        }

        /* fold: special compilation */
        if (strcmp(name, "fold") == 0 &&
            e->as.call.arg && e->as.call.arg->kind == EXPR_RECORD) {
            /* Extract args from record literal */
            const AstExpr *list_expr = NULL;
            const AstExpr *initial_expr = NULL;
            const char *fn_name = NULL;

            for (AstLabel *l = e->as.call.arg->as.record.labels; l; l = l->next) {
                if (strcmp(l->name, "list") == 0) list_expr = l->value;
                else if (strcmp(l->name, "initial") == 0) initial_expr = l->value;
                else if (strcmp(l->name, "fn") == 0 &&
                         l->value->kind == EXPR_STRING) {
                    fn_name = resolve_string(c->arena,
                                             l->value->as.string_val.value,
                                             l->value->as.string_val.length);
                }
            }

            if (list_expr && initial_expr && fn_name) {
                /* Compile initial value into accumulator slot */
                uint16_t acc_slot = scratch_alloc(c);
                compile_expr(c, initial_expr, (int)acc_slot);

                /* Compile list */
                CompileResult list_r = compile_expr(c, list_expr, -1);

                /* Element slot */
                uint16_t elem_slot = scratch_alloc(c);

                /* Determine body length and fold opcode */
                uint8_t fold_op = OP_ADD;
                CompileType fold_type = CTYPE_INTEGER;
                if (strcmp(fn_name, "multiply") == 0) {
                    fold_op = OP_MUL;
                } else if (strcmp(fn_name, "add") == 0) {
                    fold_op = OP_ADD;
                }

                /* ITER_SETUP: fold mode, sequential */
                uint8_t iter_flags = make_iter_flags(ITER_FOLD, ITER_SEQUENTIAL, 0);
                emit(c, OP_ITER_SETUP, iter_flags, elem_slot, list_r.slot, 1);

                /* Body: acc = acc op element */
                emit(c, fold_op,
                     make_flags(THINT_UNSPECIFIED, TMODE_PROPAGATE),
                     acc_slot, acc_slot, elem_slot);

                /* ITER_COLLECT */
                emit(c, OP_ITER_COLLECT,
                     make_flags(THINT_UNSPECIFIED, TMODE_PROPAGATE),
                     dest, acc_slot, 0);

                r.slot = dest;
                r.type = fold_type;
                break;
            }
            /* Fall through to generic call if fold args not recognized */
        }

        /* Generic stdlib call or sanitizer call */
        uint16_t func_id;
        uint8_t tag_mode = TMODE_PROPAGATE;

        /* Check if it's a sanitizer */
        const AstContract *ct = c->prog->contract;
        int is_sanitizer = 0;
        for (const AstSanitizerDef *s = ct->sanitizers; s; s = s->next) {
            if (strcmp(s->name, name) == 0) {
                is_sanitizer = 1;
                tag_mode = TMODE_CLEAR;
                break;
            }
        }

        if (is_sanitizer) {
            /* Sanitizers are implemented as stdlib calls; we need to find
             * which stdlib function implements the sanitizer. For now we
             * look it up by name in the stdlib table. */
            if (!stdlib_lookup(name, &func_id)) {
                /* Sanitizer name is not a stdlib function — it might be
                 * a uses function. Use func_id 0 as placeholder. */
                func_id = 0;
            }
        } else if (!stdlib_lookup(name, &func_id)) {
            /* Not a known stdlib function — could be a `uses` function.
             * Emit a placeholder. */
            func_id = 0;
        }

        if (func_id != 0) {
            track_stdlib(c, func_id);
        }

        CompileResult arg = compile_expr(c, e->as.call.arg, -1);
        emit(c, OP_STDLIB_CALL,
             make_flags(THINT_UNSPECIFIED, tag_mode),
             dest, func_id, arg.slot);
        r.slot = dest;
        r.type = CTYPE_UNKNOWN;
        break;
    }

    case EXPR_FILTER: {
        /* Compile list */
        CompileResult list_r = compile_expr(c, e->as.filter.list, -1);

        /* Element slot */
        uint16_t elem_slot = scratch_alloc(c);

        /* Push iteration variable into scope */
        int scope_mark = c->scope_count;
        scope_push(c, e->as.filter.var_name, elem_slot, CTYPE_UNKNOWN);

        /* Record instruction position before body to calculate body_length */
        int setup_idx = emit(c, OP_ITER_SETUP,
                             make_iter_flags(ITER_FILTER, ITER_INDEPENDENT, 0),
                             elem_slot, list_r.slot, 0);

        /* Predicate body */
        CompileResult pred = compile_expr(c, e->as.filter.predicate, -1);

        /* Backpatch body_length */
        int body_len = c->instr_count - setup_idx - 1;
        c->instructions[setup_idx].operand2 = (uint16_t)body_len;

        /* ITER_COLLECT: pred_slot, element_slot */
        emit(c, OP_ITER_COLLECT, make_flags(THINT_LIST, TMODE_PROPAGATE),
             dest, pred.slot, elem_slot);

        c->scope_count = scope_mark;
        r.slot = dest;
        r.type = CTYPE_LIST;
        break;
    }

    case EXPR_MAP: {
        /* Compile list */
        CompileResult list_r = compile_expr(c, e->as.map.list, -1);

        /* Element slot */
        uint16_t elem_slot = scratch_alloc(c);

        /* Push iteration variable into scope */
        int scope_mark = c->scope_count;
        scope_push(c, e->as.map.var_name, elem_slot, CTYPE_UNKNOWN);

        /* ITER_SETUP */
        int setup_idx = emit(c, OP_ITER_SETUP,
                             make_iter_flags(ITER_MAP, ITER_INDEPENDENT, 0),
                             elem_slot, list_r.slot, 0);

        /* Body */
        CompileResult body = compile_expr(c, e->as.map.body, -1);

        /* Backpatch body_length */
        int body_len = c->instr_count - setup_idx - 1;
        c->instructions[setup_idx].operand2 = (uint16_t)body_len;

        /* ITER_COLLECT: body_result_slot, 0 */
        emit(c, OP_ITER_COLLECT, make_flags(THINT_LIST, TMODE_PROPAGATE),
             dest, body.slot, 0);

        c->scope_count = scope_mark;
        r.slot = dest;
        r.type = CTYPE_LIST;
        break;
    }

    case EXPR_THROUGH: {
        /* Compile left side */
        CompileResult left = compile_expr(c, e->as.through.left, -1);

        /* Right side: handle call, filter, map */
        const AstExpr *right = e->as.through.right;

        if (right->kind == EXPR_CALL) {
            /* Compile the call's arg record, then inject left as "value" field */
            CompileResult arg = compile_expr(c, right->as.call.arg, -1);

            /* Load "value" key */
            uint16_t key_ci = add_const_string(c, "value", 5);
            uint16_t key_slot = scratch_alloc(c);
            emit(c, OP_LOAD_CONST, make_flags(THINT_STRING, TMODE_PROPAGATE),
                 key_slot, key_ci, 0);

            /* Set value field on the arg record */
            emit(c, OP_RECORD_SET, make_flags(THINT_RECORD, TMODE_PROPAGATE),
                 arg.slot, key_slot, left.slot);

            /* Now look up the function */
            const char *fn_name = right->as.call.name;

            /* Conversion functions: through pipeline injects "value" field,
             * then conversion returns { value: converted } record */
            if (strcmp(fn_name, "to-string") == 0 ||
                strcmp(fn_name, "to-float") == 0 ||
                strcmp(fn_name, "to-integer") == 0) {
                uint16_t conv_slot = scratch_alloc(c);
                uint8_t conv_op;
                if (strcmp(fn_name, "to-string") == 0) conv_op = OP_TO_STRING;
                else if (strcmp(fn_name, "to-float") == 0) conv_op = OP_TO_FLOAT;
                else conv_op = OP_TO_INT;

                emit(c, conv_op, make_flags(THINT_UNSPECIFIED, TMODE_PROPAGATE),
                     conv_slot, left.slot, 0);

                /* Wrap in { value: result } record */
                emit(c, OP_RECORD_NEW, make_flags(THINT_RECORD, TMODE_PROPAGATE),
                     dest, 0, 0);
                emit(c, OP_RECORD_SET, make_flags(THINT_RECORD, TMODE_PROPAGATE),
                     dest, key_slot, conv_slot);

                r.slot = dest;
                r.type = CTYPE_RECORD;
                break;
            }

            uint16_t func_id = 0;
            uint8_t tag_mode = TMODE_PROPAGATE;

            /* Check sanitizer */
            for (const AstSanitizerDef *s = c->prog->contract->sanitizers;
                 s; s = s->next) {
                if (strcmp(s->name, fn_name) == 0) {
                    tag_mode = TMODE_CLEAR;
                    break;
                }
            }

            if (stdlib_lookup(fn_name, &func_id)) {
                track_stdlib(c, func_id);
            }

            emit(c, OP_STDLIB_CALL,
                 make_flags(THINT_UNSPECIFIED, tag_mode),
                 dest, func_id, arg.slot);
            r.slot = dest;
            r.type = CTYPE_UNKNOWN;
        } else if (right->kind == EXPR_FILTER) {
            /* Use left slot as source list for filter */
            uint16_t elem_slot = scratch_alloc(c);
            int scope_mark = c->scope_count;
            scope_push(c, right->as.filter.var_name, elem_slot, CTYPE_UNKNOWN);

            int setup_idx = emit(c, OP_ITER_SETUP,
                                 make_iter_flags(ITER_FILTER, ITER_INDEPENDENT, 0),
                                 elem_slot, left.slot, 0);

            CompileResult pred = compile_expr(c, right->as.filter.predicate, -1);

            int body_len = c->instr_count - setup_idx - 1;
            c->instructions[setup_idx].operand2 = (uint16_t)body_len;

            emit(c, OP_ITER_COLLECT, make_flags(THINT_LIST, TMODE_PROPAGATE),
                 dest, pred.slot, elem_slot);

            c->scope_count = scope_mark;
            r.slot = dest;
            r.type = CTYPE_LIST;
        } else if (right->kind == EXPR_MAP) {
            /* Use left slot as source list for map */
            uint16_t elem_slot = scratch_alloc(c);
            int scope_mark = c->scope_count;
            scope_push(c, right->as.map.var_name, elem_slot, CTYPE_UNKNOWN);

            int setup_idx = emit(c, OP_ITER_SETUP,
                                 make_iter_flags(ITER_MAP, ITER_INDEPENDENT, 0),
                                 elem_slot, left.slot, 0);

            CompileResult body = compile_expr(c, right->as.map.body, -1);

            int body_len = c->instr_count - setup_idx - 1;
            c->instructions[setup_idx].operand2 = (uint16_t)body_len;

            emit(c, OP_ITER_COLLECT, make_flags(THINT_LIST, TMODE_PROPAGATE),
                 dest, body.slot, 0);

            c->scope_count = scope_mark;
            r.slot = dest;
            r.type = CTYPE_LIST;
        } else {
            /* Generic: just compile the right side normally */
            CompileResult right_r = compile_expr(c, right, dest);
            r = right_r;
        }
        break;
    }

    case EXPR_LOOKUP: {
        /* Phase 1: emit LOAD_NOTHING as placeholder */
        emit(c, OP_LOAD_NOTHING, make_flags(THINT_NOTHING, TMODE_PROPAGATE),
             dest, 0, 0);
        r.slot = dest;
        r.type = CTYPE_NOTHING;
        break;
    }

    } /* end switch */

    return r;
}

/* ── Pattern compilation ─────────────────────────────────── */

static void compile_pattern_test(Compiler *c, const AstPattern *p,
                                 uint16_t subject_slot, uint16_t result_slot) {
    if (!p) {
        uint16_t ci = add_const_boolean(c, 1);
        emit(c, OP_LOAD_CONST, make_flags(THINT_BOOLEAN, TMODE_PROPAGATE),
             result_slot, ci, 0);
        return;
    }

    switch (p->kind) {

    case PAT_LITERAL: {
        if (p->as.literal.value->kind == EXPR_NOTHING) {
            emit(c, OP_IS_NOTHING, make_flags(THINT_BOOLEAN, TMODE_PROPAGATE),
                 result_slot, subject_slot, 0);
        } else {
            CompileResult lit = compile_expr(c, p->as.literal.value, -1);
            emit(c, OP_EQ, make_flags(THINT_BOOLEAN, TMODE_PROPAGATE),
                 result_slot, subject_slot, lit.slot);
        }
        break;
    }

    case PAT_WILDCARD: {
        uint16_t ci = add_const_boolean(c, 1);
        emit(c, OP_LOAD_CONST, make_flags(THINT_BOOLEAN, TMODE_PROPAGATE),
             result_slot, ci, 0);
        break;
    }

    case PAT_BINDING: {
        /* Binding always matches */
        uint16_t ci = add_const_boolean(c, 1);
        emit(c, OP_LOAD_CONST, make_flags(THINT_BOOLEAN, TMODE_PROPAGATE),
             result_slot, ci, 0);
        break;
    }

    case PAT_RANGE: {
        /* Compile low and high */
        CompileResult low  = compile_expr(c, p->as.range.low, -1);
        CompileResult high = compile_expr(c, p->as.range.high, -1);

        /* subject >= low */
        uint16_t gte_slot = scratch_alloc(c);
        emit(c, OP_GTE, make_flags(THINT_BOOLEAN, TMODE_PROPAGATE),
             gte_slot, subject_slot, low.slot);

        /* subject <= high */
        uint16_t lte_slot = scratch_alloc(c);
        emit(c, OP_LTE, make_flags(THINT_BOOLEAN, TMODE_PROPAGATE),
             lte_slot, subject_slot, high.slot);

        /* gte AND lte */
        emit(c, OP_AND, make_flags(THINT_BOOLEAN, TMODE_PROPAGATE),
             result_slot, gte_slot, lte_slot);
        break;
    }

    case PAT_LIST: {
        /* IS_LIST check */
        uint16_t is_list_slot = scratch_alloc(c);
        emit(c, OP_IS_LIST, make_flags(THINT_BOOLEAN, TMODE_PROPAGATE),
             is_list_slot, subject_slot, 0);

        /* Length check */
        uint16_t len_slot = scratch_alloc(c);
        emit(c, OP_LIST_LENGTH, make_flags(THINT_INTEGER, TMODE_PROPAGATE),
             len_slot, subject_slot, 0);

        int elem_count = 0;
        for (AstPatternElem *el = p->as.list.elements; el; el = el->next) {
            elem_count++;
        }

        uint16_t expected_len_ci = add_const_integer(c, elem_count);
        uint16_t expected_len_slot = scratch_alloc(c);
        emit(c, OP_LOAD_CONST, make_flags(THINT_INTEGER, TMODE_PROPAGATE),
             expected_len_slot, expected_len_ci, 0);

        uint16_t len_ok_slot = scratch_alloc(c);
        if (p->as.list.rest_name) {
            /* With rest: length >= elem_count */
            emit(c, OP_GTE, make_flags(THINT_BOOLEAN, TMODE_PROPAGATE),
                 len_ok_slot, len_slot, expected_len_slot);
        } else {
            /* Exact: length == elem_count */
            emit(c, OP_EQ, make_flags(THINT_BOOLEAN, TMODE_PROPAGATE),
                 len_ok_slot, len_slot, expected_len_slot);
        }

        /* Combine is_list AND len_ok */
        emit(c, OP_AND, make_flags(THINT_BOOLEAN, TMODE_PROPAGATE),
             result_slot, is_list_slot, len_ok_slot);

        /* Per-element sub-pattern checks */
        int idx = 0;
        for (AstPatternElem *el = p->as.list.elements; el; el = el->next) {
            uint16_t idx_ci = add_const_integer(c, idx);
            uint16_t idx_slot = scratch_alloc(c);
            emit(c, OP_LOAD_CONST, make_flags(THINT_INTEGER, TMODE_PROPAGATE),
                 idx_slot, idx_ci, 0);

            uint16_t elem_slot = scratch_alloc(c);
            emit(c, OP_LIST_GET, make_flags(THINT_UNSPECIFIED, TMODE_PROPAGATE),
                 elem_slot, subject_slot, idx_slot);

            uint16_t sub_result = scratch_alloc(c);
            compile_pattern_test(c, el->pattern, elem_slot, sub_result);

            emit(c, OP_AND, make_flags(THINT_BOOLEAN, TMODE_PROPAGATE),
                 result_slot, result_slot, sub_result);
            idx++;
        }
        break;
    }

    case PAT_RECORD: {
        /* IS_RECORD check */
        uint16_t is_rec_slot = scratch_alloc(c);
        emit(c, OP_IS_RECORD, make_flags(THINT_BOOLEAN, TMODE_PROPAGATE),
             is_rec_slot, subject_slot, 0);

        /* Start with is_record as result */
        emit(c, OP_COPY, make_flags(THINT_BOOLEAN, TMODE_PROPAGATE),
             result_slot, is_rec_slot, 0);

        /* Per-field checks */
        for (AstFieldPattern *fp = p->as.record.fields; fp; fp = fp->next) {
            /* RECORD_HAS */
            int klen = (int)strlen(fp->name);
            uint16_t key_ci = add_const_string(c, fp->name, klen);
            uint16_t key_slot = scratch_alloc(c);
            emit(c, OP_LOAD_CONST, make_flags(THINT_STRING, TMODE_PROPAGATE),
                 key_slot, key_ci, 0);

            uint16_t has_slot = scratch_alloc(c);
            emit(c, OP_RECORD_HAS, make_flags(THINT_BOOLEAN, TMODE_PROPAGATE),
                 has_slot, subject_slot, key_slot);

            emit(c, OP_AND, make_flags(THINT_BOOLEAN, TMODE_PROPAGATE),
                 result_slot, result_slot, has_slot);

            /* RECORD_GET + sub-pattern */
            uint16_t field_slot = scratch_alloc(c);
            emit(c, OP_RECORD_GET, make_flags(THINT_UNSPECIFIED, TMODE_PROPAGATE),
                 field_slot, subject_slot, key_slot);

            uint16_t sub_result = scratch_alloc(c);
            compile_pattern_test(c, fp->pattern, field_slot, sub_result);

            emit(c, OP_AND, make_flags(THINT_BOOLEAN, TMODE_PROPAGATE),
                 result_slot, result_slot, sub_result);
        }
        break;
    }

    } /* end switch */
}

static void compile_pattern_bindings(Compiler *c, const AstPattern *p,
                                     uint16_t subject_slot) {
    if (!p) return;

    switch (p->kind) {
    case PAT_LITERAL:
    case PAT_WILDCARD:
    case PAT_RANGE:
        break;

    case PAT_BINDING:
        scope_push(c, p->as.binding.name, subject_slot, CTYPE_UNKNOWN);
        break;

    case PAT_LIST: {
        int idx = 0;
        for (AstPatternElem *el = p->as.list.elements; el; el = el->next) {
            uint16_t idx_ci = add_const_integer(c, idx);
            uint16_t idx_slot = scratch_alloc(c);
            emit(c, OP_LOAD_CONST, make_flags(THINT_INTEGER, TMODE_PROPAGATE),
                 idx_slot, idx_ci, 0);

            uint16_t elem_slot = scratch_alloc(c);
            emit(c, OP_LIST_GET, make_flags(THINT_UNSPECIFIED, TMODE_PROPAGATE),
                 elem_slot, subject_slot, idx_slot);

            compile_pattern_bindings(c, el->pattern, elem_slot);
            idx++;
        }
        break;
    }

    case PAT_RECORD:
        for (AstFieldPattern *fp = p->as.record.fields; fp; fp = fp->next) {
            int klen = (int)strlen(fp->name);
            uint16_t key_ci = add_const_string(c, fp->name, klen);
            uint16_t key_slot = scratch_alloc(c);
            emit(c, OP_LOAD_CONST, make_flags(THINT_STRING, TMODE_PROPAGATE),
                 key_slot, key_ci, 0);

            uint16_t field_slot = scratch_alloc(c);
            emit(c, OP_RECORD_GET, make_flags(THINT_UNSPECIFIED, TMODE_PROPAGATE),
                 field_slot, subject_slot, key_slot);

            compile_pattern_bindings(c, fp->pattern, field_slot);
        }
        break;
    }
}

/* ── Packet serialization helpers ────────────────────────── */

typedef struct {
    uint8_t *data;
    int      len;
    int      cap;
    Arena   *arena;
} PacketBuf;

static void buf_init(PacketBuf *b, Arena *arena) {
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
    b->arena = arena;
}

static void buf_grow(PacketBuf *b, int needed) {
    if (b->len + needed <= b->cap) return;
    int new_cap = b->cap ? b->cap * 2 : 4096;
    while (new_cap < b->len + needed) new_cap *= 2;
    uint8_t *new_data = arena_alloc(b->arena, (size_t)new_cap);
    if (b->len > 0) {
        memcpy(new_data, b->data, (size_t)b->len);
    }
    b->data = new_data;
    b->cap = new_cap;
}

static void buf_write_u8(PacketBuf *b, uint8_t v) {
    buf_grow(b, 1);
    b->data[b->len++] = v;
}

static void buf_write_u16(PacketBuf *b, uint16_t v) {
    buf_grow(b, 2);
    b->data[b->len++] = (uint8_t)(v & 0xFF);
    b->data[b->len++] = (uint8_t)((v >> 8) & 0xFF);
}

static void buf_write_u32(PacketBuf *b, uint32_t v) {
    buf_grow(b, 4);
    b->data[b->len++] = (uint8_t)(v & 0xFF);
    b->data[b->len++] = (uint8_t)((v >> 8) & 0xFF);
    b->data[b->len++] = (uint8_t)((v >> 16) & 0xFF);
    b->data[b->len++] = (uint8_t)((v >> 24) & 0xFF);
}

static void buf_write_u64(PacketBuf *b, uint64_t v) {
    buf_grow(b, 8);
    for (int i = 0; i < 8; i++) {
        b->data[b->len++] = (uint8_t)((v >> (i * 8)) & 0xFF);
    }
}

static void buf_write_bytes(PacketBuf *b, const void *data, int len) {
    buf_grow(b, len);
    memcpy(b->data + b->len, data, (size_t)len);
    b->len += len;
}

static void buf_write_zeros(PacketBuf *b, int count) {
    buf_grow(b, count);
    memset(b->data + b->len, 0, (size_t)count);
    b->len += count;
}

/* ── Serialize CONTRACT section ──────────────────────────── */

static void serialize_type(PacketBuf *b, const AstType *t) {
    if (!t) {
        buf_write_u8(b, FTYPE_STRING);
        return;
    }
    buf_write_u8(b, ast_type_to_ftype(t));
    switch (t->kind) {
    case TYPE_MAYBE:
        serialize_type(b, t->as.maybe.inner);
        break;
    case TYPE_LIST:
        serialize_type(b, t->as.list.inner);
        break;
    case TYPE_RECORD: {
        int count = 0;
        for (const AstFieldDecl *f = t->as.record.fields; f; f = f->next) {
            count++;
        }
        buf_write_u16(b, (uint16_t)count);
        for (const AstFieldDecl *f = t->as.record.fields; f; f = f->next) {
            int nlen = (int)strlen(f->name);
            buf_write_u16(b, (uint16_t)nlen);
            buf_write_bytes(b, f->name, nlen);
            serialize_type(b, f->type);
            buf_write_u64(b, 0); /* tag_bits */
            buf_write_u16(b, 0); /* scratchpad_offset placeholder */
        }
        break;
    }
    default:
        break;
    }
}

static void serialize_contract(PacketBuf *b, const Compiler *c) {
    const AstContract *ct = c->prog->contract;

    /* Contract name */
    int name_len = (int)strlen(ct->name);
    buf_write_u16(b, (uint16_t)name_len);
    buf_write_bytes(b, ct->name, name_len);

    /* Scratchpad size */
    buf_write_u16(b, (uint16_t)c->scratch_next);

    /* Count fields */
    int input_count = 0;
    for (const AstFieldDecl *f = ct->input; f; f = f->next) input_count++;
    int output_count = 0;
    for (const AstFieldDecl *f = ct->output; f; f = f->next) output_count++;
    int tag_count = 0;
    for (const AstTagDef *t = ct->tags; t; t = t->next) tag_count++;
    int sanitizer_count = 0;
    for (const AstSanitizerDef *s = ct->sanitizers; s; s = s->next) sanitizer_count++;
    int rule_count = 0;
    for (const AstRule *r = ct->rules; r; r = r->next) rule_count++;

    buf_write_u16(b, (uint16_t)input_count);
    buf_write_u16(b, (uint16_t)output_count);
    buf_write_u16(b, (uint16_t)tag_count);
    buf_write_u16(b, (uint16_t)sanitizer_count);
    buf_write_u16(b, (uint16_t)rule_count);

    /* Tag definitions */
    int bit_idx = 0;
    for (const AstTagDef *t = ct->tags; t; t = t->next) {
        buf_write_u8(b, (uint8_t)bit_idx);
        int tlen = (int)strlen(t->name);
        buf_write_u16(b, (uint16_t)tlen);
        buf_write_bytes(b, t->name, tlen);
        if (t->description) {
            int dlen = (int)strlen(t->description);
            buf_write_u16(b, (uint16_t)dlen);
            buf_write_bytes(b, t->description, dlen);
        } else {
            buf_write_u16(b, 0);
        }
        bit_idx++;
    }

    /* Input field declarations */
    int slot = 0;
    for (const AstFieldDecl *f = ct->input; f; f = f->next) {
        int flen = (int)strlen(f->name);
        buf_write_u16(b, (uint16_t)flen);
        buf_write_bytes(b, f->name, flen);
        serialize_type(b, f->type);
        buf_write_u64(b, compute_tag_bits(c, (const char **)f->tags, f->tag_count));
        buf_write_u16(b, (uint16_t)slot);
        slot++;
    }

    /* Output field declarations */
    for (const AstFieldDecl *f = ct->output; f; f = f->next) {
        int flen = (int)strlen(f->name);
        buf_write_u16(b, (uint16_t)flen);
        buf_write_bytes(b, f->name, flen);
        serialize_type(b, f->type);
        buf_write_u64(b, compute_tag_bits(c, (const char **)f->tags, f->tag_count));
        buf_write_u16(b, (uint16_t)slot);
        slot++;
    }

    /* Sanitizer declarations */
    for (const AstSanitizerDef *s = ct->sanitizers; s; s = s->next) {
        int slen = (int)strlen(s->name);
        buf_write_u16(b, (uint16_t)slen);
        buf_write_bytes(b, s->name, slen);

        uint16_t func_id = 0;
        stdlib_lookup(s->name, &func_id);
        buf_write_u16(b, func_id);

        uint64_t strips = compute_tag_bits(c, (const char **)s->stripped_tags,
                                           s->stripped_count);
        buf_write_u64(b, strips);
    }

    /* Rules — simplified encoding */
    for (const AstRule *r = ct->rules; r; r = r->next) {
        switch (r->kind) {
        case RULE_FORBID_FIELD:
            buf_write_u8(b, 0x01);
            if (r->as.forbid_field.field_ref &&
                r->as.forbid_field.field_ref->accessor_count > 0) {
                const char *fname = r->as.forbid_field.field_ref->accessors[0];
                int flen = (int)strlen(fname);
                buf_write_u16(b, (uint16_t)flen);
                buf_write_bytes(b, fname, flen);
                buf_write_u8(b, r->as.forbid_field.field_ref->is_output ? 1 : 0);
            } else {
                buf_write_u16(b, 0);
                buf_write_u8(b, 0);
            }
            break;

        case RULE_FORBID_TAGGED: {
            buf_write_u8(b, 0x02);
            int tidx = tag_bit_index(c, r->as.forbid_tagged.tag_name);
            uint64_t bits = (tidx >= 0) ? (1ULL << tidx) : 0;
            buf_write_u64(b, bits);
            buf_write_u8(b, 1); /* output scope */
            break;
        }

        case RULE_REQUIRE:
            buf_write_u8(b, 0x03);
            if (r->as.require.field_ref &&
                r->as.require.field_ref->accessor_count > 0) {
                const char *fname = r->as.require.field_ref->accessors[0];
                int flen = (int)strlen(fname);
                buf_write_u16(b, (uint16_t)flen);
                buf_write_bytes(b, fname, flen);
            } else {
                buf_write_u16(b, 0);
            }
            /* Simplified: just store reject message */
            if (r->as.require.reject_msg) {
                int mlen = (int)strlen(r->as.require.reject_msg);
                buf_write_u16(b, (uint16_t)mlen);
                buf_write_bytes(b, r->as.require.reject_msg, mlen);
            } else {
                buf_write_u16(b, 0);
            }
            break;

        case RULE_MATCH:
            buf_write_u8(b, 0x04);
            if (r->as.match.field_ref &&
                r->as.match.field_ref->accessor_count > 0) {
                const char *fname = r->as.match.field_ref->accessors[0];
                int flen = (int)strlen(fname);
                buf_write_u16(b, (uint16_t)flen);
                buf_write_bytes(b, fname, flen);
            } else {
                buf_write_u16(b, 0);
            }
            if (r->as.match.reject_msg) {
                int mlen = (int)strlen(r->as.match.reject_msg);
                buf_write_u16(b, (uint16_t)mlen);
                buf_write_bytes(b, r->as.match.reject_msg, mlen);
            } else {
                buf_write_u16(b, 0);
            }
            break;
        }
    }
}

/* ── Serialize CONSTANTS section ─────────────────────────── */

static void serialize_constants(PacketBuf *b, const Compiler *c) {
    for (int i = 0; i < c->const_count; i++) {
        buf_write_u8(b, c->const_types[i]);
        buf_write_u32(b, (uint32_t)c->const_lengths[i]);
        buf_write_bytes(b, c->const_data + c->const_offsets[i],
                        c->const_lengths[i]);
    }
}

/* ── Serialize STDLIB_DEPS section ───────────────────────── */

static void serialize_stdlib_deps(PacketBuf *b, const Compiler *c) {
    buf_write_u16(b, (uint16_t)c->stdlib_dep_count);
    for (int i = 0; i < c->stdlib_dep_count; i++) {
        buf_write_u16(b, c->stdlib_deps[i]);
    }
}

/* ── Serialize BYTECODE section ──────────────────────────── */

static void serialize_bytecode(PacketBuf *b, const Compiler *c) {
    for (int i = 0; i < c->instr_count; i++) {
        const Instruction *ins = &c->instructions[i];
        buf_write_u8(b, ins->opcode);
        buf_write_u8(b, ins->flags);
        buf_write_u16(b, ins->dest);
        buf_write_u16(b, ins->operand1);
        buf_write_u16(b, ins->operand2);
    }
}

/* ── Assemble final packet ───────────────────────────────── */

static PacketResult assemble_packet(Compiler *c) {
    PacketResult result = { NULL, 0 };

    /* Serialize each section into temporary buffers */
    PacketBuf contract_buf;
    buf_init(&contract_buf, c->arena);
    serialize_contract(&contract_buf, c);

    PacketBuf constants_buf;
    buf_init(&constants_buf, c->arena);
    serialize_constants(&constants_buf, c);

    PacketBuf stdlib_buf;
    buf_init(&stdlib_buf, c->arena);
    serialize_stdlib_deps(&stdlib_buf, c);

    PacketBuf bytecode_buf;
    buf_init(&bytecode_buf, c->arena);
    serialize_bytecode(&bytecode_buf, c);

    /* Calculate sizes */
    int section_count = 4; /* CONTRACT, CONSTANTS, STDLIB_DEPS, BYTECODE */
    int dir_size = section_count * SECTION_DIR_ENTRY;
    int sections_size = contract_buf.len + constants_buf.len +
                        stdlib_buf.len + bytecode_buf.len;
    int total_size = PACKET_HEADER_SIZE + dir_size + sections_size;

    /* Allocate final buffer */
    PacketBuf pkt;
    buf_init(&pkt, c->arena);

    /* ── Header (88 bytes) ── */
    buf_write_u32(&pkt, PACKET_MAGIC);           /* magic */
    buf_write_u16(&pkt, PACKET_FORMAT_VER);       /* format_version */
    buf_write_u16(&pkt, PACKET_MIN_SPEC_VER);     /* min_spec_version */
    buf_write_u32(&pkt, (uint32_t)total_size);    /* total_size */
    buf_write_u16(&pkt, (uint16_t)section_count); /* section_count */
    buf_write_u16(&pkt, 0);                       /* flags */
    buf_write_u64(&pkt, 0);                       /* key_fingerprint */
    buf_write_zeros(&pkt, 64);                    /* signature */

    /* ── Section directory ── */
    int offset = PACKET_HEADER_SIZE + dir_size;

    /* CONTRACT */
    buf_write_u16(&pkt, SEC_CONTRACT);
    buf_write_u32(&pkt, (uint32_t)offset);
    buf_write_u32(&pkt, (uint32_t)contract_buf.len);
    offset += contract_buf.len;

    /* CONSTANTS */
    buf_write_u16(&pkt, SEC_CONSTANTS);
    buf_write_u32(&pkt, (uint32_t)offset);
    buf_write_u32(&pkt, (uint32_t)constants_buf.len);
    offset += constants_buf.len;

    /* STDLIB_DEPS */
    buf_write_u16(&pkt, SEC_STDLIB_DEPS);
    buf_write_u32(&pkt, (uint32_t)offset);
    buf_write_u32(&pkt, (uint32_t)stdlib_buf.len);
    offset += stdlib_buf.len;

    /* BYTECODE */
    buf_write_u16(&pkt, SEC_BYTECODE);
    buf_write_u32(&pkt, (uint32_t)offset);
    buf_write_u32(&pkt, (uint32_t)bytecode_buf.len);

    /* ── Section data ── */
    buf_write_bytes(&pkt, contract_buf.data, contract_buf.len);
    buf_write_bytes(&pkt, constants_buf.data, constants_buf.len);
    buf_write_bytes(&pkt, stdlib_buf.data, stdlib_buf.len);
    buf_write_bytes(&pkt, bytecode_buf.data, bytecode_buf.len);

    result.data = pkt.data;
    result.size = (size_t)pkt.len;
    return result;
}

/* ── Public API ──────────────────────────────────────────── */

void compiler_init(Compiler *c, const AstProgram *prog, Arena *arena) {
    memset(c, 0, sizeof(*c));
    c->prog  = prog;
    c->arena = arena;
    c->errors.arena = arena;
}

PacketResult compiler_compile(Compiler *c) {
    PacketResult empty = { NULL, 0 };
    const AstContract *ct = c->prog->contract;

    /* Only function contracts can be compiled */
    if (ct->kind != CONTRACT_FUNCTION) {
        add_error(c, HELUNA_ERR_CONTRACT,
                  ct->loc, "only function contracts can be compiled");
        return empty;
    }

    /* Build tag map */
    int tag_count = 0;
    for (const AstTagDef *t = ct->tags; t; t = t->next) tag_count++;
    if (tag_count > 0) {
        c->tag_map = arena_alloc(c->arena,
                                 (size_t)tag_count * sizeof(*c->tag_map));
        int idx = 0;
        for (const AstTagDef *t = ct->tags; t; t = t->next) {
            c->tag_map[idx].name = t->name;
            c->tag_map[idx].bit_index = idx;
            idx++;
        }
        c->tag_map_count = tag_count;
    }

    /* Assign input field slots */
    for (const AstFieldDecl *f = ct->input; f; f = f->next) {
        uint16_t slot = scratch_alloc(c);
        CompileType ctype = ast_type_to_ctype(f->type);
        scope_push(c, f->name, slot, ctype);
        c->input_count++;
    }

    /* Assign output field slots */
    for (const AstFieldDecl *f = ct->output; f; f = f->next) {
        scratch_alloc(c);
        c->output_count++;
    }

    /* Compile function body */
    if (!c->prog->function || !c->prog->function->body) {
        add_error(c, HELUNA_ERR_CONTRACT, ct->loc, "no function body to compile");
        return empty;
    }

    /* The function body should be a chain of let bindings ending in
     * a result record. The result record should have its labels
     * assigned to output slots. */
    compile_expr(c, c->prog->function->body, -1);

    if (c->errors.count > 0) {
        return empty;
    }

    return assemble_packet(c);
}

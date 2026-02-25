#include "heluna/vm.h"
#include "heluna/json.h"
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

/* ── Binary reading helpers ──────────────────────────────── */

typedef struct {
    const uint8_t *data;
    size_t         size;
    size_t         pos;
} Cursor;

static int cursor_check(Cursor *c, size_t needed) {
    return c->pos + needed <= c->size;
}

static uint8_t read_u8(Cursor *c) {
    if (!cursor_check(c, 1)) return 0;
    return c->data[c->pos++];
}

static uint16_t read_u16_le(Cursor *c) {
    if (!cursor_check(c, 2)) return 0;
    uint16_t v = (uint16_t)c->data[c->pos] |
                 ((uint16_t)c->data[c->pos + 1] << 8);
    c->pos += 2;
    return v;
}

static uint32_t read_u32_le(Cursor *c) {
    if (!cursor_check(c, 4)) return 0;
    uint32_t v = (uint32_t)c->data[c->pos] |
                 ((uint32_t)c->data[c->pos + 1] << 8) |
                 ((uint32_t)c->data[c->pos + 2] << 16) |
                 ((uint32_t)c->data[c->pos + 3] << 24);
    c->pos += 4;
    return v;
}

static uint64_t read_u64_le(Cursor *c) {
    if (!cursor_check(c, 8)) return 0;
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) {
        v |= (uint64_t)c->data[c->pos + i] << (i * 8);
    }
    c->pos += 8;
    return v;
}

/* ── Packet loading ──────────────────────────────────────── */

/* Section directory entry */
typedef struct {
    uint16_t type;
    uint32_t offset;
    uint32_t length;
} SectionEntry;

/* Skip a type encoding in the contract section (recursive for compound types) */
static void skip_type(Cursor *c) {
    if (!cursor_check(c, 1)) return;
    uint8_t tid = read_u8(c);
    if (tid == FTYPE_MAYBE || tid == FTYPE_LIST) {
        skip_type(c);
    } else if (tid == FTYPE_RECORD) {
        uint16_t count = read_u16_le(c);
        for (int i = 0; i < count; i++) {
            uint16_t nlen = read_u16_le(c);
            c->pos += nlen;      /* name */
            skip_type(c);         /* nested type */
            c->pos += 8;         /* tag_bits */
            c->pos += 2;         /* scratchpad_offset */
        }
    }
}

VmPacket *vm_load_packet(const uint8_t *data, size_t size,
                         Arena *arena, HelunaError *err) {
    if (size < PACKET_HEADER_SIZE) {
        heluna_error_set(err, HELUNA_ERR_IO, (SrcLoc){0},
                         "packet too small (%zu bytes)", size);
        return NULL;
    }

    Cursor hdr = { data, size, 0 };

    /* Header */
    uint32_t magic = read_u32_le(&hdr);
    if (magic != PACKET_MAGIC) {
        heluna_error_set(err, HELUNA_ERR_IO, (SrcLoc){0},
                         "bad magic: 0x%08X (expected 0x%08X)",
                         magic, PACKET_MAGIC);
        return NULL;
    }

    uint16_t format_ver = read_u16_le(&hdr);
    (void)format_ver;
    /*uint16_t min_spec =*/ read_u16_le(&hdr);
    uint32_t total_size = read_u32_le(&hdr);
    if (total_size > size) {
        heluna_error_set(err, HELUNA_ERR_IO, (SrcLoc){0},
                         "total_size %u exceeds data size %zu",
                         total_size, size);
        return NULL;
    }

    uint16_t section_count = read_u16_le(&hdr);
    /*uint16_t flags =*/ read_u16_le(&hdr);
    /* key_fingerprint (8 bytes) + signature (64 bytes) = 72 bytes */
    hdr.pos += 72;

    /* Section directory */
    SectionEntry *sections = arena_alloc(arena,
        (size_t)section_count * sizeof(SectionEntry));

    for (int i = 0; i < section_count; i++) {
        sections[i].type   = read_u16_le(&hdr);
        sections[i].offset = read_u32_le(&hdr);
        sections[i].length = read_u32_le(&hdr);
    }

    /* Find required sections */
    int found[4] = {0, 0, 0, 0};
    SectionEntry sec_contract = {0, 0, 0};
    SectionEntry sec_constants = {0, 0, 0};
    SectionEntry sec_stdlib = {0, 0, 0};
    SectionEntry sec_bytecode = {0, 0, 0};
    SectionEntry sec_sources = {0, 0, 0};
    int found_sources = 0;

    for (int i = 0; i < section_count; i++) {
        switch (sections[i].type) {
        case SEC_CONTRACT:    sec_contract  = sections[i]; found[0] = 1; break;
        case SEC_CONSTANTS:   sec_constants = sections[i]; found[1] = 1; break;
        case SEC_STDLIB_DEPS: sec_stdlib    = sections[i]; found[2] = 1; break;
        case SEC_BYTECODE:    sec_bytecode  = sections[i]; found[3] = 1; break;
        case SEC_SOURCES:     sec_sources   = sections[i]; found_sources = 1; break;
        }
    }

    for (int i = 0; i < 4; i++) {
        if (!found[i]) {
            const char *names[] = {"CONTRACT","CONSTANTS","STDLIB_DEPS","BYTECODE"};
            heluna_error_set(err, HELUNA_ERR_IO, (SrcLoc){0},
                             "missing required section: %s", names[i]);
            return NULL;
        }
    }

    VmPacket *pkt = arena_calloc(arena, sizeof(VmPacket));

    /* ── Parse CONTRACT section ── */
    {
        Cursor c = { data, size, sec_contract.offset };

        /* Contract name */
        uint16_t name_len = read_u16_le(&c);
        pkt->name = arena_strndup(arena, (const char *)c.data + c.pos, name_len);
        c.pos += name_len;

        /* Scratchpad size */
        pkt->scratchpad_size = read_u16_le(&c);

        /* Counts */
        pkt->input_count     = read_u16_le(&c);
        pkt->output_count    = read_u16_le(&c);
        pkt->tag_count       = read_u16_le(&c);
        pkt->sanitizer_count = read_u16_le(&c);
        pkt->rule_count      = read_u16_le(&c);

        /* Tag definitions */
        if (pkt->tag_count > 0) {
            pkt->tag_defs = arena_alloc(arena,
                (size_t)pkt->tag_count * sizeof(VmTagDef));
            for (int i = 0; i < pkt->tag_count; i++) {
                pkt->tag_defs[i].bit_index = read_u8(&c);
                uint16_t tlen = read_u16_le(&c);
                pkt->tag_defs[i].name = arena_strndup(arena,
                    (const char *)c.data + c.pos, tlen);
                c.pos += tlen;
                uint16_t dlen = read_u16_le(&c);
                if (dlen > 0) {
                    pkt->tag_defs[i].description = arena_strndup(arena,
                        (const char *)c.data + c.pos, dlen);
                    c.pos += dlen;
                } else {
                    pkt->tag_defs[i].description = "";
                }
            }
        }

        /* Input field declarations */
        if (pkt->input_count > 0) {
            pkt->input_fields = arena_alloc(arena,
                (size_t)pkt->input_count * sizeof(VmFieldDecl));
            for (int i = 0; i < pkt->input_count; i++) {
                uint16_t flen = read_u16_le(&c);
                pkt->input_fields[i].name = arena_strndup(arena,
                    (const char *)c.data + c.pos, flen);
                c.pos += flen;
                pkt->input_fields[i].type_id = read_u8(&c);
                /* Skip compound type details */
                if (pkt->input_fields[i].type_id == FTYPE_MAYBE ||
                    pkt->input_fields[i].type_id == FTYPE_LIST) {
                    skip_type(&c);
                } else if (pkt->input_fields[i].type_id == FTYPE_RECORD) {
                    /* Skip the record sub-fields */
                    uint16_t count = read_u16_le(&c);
                    for (int j = 0; j < count; j++) {
                        uint16_t nlen = read_u16_le(&c);
                        c.pos += nlen;
                        skip_type(&c);
                        c.pos += 8 + 2;
                    }
                }
                pkt->input_fields[i].tag_bits = read_u64_le(&c);
                pkt->input_fields[i].scratchpad_offset = read_u16_le(&c);
            }
        }

        /* Output field declarations */
        if (pkt->output_count > 0) {
            pkt->output_fields = arena_alloc(arena,
                (size_t)pkt->output_count * sizeof(VmFieldDecl));
            for (int i = 0; i < pkt->output_count; i++) {
                uint16_t flen = read_u16_le(&c);
                pkt->output_fields[i].name = arena_strndup(arena,
                    (const char *)c.data + c.pos, flen);
                c.pos += flen;
                pkt->output_fields[i].type_id = read_u8(&c);
                if (pkt->output_fields[i].type_id == FTYPE_MAYBE ||
                    pkt->output_fields[i].type_id == FTYPE_LIST) {
                    skip_type(&c);
                } else if (pkt->output_fields[i].type_id == FTYPE_RECORD) {
                    uint16_t count = read_u16_le(&c);
                    for (int j = 0; j < count; j++) {
                        uint16_t nlen = read_u16_le(&c);
                        c.pos += nlen;
                        skip_type(&c);
                        c.pos += 8 + 2;
                    }
                }
                pkt->output_fields[i].tag_bits = read_u64_le(&c);
                pkt->output_fields[i].scratchpad_offset = read_u16_le(&c);
            }
        }

        /* Sanitizer declarations */
        if (pkt->sanitizer_count > 0) {
            pkt->sanitizers = arena_alloc(arena,
                (size_t)pkt->sanitizer_count * sizeof(VmSanitizer));
            for (int i = 0; i < pkt->sanitizer_count; i++) {
                uint16_t slen = read_u16_le(&c);
                pkt->sanitizers[i].name = arena_strndup(arena,
                    (const char *)c.data + c.pos, slen);
                c.pos += slen;
                pkt->sanitizers[i].func_id = read_u16_le(&c);
                pkt->sanitizers[i].strips_tags = read_u64_le(&c);
            }
        }

        /* Rules */
        if (pkt->rule_count > 0) {
            pkt->rules = arena_alloc(arena,
                (size_t)pkt->rule_count * sizeof(VmRule));
            for (int i = 0; i < pkt->rule_count; i++) {
                uint8_t kind = read_u8(&c);
                pkt->rules[i].kind = (VmRuleKind)kind;

                switch (kind) {
                case 0x01: { /* FORBID_FIELD */
                    uint16_t flen = read_u16_le(&c);
                    if (flen > 0) {
                        pkt->rules[i].as.forbid_field.field_name =
                            arena_strndup(arena, (const char *)c.data + c.pos, flen);
                        c.pos += flen;
                    } else {
                        pkt->rules[i].as.forbid_field.field_name = "";
                    }
                    pkt->rules[i].as.forbid_field.is_output = read_u8(&c);
                    break;
                }
                case 0x02: { /* FORBID_TAGGED */
                    pkt->rules[i].as.forbid_tagged.tag_bits = read_u64_le(&c);
                    pkt->rules[i].as.forbid_tagged.is_output = read_u8(&c);
                    break;
                }
                case 0x03: { /* REQUIRE */
                    uint16_t flen = read_u16_le(&c);
                    if (flen > 0) {
                        pkt->rules[i].as.require.field_name =
                            arena_strndup(arena, (const char *)c.data + c.pos, flen);
                        c.pos += flen;
                    } else {
                        pkt->rules[i].as.require.field_name = "";
                    }
                    uint16_t mlen = read_u16_le(&c);
                    if (mlen > 0) {
                        pkt->rules[i].as.require.reject_msg =
                            arena_strndup(arena, (const char *)c.data + c.pos, mlen);
                        c.pos += mlen;
                    } else {
                        pkt->rules[i].as.require.reject_msg = "";
                    }
                    break;
                }
                case 0x04: { /* MATCH */
                    uint16_t flen = read_u16_le(&c);
                    if (flen > 0) {
                        pkt->rules[i].as.match.field_name =
                            arena_strndup(arena, (const char *)c.data + c.pos, flen);
                        c.pos += flen;
                    } else {
                        pkt->rules[i].as.match.field_name = "";
                    }
                    uint16_t mlen = read_u16_le(&c);
                    if (mlen > 0) {
                        pkt->rules[i].as.match.reject_msg =
                            arena_strndup(arena, (const char *)c.data + c.pos, mlen);
                        c.pos += mlen;
                    } else {
                        pkt->rules[i].as.match.reject_msg = "";
                    }
                    break;
                }
                default:
                    break;
                }
            }
        }
    }

    /* ── Parse CONSTANTS section ── */
    {
        Cursor c = { data, size, sec_constants.offset };
        size_t end = sec_constants.offset + sec_constants.length;

        /* First pass: count constants */
        int count = 0;
        size_t save_pos = c.pos;
        while (c.pos < end && cursor_check(&c, 5)) {
            /*uint8_t tid =*/ read_u8(&c);
            uint32_t dlen = read_u32_le(&c);
            c.pos += dlen;
            count++;
        }

        pkt->constant_count = count;
        if (count > 0) {
            pkt->constants = arena_alloc(arena,
                (size_t)count * sizeof(VmConstant));
        }

        /* Second pass: materialize */
        c.pos = save_pos;
        for (int i = 0; i < count; i++) {
            uint8_t tid = read_u8(&c);
            uint32_t dlen = read_u32_le(&c);
            pkt->constants[i].type_id = tid;

            switch (tid) {
            case FTYPE_STRING:
                pkt->constants[i].value = make_string(arena,
                    arena_strndup(arena, (const char *)c.data + c.pos, dlen));
                break;
            case FTYPE_INTEGER: {
                int64_t val = 0;
                if (dlen >= 8) {
                    for (int b = 0; b < 8; b++) {
                        val |= (int64_t)c.data[c.pos + b] << (b * 8);
                    }
                }
                pkt->constants[i].value = make_integer(arena, (long long)val);
                break;
            }
            case FTYPE_FLOAT: {
                double val = 0;
                if (dlen >= 8) {
                    uint64_t bits = 0;
                    for (int b = 0; b < 8; b++) {
                        bits |= (uint64_t)c.data[c.pos + b] << (b * 8);
                    }
                    memcpy(&val, &bits, 8);
                }
                pkt->constants[i].value = make_float(arena, val);
                break;
            }
            case FTYPE_BOOLEAN:
                pkt->constants[i].value = make_boolean(arena,
                    dlen > 0 ? c.data[c.pos] : 0);
                break;
            case FTYPE_NOTHING:
                pkt->constants[i].value = make_nothing(arena);
                break;
            default:
                pkt->constants[i].value = make_nothing(arena);
                break;
            }
            c.pos += dlen;
        }
    }

    /* ── Parse STDLIB_DEPS section ── */
    {
        Cursor c = { data, size, sec_stdlib.offset };
        uint16_t count = read_u16_le(&c);
        pkt->stdlib_dep_count = count;
        if (count > 0) {
            pkt->stdlib_deps = arena_alloc(arena,
                (size_t)count * sizeof(uint16_t));
            for (int i = 0; i < count; i++) {
                pkt->stdlib_deps[i] = read_u16_le(&c);
            }
        }
    }

    /* ── Parse BYTECODE section ── */
    {
        Cursor c = { data, size, sec_bytecode.offset };
        int count = (int)(sec_bytecode.length / INSTRUCTION_SIZE);
        pkt->instr_count = count;
        if (count > 0) {
            pkt->instructions = arena_alloc(arena,
                (size_t)count * sizeof(Instruction));
            for (int i = 0; i < count; i++) {
                pkt->instructions[i].opcode   = read_u8(&c);
                pkt->instructions[i].flags    = read_u8(&c);
                pkt->instructions[i].dest     = read_u16_le(&c);
                pkt->instructions[i].operand1 = read_u16_le(&c);
                pkt->instructions[i].operand2 = read_u16_le(&c);
            }
        }
    }

    /* ── Parse SOURCES section (optional) ── */
    if (found_sources) {
        Cursor c = { data, size, sec_sources.offset };
        uint16_t count = read_u16_le(&c);
        pkt->source_count = count;
        if (count > 0) {
            pkt->sources = arena_alloc(arena,
                (size_t)count * sizeof(VmSource));
            for (int i = 0; i < count; i++) {
                /* name */
                uint16_t nlen = read_u16_le(&c);
                pkt->sources[i].name = arena_strndup(arena,
                    (const char *)c.data + c.pos, nlen);
                c.pos += nlen;
                /* config_json */
                uint16_t clen = read_u16_le(&c);
                if (clen > 0) {
                    pkt->sources[i].config_json = arena_strndup(arena,
                        (const char *)c.data + c.pos, clen);
                    c.pos += clen;
                } else {
                    pkt->sources[i].config_json = NULL;
                }
                /* keyed_by */
                uint16_t klen = read_u16_le(&c);
                if (klen > 0) {
                    pkt->sources[i].keyed_by = arena_strndup(arena,
                        (const char *)c.data + c.pos, klen);
                    c.pos += klen;
                } else {
                    pkt->sources[i].keyed_by = NULL;
                }
            }
        }
    }

    return pkt;
}

/* ── VM initialization ───────────────────────────────────── */

void vm_init(Vm *vm, VmPacket *packet, Arena *arena) {
    memset(vm, 0, sizeof(*vm));
    vm->packet = packet;
    vm->arena  = arena;
    if (packet->source_count > 0) {
        vm->source_cache = arena_calloc(arena,
            (size_t)packet->source_count * sizeof(HVal *));
    }
}

/* ── Helpers ─────────────────────────────────────────────── */

static void vm_error(Vm *vm, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(vm->error.message, sizeof(vm->error.message), fmt, ap);
    va_end(ap);
    vm->error.kind = HELUNA_ERR_RUNTIME;
    vm->error.loc  = (SrcLoc){0};
    vm->had_error  = 1;
}

static HField *record_get_field(HVal *rec, const char *name) {
    if (!rec || rec->kind != VAL_RECORD) return NULL;
    for (HField *f = rec->as.record_fields; f; f = f->next) {
        if (strcmp(f->name, name) == 0) return f;
    }
    return NULL;
}

/* Count list elements */
static int list_length(HVal *list) {
    int n = 0;
    for (HVal *v = list->as.list_head; v; v = v->next) n++;
    return n;
}

/* Get numeric value as double */
static double to_double(HVal *v) {
    if (v->kind == VAL_FLOAT) return v->as.float_val;
    if (v->kind == VAL_INTEGER) return (double)v->as.integer_val;
    return 0.0;
}

/* Tag propagation helper */
static void propagate_tags(Vm *vm, uint16_t dest,
                           const Instruction *instr, uint64_t operand_tags) {
    int tmode = (instr->flags >> 3) & 0x03;
    switch (tmode) {
    case TMODE_PROPAGATE:
        vm->scratchpad[dest].tags |= operand_tags;
        break;
    case TMODE_CLEAR:
        vm->scratchpad[dest].tags = 0;
        break;
    case TMODE_SET:
        /* Tag bits come from operand2 as constant index */
        if (instr->operand2 < (uint16_t)vm->packet->constant_count) {
            HVal *cv = vm->packet->constants[instr->operand2].value;
            if (cv && cv->kind == VAL_INTEGER) {
                vm->scratchpad[dest].tags = (uint64_t)cv->as.integer_val;
            }
        }
        break;
    }
}

/* Ensure slot_tails array is allocated (lazy init) */
static inline void ensure_slot_tails(Vm *vm, int sp_size) {
    if (!vm->slot_tails) {
        vm->slot_tails = arena_calloc(vm->arena,
            (size_t)sp_size * sizeof(VmSlotTail));
    }
}

/* Convert HVal to string representation (for STR_CONCAT auto-convert) */
static const char *val_to_str(Arena *arena, HVal *v) {
    if (!v) return "nothing";
    switch (v->kind) {
    case VAL_STRING:  return v->as.string_val;
    case VAL_INTEGER: {
        char buf[64];
        snprintf(buf, sizeof buf, "%lld", v->as.integer_val);
        return arena_strdup(arena, buf);
    }
    case VAL_FLOAT: {
        char buf[64];
        snprintf(buf, sizeof buf, "%g", v->as.float_val);
        /* Ensure float strings have a decimal point (e.g. "5" → "5.0") */
        if (!strchr(buf, '.') && !strchr(buf, 'e') && !strchr(buf, 'E')) {
            size_t len = strlen(buf);
            if (len + 2 < sizeof buf) {
                buf[len] = '.';
                buf[len + 1] = '0';
                buf[len + 2] = '\0';
            }
        }
        return arena_strdup(arena, buf);
    }
    case VAL_BOOLEAN: return v->as.boolean_val ? "true" : "false";
    case VAL_NOTHING: return "nothing";
    default: return "<value>";
    }
}

/* ── Execution engine ────────────────────────────────────── */

HVal *vm_execute(Vm *vm, HVal *input) {
    VmPacket *pkt = vm->packet;
    Arena *arena = vm->arena;

    /* Allocate scratchpad */
    int sp_size = pkt->scratchpad_size;
    if (sp_size < pkt->input_count + pkt->output_count) {
        sp_size = pkt->input_count + pkt->output_count;
    }
    /* Ensure minimum scratchpad size */
    if (sp_size < 1) sp_size = 1;

    vm->scratchpad = arena_calloc(arena, (size_t)sp_size * sizeof(VmSlot));
    vm->slot_tails  = NULL;  /* allocated lazily on first list/record construction */

    /* Intern singleton values */
    vm->val_true    = make_boolean(arena, 1);
    vm->val_false   = make_boolean(arena, 0);
    vm->val_nothing = make_nothing(arena);
    vm->iter_stack  = NULL;
    vm->iter_depth  = 0;

    /* Map input fields to input slots */
    if (input && input->kind == VAL_RECORD) {
        for (int i = 0; i < pkt->input_count; i++) {
            HField *f = record_get_field(input, pkt->input_fields[i].name);
            if (f) {
                vm->scratchpad[i].value = f->value;
            } else {
                vm->scratchpad[i].value = vm->val_nothing;
            }
            vm->scratchpad[i].tags = pkt->input_fields[i].tag_bits;
        }
    }

    /* Execute instruction loop */
    Instruction *instrs = pkt->instructions;
    int ic = pkt->instr_count;
    int pc = 0;
    uint16_t result_slot = 0;

    while (pc < ic) {
        const Instruction *ins = &instrs[pc];
        uint16_t d  = ins->dest;
        uint16_t o1 = ins->operand1;
        uint16_t o2 = ins->operand2;

#ifdef HELUNA_DEBUG
        /* Bounds check dest slot (except for jumps) */
        if (ins->opcode != OP_JUMP && ins->opcode != OP_JUMP_IF &&
            ins->opcode != OP_JUMP_IF_NOT && d >= sp_size) {
            vm_error(vm, "dest slot %u out of range (scratchpad size %d)",
                     d, sp_size);
            return NULL;
        }
#endif

        switch (ins->opcode) {

        /* ── Load/copy ── */

        case OP_LOAD_CONST:
            if (o1 >= (uint16_t)pkt->constant_count) {
                vm_error(vm, "constant index %u out of range", o1);
                return NULL;
            }
            vm->scratchpad[d].value = pkt->constants[o1].value;
            vm->scratchpad[d].tags  = 0;
            result_slot = d;
            break;

        case OP_LOAD_FIELD:
            vm->scratchpad[d].value = vm->scratchpad[o1].value;
            vm->scratchpad[d].tags  = vm->scratchpad[o1].tags;
            result_slot = d;
            break;

        case OP_LOAD_NOTHING:
            vm->scratchpad[d].value = vm->val_nothing;
            vm->scratchpad[d].tags  = 0;
            result_slot = d;
            break;

        case OP_COPY:
            vm->scratchpad[d].value = vm->scratchpad[o1].value;
            vm->scratchpad[d].tags  = vm->scratchpad[o1].tags;
            result_slot = d;
            break;

        /* ── Arithmetic ── */

        case OP_ADD: {
            HVal *a = vm->scratchpad[o1].value;
            HVal *b = vm->scratchpad[o2].value;
            if (!a || !b) { vm_error(vm, "ADD: null operand"); return NULL; }
            uint64_t tags = vm->scratchpad[o1].tags | vm->scratchpad[o2].tags;

            if (a->kind == VAL_INTEGER && b->kind == VAL_INTEGER) {
                vm->scratchpad[d].value = make_integer(arena,
                    a->as.integer_val + b->as.integer_val);
            } else if ((a->kind == VAL_INTEGER || a->kind == VAL_FLOAT) &&
                       (b->kind == VAL_INTEGER || b->kind == VAL_FLOAT)) {
                vm->scratchpad[d].value = make_float(arena,
                    to_double(a) + to_double(b));
            } else {
                vm_error(vm, "ADD: non-numeric operands");
                return NULL;
            }
            vm->scratchpad[d].tags = 0;
            propagate_tags(vm, d, ins, tags);
            result_slot = d;
            break;
        }

        case OP_SUB: {
            HVal *a = vm->scratchpad[o1].value;
            HVal *b = vm->scratchpad[o2].value;
            if (!a || !b) { vm_error(vm, "SUB: null operand"); return NULL; }
            uint64_t tags = vm->scratchpad[o1].tags | vm->scratchpad[o2].tags;

            if (a->kind == VAL_INTEGER && b->kind == VAL_INTEGER) {
                vm->scratchpad[d].value = make_integer(arena,
                    a->as.integer_val - b->as.integer_val);
            } else if ((a->kind == VAL_INTEGER || a->kind == VAL_FLOAT) &&
                       (b->kind == VAL_INTEGER || b->kind == VAL_FLOAT)) {
                vm->scratchpad[d].value = make_float(arena,
                    to_double(a) - to_double(b));
            } else {
                vm_error(vm, "SUB: non-numeric operands");
                return NULL;
            }
            vm->scratchpad[d].tags = 0;
            propagate_tags(vm, d, ins, tags);
            result_slot = d;
            break;
        }

        case OP_MUL: {
            HVal *a = vm->scratchpad[o1].value;
            HVal *b = vm->scratchpad[o2].value;
            if (!a || !b) { vm_error(vm, "MUL: null operand"); return NULL; }
            uint64_t tags = vm->scratchpad[o1].tags | vm->scratchpad[o2].tags;

            if (a->kind == VAL_INTEGER && b->kind == VAL_INTEGER) {
                vm->scratchpad[d].value = make_integer(arena,
                    a->as.integer_val * b->as.integer_val);
            } else if ((a->kind == VAL_INTEGER || a->kind == VAL_FLOAT) &&
                       (b->kind == VAL_INTEGER || b->kind == VAL_FLOAT)) {
                vm->scratchpad[d].value = make_float(arena,
                    to_double(a) * to_double(b));
            } else {
                vm_error(vm, "MUL: non-numeric operands");
                return NULL;
            }
            vm->scratchpad[d].tags = 0;
            propagate_tags(vm, d, ins, tags);
            result_slot = d;
            break;
        }

        case OP_DIV: {
            HVal *a = vm->scratchpad[o1].value;
            HVal *b = vm->scratchpad[o2].value;
            if (!a || !b) { vm_error(vm, "DIV: null operand"); return NULL; }
            uint64_t tags = vm->scratchpad[o1].tags | vm->scratchpad[o2].tags;

            if (b->kind == VAL_INTEGER && b->as.integer_val == 0) {
                vm_error(vm, "DIV: division by zero");
                return NULL;
            }
            if (b->kind == VAL_FLOAT && b->as.float_val == 0.0) {
                vm_error(vm, "DIV: division by zero");
                return NULL;
            }

            if (a->kind == VAL_INTEGER && b->kind == VAL_INTEGER) {
                vm->scratchpad[d].value = make_integer(arena,
                    a->as.integer_val / b->as.integer_val);
            } else if ((a->kind == VAL_INTEGER || a->kind == VAL_FLOAT) &&
                       (b->kind == VAL_INTEGER || b->kind == VAL_FLOAT)) {
                vm->scratchpad[d].value = make_float(arena,
                    to_double(a) / to_double(b));
            } else {
                vm_error(vm, "DIV: non-numeric operands");
                return NULL;
            }
            vm->scratchpad[d].tags = 0;
            propagate_tags(vm, d, ins, tags);
            result_slot = d;
            break;
        }

        case OP_MOD: {
            HVal *a = vm->scratchpad[o1].value;
            HVal *b = vm->scratchpad[o2].value;
            if (!a || !b) { vm_error(vm, "MOD: null operand"); return NULL; }
            uint64_t tags = vm->scratchpad[o1].tags | vm->scratchpad[o2].tags;

            if (a->kind == VAL_INTEGER && b->kind == VAL_INTEGER) {
                if (b->as.integer_val == 0) {
                    vm_error(vm, "MOD: division by zero");
                    return NULL;
                }
                vm->scratchpad[d].value = make_integer(arena,
                    a->as.integer_val % b->as.integer_val);
            } else if ((a->kind == VAL_INTEGER || a->kind == VAL_FLOAT) &&
                       (b->kind == VAL_INTEGER || b->kind == VAL_FLOAT)) {
                vm->scratchpad[d].value = make_float(arena,
                    fmod(to_double(a), to_double(b)));
            } else {
                vm_error(vm, "MOD: non-numeric operands");
                return NULL;
            }
            vm->scratchpad[d].tags = 0;
            propagate_tags(vm, d, ins, tags);
            result_slot = d;
            break;
        }

        case OP_NEGATE: {
            HVal *a = vm->scratchpad[o1].value;
            if (!a) { vm_error(vm, "NEGATE: null operand"); return NULL; }
            if (a->kind == VAL_INTEGER) {
                vm->scratchpad[d].value = make_integer(arena, -a->as.integer_val);
            } else if (a->kind == VAL_FLOAT) {
                vm->scratchpad[d].value = make_float(arena, -a->as.float_val);
            } else {
                vm_error(vm, "NEGATE: non-numeric operand");
                return NULL;
            }
            vm->scratchpad[d].tags = vm->scratchpad[o1].tags;
            result_slot = d;
            break;
        }

        /* ── Comparison ── */

        case OP_EQ: {
            HVal *a = vm->scratchpad[o1].value;
            HVal *b = vm->scratchpad[o2].value;
            vm->scratchpad[d].value = hval_equal(a, b) ? vm->val_true : vm->val_false;
            vm->scratchpad[d].tags = 0;
            result_slot = d;
            break;
        }

        case OP_NEQ: {
            HVal *a = vm->scratchpad[o1].value;
            HVal *b = vm->scratchpad[o2].value;
            vm->scratchpad[d].value = !hval_equal(a, b) ? vm->val_true : vm->val_false;
            vm->scratchpad[d].tags = 0;
            result_slot = d;
            break;
        }

        case OP_LT: {
            HVal *a = vm->scratchpad[o1].value;
            HVal *b = vm->scratchpad[o2].value;
            if (!a || !b) { vm_error(vm, "LT: null operand"); return NULL; }

            if ((a->kind == VAL_INTEGER || a->kind == VAL_FLOAT) &&
                (b->kind == VAL_INTEGER || b->kind == VAL_FLOAT)) {
                vm->scratchpad[d].value = to_double(a) < to_double(b)
                    ? vm->val_true : vm->val_false;
            } else if (a->kind == VAL_STRING && b->kind == VAL_STRING) {
                vm->scratchpad[d].value = strcmp(a->as.string_val, b->as.string_val) < 0
                    ? vm->val_true : vm->val_false;
            } else {
                vm_error(vm, "LT: incompatible types");
                return NULL;
            }
            vm->scratchpad[d].tags = 0;
            result_slot = d;
            break;
        }

        case OP_GT: {
            HVal *a = vm->scratchpad[o1].value;
            HVal *b = vm->scratchpad[o2].value;
            if (!a || !b) { vm_error(vm, "GT: null operand"); return NULL; }

            if ((a->kind == VAL_INTEGER || a->kind == VAL_FLOAT) &&
                (b->kind == VAL_INTEGER || b->kind == VAL_FLOAT)) {
                vm->scratchpad[d].value = to_double(a) > to_double(b)
                    ? vm->val_true : vm->val_false;
            } else if (a->kind == VAL_STRING && b->kind == VAL_STRING) {
                vm->scratchpad[d].value = strcmp(a->as.string_val, b->as.string_val) > 0
                    ? vm->val_true : vm->val_false;
            } else {
                vm_error(vm, "GT: incompatible types");
                return NULL;
            }
            vm->scratchpad[d].tags = 0;
            result_slot = d;
            break;
        }

        case OP_LTE: {
            HVal *a = vm->scratchpad[o1].value;
            HVal *b = vm->scratchpad[o2].value;
            if (!a || !b) { vm_error(vm, "LTE: null operand"); return NULL; }

            if ((a->kind == VAL_INTEGER || a->kind == VAL_FLOAT) &&
                (b->kind == VAL_INTEGER || b->kind == VAL_FLOAT)) {
                vm->scratchpad[d].value = to_double(a) <= to_double(b)
                    ? vm->val_true : vm->val_false;
            } else if (a->kind == VAL_STRING && b->kind == VAL_STRING) {
                vm->scratchpad[d].value = strcmp(a->as.string_val, b->as.string_val) <= 0
                    ? vm->val_true : vm->val_false;
            } else {
                vm_error(vm, "LTE: incompatible types");
                return NULL;
            }
            vm->scratchpad[d].tags = 0;
            result_slot = d;
            break;
        }

        case OP_GTE: {
            HVal *a = vm->scratchpad[o1].value;
            HVal *b = vm->scratchpad[o2].value;
            if (!a || !b) { vm_error(vm, "GTE: null operand"); return NULL; }

            if ((a->kind == VAL_INTEGER || a->kind == VAL_FLOAT) &&
                (b->kind == VAL_INTEGER || b->kind == VAL_FLOAT)) {
                vm->scratchpad[d].value = to_double(a) >= to_double(b)
                    ? vm->val_true : vm->val_false;
            } else if (a->kind == VAL_STRING && b->kind == VAL_STRING) {
                vm->scratchpad[d].value = strcmp(a->as.string_val, b->as.string_val) >= 0
                    ? vm->val_true : vm->val_false;
            } else {
                vm_error(vm, "GTE: incompatible types");
                return NULL;
            }
            vm->scratchpad[d].tags = 0;
            result_slot = d;
            break;
        }

        /* ── Boolean logic ── */

        case OP_AND: {
            HVal *a = vm->scratchpad[o1].value;
            HVal *b = vm->scratchpad[o2].value;
            if (!a || !b || a->kind != VAL_BOOLEAN || b->kind != VAL_BOOLEAN) {
                vm_error(vm, "AND: non-boolean operands");
                return NULL;
            }
            vm->scratchpad[d].value = (a->as.boolean_val && b->as.boolean_val)
                ? vm->val_true : vm->val_false;
            vm->scratchpad[d].tags = 0;
            result_slot = d;
            break;
        }

        case OP_OR: {
            HVal *a = vm->scratchpad[o1].value;
            HVal *b = vm->scratchpad[o2].value;
            if (!a || !b || a->kind != VAL_BOOLEAN || b->kind != VAL_BOOLEAN) {
                vm_error(vm, "OR: non-boolean operands");
                return NULL;
            }
            vm->scratchpad[d].value = (a->as.boolean_val || b->as.boolean_val)
                ? vm->val_true : vm->val_false;
            vm->scratchpad[d].tags = 0;
            result_slot = d;
            break;
        }

        case OP_NOT: {
            HVal *a = vm->scratchpad[o1].value;
            if (!a || a->kind != VAL_BOOLEAN) {
                vm_error(vm, "NOT: non-boolean operand");
                return NULL;
            }
            vm->scratchpad[d].value = !a->as.boolean_val
                ? vm->val_true : vm->val_false;
            vm->scratchpad[d].tags = 0;
            result_slot = d;
            break;
        }

        /* ── String ── */

        case OP_STR_CONCAT: {
            HVal *a = vm->scratchpad[o1].value;
            HVal *b = vm->scratchpad[o2].value;
            uint64_t tags = vm->scratchpad[o1].tags | vm->scratchpad[o2].tags;

            const char *sa = val_to_str(arena, a);
            const char *sb = val_to_str(arena, b);
            size_t la = strlen(sa), lb = strlen(sb);
            char *buf = arena_alloc(arena, la + lb + 1);
            memcpy(buf, sa, la);
            memcpy(buf + la, sb, lb);
            buf[la + lb] = '\0';

            vm->scratchpad[d].value = make_string(arena, buf);
            vm->scratchpad[d].tags = 0;
            propagate_tags(vm, d, ins, tags);
            result_slot = d;
            break;
        }

        /* ── Type testing ── */

        case OP_IS_STRING: {
            HVal *a = vm->scratchpad[o1].value;
            vm->scratchpad[d].value = (a && a->kind == VAL_STRING)
                ? vm->val_true : vm->val_false;
            vm->scratchpad[d].tags = 0;
            result_slot = d;
            break;
        }

        case OP_IS_INT: {
            HVal *a = vm->scratchpad[o1].value;
            vm->scratchpad[d].value = (a && a->kind == VAL_INTEGER)
                ? vm->val_true : vm->val_false;
            vm->scratchpad[d].tags = 0;
            result_slot = d;
            break;
        }

        case OP_IS_FLOAT: {
            HVal *a = vm->scratchpad[o1].value;
            vm->scratchpad[d].value = (a && a->kind == VAL_FLOAT)
                ? vm->val_true : vm->val_false;
            vm->scratchpad[d].tags = 0;
            result_slot = d;
            break;
        }

        case OP_IS_BOOL: {
            HVal *a = vm->scratchpad[o1].value;
            vm->scratchpad[d].value = (a && a->kind == VAL_BOOLEAN)
                ? vm->val_true : vm->val_false;
            vm->scratchpad[d].tags = 0;
            result_slot = d;
            break;
        }

        case OP_IS_NOTHING: {
            HVal *a = vm->scratchpad[o1].value;
            vm->scratchpad[d].value = (!a || a->kind == VAL_NOTHING)
                ? vm->val_true : vm->val_false;
            vm->scratchpad[d].tags = 0;
            result_slot = d;
            break;
        }

        case OP_IS_LIST: {
            HVal *a = vm->scratchpad[o1].value;
            vm->scratchpad[d].value = (a && a->kind == VAL_LIST)
                ? vm->val_true : vm->val_false;
            vm->scratchpad[d].tags = 0;
            result_slot = d;
            break;
        }

        case OP_IS_RECORD: {
            HVal *a = vm->scratchpad[o1].value;
            vm->scratchpad[d].value = (a && a->kind == VAL_RECORD)
                ? vm->val_true : vm->val_false;
            vm->scratchpad[d].tags = 0;
            result_slot = d;
            break;
        }

        /* ── Type conversion ── */

        case OP_TO_STRING: {
            HVal *a = vm->scratchpad[o1].value;
            vm->scratchpad[d].value = make_string(arena, val_to_str(arena, a));
            vm->scratchpad[d].tags = vm->scratchpad[o1].tags;
            result_slot = d;
            break;
        }

        case OP_TO_INT: {
            HVal *a = vm->scratchpad[o1].value;
            if (!a) { vm_error(vm, "TO_INT: null operand"); return NULL; }
            long long n = 0;
            switch (a->kind) {
            case VAL_INTEGER: n = a->as.integer_val; break;
            case VAL_FLOAT:   n = (long long)a->as.float_val; break;
            case VAL_STRING:  n = strtoll(a->as.string_val, NULL, 10); break;
            case VAL_BOOLEAN: n = a->as.boolean_val; break;
            default:
                vm_error(vm, "TO_INT: cannot convert"); return NULL;
            }
            vm->scratchpad[d].value = make_integer(arena, n);
            vm->scratchpad[d].tags = vm->scratchpad[o1].tags;
            result_slot = d;
            break;
        }

        case OP_TO_FLOAT: {
            HVal *a = vm->scratchpad[o1].value;
            if (!a) { vm_error(vm, "TO_FLOAT: null operand"); return NULL; }
            double f = 0;
            switch (a->kind) {
            case VAL_INTEGER: f = (double)a->as.integer_val; break;
            case VAL_FLOAT:   f = a->as.float_val; break;
            case VAL_STRING:  f = strtod(a->as.string_val, NULL); break;
            default:
                vm_error(vm, "TO_FLOAT: cannot convert"); return NULL;
            }
            vm->scratchpad[d].value = make_float(arena, f);
            vm->scratchpad[d].tags = vm->scratchpad[o1].tags;
            result_slot = d;
            break;
        }

        case OP_TO_BOOL: {
            HVal *a = vm->scratchpad[o1].value;
            int b = 0;
            if (a) {
                switch (a->kind) {
                case VAL_INTEGER: b = a->as.integer_val != 0; break;
                case VAL_FLOAT:   b = a->as.float_val != 0.0; break;
                case VAL_STRING:  b = a->as.string_val[0] != '\0'; break;
                case VAL_BOOLEAN: b = a->as.boolean_val; break;
                case VAL_NOTHING: b = 0; break;
                default: b = 1; break;
                }
            }
            vm->scratchpad[d].value = b ? vm->val_true : vm->val_false;
            vm->scratchpad[d].tags = 0;
            result_slot = d;
            break;
        }

        /* ── Record operations ── */

        case OP_RECORD_NEW:
            vm->scratchpad[d].value = make_record(arena, NULL);
            vm->scratchpad[d].tags = 0;
            ensure_slot_tails(vm, sp_size);
            vm->slot_tails[d].record_tail = NULL;
            result_slot = d;
            break;

        case OP_RECORD_SET: {
            HVal *rec = vm->scratchpad[d].value;
            HVal *key_val = vm->scratchpad[o1].value;
            HVal *val = vm->scratchpad[o2].value;

            if (!rec || rec->kind != VAL_RECORD) {
                vm_error(vm, "RECORD_SET: dest is not a record");
                return NULL;
            }
            if (!key_val || key_val->kind != VAL_STRING) {
                vm_error(vm, "RECORD_SET: key is not a string");
                return NULL;
            }

            /* Check if field already exists */
            const char *key = key_val->as.string_val;
            HField *existing = NULL;
            for (HField *f = rec->as.record_fields; f; f = f->next) {
                if (strcmp(f->name, key) == 0) {
                    existing = f;
                    break;
                }
            }

            if (existing) {
                existing->value = val;
            } else {
                HField *nf = arena_calloc(arena, sizeof(HField));
                nf->name  = key;
                nf->value = val;
                /* Append via tail pointer for O(1) */
                ensure_slot_tails(vm, sp_size);
                if (vm->slot_tails[d].record_tail) {
                    vm->slot_tails[d].record_tail->next = nf;
                } else if (!rec->as.record_fields) {
                    rec->as.record_fields = nf;
                } else {
                    /* Loaded from elsewhere — traverse once to find tail */
                    HField *last = rec->as.record_fields;
                    while (last->next) last = last->next;
                    last->next = nf;
                }
                vm->slot_tails[d].record_tail = nf;
            }

            /* Propagate tags from value to record */
            vm->scratchpad[d].tags |= vm->scratchpad[o2].tags;
            result_slot = d;
            break;
        }

        case OP_RECORD_GET: {
            HVal *rec = vm->scratchpad[o1].value;
            HVal *key_val = vm->scratchpad[o2].value;

            if (!rec || rec->kind != VAL_RECORD || !key_val ||
                key_val->kind != VAL_STRING) {
                vm->scratchpad[d].value = vm->val_nothing;
                vm->scratchpad[d].tags = 0;
            } else {
                HField *f = record_get_field(rec, key_val->as.string_val);
                if (f) {
                    vm->scratchpad[d].value = f->value;
                    vm->scratchpad[d].tags = vm->scratchpad[o1].tags;
                } else {
                    vm->scratchpad[d].value = vm->val_nothing;
                    vm->scratchpad[d].tags = 0;
                }
            }
            result_slot = d;
            break;
        }

        case OP_RECORD_HAS: {
            HVal *rec = vm->scratchpad[o1].value;
            HVal *key_val = vm->scratchpad[o2].value;

            int has = 0;
            if (rec && rec->kind == VAL_RECORD && key_val &&
                key_val->kind == VAL_STRING) {
                has = record_get_field(rec, key_val->as.string_val) != NULL;
            }
            vm->scratchpad[d].value = has ? vm->val_true : vm->val_false;
            vm->scratchpad[d].tags = 0;
            result_slot = d;
            break;
        }

        /* ── List operations ── */

        case OP_LIST_NEW:
            vm->scratchpad[d].value = make_list(arena, NULL);
            vm->scratchpad[d].tags = 0;
            ensure_slot_tails(vm, sp_size);
            vm->slot_tails[d].list_tail = NULL;
            result_slot = d;
            break;

        case OP_LIST_APPEND: {
            HVal *list = vm->scratchpad[d].value;
            HVal *val = vm->scratchpad[o1].value;

            if (!list || list->kind != VAL_LIST) {
                vm_error(vm, "LIST_APPEND: dest is not a list");
                return NULL;
            }

            /* Copy value for list membership */
            HVal *item = arena_calloc(arena, sizeof(HVal));
            if (val) {
                *item = *val;
            } else {
                item->kind = VAL_NOTHING;
            }
            item->next = NULL;

            /* Append via tail pointer for O(1) */
            ensure_slot_tails(vm, sp_size);
            if (vm->slot_tails[d].list_tail) {
                vm->slot_tails[d].list_tail->next = item;
            } else if (!list->as.list_head) {
                list->as.list_head = item;
            } else {
                /* Loaded from elsewhere — traverse once to find tail */
                HVal *last = list->as.list_head;
                while (last->next) last = last->next;
                last->next = item;
            }
            vm->slot_tails[d].list_tail = item;

            vm->scratchpad[d].tags |= vm->scratchpad[o1].tags;
            result_slot = d;
            break;
        }

        case OP_LIST_GET: {
            HVal *list = vm->scratchpad[o1].value;
            HVal *idx_val = vm->scratchpad[o2].value;

            if (!list || list->kind != VAL_LIST || !idx_val ||
                idx_val->kind != VAL_INTEGER) {
                vm_error(vm, "LIST_GET: invalid operands");
                return NULL;
            }

            int idx = (int)idx_val->as.integer_val;
            HVal *elem = list->as.list_head;
            for (int i = 0; i < idx && elem; i++) elem = elem->next;

            if (!elem) {
                /* Out-of-bounds access returns nothing (matches Heluna's
                   maybe/nothing philosophy; needed for list pattern matching) */
                vm->scratchpad[d].value = vm->val_nothing;
                vm->scratchpad[d].tags = 0;
                result_slot = d;
                break;
            }

            vm->scratchpad[d].value = elem;
            vm->scratchpad[d].tags = vm->scratchpad[o1].tags;
            result_slot = d;
            break;
        }

        case OP_LIST_LENGTH: {
            HVal *list = vm->scratchpad[o1].value;
            int len = 0;
            if (list && list->kind == VAL_LIST) {
                len = list_length(list);
            }
            vm->scratchpad[d].value = make_integer(arena, len);
            vm->scratchpad[d].tags = 0;
            result_slot = d;
            break;
        }

        /* ── Control flow ── */

        case OP_JUMP:
            pc = d;
            continue;

        case OP_JUMP_IF: {
            HVal *cond = vm->scratchpad[o1].value;
            if (cond && cond->kind == VAL_BOOLEAN && cond->as.boolean_val) {
                pc = d;
                continue;
            }
            break;
        }

        case OP_JUMP_IF_NOT: {
            HVal *cond = vm->scratchpad[o1].value;
            if (!cond || cond->kind != VAL_BOOLEAN || !cond->as.boolean_val) {
                pc = d;
                continue;
            }
            break;
        }

        /* ── Nothing handling ── */

        case OP_COALESCE: {
            HVal *a = vm->scratchpad[o1].value;
            if (!a || a->kind == VAL_NOTHING) {
                vm->scratchpad[d].value = vm->scratchpad[o2].value;
                vm->scratchpad[d].tags  = vm->scratchpad[o2].tags;
            } else {
                vm->scratchpad[d].value = a;
                vm->scratchpad[d].tags  = vm->scratchpad[o1].tags;
            }
            result_slot = d;
            break;
        }

        /* ── Iteration ── */

        case OP_ITER_SETUP: {
            int mode = ins->flags & 0x03;
            uint16_t elem_slot   = d;  /* dest = element slot */
            uint16_t source_slot = o1; /* operand1 = source list slot */
            uint16_t body_length = o2; /* operand2 = body instruction count */

            HVal *source = vm->scratchpad[source_slot].value;
            if (!source || source->kind != VAL_LIST) {
                vm_error(vm, "ITER_SETUP: source is not a list");
                return NULL;
            }

            int body_start = pc + 1;

            /* Find ITER_COLLECT after body */
            int collect_pc_val = body_start + body_length;
            if (collect_pc_val >= ic ||
                instrs[collect_pc_val].opcode != OP_ITER_COLLECT) {
                vm_error(vm, "ITER_SETUP: missing ITER_COLLECT");
                return NULL;
            }

            const Instruction *collect = &instrs[collect_pc_val];

            /* Empty list: store empty result, skip to after ITER_COLLECT */
            if (!source->as.list_head) {
                uint16_t result_dest = collect->dest;
                if (mode == ITER_FOLD) {
                    vm->scratchpad[result_dest].value =
                        vm->scratchpad[collect->operand1].value;
                } else {
                    vm->scratchpad[result_dest].value = make_list(arena, NULL);
                }
                vm->scratchpad[result_dest].tags = 0;
                result_slot = result_dest;
                pc = collect_pc_val + 1;
                continue;
            }

            /* Push iteration frame (allocate stack lazily) */
            if (!vm->iter_stack) {
                vm->iter_stack = arena_calloc(arena,
                    VM_MAX_ITER_DEPTH * sizeof(VmIterFrame));
            }
            if (vm->iter_depth >= VM_MAX_ITER_DEPTH) {
                vm_error(vm, "ITER_SETUP: iteration nesting too deep");
                return NULL;
            }
            VmIterFrame *frame = &vm->iter_stack[vm->iter_depth++];
            frame->mode        = mode;
            frame->next_elem   = source->as.list_head->next;
            frame->elem_slot   = elem_slot;
            frame->source_slot = source_slot;
            frame->body_start  = body_start;
            frame->collect_pc  = collect_pc_val;
            frame->result_dest = collect->dest;
            frame->slot_a      = collect->operand1;
            frame->slot_b      = collect->operand2;
            frame->result_head = NULL;
            frame->result_tail = &frame->result_head;
            frame->acc         = (mode == ITER_FOLD)
                                 ? vm->scratchpad[collect->operand1].value
                                 : NULL;

            /* Place first element and jump to body start */
            vm->scratchpad[elem_slot].value = source->as.list_head;
            vm->scratchpad[elem_slot].tags  = vm->scratchpad[source_slot].tags;

            pc = body_start;
            continue;
        }

        case OP_ITER_COLLECT: {
            if (vm->iter_depth <= 0) {
                /* Not inside iteration — no-op (shouldn't happen) */
                break;
            }
            VmIterFrame *frame = &vm->iter_stack[vm->iter_depth - 1];

            /* Collect current element's result */
            switch (frame->mode) {
            case ITER_MAP: {
                HVal *item = arena_calloc(arena, sizeof(HVal));
                HVal *src = vm->scratchpad[frame->slot_a].value;
                if (src) *item = *src; else item->kind = VAL_NOTHING;
                item->next = NULL;
                *frame->result_tail = item;
                frame->result_tail = &item->next;
                break;
            }
            case ITER_FILTER: {
                HVal *cond = vm->scratchpad[frame->slot_a].value;
                if (cond && cond->kind == VAL_BOOLEAN && cond->as.boolean_val) {
                    HVal *val = vm->scratchpad[frame->slot_b].value;
                    HVal *item = arena_calloc(arena, sizeof(HVal));
                    if (val) *item = *val; else item->kind = VAL_NOTHING;
                    item->next = NULL;
                    *frame->result_tail = item;
                    frame->result_tail = &item->next;
                }
                break;
            }
            case ITER_FOLD:
                frame->acc = vm->scratchpad[frame->slot_a].value;
                break;
            }

            /* Advance to next element */
            if (frame->next_elem) {
                vm->scratchpad[frame->elem_slot].value = frame->next_elem;
                vm->scratchpad[frame->elem_slot].tags  =
                    vm->scratchpad[frame->source_slot].tags;
                frame->next_elem = frame->next_elem->next;
                pc = frame->body_start;
                continue;
            }

            /* Iteration done — pop frame and store final result */
            uint16_t result_dest = frame->result_dest;
            vm->iter_depth--;

            switch (frame->mode) {
            case ITER_MAP:
            case ITER_FILTER:
                vm->scratchpad[result_dest].value =
                    make_list(arena, frame->result_head);
                vm->scratchpad[result_dest].tags = 0;
                break;
            case ITER_FOLD:
                vm->scratchpad[result_dest].value =
                    frame->acc ? frame->acc : vm->val_nothing;
                vm->scratchpad[result_dest].tags = 0;
                break;
            }
            result_slot = result_dest;
            break;
        }

        /* ── Stdlib dispatch ── */

        case OP_STDLIB_CALL: {
            HVal *arg = vm->scratchpad[o2].value;
            if (o1 == 0) {
                /* func_id=0: sanitizer or cross-contract 'uses' call.
                 * Reference VM cannot resolve these — passthrough the
                 * argument value and apply tag mode from flags. */
                vm->scratchpad[d].value = arg ? arg : vm->val_nothing;
                vm->scratchpad[d].tags = 0;
                propagate_tags(vm, d, ins, vm->scratchpad[o2].tags);
            } else {
                HelunaError serr = {0};
                HVal *ret = vm_stdlib_call(o1, arg, arena, &serr);
                if (!ret && serr.kind != HELUNA_OK) {
                    vm->error = serr;
                    vm->had_error = 1;
                    return NULL;
                }
                vm->scratchpad[d].value = ret ? ret : vm->val_nothing;
                vm->scratchpad[d].tags = 0;
                propagate_tags(vm, d, ins, vm->scratchpad[o2].tags);
            }
            result_slot = d;
            break;
        }

        /* ── Tag operations ── */

        case OP_TAG_SET: {
            /* Set tag bits from constant on slot */
            uint64_t bits = 0;
            if (o1 < (uint16_t)pkt->constant_count) {
                HVal *cv = pkt->constants[o1].value;
                if (cv && cv->kind == VAL_INTEGER) {
                    bits = (uint64_t)cv->as.integer_val;
                }
            }
            vm->scratchpad[d].tags |= bits;
            result_slot = d;
            break;
        }

        case OP_TAG_CHECK: {
            uint64_t bits = 0;
            if (o2 < (uint16_t)pkt->constant_count) {
                HVal *cv = pkt->constants[o2].value;
                if (cv && cv->kind == VAL_INTEGER) {
                    bits = (uint64_t)cv->as.integer_val;
                }
            }
            vm->scratchpad[d].value = ((vm->scratchpad[o1].tags & bits) == bits)
                ? vm->val_true : vm->val_false;
            vm->scratchpad[d].tags = 0;
            result_slot = d;
            break;
        }

        case OP_SOURCE_LOOKUP: {
            int source_idx = o1;
            HVal *keys = vm->scratchpad[o2].value;
            HVal *result_val = vm_source_resolve(vm, source_idx, keys);
            vm->scratchpad[d].value = result_val ? result_val : vm->val_nothing;
            vm->scratchpad[d].tags = 0;
            result_slot = d;
            break;
        }

        /* ── Superinstructions ── */

        case OP_RECORD_GET_C: {
            HVal *rec = vm->scratchpad[o1].value;
            HVal *key_val = (o2 < (uint16_t)pkt->constant_count)
                ? pkt->constants[o2].value : NULL;

            if (!rec || rec->kind != VAL_RECORD || !key_val ||
                key_val->kind != VAL_STRING) {
                vm->scratchpad[d].value = vm->val_nothing;
                vm->scratchpad[d].tags = 0;
            } else {
                HField *f = record_get_field(rec, key_val->as.string_val);
                if (f) {
                    vm->scratchpad[d].value = f->value;
                    vm->scratchpad[d].tags = vm->scratchpad[o1].tags;
                } else {
                    vm->scratchpad[d].value = vm->val_nothing;
                    vm->scratchpad[d].tags = 0;
                }
            }
            result_slot = d;
            break;
        }

        case OP_RECORD_SET_C: {
            HVal *rec = vm->scratchpad[d].value;
            HVal *key_val = (o1 < (uint16_t)pkt->constant_count)
                ? pkt->constants[o1].value : NULL;
            HVal *val = vm->scratchpad[o2].value;

            if (!rec || rec->kind != VAL_RECORD) {
                vm_error(vm, "RECORD_SET_C: dest is not a record");
                return NULL;
            }
            if (!key_val || key_val->kind != VAL_STRING) {
                vm_error(vm, "RECORD_SET_C: key is not a string");
                return NULL;
            }

            const char *key = key_val->as.string_val;
            HField *existing = NULL;
            for (HField *f = rec->as.record_fields; f; f = f->next) {
                if (strcmp(f->name, key) == 0) {
                    existing = f;
                    break;
                }
            }

            if (existing) {
                existing->value = val;
            } else {
                HField *nf = arena_calloc(arena, sizeof(HField));
                nf->name  = key;
                nf->value = val;
                ensure_slot_tails(vm, sp_size);
                if (vm->slot_tails[d].record_tail) {
                    vm->slot_tails[d].record_tail->next = nf;
                } else if (!rec->as.record_fields) {
                    rec->as.record_fields = nf;
                } else {
                    HField *last = rec->as.record_fields;
                    while (last->next) last = last->next;
                    last->next = nf;
                }
                vm->slot_tails[d].record_tail = nf;
            }

            vm->scratchpad[d].tags |= vm->scratchpad[o2].tags;
            result_slot = d;
            break;
        }

        case OP_RECORD_NEW_SET_C: {
            HVal *key_val = (o1 < (uint16_t)pkt->constant_count)
                ? pkt->constants[o1].value : NULL;
            HVal *val = vm->scratchpad[o2].value;

            if (!key_val || key_val->kind != VAL_STRING) {
                vm_error(vm, "RECORD_NEW_SET_C: key is not a string");
                return NULL;
            }

            HField *nf = arena_calloc(arena, sizeof(HField));
            nf->name  = key_val->as.string_val;
            nf->value = val;

            vm->scratchpad[d].value = make_record(arena, nf);
            vm->scratchpad[d].tags = vm->scratchpad[o2].tags;
            ensure_slot_tails(vm, sp_size);
            vm->slot_tails[d].record_tail = nf;
            result_slot = d;
            break;
        }

        case OP_STDLIB_CALL_1: {
            HVal *val = vm->scratchpad[o2].value;
            if (o1 == 0) {
                /* Sanitizer passthrough */
                vm->scratchpad[d].value = val ? val : vm->val_nothing;
                vm->scratchpad[d].tags = 0;
                propagate_tags(vm, d, ins, vm->scratchpad[o2].tags);
            } else {
                /* Build temporary {value: val} record */
                HField *vf = arena_calloc(arena, sizeof(HField));
                vf->name  = "value";
                vf->value = val;
                HVal *arg_rec = make_record(arena, vf);

                HelunaError serr = {0};
                HVal *ret = vm_stdlib_call(o1, arg_rec, arena, &serr);
                if (!ret && serr.kind != HELUNA_OK) {
                    vm->error = serr;
                    vm->had_error = 1;
                    return NULL;
                }
                vm->scratchpad[d].value = ret ? ret : vm->val_nothing;
                vm->scratchpad[d].tags = 0;
                propagate_tags(vm, d, ins, vm->scratchpad[o2].tags);
            }
            result_slot = d;
            break;
        }

        case OP_CMP_JUMP_EQ: {
            HVal *a = vm->scratchpad[o1].value;
            HVal *b = vm->scratchpad[o2].value;
            if (!hval_equal(a, b)) { pc = d; continue; }
            break;
        }

        case OP_CMP_JUMP_NEQ: {
            HVal *a = vm->scratchpad[o1].value;
            HVal *b = vm->scratchpad[o2].value;
            if (hval_equal(a, b)) { pc = d; continue; }
            break;
        }

        case OP_CMP_JUMP_LT: {
            HVal *a = vm->scratchpad[o1].value;
            HVal *b = vm->scratchpad[o2].value;
            if (!a || !b) { vm_error(vm, "CMP_JUMP_LT: null operand"); return NULL; }
            if ((a->kind == VAL_INTEGER || a->kind == VAL_FLOAT) &&
                (b->kind == VAL_INTEGER || b->kind == VAL_FLOAT)) {
                if (!(to_double(a) < to_double(b))) { pc = d; continue; }
            } else if (a->kind == VAL_STRING && b->kind == VAL_STRING) {
                if (!(strcmp(a->as.string_val, b->as.string_val) < 0)) { pc = d; continue; }
            } else {
                vm_error(vm, "CMP_JUMP_LT: incompatible types");
                return NULL;
            }
            break;
        }

        case OP_CMP_JUMP_GT: {
            HVal *a = vm->scratchpad[o1].value;
            HVal *b = vm->scratchpad[o2].value;
            if (!a || !b) { vm_error(vm, "CMP_JUMP_GT: null operand"); return NULL; }
            if ((a->kind == VAL_INTEGER || a->kind == VAL_FLOAT) &&
                (b->kind == VAL_INTEGER || b->kind == VAL_FLOAT)) {
                if (!(to_double(a) > to_double(b))) { pc = d; continue; }
            } else if (a->kind == VAL_STRING && b->kind == VAL_STRING) {
                if (!(strcmp(a->as.string_val, b->as.string_val) > 0)) { pc = d; continue; }
            } else {
                vm_error(vm, "CMP_JUMP_GT: incompatible types");
                return NULL;
            }
            break;
        }

        case OP_CMP_JUMP_LTE: {
            HVal *a = vm->scratchpad[o1].value;
            HVal *b = vm->scratchpad[o2].value;
            if (!a || !b) { vm_error(vm, "CMP_JUMP_LTE: null operand"); return NULL; }
            if ((a->kind == VAL_INTEGER || a->kind == VAL_FLOAT) &&
                (b->kind == VAL_INTEGER || b->kind == VAL_FLOAT)) {
                if (!(to_double(a) <= to_double(b))) { pc = d; continue; }
            } else if (a->kind == VAL_STRING && b->kind == VAL_STRING) {
                if (!(strcmp(a->as.string_val, b->as.string_val) <= 0)) { pc = d; continue; }
            } else {
                vm_error(vm, "CMP_JUMP_LTE: incompatible types");
                return NULL;
            }
            break;
        }

        case OP_CMP_JUMP_GTE: {
            HVal *a = vm->scratchpad[o1].value;
            HVal *b = vm->scratchpad[o2].value;
            if (!a || !b) { vm_error(vm, "CMP_JUMP_GTE: null operand"); return NULL; }
            if ((a->kind == VAL_INTEGER || a->kind == VAL_FLOAT) &&
                (b->kind == VAL_INTEGER || b->kind == VAL_FLOAT)) {
                if (!(to_double(a) >= to_double(b))) { pc = d; continue; }
            } else if (a->kind == VAL_STRING && b->kind == VAL_STRING) {
                if (!(strcmp(a->as.string_val, b->as.string_val) >= 0)) { pc = d; continue; }
            } else {
                vm_error(vm, "CMP_JUMP_GTE: incompatible types");
                return NULL;
            }
            break;
        }

        case OP_IS_NOTHING_JUMP: {
            HVal *a = vm->scratchpad[o1].value;
            if (!a || a->kind == VAL_NOTHING) {
                pc = d;
                continue;
            }
            break;
        }

        default:
            vm_error(vm, "unknown opcode 0x%02X at PC=%d", ins->opcode, pc);
            return NULL;
        }

        pc++;
    }

    if (vm->had_error) return NULL;

    /* Return value from result_slot — the compiler constructs the output
     * record in working slots, not in the output field slots. */
    if (result_slot < sp_size && vm->scratchpad[result_slot].value) {
        return vm->scratchpad[result_slot].value;
    }

    return vm->val_nothing;
}

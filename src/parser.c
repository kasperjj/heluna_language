#include "heluna/parser.h"
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

/* ── Helpers ─────────────────────────────────────────────── */

static void error(Parser *p, SrcLoc loc, const char *fmt, ...) {
    if (p->had_error) return;
    p->had_error = 1;
    va_list ap;
    va_start(ap, fmt);
    p->error.kind = HELUNA_ERR_SYNTAX;
    p->error.loc = loc;
    vsnprintf(p->error.message, sizeof(p->error.message), fmt, ap);
    va_end(ap);
}

static void skip_comments(Parser *p) {
    while (p->next.kind == TOK_COMMENT) {
        p->next = lexer_next(p->lex);
    }
}

static void advance(Parser *p) {
    p->current = p->next;
    p->next = lexer_next(p->lex);
    skip_comments(p);
}

static TokenKind peek(Parser *p) {
    return p->next.kind;
}

static int match(Parser *p, TokenKind kind) {
    if (peek(p) == kind) {
        advance(p);
        return 1;
    }
    return 0;
}

static void expect(Parser *p, TokenKind kind) {
    if (!match(p, kind)) {
        error(p, p->next.loc, "expected %s, got %s",
              token_kind_name(kind), token_kind_name(p->next.kind));
    }
}

static const char *token_text(Parser *p, Token tok) {
    return arena_strndup(p->arena, tok.start, (size_t)tok.length);
}

/* Extract identifier name from current token */
static const char *ident_name(Parser *p) {
    return token_text(p, p->current);
}

/* Extract input ref name (strip leading $) */
static const char *input_ref_name(Parser *p) {
    return arena_strndup(p->arena, p->current.start + 1,
                         (size_t)(p->current.length - 1));
}

/* Check if a token kind can serve as a name (identifier or keyword) */
static int is_name_token(TokenKind k) {
    if (k == TOK_IDENT) return 1;
    /* All keywords are valid as field/label names in context */
    switch (k) {
    case TOK_CONTRACT: case TOK_USES: case TOK_USING: case TOK_TAGS:
    case TOK_TAGGED: case TOK_SANITIZERS: case TOK_STRIPS:
    case TOK_INPUT: case TOK_OUTPUT:
    case TOK_RULES: case TOK_FORBID: case TOK_REQUIRE: case TOK_REJECT:
    case TOK_TESTS: case TOK_TEST: case TOK_GIVEN: case TOK_EXPECT:
    case TOK_AS: case TOK_STRING_TYPE: case TOK_INTEGER_TYPE:
    case TOK_FLOAT_TYPE: case TOK_BOOLEAN_TYPE: case TOK_MAYBE:
    case TOK_LIST: case TOK_OF: case TOK_RECORD:
    case TOK_DEFINE: case TOK_WITH: case TOK_LET: case TOK_BE:
    case TOK_RESULT: case TOK_IF: case TOK_THEN: case TOK_ELSE:
    case TOK_END: case TOK_MATCH: case TOK_WHEN: case TOK_BETWEEN:
    case TOK_AND: case TOK_OR: case TOK_NOT: case TOK_IN:
    case TOK_IS: case TOK_MOD:
    case TOK_THROUGH: case TOK_MAP: case TOK_DO: case TOK_FILTER:
    case TOK_WHERE: case TOK_SOURCE: case TOK_SOURCES: case TOK_KEYED_BY:
    case TOK_RETURNS: case TOK_LOOKUP: case TOK_CONFIG:
        return 1;
    default:
        return 0;
    }
}

/* Expect an identifier or keyword-as-name */
static void expect_name(Parser *p) {
    if (is_name_token(peek(p))) {
        advance(p);
    } else {
        error(p, p->next.loc, "expected identifier, got %s",
              token_kind_name(p->next.kind));
    }
}

/* ── Allocators ──────────────────────────────────────────── */

static AstExpr *new_expr(Parser *p, AstExprKind kind, SrcLoc loc) {
    AstExpr *e = arena_calloc(p->arena, sizeof(AstExpr));
    e->kind = kind;
    e->loc = loc;
    return e;
}

static AstPattern *new_pattern(Parser *p, AstPatternKind kind, SrcLoc loc) {
    AstPattern *pat = arena_calloc(p->arena, sizeof(AstPattern));
    pat->kind = kind;
    pat->loc = loc;
    return pat;
}

static AstType *new_type(Parser *p, AstTypeKind kind, SrcLoc loc) {
    AstType *t = arena_calloc(p->arena, sizeof(AstType));
    t->kind = kind;
    t->loc = loc;
    return t;
}

/* ── Forward declarations ────────────────────────────────── */

static AstExpr    *parse_expression(Parser *p);
static AstExpr    *parse_or_expr(Parser *p);
static AstExpr    *parse_and_expr(Parser *p);
static AstExpr    *parse_not_expr(Parser *p);
static AstExpr    *parse_through_expr(Parser *p);
static AstExpr    *parse_compare_expr(Parser *p);
static AstExpr    *parse_arith_expr(Parser *p);
static AstExpr    *parse_term(Parser *p);
static AstExpr    *parse_unary(Parser *p);
static AstExpr    *parse_atom(Parser *p);
static AstPattern *parse_pattern(Parser *p);
static AstType    *parse_type(Parser *p);

/* ── Accessor chain ──────────────────────────────────────── */

static AstExpr *parse_accessors(Parser *p, AstExpr *base) {
    while (peek(p) == TOK_DOT) {
        advance(p); /* consume '.' */
        SrcLoc loc = p->current.loc;
        if (peek(p) == TOK_IDENT || peek(p) == TOK_INTEGER) {
            advance(p);
            const char *field = token_text(p, p->current);
            AstExpr *acc = new_expr(p, EXPR_ACCESS, loc);
            acc->as.access.object = base;
            acc->as.access.field = field;
            base = acc;
        } else {
            /* Some keywords can appear as field names after dot */
            TokenKind k = peek(p);
            if (k == TOK_INPUT || k == TOK_OUTPUT || k == TOK_LIST ||
                k == TOK_RECORD || k == TOK_MATCH || k == TOK_FILTER ||
                k == TOK_MAP || k == TOK_STRING_TYPE || k == TOK_INTEGER_TYPE ||
                k == TOK_FLOAT_TYPE || k == TOK_BOOLEAN_TYPE || k == TOK_MAYBE ||
                k == TOK_TRUE || k == TOK_FALSE || k == TOK_NOTHING ||
                k == TOK_TAGS || k == TOK_RULES || k == TOK_TESTS ||
                k == TOK_TEST || k == TOK_GIVEN || k == TOK_EXPECT ||
                k == TOK_RESULT || k == TOK_LET || k == TOK_BE ||
                k == TOK_DO || k == TOK_END || k == TOK_DEFINE ||
                k == TOK_WITH || k == TOK_USES || k == TOK_USING || k == TOK_AS ||
                k == TOK_OF || k == TOK_WHEN || k == TOK_THEN ||
                k == TOK_ELSE || k == TOK_IF || k == TOK_AND ||
                k == TOK_OR || k == TOK_NOT || k == TOK_IN ||
                k == TOK_IS || k == TOK_MOD ||
                k == TOK_WHERE || k == TOK_THROUGH || k == TOK_BETWEEN ||
                k == TOK_FORBID || k == TOK_REQUIRE || k == TOK_REJECT ||
                k == TOK_TAGGED || k == TOK_SANITIZERS || k == TOK_STRIPS ||
                k == TOK_CONTRACT || k == TOK_SOURCE || k == TOK_SOURCES ||
                k == TOK_KEYED_BY || k == TOK_RETURNS || k == TOK_LOOKUP ||
                k == TOK_CONFIG) {
                advance(p);
                const char *field = token_text(p, p->current);
                AstExpr *acc = new_expr(p, EXPR_ACCESS, loc);
                acc->as.access.object = base;
                acc->as.access.field = field;
                base = acc;
            } else {
                error(p, p->next.loc, "expected field name after '.'");
                break;
            }
        }
    }
    return base;
}

/* ── Atom parsing ────────────────────────────────────────── */

static long long parse_integer_value(const char *start, int length) {
    char buf[64];
    int len = length < 63 ? length : 63;
    memcpy(buf, start, (size_t)len);
    buf[len] = '\0';
    return strtoll(buf, NULL, 10);
}

static double parse_float_value(const char *start, int length) {
    char buf[128];
    int len = length < 127 ? length : 127;
    memcpy(buf, start, (size_t)len);
    buf[len] = '\0';
    return strtod(buf, NULL);
}

static AstExpr *parse_record_literal(Parser *p) {
    SrcLoc loc = p->current.loc; /* on '{' */
    AstExpr *rec = new_expr(p, EXPR_RECORD, loc);
    rec->as.record.labels = NULL;

    if (peek(p) == TOK_RBRACE) {
        advance(p);
        return rec;
    }

    AstLabel *head = NULL;
    AstLabel **tail = &head;

    do {
        expect_name(p);
        if (p->had_error) return rec;
        SrcLoc label_loc = p->current.loc;
        const char *name = ident_name(p);

        expect(p, TOK_COLON);
        if (p->had_error) return rec;

        AstExpr *value = parse_expression(p);

        AstLabel *label = arena_calloc(p->arena, sizeof(AstLabel));
        label->name = name;
        label->value = value;
        label->loc = label_loc;
        *tail = label;
        tail = &label->next;
    } while (match(p, TOK_COMMA));

    expect(p, TOK_RBRACE);
    rec->as.record.labels = head;
    return rec;
}

/* Parse a config block delimited by config...end (no braces) */
static AstExpr *parse_config_block(Parser *p) {
    SrcLoc loc = p->current.loc; /* on 'config' */
    AstExpr *rec = new_expr(p, EXPR_RECORD, loc);
    rec->as.record.labels = NULL;

    if (peek(p) == TOK_END) {
        advance(p);
        return rec;
    }

    AstLabel *head = NULL;
    AstLabel **tail = &head;

    do {
        expect_name(p);
        if (p->had_error) return rec;
        SrcLoc label_loc = p->current.loc;
        const char *name = ident_name(p);

        expect(p, TOK_COLON);
        if (p->had_error) return rec;

        AstExpr *value = parse_expression(p);

        AstLabel *label = arena_calloc(p->arena, sizeof(AstLabel));
        label->name = name;
        label->value = value;
        label->loc = label_loc;
        *tail = label;
        tail = &label->next;
    } while (match(p, TOK_COMMA));

    expect(p, TOK_END);
    rec->as.record.labels = head;
    return rec;
}

static AstExpr *parse_list_literal(Parser *p) {
    SrcLoc loc = p->current.loc; /* on '[' */
    AstExpr *list = new_expr(p, EXPR_LIST, loc);
    list->as.list.elements = NULL;

    if (peek(p) == TOK_RBRACKET) {
        advance(p);
        return list;
    }

    AstExpr *head = NULL;
    AstExpr **tail = &head;

    do {
        AstExpr *elem = parse_expression(p);
        *tail = elem;
        tail = &elem->next;
    } while (match(p, TOK_COMMA));

    expect(p, TOK_RBRACKET);
    list->as.list.elements = head;
    return list;
}

/* Parse a function call: name '(' record-literal ')'
 * Called when we've already consumed the identifier and see '(' next. */
static AstExpr *parse_call(Parser *p, const char *name, SrcLoc loc) {
    advance(p); /* consume '(' */
    /* The argument is a record literal (with braces) */
    expect(p, TOK_LBRACE);
    if (p->had_error) return new_expr(p, EXPR_NOTHING, loc);
    AstExpr *arg = parse_record_literal(p);
    expect(p, TOK_RPAREN);

    AstExpr *call = new_expr(p, EXPR_CALL, loc);
    call->as.call.name = name;
    call->as.call.arg = arg;
    return call;
}

static AstExpr *parse_atom(Parser *p) {
    if (p->had_error) return new_expr(p, EXPR_NOTHING, p->next.loc);

    TokenKind k = peek(p);
    SrcLoc loc = p->next.loc;

    switch (k) {
    case TOK_INTEGER: {
        advance(p);
        AstExpr *e = new_expr(p, EXPR_INTEGER, loc);
        e->as.integer_val = parse_integer_value(p->current.start, p->current.length);
        return parse_accessors(p, e);
    }
    case TOK_FLOAT: {
        advance(p);
        AstExpr *e = new_expr(p, EXPR_FLOAT, loc);
        e->as.float_val = parse_float_value(p->current.start, p->current.length);
        return parse_accessors(p, e);
    }
    case TOK_STRING: {
        advance(p);
        AstExpr *e = new_expr(p, EXPR_STRING, loc);
        e->as.string_val.value = p->current.start;
        e->as.string_val.length = p->current.length;
        return parse_accessors(p, e);
    }
    case TOK_TRUE: {
        advance(p);
        return parse_accessors(p, new_expr(p, EXPR_TRUE, loc));
    }
    case TOK_FALSE: {
        advance(p);
        return parse_accessors(p, new_expr(p, EXPR_FALSE, loc));
    }
    case TOK_NOTHING: {
        advance(p);
        return parse_accessors(p, new_expr(p, EXPR_NOTHING, loc));
    }
    case TOK_IDENT: {
        advance(p);
        const char *name = ident_name(p);
        /* Check for function call: ident '(' */
        if (peek(p) == TOK_LPAREN) {
            AstExpr *call = parse_call(p, name, loc);
            return parse_accessors(p, call);
        }
        AstExpr *e = new_expr(p, EXPR_IDENT, loc);
        e->as.ident.name = name;
        return parse_accessors(p, e);
    }
    case TOK_INPUT_REF: {
        advance(p);
        AstExpr *e = new_expr(p, EXPR_INPUT_REF, loc);
        e->as.input_ref.name = input_ref_name(p);
        return parse_accessors(p, e);
    }
    case TOK_INPUT:
    case TOK_OUTPUT: {
        advance(p);
        const char *name = ident_name(p);
        if (peek(p) == TOK_LPAREN) {
            AstExpr *call = parse_call(p, name, loc);
            return parse_accessors(p, call);
        }
        AstExpr *e = new_expr(p, EXPR_IDENT, loc);
        e->as.ident.name = name;
        return parse_accessors(p, e);
    }
    case TOK_LBRACE: {
        advance(p);
        AstExpr *rec = parse_record_literal(p);
        return parse_accessors(p, rec);
    }
    case TOK_LBRACKET: {
        advance(p);
        AstExpr *list = parse_list_literal(p);
        return parse_accessors(p, list);
    }
    case TOK_LPAREN: {
        advance(p);
        AstExpr *inner = parse_expression(p);
        expect(p, TOK_RPAREN);
        AstExpr *paren = new_expr(p, EXPR_PAREN, loc);
        paren->as.paren.inner = inner;
        return parse_accessors(p, paren);
    }
    default:
        error(p, loc, "expected expression, got %s", token_kind_name(k));
        return new_expr(p, EXPR_NOTHING, loc);
    }
}

/* ── Unary ───────────────────────────────────────────────── */

static AstExpr *parse_unary(Parser *p) {
    if (peek(p) == TOK_MINUS) {
        SrcLoc loc = p->next.loc;
        advance(p);
        AstExpr *operand = parse_unary(p);
        AstExpr *e = new_expr(p, EXPR_UNARY_NEG, loc);
        e->as.unary.operand = operand;
        return e;
    }
    return parse_atom(p);
}

/* ── Term (* / %) ────────────────────────────────────────── */

static AstExpr *parse_term(Parser *p) {
    AstExpr *left = parse_unary(p);

    while (peek(p) == TOK_STAR || peek(p) == TOK_SLASH || peek(p) == TOK_PERCENT || peek(p) == TOK_MOD) {
        SrcLoc loc = p->next.loc;
        AstBinOp op;
        if (peek(p) == TOK_STAR)         op = BIN_MUL;
        else if (peek(p) == TOK_SLASH)   op = BIN_DIV;
        else                              op = BIN_MOD;
        advance(p);

        AstExpr *right = parse_unary(p);
        AstExpr *bin = new_expr(p, EXPR_BINARY, loc);
        bin->as.binary.op = op;
        bin->as.binary.left = left;
        bin->as.binary.right = right;
        left = bin;
    }
    return left;
}

/* ── ArithExpr (+ -) ────────────────────────────────────── */

static AstExpr *parse_arith_expr(Parser *p) {
    AstExpr *left = parse_term(p);

    while (peek(p) == TOK_PLUS || peek(p) == TOK_MINUS) {
        SrcLoc loc = p->next.loc;
        AstBinOp op = (peek(p) == TOK_PLUS) ? BIN_ADD : BIN_SUB;
        advance(p);

        AstExpr *right = parse_term(p);
        AstExpr *bin = new_expr(p, EXPR_BINARY, loc);
        bin->as.binary.op = op;
        bin->as.binary.left = left;
        bin->as.binary.right = right;
        left = bin;
    }
    return left;
}

/* ── CompareExpr ─────────────────────────────────────────── */

static AstExpr *parse_compare_expr(Parser *p) {
    AstExpr *left = parse_arith_expr(p);

    TokenKind k = peek(p);

    /* 'is' type check: <expr> is <type-keyword> */
    if (k == TOK_IS) {
        SrcLoc loc = p->next.loc;
        advance(p); /* consume 'is' */
        TokenKind tk = peek(p);
        const char *type_name;
        switch (tk) {
        case TOK_STRING_TYPE:   type_name = "string";  break;
        case TOK_INTEGER_TYPE:  type_name = "integer"; break;
        case TOK_FLOAT_TYPE:    type_name = "float";   break;
        case TOK_BOOLEAN_TYPE:  type_name = "boolean"; break;
        case TOK_NOTHING:       type_name = "nothing"; break;
        case TOK_LIST:          type_name = "list";    break;
        case TOK_RECORD:        type_name = "record";  break;
        default:
            error(p, p->next.loc, "expected type keyword after 'is', got %s",
                  token_kind_name(tk));
            return left;
        }
        advance(p); /* consume type keyword */
        AstExpr *e = new_expr(p, EXPR_IS_TYPE, loc);
        e->as.is_type.operand = left;
        e->as.is_type.type_name = type_name;
        return e;
    }

    if (k == TOK_EQ || k == TOK_NEQ || k == TOK_LT || k == TOK_GT ||
        k == TOK_LTE || k == TOK_GTE) {
        SrcLoc loc = p->next.loc;
        AstBinOp op;
        switch (k) {
        case TOK_EQ:  op = BIN_EQ;  break;
        case TOK_NEQ: op = BIN_NEQ; break;
        case TOK_LT:  op = BIN_LT;  break;
        case TOK_GT:  op = BIN_GT;  break;
        case TOK_LTE: op = BIN_LTE; break;
        case TOK_GTE: op = BIN_GTE; break;
        default:      op = BIN_EQ;  break;
        }
        advance(p);
        AstExpr *right = parse_arith_expr(p);
        AstExpr *bin = new_expr(p, EXPR_BINARY, loc);
        bin->as.binary.op = op;
        bin->as.binary.left = left;
        bin->as.binary.right = right;
        return bin;
    }
    return left;
}

/* ── Boolean operators (integrated into precedence chain) ── */

static AstExpr *parse_not_expr(Parser *p) {
    if (peek(p) == TOK_NOT) {
        SrcLoc loc = p->next.loc;
        advance(p);
        AstExpr *operand = parse_not_expr(p);
        AstExpr *e = new_expr(p, EXPR_NOT, loc);
        e->as.not_expr.operand = operand;
        return e;
    }
    return parse_compare_expr(p);
}

static AstExpr *parse_and_expr(Parser *p) {
    AstExpr *left = parse_not_expr(p);

    while (peek(p) == TOK_AND) {
        SrcLoc loc = p->next.loc;
        advance(p);
        AstExpr *right = parse_not_expr(p);
        AstExpr *bin = new_expr(p, EXPR_BINARY, loc);
        bin->as.binary.op = BIN_AND;
        bin->as.binary.left = left;
        bin->as.binary.right = right;
        left = bin;
    }
    return left;
}

static AstExpr *parse_or_expr(Parser *p) {
    AstExpr *left = parse_and_expr(p);

    while (peek(p) == TOK_OR) {
        SrcLoc loc = p->next.loc;
        advance(p); /* consume 'or' */

        /* 'or else' → coalesce expression */
        if (peek(p) == TOK_ELSE) {
            advance(p); /* consume 'else' */
            AstExpr *fallback = parse_and_expr(p);
            AstExpr *oe = new_expr(p, EXPR_OR_ELSE, loc);
            oe->as.or_else.primary = left;
            oe->as.or_else.fallback = fallback;
            left = oe;
        } else {
            AstExpr *right = parse_and_expr(p);
            AstExpr *bin = new_expr(p, EXPR_BINARY, loc);
            bin->as.binary.op = BIN_OR;
            bin->as.binary.left = left;
            bin->as.binary.right = right;
            left = bin;
        }
    }
    return left;
}

/* BoolExpr contexts (if, filter, require, guards) use parse_or_expr */
static AstExpr *parse_bool_expr(Parser *p) {
    return parse_or_expr(p);
}

/* ── ThroughExpr ─────────────────────────────────────────── */

static AstExpr *parse_filter_expr(Parser *p);
static AstExpr *parse_map_expr(Parser *p);

static AstExpr *parse_through_expr(Parser *p) {
    AstExpr *left = parse_or_expr(p);

    while (peek(p) == TOK_THROUGH) {
        SrcLoc loc = p->next.loc;
        advance(p); /* consume 'through' */

        AstExpr *right;
        if (peek(p) == TOK_FILTER) {
            right = parse_filter_expr(p);
        } else if (peek(p) == TOK_MAP) {
            right = parse_map_expr(p);
        } else {
            /* Must be a function call: ident '(' ... ')' */
            expect(p, TOK_IDENT);
            if (p->had_error) return left;
            const char *name = ident_name(p);
            SrcLoc call_loc = p->current.loc;
            if (peek(p) == TOK_LPAREN) {
                right = parse_call(p, name, call_loc);
            } else {
                /* Just an identifier reference — shouldn't happen per grammar,
                   but handle it gracefully */
                right = new_expr(p, EXPR_IDENT, call_loc);
                right->as.ident.name = name;
            }
        }

        AstExpr *through = new_expr(p, EXPR_THROUGH, loc);
        through->as.through.left = left;
        through->as.through.right = right;
        left = through;
    }
    return left;
}

/* ── If expression ───────────────────────────────────────── */

static AstExpr *parse_if_expr(Parser *p) {
    SrcLoc loc = p->current.loc; /* on 'if' */
    AstExpr *node = new_expr(p, EXPR_IF, loc);

    AstIfBranch *head = NULL;
    AstIfBranch **tail = &head;

    /* First branch: if condition then body */
    AstExpr *cond = parse_bool_expr(p);
    expect(p, TOK_THEN);
    AstExpr *body = parse_expression(p);

    AstIfBranch *branch = arena_calloc(p->arena, sizeof(AstIfBranch));
    branch->condition = cond;
    branch->body = body;
    branch->loc = loc;
    *tail = branch;
    tail = &branch->next;

    /* else if ... / else ... */
    int has_else = 0;
    while (peek(p) == TOK_ELSE) {
        SrcLoc else_loc = p->next.loc;
        advance(p); /* consume 'else' */

        if (peek(p) == TOK_IF) {
            advance(p); /* consume 'if' */
            AstExpr *elif_cond = parse_bool_expr(p);
            expect(p, TOK_THEN);
            AstExpr *elif_body = parse_expression(p);

            AstIfBranch *elif = arena_calloc(p->arena, sizeof(AstIfBranch));
            elif->condition = elif_cond;
            elif->body = elif_body;
            elif->loc = else_loc;
            *tail = elif;
            tail = &elif->next;
        } else {
            /* final else */
            AstExpr *else_body = parse_expression(p);
            AstIfBranch *else_br = arena_calloc(p->arena, sizeof(AstIfBranch));
            else_br->condition = NULL;
            else_br->body = else_body;
            else_br->loc = else_loc;
            *tail = else_br;
            tail = &else_br->next;
            has_else = 1;
            break;
        }
    }

    if (!has_else) {
        error(p, loc, "if expression requires an else branch");
    }

    expect(p, TOK_END);
    node->as.if_expr.branches = head;
    return node;
}

/* ── Pattern parsing ─────────────────────────────────────── */

static AstPattern *parse_pattern(Parser *p) {
    SrcLoc loc = p->next.loc;
    TokenKind k = peek(p);

    switch (k) {
    case TOK_NOTHING: {
        advance(p);
        AstPattern *pat = new_pattern(p, PAT_LITERAL, loc);
        pat->as.literal.value = new_expr(p, EXPR_NOTHING, loc);
        return pat;
    }
    case TOK_TRUE: {
        advance(p);
        AstPattern *pat = new_pattern(p, PAT_LITERAL, loc);
        pat->as.literal.value = new_expr(p, EXPR_TRUE, loc);
        return pat;
    }
    case TOK_FALSE: {
        advance(p);
        AstPattern *pat = new_pattern(p, PAT_LITERAL, loc);
        pat->as.literal.value = new_expr(p, EXPR_FALSE, loc);
        return pat;
    }
    case TOK_INTEGER: {
        advance(p);
        AstPattern *pat = new_pattern(p, PAT_LITERAL, loc);
        AstExpr *val = new_expr(p, EXPR_INTEGER, loc);
        val->as.integer_val = parse_integer_value(p->current.start, p->current.length);
        pat->as.literal.value = val;
        return pat;
    }
    case TOK_FLOAT: {
        advance(p);
        AstPattern *pat = new_pattern(p, PAT_LITERAL, loc);
        AstExpr *val = new_expr(p, EXPR_FLOAT, loc);
        val->as.float_val = parse_float_value(p->current.start, p->current.length);
        pat->as.literal.value = val;
        return pat;
    }
    case TOK_STRING: {
        advance(p);
        AstPattern *pat = new_pattern(p, PAT_LITERAL, loc);
        AstExpr *val = new_expr(p, EXPR_STRING, loc);
        val->as.string_val.value = p->current.start;
        val->as.string_val.length = p->current.length;
        pat->as.literal.value = val;
        return pat;
    }
    case TOK_MINUS: {
        /* Negative number literal in pattern */
        advance(p); /* consume '-' */
        if (peek(p) == TOK_INTEGER) {
            advance(p);
            AstPattern *pat = new_pattern(p, PAT_LITERAL, loc);
            AstExpr *val = new_expr(p, EXPR_INTEGER, loc);
            val->as.integer_val = -parse_integer_value(p->current.start, p->current.length);
            pat->as.literal.value = val;
            return pat;
        } else if (peek(p) == TOK_FLOAT) {
            advance(p);
            AstPattern *pat = new_pattern(p, PAT_LITERAL, loc);
            AstExpr *val = new_expr(p, EXPR_FLOAT, loc);
            val->as.float_val = -parse_float_value(p->current.start, p->current.length);
            pat->as.literal.value = val;
            return pat;
        }
        error(p, loc, "expected number after '-' in pattern");
        return new_pattern(p, PAT_WILDCARD, loc);
    }
    case TOK_UNDERSCORE: {
        advance(p);
        return new_pattern(p, PAT_WILDCARD, loc);
    }
    case TOK_BETWEEN: {
        advance(p); /* consume 'between' */
        /* Use parse_compare_expr to avoid consuming 'and' as boolean op */
        AstExpr *low = parse_compare_expr(p);
        expect(p, TOK_AND);
        AstExpr *high = parse_compare_expr(p);
        AstPattern *pat = new_pattern(p, PAT_RANGE, loc);
        pat->as.range.low = low;
        pat->as.range.high = high;
        return pat;
    }
    case TOK_LBRACKET: {
        advance(p); /* consume '[' */
        AstPattern *pat = new_pattern(p, PAT_LIST, loc);
        pat->as.list.elements = NULL;
        pat->as.list.rest_name = NULL;

        if (peek(p) == TOK_RBRACKET) {
            advance(p);
            return pat;
        }

        AstPatternElem *head = NULL;
        AstPatternElem **tail = &head;

        for (;;) {
            /* Check for ..rest */
            if (peek(p) == TOK_DOTDOT) {
                advance(p); /* consume '..' */
                if (peek(p) == TOK_IDENT) {
                    advance(p);
                    pat->as.list.rest_name = ident_name(p);
                } else {
                    pat->as.list.rest_name = "";
                }
                /* Might have trailing comma before ] */
                match(p, TOK_COMMA);
                break;
            }

            AstPattern *elem = parse_pattern(p);
            AstPatternElem *node = arena_calloc(p->arena, sizeof(AstPatternElem));
            node->pattern = elem;
            *tail = node;
            tail = &node->next;

            if (!match(p, TOK_COMMA)) break;

            /* Check if .. follows the comma */
            if (peek(p) == TOK_DOTDOT) {
                advance(p); /* consume '..' */
                if (peek(p) == TOK_IDENT) {
                    advance(p);
                    pat->as.list.rest_name = ident_name(p);
                } else {
                    pat->as.list.rest_name = "";
                }
                break;
            }
        }

        expect(p, TOK_RBRACKET);
        pat->as.list.elements = head;
        return pat;
    }
    case TOK_LBRACE: {
        advance(p); /* consume '{' */
        AstPattern *pat = new_pattern(p, PAT_RECORD, loc);

        AstFieldPattern *head = NULL;
        AstFieldPattern **tail = &head;

        if (peek(p) != TOK_RBRACE) {
            do {
                expect_name(p);
                if (p->had_error) break;
                SrcLoc floc = p->current.loc;
                const char *fname = ident_name(p);

                expect(p, TOK_COLON);
                if (p->had_error) break;

                AstPattern *fpat = parse_pattern(p);

                AstFieldPattern *fp = arena_calloc(p->arena, sizeof(AstFieldPattern));
                fp->name = fname;
                fp->pattern = fpat;
                fp->loc = floc;
                *tail = fp;
                tail = &fp->next;
            } while (match(p, TOK_COMMA));
        }

        expect(p, TOK_RBRACE);
        pat->as.record.fields = head;
        return pat;
    }
    case TOK_IDENT: {
        advance(p);
        AstPattern *pat = new_pattern(p, PAT_BINDING, loc);
        pat->as.binding.name = ident_name(p);
        return pat;
    }
    default:
        error(p, loc, "expected pattern, got %s", token_kind_name(k));
        return new_pattern(p, PAT_WILDCARD, loc);
    }
}

/* ── Match expression ────────────────────────────────────── */

static AstExpr *parse_match_expr(Parser *p) {
    SrcLoc loc = p->current.loc; /* on 'match' */
    AstExpr *subject = parse_expression(p);

    AstExpr *node = new_expr(p, EXPR_MATCH, loc);
    node->as.match.subject = subject;
    node->as.match.else_body = NULL;

    AstWhenClause *head = NULL;
    AstWhenClause **tail = &head;

    while (peek(p) == TOK_WHEN) {
        SrcLoc wloc = p->next.loc;
        advance(p); /* consume 'when' */

        AstPattern *pattern = parse_pattern(p);
        AstExpr *guard = NULL;

        /* Check for 'and' guard */
        if (peek(p) == TOK_AND) {
            advance(p);
            guard = parse_bool_expr(p);
        }

        expect(p, TOK_THEN);
        AstExpr *body = parse_expression(p);

        AstWhenClause *clause = arena_calloc(p->arena, sizeof(AstWhenClause));
        clause->pattern = pattern;
        clause->guard = guard;
        clause->body = body;
        clause->loc = wloc;
        *tail = clause;
        tail = &clause->next;
    }

    if (peek(p) == TOK_ELSE) {
        advance(p);
        node->as.match.else_body = parse_expression(p);
    }

    expect(p, TOK_END);
    node->as.match.clauses = head;
    return node;
}

/* ── Let expression ──────────────────────────────────────── */

static AstExpr *parse_let_expr(Parser *p) {
    SrcLoc loc = p->current.loc; /* on 'let' */

    expect(p, TOK_IDENT);
    if (p->had_error) return new_expr(p, EXPR_NOTHING, loc);
    const char *name = ident_name(p);

    expect(p, TOK_BE);
    if (p->had_error) return new_expr(p, EXPR_NOTHING, loc);

    AstExpr *binding = parse_expression(p);

    /* The body is either another let or a standalone expression.
     * In function def context, the body starts with 'let' or 'result'.
     * We handle this at the function-def level instead. */
    AstExpr *node = new_expr(p, EXPR_LET, loc);
    node->as.let.name = name;
    node->as.let.binding = binding;
    node->as.let.body = NULL; /* filled in by caller */
    return node;
}

/* ── Filter expression ───────────────────────────────────── */

static AstExpr *parse_filter_expr(Parser *p) {
    SrcLoc loc = p->next.loc;
    advance(p); /* consume 'filter' */

    AstExpr *list = parse_expression(p);
    expect(p, TOK_WHERE);
    if (p->had_error) return new_expr(p, EXPR_NOTHING, loc);

    expect(p, TOK_IDENT);
    if (p->had_error) return new_expr(p, EXPR_NOTHING, loc);
    const char *var_name = ident_name(p);

    AstExpr *predicate = parse_bool_expr(p);
    expect(p, TOK_END);

    AstExpr *node = new_expr(p, EXPR_FILTER, loc);
    node->as.filter.list = list;
    node->as.filter.var_name = var_name;
    node->as.filter.predicate = predicate;
    return node;
}

/* ── Map expression ──────────────────────────────────────── */

static AstExpr *parse_map_expr(Parser *p) {
    SrcLoc loc = p->next.loc;
    advance(p); /* consume 'map' */

    AstExpr *list = parse_expression(p);
    expect(p, TOK_AS);
    if (p->had_error) return new_expr(p, EXPR_NOTHING, loc);

    expect(p, TOK_IDENT);
    if (p->had_error) return new_expr(p, EXPR_NOTHING, loc);
    const char *var_name = ident_name(p);

    expect(p, TOK_DO);
    if (p->had_error) return new_expr(p, EXPR_NOTHING, loc);

    AstExpr *body = parse_expression(p);
    expect(p, TOK_END);

    AstExpr *node = new_expr(p, EXPR_MAP, loc);
    node->as.map.list = list;
    node->as.map.var_name = var_name;
    node->as.map.body = body;
    return node;
}

/* ── Lookup expression ───────────────────────────────────── */

static AstExpr *parse_lookup_expr(Parser *p) {
    SrcLoc loc = p->current.loc; /* on 'lookup' */

    /* Source name is an identifier */
    expect(p, TOK_IDENT);
    if (p->had_error) return new_expr(p, EXPR_NOTHING, loc);
    const char *source = ident_name(p);

    expect(p, TOK_WHERE);
    if (p->had_error) return new_expr(p, EXPR_NOTHING, loc);

    /* Parse key bindings: name = expr (',' name = expr)* */
    AstLookupKey *head = NULL;
    AstLookupKey **tail = &head;

    do {
        expect_name(p);
        if (p->had_error) break;
        SrcLoc kloc = p->current.loc;
        const char *kname = ident_name(p);

        expect(p, TOK_EQ);
        if (p->had_error) break;

        AstExpr *value = parse_expression(p);

        AstLookupKey *key = arena_calloc(p->arena, sizeof(AstLookupKey));
        key->name = kname;
        key->value = value;
        key->loc = kloc;
        *tail = key;
        tail = &key->next;
    } while (match(p, TOK_COMMA));

    expect(p, TOK_END);

    AstExpr *node = new_expr(p, EXPR_LOOKUP, loc);
    node->as.lookup.source_name = source;
    node->as.lookup.keys = head;
    return node;
}

/* ── Expression dispatcher ───────────────────────────────── */

static AstExpr *parse_expression(Parser *p) {
    if (p->had_error) return new_expr(p, EXPR_NOTHING, p->next.loc);

    TokenKind k = peek(p);

    switch (k) {
    case TOK_LET: {
        advance(p);
        return parse_let_expr(p);
    }
    case TOK_IF: {
        advance(p);
        return parse_if_expr(p);
    }
    case TOK_MATCH: {
        advance(p);
        return parse_match_expr(p);
    }
    case TOK_FILTER:
        return parse_filter_expr(p);
    case TOK_MAP:
        return parse_map_expr(p);
    case TOK_LOOKUP: {
        advance(p);
        return parse_lookup_expr(p);
    }
    default:
        return parse_through_expr(p);
    }
}

/* ── Type parsing ────────────────────────────────────────── */

static AstType *parse_type(Parser *p) {
    SrcLoc loc = p->next.loc;
    TokenKind k = peek(p);

    switch (k) {
    case TOK_STRING_TYPE:
        advance(p);
        return new_type(p, TYPE_STRING, loc);
    case TOK_INTEGER_TYPE:
        advance(p);
        return new_type(p, TYPE_INTEGER, loc);
    case TOK_FLOAT_TYPE:
        advance(p);
        return new_type(p, TYPE_FLOAT, loc);
    case TOK_BOOLEAN_TYPE:
        advance(p);
        return new_type(p, TYPE_BOOLEAN, loc);
    case TOK_MAYBE: {
        advance(p);
        AstType *t = new_type(p, TYPE_MAYBE, loc);
        t->as.maybe.inner = parse_type(p);
        return t;
    }
    case TOK_LIST: {
        advance(p);
        expect(p, TOK_OF);
        AstType *t = new_type(p, TYPE_LIST, loc);
        t->as.list.inner = parse_type(p);
        return t;
    }
    case TOK_RECORD: {
        advance(p);
        AstType *t = new_type(p, TYPE_RECORD, loc);

        AstFieldDecl *head = NULL;
        AstFieldDecl **tail = &head;

        do {
            expect_name(p);
            if (p->had_error) break;
            SrcLoc floc = p->current.loc;
            const char *fname = ident_name(p);

            expect(p, TOK_AS);
            if (p->had_error) break;

            AstType *ftype = parse_type(p);

            AstFieldDecl *fd = arena_calloc(p->arena, sizeof(AstFieldDecl));
            fd->name = fname;
            fd->type = ftype;
            fd->loc = floc;

            /* tagged Identifier+ */
            if (peek(p) == TOK_TAGGED) {
                advance(p);
                const char *tags[32];
                int count = 0;
                while (peek(p) == TOK_IDENT && count < 32) {
                    advance(p);
                    tags[count++] = ident_name(p);
                }
                if (count > 0) {
                    fd->tags = arena_alloc(p->arena, sizeof(char *) * (size_t)(count + 1));
                    for (int i = 0; i < count; i++) fd->tags[i] = tags[i];
                    fd->tags[count] = NULL;
                    fd->tag_count = count;
                }
            }

            *tail = fd;
            tail = &fd->next;
        } while (match(p, TOK_COMMA));

        expect(p, TOK_END);
        t->as.record.fields = head;
        return t;
    }
    default:
        error(p, loc, "expected type, got %s", token_kind_name(k));
        return new_type(p, TYPE_STRING, loc);
    }
}

/* ── FieldDecl parsing (for input/output specs) ──────────── */

static AstFieldDecl *parse_field_decl_list(Parser *p) {
    AstFieldDecl *head = NULL;
    AstFieldDecl **tail = &head;

    do {
        expect_name(p);
        if (p->had_error) break;
        SrcLoc floc = p->current.loc;
        const char *fname = ident_name(p);

        expect(p, TOK_AS);
        if (p->had_error) break;

        AstType *ftype = parse_type(p);

        AstFieldDecl *fd = arena_calloc(p->arena, sizeof(AstFieldDecl));
        fd->name = fname;
        fd->type = ftype;
        fd->loc = floc;

        /* tagged Identifier+ */
        if (peek(p) == TOK_TAGGED) {
            advance(p);
            const char *tags[32];
            int count = 0;
            while (peek(p) == TOK_IDENT && count < 32) {
                advance(p);
                tags[count++] = ident_name(p);
            }
            if (count > 0) {
                fd->tags = arena_alloc(p->arena, sizeof(char *) * (size_t)(count + 1));
                for (int i = 0; i < count; i++) fd->tags[i] = tags[i];
                fd->tags[count] = NULL;
                fd->tag_count = count;
            }
        }

        *tail = fd;
        tail = &fd->next;
    } while (match(p, TOK_COMMA));

    return head;
}

/* ── FieldRef parsing (for rules) ────────────────────────── */

static AstFieldRef *parse_field_ref(Parser *p) {
    AstFieldRef *ref = arena_calloc(p->arena, sizeof(AstFieldRef));
    ref->loc = p->next.loc;

    if (peek(p) == TOK_INPUT) {
        advance(p);
        ref->is_output = 0;
    } else if (peek(p) == TOK_OUTPUT) {
        advance(p);
        ref->is_output = 1;
    } else {
        error(p, p->next.loc, "expected 'input' or 'output' in field reference");
        return ref;
    }

    const char *accs[32];
    int count = 0;
    while (peek(p) == TOK_DOT && count < 32) {
        advance(p); /* consume '.' */
        if (peek(p) == TOK_IDENT) {
            advance(p);
            accs[count++] = ident_name(p);
        } else {
            /* keyword as field name */
            advance(p);
            accs[count++] = token_text(p, p->current);
        }
    }

    if (count > 0) {
        ref->accessors = arena_alloc(p->arena, sizeof(char *) * (size_t)(count + 1));
        for (int i = 0; i < count; i++) ref->accessors[i] = accs[i];
        ref->accessors[count] = NULL;
    }
    ref->accessor_count = count;
    return ref;
}

/* ── Contract sections ───────────────────────────────────── */

static AstContract *parse_contract(Parser *p) {
    SrcLoc loc = p->current.loc; /* on 'contract' */

    expect(p, TOK_IDENT);
    if (p->had_error) return NULL;

    AstContract *contract = arena_calloc(p->arena, sizeof(AstContract));
    contract->name = ident_name(p);
    contract->loc = loc;

    /* Optional: uses */
    if (peek(p) == TOK_USES) {
        advance(p);
        const char *names[64];
        int count = 0;
        do {
            expect(p, TOK_IDENT);
            if (p->had_error) return contract;
            names[count++] = ident_name(p);
        } while (match(p, TOK_COMMA));

        contract->uses = arena_alloc(p->arena, sizeof(char *) * (size_t)(count + 1));
        for (int i = 0; i < count; i++) contract->uses[i] = names[i];
        contract->uses[count] = NULL;
        contract->uses_count = count;
    }

    /* Optional: tags */
    if (peek(p) == TOK_TAGS) {
        advance(p);
        AstTagDef *head = NULL;
        AstTagDef **tail = &head;

        do {
            expect(p, TOK_IDENT);
            if (p->had_error) break;
            SrcLoc tloc = p->current.loc;
            const char *tname = ident_name(p);

            const char *desc = NULL;
            if (peek(p) == TOK_STRING) {
                advance(p);
                desc = arena_strndup(p->arena, p->current.start, (size_t)p->current.length);
            }

            AstTagDef *td = arena_calloc(p->arena, sizeof(AstTagDef));
            td->name = tname;
            td->description = desc;
            td->loc = tloc;
            *tail = td;
            tail = &td->next;
        } while (match(p, TOK_COMMA));

        expect(p, TOK_END);
        contract->tags = head;
    }

    /* ── Determine contract kind ──────────────────────────── */
    /* After uses and optional tags, peek to determine:
     *   - 'source' keyword → source contract
     *   - 'input' or 'sanitizers' or 'sources' → function contract
     *   - at 'end' (nothing left) → tag contract (tags only)
     */
    if (peek(p) == TOK_SOURCE) {
        /* Source contract */
        contract->kind = CONTRACT_SOURCE;
        advance(p); /* consume 'source' */

        expect(p, TOK_STRING);
        if (p->had_error) return contract;
        contract->source_name = arena_strndup(p->arena, p->current.start,
                                               (size_t)p->current.length);

        /* Optional: config block */
        if (peek(p) == TOK_CONFIG) {
            advance(p); /* consume 'config' */
            contract->config = parse_config_block(p);
        }

        expect(p, TOK_KEYED_BY);
        if (p->had_error) return contract;
        contract->keyed_by = parse_field_decl_list(p);

        expect(p, TOK_RETURNS);
        if (p->had_error) return contract;
        contract->returns_type = parse_type(p);

        expect(p, TOK_END);
        return contract;
    }

    if (peek(p) == TOK_END && !contract->sanitizers && !contract->input) {
        /* Tag contract — only tags (already parsed above) */
        contract->kind = CONTRACT_TAG;
        expect(p, TOK_END);
        return contract;
    }

    /* Function contract */
    contract->kind = CONTRACT_FUNCTION;

    /* Optional: sanitizers */
    if (peek(p) == TOK_SANITIZERS) {
        advance(p);
        AstSanitizerDef *head = NULL;
        AstSanitizerDef **tail = &head;

        while (peek(p) == TOK_IDENT) {
            SrcLoc sloc = p->next.loc;
            advance(p);
            const char *sname = ident_name(p);

            /* Optional: using <impl-name> */
            const char *impl = NULL;
            if (peek(p) == TOK_USING) {
                advance(p);
                expect(p, TOK_IDENT);
                if (p->had_error) break;
                impl = ident_name(p);
            }

            expect(p, TOK_STRIPS);
            if (p->had_error) break;

            const char *stripped[32];
            int count = 0;
            while (peek(p) == TOK_IDENT && count < 32) {
                advance(p);
                stripped[count++] = ident_name(p);
            }

            AstSanitizerDef *sd = arena_calloc(p->arena, sizeof(AstSanitizerDef));
            sd->name = sname;
            sd->impl_name = impl;
            sd->loc = sloc;
            if (count > 0) {
                sd->stripped_tags = arena_alloc(p->arena, sizeof(char *) * (size_t)(count + 1));
                for (int i = 0; i < count; i++) sd->stripped_tags[i] = stripped[i];
                sd->stripped_tags[count] = NULL;
                sd->stripped_count = count;
            }
            *tail = sd;
            tail = &sd->next;

            match(p, TOK_COMMA);
        }

        expect(p, TOK_END);
        contract->sanitizers = head;
    }

    /* Optional: sources (function contract referencing source contracts) */
    if (peek(p) == TOK_SOURCES) {
        advance(p);
        const char *names[64];
        int count = 0;
        do {
            expect(p, TOK_IDENT);
            if (p->had_error) return contract;
            names[count++] = ident_name(p);
        } while (match(p, TOK_COMMA));

        contract->sources_refs = arena_alloc(p->arena, sizeof(char *) * (size_t)(count + 1));
        for (int i = 0; i < count; i++) contract->sources_refs[i] = names[i];
        contract->sources_refs[count] = NULL;
        contract->sources_count = count;
    }

    /* input spec */
    expect(p, TOK_INPUT);
    if (p->had_error) return contract;
    contract->input = parse_field_decl_list(p);
    expect(p, TOK_END);

    /* output spec */
    expect(p, TOK_OUTPUT);
    if (p->had_error) return contract;
    contract->output = parse_field_decl_list(p);
    expect(p, TOK_END);

    /* Optional: rules */
    if (peek(p) == TOK_RULES) {
        advance(p);
        AstRule *rhead = NULL;
        AstRule **rtail = &rhead;

        while (peek(p) != TOK_END && !p->had_error) {
            SrcLoc rloc = p->next.loc;

            if (peek(p) == TOK_FORBID) {
                advance(p);

                if (peek(p) == TOK_TAGGED) {
                    advance(p);
                    expect(p, TOK_IDENT);
                    if (p->had_error) break;
                    const char *tag = ident_name(p);
                    expect(p, TOK_IN);
                    expect(p, TOK_OUTPUT);

                    AstRule *r = arena_calloc(p->arena, sizeof(AstRule));
                    r->kind = RULE_FORBID_TAGGED;
                    r->loc = rloc;
                    r->as.forbid_tagged.tag_name = tag;
                    *rtail = r;
                    rtail = &r->next;
                } else {
                    AstFieldRef *fref = parse_field_ref(p);
                    expect(p, TOK_IN);
                    expect(p, TOK_OUTPUT);

                    AstRule *r = arena_calloc(p->arena, sizeof(AstRule));
                    r->kind = RULE_FORBID_FIELD;
                    r->loc = rloc;
                    r->as.forbid_field.field_ref = fref;
                    *rtail = r;
                    rtail = &r->next;
                }
            } else if (peek(p) == TOK_REQUIRE) {
                advance(p);
                AstFieldRef *fref = parse_field_ref(p);
                AstExpr *cond = parse_bool_expr(p);

                const char *reject_msg = NULL;
                if (peek(p) == TOK_ELSE) {
                    advance(p);
                    expect(p, TOK_REJECT);
                    expect(p, TOK_STRING);
                    if (!p->had_error) {
                        reject_msg = arena_strndup(p->arena, p->current.start,
                                                   (size_t)p->current.length);
                    }
                }
                expect(p, TOK_END);

                AstRule *r = arena_calloc(p->arena, sizeof(AstRule));
                r->kind = RULE_REQUIRE;
                r->loc = rloc;
                r->as.require.field_ref = fref;
                r->as.require.condition = cond;
                r->as.require.reject_msg = reject_msg;
                *rtail = r;
                rtail = &r->next;
            } else if (peek(p) == TOK_MATCH) {
                advance(p);
                AstFieldRef *fref = parse_field_ref(p);

                AstRuleWhenClause *whead = NULL;
                AstRuleWhenClause **wtail = &whead;

                while (peek(p) == TOK_WHEN) {
                    SrcLoc wloc = p->next.loc;
                    advance(p);
                    AstPattern *pat = parse_pattern(p);
                    AstExpr *guard = NULL;
                    if (peek(p) == TOK_AND) {
                        advance(p);
                        guard = parse_bool_expr(p);
                    }
                    expect(p, TOK_THEN);
                    AstExpr *body = parse_expression(p);

                    AstRuleWhenClause *wc = arena_calloc(p->arena, sizeof(AstRuleWhenClause));
                    wc->pattern = pat;
                    wc->guard = guard;
                    wc->body = body;
                    wc->loc = wloc;
                    *wtail = wc;
                    wtail = &wc->next;
                }

                const char *reject_msg = NULL;
                if (peek(p) == TOK_ELSE) {
                    advance(p);
                    expect(p, TOK_REJECT);
                    expect(p, TOK_STRING);
                    if (!p->had_error) {
                        reject_msg = arena_strndup(p->arena, p->current.start,
                                                   (size_t)p->current.length);
                    }
                }
                expect(p, TOK_END);

                AstRule *r = arena_calloc(p->arena, sizeof(AstRule));
                r->kind = RULE_MATCH;
                r->loc = rloc;
                r->as.match.field_ref = fref;
                r->as.match.clauses = whead;
                r->as.match.reject_msg = reject_msg;
                *rtail = r;
                rtail = &r->next;
            } else {
                error(p, p->next.loc, "expected rule (forbid, require, or match)");
                break;
            }
        }

        expect(p, TOK_END);
        contract->rules = rhead;
    }

    /* Optional: tests */
    if (peek(p) == TOK_TESTS) {
        advance(p);
        AstTestCase *thead = NULL;
        AstTestCase **ttail = &thead;

        while (peek(p) == TOK_TEST) {
            SrcLoc tloc = p->next.loc;
            advance(p);

            expect(p, TOK_STRING);
            if (p->had_error) break;
            const char *tname = arena_strndup(p->arena, p->current.start,
                                              (size_t)p->current.length);

            expect(p, TOK_GIVEN);
            if (p->had_error) break;
            expect(p, TOK_LBRACE);
            if (p->had_error) break;
            AstExpr *given = parse_record_literal(p);

            expect(p, TOK_EXPECT);
            if (p->had_error) break;
            expect(p, TOK_LBRACE);
            if (p->had_error) break;
            AstExpr *expect_rec = parse_record_literal(p);

            expect(p, TOK_END);

            AstTestCase *tc = arena_calloc(p->arena, sizeof(AstTestCase));
            tc->name = tname;
            tc->given = given;
            tc->expect = expect_rec;
            tc->loc = tloc;
            *ttail = tc;
            ttail = &tc->next;
        }

        expect(p, TOK_END);
        contract->tests = thead;
    }

    /* contract end */
    expect(p, TOK_END);
    return contract;
}

/* ── FunctionDef parsing ─────────────────────────────────── */

static AstFunctionDef *parse_function_def(Parser *p) {
    SrcLoc loc = p->current.loc; /* on 'define' */

    expect(p, TOK_IDENT);
    if (p->had_error) return NULL;
    const char *name = ident_name(p);

    expect(p, TOK_WITH);
    expect(p, TOK_INPUT);
    if (p->had_error) return NULL;

    /* Parse body: zero or more let bindings, then 'result' expr 'end' */
    /* Collect let nodes and chain them */
    AstExpr *first_let = NULL;
    AstExpr *last_let = NULL;

    while (peek(p) == TOK_LET) {
        advance(p);
        AstExpr *let_node = parse_let_expr(p);

        if (!first_let) {
            first_let = let_node;
        } else {
            last_let->as.let.body = let_node;
        }
        last_let = let_node;
    }

    expect(p, TOK_RESULT);
    if (p->had_error) return NULL;

    AstExpr *result = parse_expression(p);

    expect(p, TOK_END);

    /* Chain: if we had lets, the last let's body is the result.
     * The function body is the first let. */
    AstExpr *body;
    if (first_let) {
        last_let->as.let.body = result;
        body = first_let;
    } else {
        body = result;
    }

    AstFunctionDef *fn = arena_calloc(p->arena, sizeof(AstFunctionDef));
    fn->name = name;
    fn->body = body;
    fn->loc = loc;
    return fn;
}

/* ── Program (top level) ─────────────────────────────────── */

void parser_init(Parser *p, Lexer *lex, Arena *arena) {
    p->lex = lex;
    p->arena = arena;
    p->had_error = 0;
    p->error.kind = HELUNA_OK;

    /* Prime the lookahead */
    p->next = lexer_next(lex);
    skip_comments(p);

    /* Set current to a dummy token */
    p->current.kind = TOK_EOF;
    p->current.start = "";
    p->current.length = 0;
    p->current.loc = (SrcLoc){ .filename = lex->filename, .line = 0, .col = 0 };
}

AstProgram *parser_parse(Parser *p) {
    AstProgram *prog = arena_calloc(p->arena, sizeof(AstProgram));
    prog->loc = p->next.loc;

    /* Expect contract */
    expect(p, TOK_CONTRACT);
    if (p->had_error) return NULL;
    prog->contract = parse_contract(p);
    if (p->had_error) return NULL;

    /* Function def is required for function contracts, absent for tag/source */
    if (prog->contract->kind == CONTRACT_FUNCTION) {
        expect(p, TOK_DEFINE);
        if (p->had_error) return NULL;
        prog->function = parse_function_def(p);
        if (p->had_error) return NULL;
    }

    /* Should be at EOF (skip trailing comments) */
    if (peek(p) != TOK_EOF) {
        error(p, p->next.loc, "expected end of file, got %s",
              token_kind_name(peek(p)));
    }

    return prog;
}

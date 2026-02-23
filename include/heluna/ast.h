#ifndef HELUNA_AST_H
#define HELUNA_AST_H

#include "heluna/errors.h"
#include <stdio.h>

/* Forward declarations */
typedef struct AstExpr    AstExpr;
typedef struct AstPattern AstPattern;
typedef struct AstType    AstType;

/* ── Expression nodes ─────────────────────────────────────── */

typedef enum {
    EXPR_INTEGER,
    EXPR_FLOAT,
    EXPR_STRING,
    EXPR_TRUE,
    EXPR_FALSE,
    EXPR_NOTHING,
    EXPR_IDENT,
    EXPR_INPUT_REF,
    EXPR_BINARY,
    EXPR_UNARY_NEG,
    EXPR_NOT,
    EXPR_IF,
    EXPR_MATCH,
    EXPR_LET,
    EXPR_FILTER,
    EXPR_MAP,
    EXPR_THROUGH,
    EXPR_CALL,
    EXPR_RECORD,
    EXPR_LIST,
    EXPR_ACCESS,
    EXPR_PAREN,
    EXPR_LOOKUP,
} AstExprKind;

typedef enum {
    BIN_ADD, BIN_SUB, BIN_MUL, BIN_DIV, BIN_MOD,
    BIN_EQ, BIN_NEQ, BIN_LT, BIN_GT, BIN_LTE, BIN_GTE,
    BIN_AND, BIN_OR,
} AstBinOp;

typedef struct AstLabel {
    const char      *name;
    AstExpr         *value;
    struct AstLabel *next;
    SrcLoc           loc;
} AstLabel;

typedef struct AstLookupKey {
    const char          *name;
    AstExpr             *value;
    struct AstLookupKey *next;
    SrcLoc               loc;
} AstLookupKey;

typedef struct AstWhenClause {
    AstPattern           *pattern;
    AstExpr              *guard;    /* NULL if no guard */
    AstExpr              *body;
    struct AstWhenClause *next;
    SrcLoc                loc;
} AstWhenClause;

typedef struct AstIfBranch {
    AstExpr             *condition;  /* NULL for final else */
    AstExpr             *body;
    struct AstIfBranch  *next;
    SrcLoc               loc;
} AstIfBranch;

struct AstExpr {
    AstExprKind kind;
    SrcLoc      loc;
    AstExpr    *next;   /* for use in linked lists */

    union {
        /* EXPR_INTEGER */
        long long integer_val;

        /* EXPR_FLOAT */
        double float_val;

        /* EXPR_STRING — points into arena, includes quotes */
        struct { const char *value; int length; } string_val;

        /* EXPR_IDENT */
        struct { const char *name; } ident;

        /* EXPR_INPUT_REF — name without the $ prefix */
        struct { const char *name; } input_ref;

        /* EXPR_BINARY */
        struct { AstBinOp op; AstExpr *left; AstExpr *right; } binary;

        /* EXPR_UNARY_NEG */
        struct { AstExpr *operand; } unary;

        /* EXPR_NOT */
        struct { AstExpr *operand; } not_expr;

        /* EXPR_IF — linked list of branches */
        struct { AstIfBranch *branches; } if_expr;

        /* EXPR_MATCH */
        struct {
            AstExpr       *subject;
            AstWhenClause *clauses;
            AstExpr       *else_body; /* NULL if no else */
        } match;

        /* EXPR_LET */
        struct { const char *name; AstExpr *binding; AstExpr *body; } let;

        /* EXPR_FILTER */
        struct {
            AstExpr    *list;
            const char *var_name;
            AstExpr    *predicate;
        } filter;

        /* EXPR_MAP */
        struct {
            AstExpr    *list;
            const char *var_name;
            AstExpr    *body;
        } map;

        /* EXPR_THROUGH */
        struct { AstExpr *left; AstExpr *right; } through;

        /* EXPR_CALL */
        struct { const char *name; AstExpr *arg; } call;

        /* EXPR_RECORD */
        struct { AstLabel *labels; } record;

        /* EXPR_LIST */
        struct { AstExpr *elements; } list;

        /* EXPR_ACCESS */
        struct { AstExpr *object; const char *field; } access;

        /* EXPR_PAREN */
        struct { AstExpr *inner; } paren;

        /* EXPR_LOOKUP */
        struct {
            const char   *source_name;
            AstLookupKey *keys;
        } lookup;
    } as;
};

/* ── Pattern nodes ────────────────────────────────────────── */

typedef enum {
    PAT_LITERAL,
    PAT_WILDCARD,
    PAT_BINDING,
    PAT_RANGE,
    PAT_LIST,
    PAT_RECORD,
} AstPatternKind;

typedef struct AstFieldPattern {
    const char           *name;
    AstPattern           *pattern;
    struct AstFieldPattern *next;
    SrcLoc                loc;
} AstFieldPattern;

typedef struct AstPatternElem {
    AstPattern            *pattern;
    struct AstPatternElem *next;
} AstPatternElem;

struct AstPattern {
    AstPatternKind kind;
    SrcLoc         loc;

    union {
        /* PAT_LITERAL — reuse an AstExpr for the literal value */
        struct { AstExpr *value; } literal;

        /* PAT_BINDING */
        struct { const char *name; } binding;

        /* PAT_RANGE */
        struct { AstExpr *low; AstExpr *high; } range;

        /* PAT_LIST */
        struct {
            AstPatternElem *elements;
            const char     *rest_name; /* NULL if no ..rest; "" if just .. */
        } list;

        /* PAT_RECORD */
        struct { AstFieldPattern *fields; } record;
    } as;
};

/* ── Type nodes ───────────────────────────────────────────── */

typedef enum {
    TYPE_STRING,
    TYPE_INTEGER,
    TYPE_FLOAT,
    TYPE_BOOLEAN,
    TYPE_MAYBE,
    TYPE_LIST,
    TYPE_RECORD,
} AstTypeKind;

typedef struct AstFieldDecl {
    const char        *name;
    AstType           *type;
    const char       **tags;     /* NULL-terminated array, or NULL */
    int                tag_count;
    struct AstFieldDecl *next;
    SrcLoc              loc;
} AstFieldDecl;

struct AstType {
    AstTypeKind kind;
    SrcLoc      loc;

    union {
        /* TYPE_MAYBE */
        struct { AstType *inner; } maybe;

        /* TYPE_LIST */
        struct { AstType *inner; } list;

        /* TYPE_RECORD */
        struct { AstFieldDecl *fields; } record;
    } as;
};

/* ── Contract kinds ───────────────────────────────────────── */

typedef enum {
    CONTRACT_FUNCTION,   /* input/output/rules/tests */
    CONTRACT_TAG,        /* tags only — reusable security vocabulary */
    CONTRACT_SOURCE,     /* source/keyed-by/returns — external data dependency */
} AstContractKind;

/* ── Contract-level declarations ──────────────────────────── */

typedef struct AstTagDef {
    const char       *name;
    const char       *description; /* NULL if none */
    struct AstTagDef *next;
    SrcLoc            loc;
} AstTagDef;

typedef struct AstSanitizerDef {
    const char            *name;
    const char           **stripped_tags; /* NULL-terminated */
    int                    stripped_count;
    struct AstSanitizerDef *next;
    SrcLoc                  loc;
} AstSanitizerDef;

typedef struct AstFieldRef {
    int         is_output; /* 0 = input, 1 = output */
    const char **accessors;
    int          accessor_count;
    SrcLoc       loc;
} AstFieldRef;

typedef enum {
    RULE_FORBID_FIELD,
    RULE_FORBID_TAGGED,
    RULE_REQUIRE,
    RULE_MATCH,
} AstRuleKind;

typedef struct AstRuleWhenClause {
    AstPattern                *pattern;
    AstExpr                   *guard;
    AstExpr                   *body;
    struct AstRuleWhenClause  *next;
    SrcLoc                     loc;
} AstRuleWhenClause;

typedef struct AstRule {
    AstRuleKind    kind;
    SrcLoc         loc;
    struct AstRule *next;

    union {
        /* RULE_FORBID_FIELD */
        struct { AstFieldRef *field_ref; } forbid_field;

        /* RULE_FORBID_TAGGED */
        struct { const char *tag_name; } forbid_tagged;

        /* RULE_REQUIRE */
        struct {
            AstFieldRef *field_ref;
            AstExpr     *condition;
            const char  *reject_msg; /* NULL if no else reject */
        } require;

        /* RULE_MATCH */
        struct {
            AstFieldRef       *field_ref;
            AstRuleWhenClause *clauses;
            const char        *reject_msg; /* NULL if no else reject */
        } match;
    } as;
} AstRule;

typedef struct AstTestCase {
    const char         *name;
    AstExpr            *given;     /* record literal */
    AstExpr            *expect;    /* record literal */
    struct AstTestCase *next;
    SrcLoc              loc;
} AstTestCase;

typedef struct AstContract {
    const char       *name;
    AstContractKind   kind;
    const char      **uses;       /* NULL-terminated, or NULL */
    int               uses_count;
    AstTagDef        *tags;

    /* Function contract fields */
    AstSanitizerDef  *sanitizers;
    const char      **sources_refs;  /* NULL-terminated source names, or NULL */
    int               sources_count;
    AstFieldDecl     *input;
    AstFieldDecl     *output;
    AstRule          *rules;
    AstTestCase      *tests;

    /* Source contract fields */
    const char       *source_name;   /* the collection name string */
    AstFieldDecl     *keyed_by;      /* key field declarations */
    AstType          *returns_type;  /* return type */

    SrcLoc            loc;
} AstContract;

typedef struct AstFunctionDef {
    const char *name;
    AstExpr    *body;       /* nested lets wrapping result expr */
    SrcLoc      loc;
} AstFunctionDef;

typedef struct AstProgram {
    AstContract    *contract;
    AstFunctionDef *function;
    SrcLoc          loc;
} AstProgram;

/* ── AST printer ──────────────────────────────────────────── */

void ast_print(const AstProgram *prog, FILE *out);

#endif /* HELUNA_AST_H */

#ifndef HELUNA_EVALUATOR_H
#define HELUNA_EVALUATOR_H

#include "heluna/ast.h"
#include "heluna/arena.h"
#include "heluna/errors.h"

/* ── Value representation ────────────────────────────────── */

typedef enum {
    VAL_INTEGER,
    VAL_FLOAT,
    VAL_STRING,
    VAL_BOOLEAN,
    VAL_NOTHING,
    VAL_RECORD,
    VAL_LIST,
} HValKind;

typedef struct HVal   HVal;
typedef struct HField HField;

struct HField {
    const char *name;
    HVal       *value;
    HField     *next;
};

struct HVal {
    HValKind kind;
    HVal    *next;   /* for list membership */

    union {
        long long    integer_val;
        double       float_val;
        const char  *string_val;   /* no quotes, escapes resolved */
        int          boolean_val;  /* 0 or 1 */
        HField      *record_fields;
        HVal        *list_head;
    } as;
};

/* ── Environment ─────────────────────────────────────────── */

typedef struct {
    const char *name;
    HVal       *value;
} EnvEntry;

/* ── Evaluator ───────────────────────────────────────────── */

typedef struct {
    Arena            *arena;
    const AstProgram *prog;

    /* Environment: flat array with mark/restore */
    EnvEntry *env;
    int       env_count;
    int       env_capacity;

    /* Input record for $field-name lookups */
    HVal *input_record;

    /* Error state */
    HelunaError error;
    int         had_error;
} Evaluator;

/* Initialize an evaluator for a parsed+checked program. */
void evaluator_init(Evaluator *ev, const AstProgram *prog, Arena *arena);

/* Evaluate the function body with the given input record.
 * Returns the result value, or NULL on runtime error. */
HVal *evaluator_eval(Evaluator *ev, HVal *input);

/* Deep equality comparison. Returns 1 if equal, 0 if not. */
int hval_equal(const HVal *a, const HVal *b);

/* ── Stdlib dispatch (defined in stdlib.c) ───────────────── */

HVal *stdlib_call(const char *name, HVal *arg, Arena *arena, HelunaError *err);

#endif /* HELUNA_EVALUATOR_H */

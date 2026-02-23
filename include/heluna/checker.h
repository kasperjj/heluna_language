#ifndef HELUNA_CHECKER_H
#define HELUNA_CHECKER_H

#include "heluna/ast.h"
#include "heluna/arena.h"
#include "heluna/errors.h"

typedef struct {
    HelunaError *errors;
    int          count;
    int          capacity;
    Arena       *arena;
} CheckerErrors;

typedef enum {
    SCOPE_INPUT,
    SCOPE_LET,
    SCOPE_FILTER_VAR,
    SCOPE_MAP_VAR,
    SCOPE_MATCH_BINDING,
} ScopeEntryKind;

typedef struct {
    const char     *name;
    ScopeEntryKind  kind;
    SrcLoc          loc;
} ScopeEntry;

typedef struct {
    const AstProgram *prog;
    Arena            *arena;
    CheckerErrors     errors;
    ScopeEntry       *scope;
    int               scope_count;
    int               scope_capacity;
} Checker;

/* Initialize a checker for a parsed program. */
void checker_init(Checker *c, const AstProgram *prog, Arena *arena);

/* Run all checks. Returns 0 on success, error count on failure. */
int  checker_check(Checker *c);

#endif /* HELUNA_CHECKER_H */

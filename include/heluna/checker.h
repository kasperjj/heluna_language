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

/* ── Dependency graph for acyclicity checking ────────────── */

typedef struct {
    const char       *name;
    const char      **deps;     /* names this contract depends on */
    int               dep_count;
} DepGraphNode;

typedef struct {
    DepGraphNode *nodes;
    int           count;
} DepGraph;

typedef struct {
    const AstProgram *prog;
    Arena            *arena;
    CheckerErrors     errors;
    ScopeEntry       *scope;
    int               scope_count;
    int               scope_capacity;
    const DepGraph   *deps;     /* NULL if no dependency graph */
} Checker;

/* Initialize a checker for a parsed program. */
void checker_init(Checker *c, const AstProgram *prog, Arena *arena);

/* Initialize with dependency graph for acyclicity checking. */
void checker_init_with_deps(Checker *c, const AstProgram *prog,
                            Arena *arena, const DepGraph *deps);

/* Run all checks. Returns 0 on success, error count on failure. */
int  checker_check(Checker *c);

#endif /* HELUNA_CHECKER_H */

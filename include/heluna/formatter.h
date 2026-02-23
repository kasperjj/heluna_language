#ifndef HELUNA_FORMATTER_H
#define HELUNA_FORMATTER_H

#include "heluna/ast.h"
#include <stdio.h>

/*
 * Format an AST back to canonical Heluna source.
 *
 * Known limitation: comments are discarded during parsing, so the
 * formatter cannot preserve them.
 */
void heluna_format(const AstProgram *prog, FILE *out);

#endif /* HELUNA_FORMATTER_H */

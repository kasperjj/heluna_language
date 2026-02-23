#ifndef HELUNA_JSON_H
#define HELUNA_JSON_H

#include "heluna/evaluator.h"
#include <stdio.h>

/* Parse a JSON string into an HVal tree. Returns NULL on error. */
HVal *json_parse(Arena *arena, const char *input, HelunaError *err);

/* Emit an HVal as compact JSON to the given file stream. */
void json_emit(const HVal *val, FILE *out);

#endif /* HELUNA_JSON_H */

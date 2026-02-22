#include "heluna/errors.h"
#include <stdio.h>
#include <stdarg.h>

static const char *kind_label(HelunaErrorKind kind) {
    switch (kind) {
    case HELUNA_OK:           return "ok";
    case HELUNA_ERR_SYNTAX:   return "syntax error";
    case HELUNA_ERR_TYPE:     return "type error";
    case HELUNA_ERR_CONTRACT: return "contract error";
    case HELUNA_ERR_RUNTIME:  return "runtime error";
    case HELUNA_ERR_TAG:      return "tag violation";
    case HELUNA_ERR_IO:       return "i/o error";
    }
    return "error";
}

void heluna_error_print(const HelunaError *err) {
    if (err->loc.filename) {
        fprintf(stderr, "%s:%d:%d: %s: %s\n",
                err->loc.filename, err->loc.line, err->loc.col,
                kind_label(err->kind), err->message);
    } else {
        fprintf(stderr, "%s: %s\n", kind_label(err->kind), err->message);
    }
}

void heluna_error_set(HelunaError *err, HelunaErrorKind kind, SrcLoc loc,
                      const char *fmt, ...) {
    err->kind = kind;
    err->loc = loc;

    va_list ap;
    va_start(ap, fmt);
    vsnprintf(err->message, sizeof(err->message), fmt, ap);
    va_end(ap);
}

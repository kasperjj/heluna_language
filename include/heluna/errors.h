#ifndef HELUNA_ERRORS_H
#define HELUNA_ERRORS_H

/*
 * Source location tracking and error reporting.
 *
 * All errors carry a location (file, line, column) so tools can
 * produce useful diagnostics.
 */

typedef struct {
    const char *filename;
    int         line;
    int         col;
} SrcLoc;

typedef enum {
    HELUNA_OK = 0,
    HELUNA_ERR_SYNTAX,
    HELUNA_ERR_TYPE,
    HELUNA_ERR_CONTRACT,
    HELUNA_ERR_RUNTIME,
    HELUNA_ERR_TAG,
    HELUNA_ERR_IO,
} HelunaErrorKind;

typedef struct {
    HelunaErrorKind kind;
    SrcLoc          loc;
    char            message[512];
} HelunaError;

/* Format and print an error to stderr. */
void heluna_error_print(const HelunaError *err);

/* Convenience: fill an error struct with a formatted message. */
void heluna_error_set(HelunaError *err, HelunaErrorKind kind, SrcLoc loc,
                      const char *fmt, ...);

#endif /* HELUNA_ERRORS_H */

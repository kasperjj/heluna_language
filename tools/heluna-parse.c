/*
 * heluna-parse: parse a Heluna source file and dump the AST.
 *
 * Usage: heluna-parse <file.heluna>
 *
 * TODO: implement parser and AST printer.
 */

#include "heluna/arena.h"
#include <stdio.h>

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: heluna-parse <file.heluna>\n");
        return 1;
    }

    (void)argv;
    Arena *arena = arena_create(64 * 1024);

    fprintf(stderr, "heluna-parse: not yet implemented\n");

    arena_destroy(arena);
    return 1;
}

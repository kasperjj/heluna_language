/*
 * heluna-check: validate a Heluna contract and function without running it.
 *
 * Usage: heluna-check <file.heluna>
 *
 * TODO: implement contract validation.
 */

#include "heluna/arena.h"
#include <stdio.h>

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: heluna-check <file.heluna>\n");
        return 1;
    }

    (void)argv;
    Arena *arena = arena_create(64 * 1024);

    fprintf(stderr, "heluna-check: not yet implemented\n");

    arena_destroy(arena);
    return 1;
}

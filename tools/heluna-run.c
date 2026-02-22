/*
 * heluna-run: execute a Heluna function with JSON input.
 *
 * Usage: heluna-run <file.heluna> [input.json]
 *
 * If input.json is omitted, reads from stdin.
 *
 * TODO: implement evaluator.
 */

#include "heluna/arena.h"
#include <stdio.h>

int main(int argc, char **argv) {
    if (argc < 2 || argc > 3) {
        fprintf(stderr, "usage: heluna-run <file.heluna> [input.json]\n");
        return 1;
    }

    (void)argv;
    Arena *arena = arena_create(64 * 1024);

    fprintf(stderr, "heluna-run: not yet implemented\n");

    arena_destroy(arena);
    return 1;
}

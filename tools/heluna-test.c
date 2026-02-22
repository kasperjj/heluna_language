/*
 * heluna-test: run embedded test cases from a Heluna contract.
 *
 * Usage: heluna-test <file.heluna>
 *
 * TODO: implement test runner.
 */

#include "heluna/arena.h"
#include <stdio.h>

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: heluna-test <file.heluna>\n");
        return 1;
    }

    (void)argv;
    Arena *arena = arena_create(64 * 1024);

    fprintf(stderr, "heluna-test: not yet implemented\n");

    arena_destroy(arena);
    return 1;
}

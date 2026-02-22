#ifndef HELUNA_ARENA_H
#define HELUNA_ARENA_H

#include <stddef.h>

/*
 * Simple arena (bump) allocator.
 *
 * Heluna programs are short-lived transformations: allocate a lot of
 * AST nodes and JSON values, produce a result, then tear everything
 * down. An arena makes this pattern fast and leak-free — allocate by
 * bumping a pointer, free everything in one shot.
 *
 * Usage:
 *   Arena *a = arena_create(64 * 1024);  // 64 KB initial block
 *   Token *t = arena_alloc(a, sizeof(Token));
 *   // ... use t, never free it individually ...
 *   arena_destroy(a);                     // frees everything
 */

typedef struct ArenaBlock {
    struct ArenaBlock *next;
    size_t             size;
    size_t             used;
    char               data[];   /* flexible array member */
} ArenaBlock;

typedef struct Arena {
    ArenaBlock *current;
    ArenaBlock *first;
    size_t      default_block_size;
} Arena;

/* Create a new arena. block_size is the initial (and default) block size. */
Arena *arena_create(size_t block_size);

/* Allocate `size` bytes, aligned to 8 bytes. Never returns NULL —
 * aborts on out-of-memory. */
void *arena_alloc(Arena *a, size_t size);

/* Convenience: allocate and zero-fill. */
void *arena_calloc(Arena *a, size_t size);

/* Duplicate a string into the arena. */
char *arena_strdup(Arena *a, const char *s);

/* Duplicate `n` bytes of a string (and null-terminate). */
char *arena_strndup(Arena *a, const char *s, size_t n);

/* Free all memory owned by the arena. */
void arena_destroy(Arena *a);

#endif /* HELUNA_ARENA_H */

#include "heluna/arena.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define ARENA_ALIGNMENT 8

static size_t align_up(size_t n, size_t align) {
    return (n + align - 1) & ~(align - 1);
}

static ArenaBlock *block_create(size_t data_size) {
    ArenaBlock *b = malloc(sizeof(ArenaBlock) + data_size);
    if (!b) {
        fprintf(stderr, "heluna: out of memory (requested %zu bytes)\n", data_size);
        abort();
    }
    b->next = NULL;
    b->size = data_size;
    b->used = 0;
    return b;
}

Arena *arena_create(size_t block_size) {
    if (block_size == 0) block_size = 64 * 1024;

    Arena *a = malloc(sizeof(Arena));
    if (!a) {
        fprintf(stderr, "heluna: out of memory\n");
        abort();
    }

    ArenaBlock *first = block_create(block_size);
    a->current = first;
    a->first = first;
    a->default_block_size = block_size;
    return a;
}

void *arena_alloc(Arena *a, size_t size) {
    size = align_up(size, ARENA_ALIGNMENT);

    /* Try current block */
    ArenaBlock *b = a->current;
    if (b->used + size <= b->size) {
        void *ptr = b->data + b->used;
        b->used += size;
        return ptr;
    }

    /* Need a new block — at least big enough for this allocation */
    size_t new_size = a->default_block_size;
    if (size > new_size) new_size = size;

    ArenaBlock *nb = block_create(new_size);
    b->next = nb;
    a->current = nb;

    void *ptr = nb->data;
    nb->used = size;
    return ptr;
}

void *arena_calloc(Arena *a, size_t size) {
    void *ptr = arena_alloc(a, size);
    memset(ptr, 0, size);
    return ptr;
}

char *arena_strdup(Arena *a, const char *s) {
    size_t len = strlen(s);
    char *dup = arena_alloc(a, len + 1);
    memcpy(dup, s, len + 1);
    return dup;
}

char *arena_strndup(Arena *a, const char *s, size_t n) {
    char *dup = arena_alloc(a, n + 1);
    memcpy(dup, s, n);
    dup[n] = '\0';
    return dup;
}

void arena_destroy(Arena *a) {
    if (!a) return;

    ArenaBlock *b = a->first;
    while (b) {
        ArenaBlock *next = b->next;
        free(b);
        b = next;
    }
    free(a);
}

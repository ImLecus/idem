#ifndef IDEM_ARENA_H
#define IDEM_ARENA_H

#include <stddef.h>

typedef union ArenaAlign {
    double d;
    void *p;
    long l;
} ArenaAlign;

typedef struct ArenaBlock {
    struct ArenaBlock *next;
    size_t used;
    size_t cap;
    ArenaAlign data[1];
} ArenaBlock;

typedef struct Arena {
    ArenaBlock *head;
} Arena;

void arena_init(Arena *a);
void *arena_alloc(Arena *a, size_t size);
char *arena_strndup(Arena *a, const char *s, size_t n);
void arena_free(Arena *a);

#endif /* IDEM_ARENA_H */

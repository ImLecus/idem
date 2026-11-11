#include "arena.h"

#include <stdlib.h>
#include <string.h>

#define IDEM_ARENA_BLOCK_UNITS 512u

void arena_init(Arena *a)
{
    a->head = NULL;
}

void *arena_alloc(Arena *a, size_t size)
{
    ArenaBlock *b;
    size_t units;
    size_t cap;
    void *p;

    if (size == 0) {
        size = 1;
    }

    units = (size + sizeof(ArenaAlign) - 1) / sizeof(ArenaAlign);
    if (units == 0) {
        units = 1;
    }

    if (a->head == NULL || units > a->head->cap - a->head->used) {
        cap = IDEM_ARENA_BLOCK_UNITS;
        if (cap < units) {
            cap = units;
        }

        b = (ArenaBlock *)malloc(sizeof(ArenaBlock) +
                                 (cap - 1) * sizeof(ArenaAlign));
        if (b == NULL) {
            return NULL;
        }
        b->next = a->head;
        b->used = 0;
        b->cap = cap;
        a->head = b;
    }

    p = (void *)&a->head->data[a->head->used];
    a->head->used += units;
    return p;
}

char *arena_strndup(Arena *a, const char *s, size_t n)
{
    char *p;

    p = (char *)arena_alloc(a, n + 1);
    if (p == NULL) {
        return NULL;
    }
    if (n != 0) {
        memcpy(p, s, n);
    }
    p[n] = '\0';
    return p;
}

void arena_free(Arena *a)
{
    ArenaBlock *b;
    ArenaBlock *next;

    b = a->head;
    while (b != NULL) {
        next = b->next;
        free(b);
        b = next;
    }
    a->head = NULL;
}

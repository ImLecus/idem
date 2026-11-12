#ifndef IDEM_CODEGEN_H
#define IDEM_CODEGEN_H

#include <stddef.h>

#include "arena.h"
#include "ir.h"

typedef struct CgTypeDesc {
    Type *type;
    int id;
    struct CgTypeDesc *next;
} CgTypeDesc;

typedef struct Codegen {
    IrProgram *program;
    Arena *arena;
    int had_error;
    char error_message[512];
    int next_label;
    CgTypeDesc *type_descs;
    int next_type_desc;
} Codegen;

void codegen_init(Codegen *codegen, IrProgram *program, Arena *arena);
int codegen_emit(Codegen *codegen, const char *path);

int codegen_had_error(const Codegen *codegen);
const char *codegen_error_message(const Codegen *codegen);

#endif /* IDEM_CODEGEN_H */

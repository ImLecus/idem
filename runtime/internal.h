#ifndef IDEM_INTERNAL_H
#define IDEM_INTERNAL_H

#include <stddef.h>

#include "value.h"

/* ------------------------------------------------------------------ */
/* Heap objects                                                        */
/* ------------------------------------------------------------------ */

typedef enum IdemObjKind {
    IDEM_OBJ_STRING = 1,
    IDEM_OBJ_BYTES,
    IDEM_OBJ_ARRAY,
    IDEM_OBJ_LIST,
    IDEM_OBJ_MAP,
    IDEM_OBJ_TUPLE,
    IDEM_OBJ_STRUCT,
    IDEM_OBJ_ENUM,
    IDEM_OBJ_CLOSURE
} IdemObjKind;

typedef struct IdemObj {
    unsigned int mark;
    unsigned int kind;
    struct IdemObj *next;
} IdemObj;

typedef struct IdemStringObj {
    IdemObj header;
    size_t length;
    char *data;
} IdemStringObj;

typedef struct IdemSeqObj {
    IdemObj header;
    const IdemTypeDesc *type;
    size_t count;
    Value *items;
} IdemSeqObj;

typedef struct IdemMapObj {
    IdemObj header;
    const IdemTypeDesc *type;
    size_t count;
    Value *keys;
    Value *values;
} IdemMapObj;

typedef struct IdemStructObj {
    IdemObj header;
    const IdemTypeDesc *type;
    size_t count;
    Value *fields;
} IdemStructObj;

typedef struct IdemEnumObj {
    IdemObj header;
    const IdemTypeDesc *type;
    unsigned int variant;
    size_t count;
    Value *payload;
} IdemEnumObj;

typedef struct IdemClosureObj {
    IdemObj header;
    const IdemTypeDesc *type;
    IdemFnPtr fn;
    size_t count;
    Value *env;
} IdemClosureObj;

/* ------------------------------------------------------------------ */
/* Allocation                                                          */
/* ------------------------------------------------------------------ */

void *idem_obj_alloc(size_t size, unsigned int obj_kind);
void idem_copy_values(Value *dst, const Value *src, size_t count);

/* ------------------------------------------------------------------ */
/* String and byte helpers                                             */
/* ------------------------------------------------------------------ */

int idem_utf8_validate(const unsigned char *data, size_t len);
int idem_utf8_decode(const unsigned char *data, size_t len,
                     size_t *consumed, unsigned long *codepoint);
int idem_utf8_encode(unsigned long codepoint, char *out, size_t *out_len);
int idem_bytes_compare_raw(const char *a, size_t a_len,
                           const char *b, size_t b_len);

/* ------------------------------------------------------------------ */
/* Growable byte buffer for rendering                                  */
/* ------------------------------------------------------------------ */

typedef struct IdemBuf {
    char *data;
    size_t len;
    size_t cap;
} IdemBuf;

void idem_buf_init(IdemBuf *buf);
void idem_buf_free(IdemBuf *buf);
void idem_buf_putc(IdemBuf *buf, char c);
void idem_buf_write(IdemBuf *buf, const char *data, size_t len);
void idem_buf_puts(IdemBuf *buf, const char *text);
void idem_buf_int(IdemBuf *buf, IdemInt value);
void idem_buf_float(IdemBuf *buf, double value);

/* ------------------------------------------------------------------ */
/* Rendering and conversions                                           */
/* ------------------------------------------------------------------ */

void idem_render(IdemBuf *buf, Value value);
Value idem_string_from_buf(IdemBuf *buf);
Value idem_seq_new(IdemTag tag, Value *items, size_t count,
                   const IdemTypeDesc *type);
Value idem_error_value(unsigned int variant, Value payload);
Value idem_error_io(const char *message);

#endif /* IDEM_INTERNAL_H */

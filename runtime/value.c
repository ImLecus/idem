#include "internal.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* 64-bit integer assumption                                           */
/* ------------------------------------------------------------------ */

#if (LONG_MAX <= 2147483647L)
#error "Idem runtime requires a C implementation with 64-bit long"
#endif

#define IDEM_INT_MAX 9223372036854775807L
#define IDEM_INT_MIN (-IDEM_INT_MAX - 1L)

/* ------------------------------------------------------------------ */
/* Garbage collector                                                   */
/* ------------------------------------------------------------------ */

typedef struct IdemFrame {
    Value *slots;
    size_t count;
    struct IdemFrame *prev;
} IdemFrame;

static IdemObj *idem_objects = NULL;
static IdemFrame *idem_frames = NULL;
static unsigned long idem_alloc_since_gc = 0;
static unsigned long idem_gc_limit = 2048;

void idem_panic_str(const char *message)
{
    fflush(stdout);
    fprintf(stderr, "panic: %s\n", message);
    exit(101);
}

void idem_panic_oom(void)
{
    idem_panic_str("out of memory");
}

void idem_runtime_init(void)
{
    idem_objects = NULL;
    idem_frames = NULL;
    idem_alloc_since_gc = 0;
    idem_gc_limit = 2048;
}

void idem_gc_push_frame(Value *slots, size_t count)
{
    IdemFrame *frame;
    frame = (IdemFrame *)malloc(sizeof(IdemFrame));
    if (frame == NULL) {
        idem_panic_oom();
    }
    frame->slots = slots;
    frame->count = count;
    frame->prev = idem_frames;
    idem_frames = frame;
}

void idem_gc_pop_frame(void)
{
    IdemFrame *frame;
    frame = idem_frames;
    if (frame != NULL) {
        idem_frames = frame->prev;
        free(frame);
    }
}

static void idem_mark_object(IdemObj *obj);

static void idem_mark_value(Value value)
{
    if ((value.tag == IDEM_TAG_STRING ||
         value.tag == IDEM_TAG_BYTES ||
         value.tag == IDEM_TAG_ARRAY ||
         value.tag == IDEM_TAG_LIST ||
         value.tag == IDEM_TAG_MAP ||
         value.tag == IDEM_TAG_TUPLE ||
         value.tag == IDEM_TAG_STRUCT ||
         value.tag == IDEM_TAG_ENUM ||
         value.tag == IDEM_TAG_CLOSURE) &&
        value.as.obj != NULL) {
        idem_mark_object((IdemObj *)value.as.obj);
    }
}

static void idem_mark_object(IdemObj *obj)
{
    size_t i;

    if (obj == NULL || obj->mark) {
        return;
    }
    obj->mark = 1;

    switch (obj->kind) {
    case IDEM_OBJ_STRING:
    case IDEM_OBJ_BYTES:
        break;
    case IDEM_OBJ_ARRAY:
    case IDEM_OBJ_LIST:
    case IDEM_OBJ_TUPLE: {
        IdemSeqObj *seq;
        seq = (IdemSeqObj *)obj;
        for (i = 0; i < seq->count; i++) {
            idem_mark_value(seq->items[i]);
        }
        break;
    }
    case IDEM_OBJ_MAP: {
        IdemMapObj *map;
        map = (IdemMapObj *)obj;
        for (i = 0; i < map->count; i++) {
            idem_mark_value(map->keys[i]);
            idem_mark_value(map->values[i]);
        }
        break;
    }
    case IDEM_OBJ_STRUCT: {
        IdemStructObj *st;
        st = (IdemStructObj *)obj;
        for (i = 0; i < st->count; i++) {
            idem_mark_value(st->fields[i]);
        }
        break;
    }
    case IDEM_OBJ_ENUM: {
        IdemEnumObj *en;
        en = (IdemEnumObj *)obj;
        for (i = 0; i < en->count; i++) {
            idem_mark_value(en->payload[i]);
        }
        break;
    }
    case IDEM_OBJ_CLOSURE: {
        IdemClosureObj *cl;
        cl = (IdemClosureObj *)obj;
        for (i = 0; i < cl->count; i++) {
            idem_mark_value(cl->env[i]);
        }
        break;
    }
    default:
        break;
    }
}

static void idem_free_object(IdemObj *obj)
{
    switch (obj->kind) {
    case IDEM_OBJ_STRING:
    case IDEM_OBJ_BYTES:
        free(((IdemStringObj *)obj)->data);
        break;
    case IDEM_OBJ_ARRAY:
    case IDEM_OBJ_LIST:
    case IDEM_OBJ_TUPLE:
        free(((IdemSeqObj *)obj)->items);
        break;
    case IDEM_OBJ_MAP:
        free(((IdemMapObj *)obj)->keys);
        free(((IdemMapObj *)obj)->values);
        break;
    case IDEM_OBJ_STRUCT:
        free(((IdemStructObj *)obj)->fields);
        break;
    case IDEM_OBJ_ENUM:
        free(((IdemEnumObj *)obj)->payload);
        break;
    case IDEM_OBJ_CLOSURE:
        free(((IdemClosureObj *)obj)->env);
        break;
    default:
        break;
    }
    free(obj);
}

void idem_gc_collect(void)
{
    IdemFrame *frame;
    IdemObj *obj;
    IdemObj *next;
    IdemObj **link;

    for (frame = idem_frames; frame != NULL; frame = frame->prev) {
        size_t i;
        for (i = 0; i < frame->count; i++) {
            idem_mark_value(frame->slots[i]);
        }
    }

    link = &idem_objects;
    while (*link != NULL) {
        obj = *link;
        if (obj->mark) {
            obj->mark = 0;
            link = &obj->next;
        } else {
            next = obj->next;
            *link = next;
            idem_free_object(obj);
        }
    }

    idem_alloc_since_gc = 0;
}

void *idem_obj_alloc(size_t size, unsigned int obj_kind)
{
    IdemObj *obj;

    if (idem_alloc_since_gc >= idem_gc_limit) {
        idem_gc_collect();
        if (idem_gc_limit < 65536) {
            idem_gc_limit *= 2;
        }
    }

    obj = (IdemObj *)malloc(size);
    if (obj == NULL) {
        idem_gc_collect();
        obj = (IdemObj *)malloc(size);
        if (obj == NULL) {
            idem_panic_oom();
        }
    }
    memset(obj, 0, size);
    obj->kind = obj_kind;
    obj->next = idem_objects;
    idem_objects = obj;
    idem_alloc_since_gc += 1;
    return obj;
}

void idem_copy_values(Value *dst, const Value *src, size_t count)
{
    size_t i;
    for (i = 0; i < count; i++) {
        dst[i] = src[i];
    }
}

/* ------------------------------------------------------------------ */
/* Constructors                                                        */
/* ------------------------------------------------------------------ */

static Value idem_make(IdemTag tag)
{
    Value value;
    value.tag = tag;
    value.as.obj = NULL;
    return value;
}

Value idem_unit(void)
{
    return idem_make(IDEM_TAG_UNIT);
}

Value idem_bool(int value)
{
    Value result;
    result = idem_make(IDEM_TAG_BOOL);
    result.as.i = value != 0;
    return result;
}

Value idem_int(IdemInt value)
{
    Value result;
    result = idem_make(IDEM_TAG_INT);
    result.as.i = value;
    return result;
}

Value idem_int_lit(unsigned int w0, unsigned int w1,
                   unsigned int w2, unsigned int w3)
{
    IdemUInt bits;
    bits = ((IdemUInt)w3 << 48) | ((IdemUInt)w2 << 32) |
           ((IdemUInt)w1 << 16) | (IdemUInt)w0;
    return idem_int((IdemInt)bits);
}

Value idem_float(double value)
{
    Value result;
    result = idem_make(IDEM_TAG_FLOAT);
    result.as.f = value;
    return result;
}

Value idem_char(IdemChar codepoint)
{
    Value result;
    result = idem_make(IDEM_TAG_CHAR);
    result.as.c = codepoint;
    return result;
}

static Value idem_raw_bytes(IdemTag tag, const char *data, size_t len)
{
    IdemStringObj *obj;
    Value result;

    obj = (IdemStringObj *)idem_obj_alloc(sizeof(IdemStringObj),
                                          tag == IDEM_TAG_STRING
                                              ? IDEM_OBJ_STRING
                                              : IDEM_OBJ_BYTES);
    obj->length = len;
    obj->data = (char *)malloc(len + 1);
    if (obj->data == NULL) {
        idem_panic_oom();
    }
    if (len != 0) {
        memcpy(obj->data, data, len);
    }
    obj->data[len] = '\0';

    result = idem_make(tag);
    result.as.obj = obj;
    return result;
}

Value idem_string(const char *data, size_t len)
{
    return idem_raw_bytes(IDEM_TAG_STRING, data, len);
}

Value idem_bytes(const char *data, size_t len)
{
    return idem_raw_bytes(IDEM_TAG_BYTES, data, len);
}

Value idem_seq_new(IdemTag tag, Value *items, size_t count,
                   const IdemTypeDesc *type)
{
    IdemSeqObj *obj;
    Value result;

    obj = (IdemSeqObj *)idem_obj_alloc(sizeof(IdemSeqObj),
                                       tag == IDEM_TAG_ARRAY ? IDEM_OBJ_ARRAY
                                       : tag == IDEM_TAG_LIST
                                           ? IDEM_OBJ_LIST
                                           : IDEM_OBJ_TUPLE);
    obj->type = type;
    obj->count = count;
    obj->items = NULL;
    if (count != 0) {
        obj->items = (Value *)malloc(sizeof(Value) * count);
        if (obj->items == NULL) {
            idem_panic_oom();
        }
        idem_copy_values(obj->items, items, count);
    }

    result = idem_make(tag);
    result.as.obj = obj;
    return result;
}

Value idem_tuple(Value *items, size_t count, const IdemTypeDesc *type)
{
    return idem_seq_new(IDEM_TAG_TUPLE, items, count, type);
}

Value idem_array(Value *items, size_t count, const IdemTypeDesc *type)
{
    return idem_seq_new(IDEM_TAG_ARRAY, items, count, type);
}

Value idem_list(Value *items, size_t count, const IdemTypeDesc *type)
{
    return idem_seq_new(IDEM_TAG_LIST, items, count, type);
}

Value idem_map_lit(Value *keys, Value *values, size_t count,
                   const IdemTypeDesc *type)
{
    IdemMapObj *obj;
    Value result;
    size_t i;
    size_t used;

    obj = (IdemMapObj *)idem_obj_alloc(sizeof(IdemMapObj),
                                       IDEM_OBJ_MAP);
    obj->type = type;
    obj->count = 0;
    obj->keys = NULL;
    obj->values = NULL;

    if (count != 0) {
        obj->keys = (Value *)malloc(sizeof(Value) * count);
        obj->values = (Value *)malloc(sizeof(Value) * count);
        if (obj->keys == NULL || obj->values == NULL) {
            idem_panic_oom();
        }
    }

    used = 0;
    for (i = 0; i < count; i++) {
        size_t j;
        int found;
        found = 0;
        for (j = 0; j < used; j++) {
            if (idem_value_equal(obj->keys[j], keys[i])) {
                obj->values[j] = values[i];
                found = 1;
                break;
            }
        }
        if (!found) {
            obj->keys[used] = keys[i];
            obj->values[used] = values[i];
            used += 1;
        }
    }
    obj->count = used;

    result = idem_make(IDEM_TAG_MAP);
    result.as.obj = obj;
    return result;
}

Value idem_struct_new(const IdemTypeDesc *type, Value *fields,
                      size_t count)
{
    IdemStructObj *obj;
    Value result;

    obj = (IdemStructObj *)idem_obj_alloc(sizeof(IdemStructObj),
                                          IDEM_OBJ_STRUCT);
    obj->type = type;
    obj->count = count;
    obj->fields = NULL;
    if (count != 0) {
        obj->fields = (Value *)malloc(sizeof(Value) * count);
        if (obj->fields == NULL) {
            idem_panic_oom();
        }
        idem_copy_values(obj->fields, fields, count);
    }

    result = idem_make(IDEM_TAG_STRUCT);
    result.as.obj = obj;
    return result;
}

Value idem_enum_new(const IdemTypeDesc *type, unsigned int variant,
                    Value *payload, size_t count)
{
    IdemEnumObj *obj;
    Value result;

    obj = (IdemEnumObj *)idem_obj_alloc(sizeof(IdemEnumObj),
                                        IDEM_OBJ_ENUM);
    obj->type = type;
    obj->variant = variant;
    obj->count = count;
    obj->payload = NULL;
    if (count != 0) {
        obj->payload = (Value *)malloc(sizeof(Value) * count);
        if (obj->payload == NULL) {
            idem_panic_oom();
        }
        idem_copy_values(obj->payload, payload, count);
    }

    result = idem_make(IDEM_TAG_ENUM);
    result.as.obj = obj;
    return result;
}

Value idem_closure_new(IdemFnPtr fn, Value *env, size_t env_count,
                       const IdemTypeDesc *type)
{
    IdemClosureObj *obj;
    Value result;

    obj = (IdemClosureObj *)idem_obj_alloc(sizeof(IdemClosureObj),
                                           IDEM_OBJ_CLOSURE);
    obj->type = type;
    obj->fn = fn;
    obj->count = env_count;
    obj->env = NULL;
    if (env_count != 0) {
        obj->env = (Value *)malloc(sizeof(Value) * env_count);
        if (obj->env == NULL) {
            idem_panic_oom();
        }
        idem_copy_values(obj->env, env, env_count);
    }

    result = idem_make(IDEM_TAG_CLOSURE);
    result.as.obj = obj;
    return result;
}

Value idem_args_array(int argc, char **argv, const IdemTypeDesc *type,
                      const char *program)
{
    Value *items;
    Value result;
    int count;
    int i;

    count = argc > 0 ? argc : 1;
    items = (Value *)malloc(sizeof(Value) * (size_t)count);
    if (items == NULL) {
        idem_panic_oom();
    }
    for (i = 0; i < count; i++) {
        items[i] = idem_unit();
    }
    idem_gc_push_frame(items, (size_t)count);

    items[0] = idem_string(program != NULL ? program : "",
                           strlen(program != NULL ? program : ""));
    for (i = 1; i < count; i++) {
        const char *arg;
        arg = argv[i] != NULL ? argv[i] : "";
        items[i] = idem_string(arg, strlen(arg));
    }

    result = idem_array(items, (size_t)count, type);
    idem_gc_pop_frame();
    free(items);
    return result;
}

/* ------------------------------------------------------------------ */
/* Error descriptor                                                    */
/* ------------------------------------------------------------------ */

static const IdemVariantDesc idem_error_variants[] = {
    { "NotFound", 0 },
    { "PermissionDenied", 0 },
    { "InvalidUtf8", 0 },
    { "Io", 1 }
};

IdemTypeDesc idem_error_type = {
    1,
    IDEM_DESC_ENUM,
    "Error",
    0,
    NULL,
    4,
    idem_error_variants
};

Value idem_error_value(unsigned int variant, Value payload)
{
    if (variant == 3) {
        return idem_enum_new(&idem_error_type, variant, &payload, 1);
    }
    return idem_enum_new(&idem_error_type, variant, NULL, 0);
}

Value idem_error_io(const char *message)
{
    Value roots[2];
    Value result;

    roots[0] = idem_unit();
    roots[1] = idem_string(message, strlen(message));
    idem_gc_push_frame(roots, 2);
    result = idem_error_value(3, roots[1]);
    idem_gc_pop_frame();
    return result;
}

/* ------------------------------------------------------------------ */
/* Accessors                                                           */
/* ------------------------------------------------------------------ */

int idem_truth(Value value)
{
    return value.as.i != 0;
}

Value idem_struct_get(Value value, size_t index)
{
    IdemStructObj *obj;
    obj = (IdemStructObj *)value.as.obj;
    if (obj == NULL || index >= obj->count) {
        idem_panic_str("invalid struct projection");
    }
    return obj->fields[index];
}

Value idem_enum_get(Value value, size_t index)
{
    IdemEnumObj *obj;
    obj = (IdemEnumObj *)value.as.obj;
    if (obj == NULL || index >= obj->count) {
        idem_panic_str("invalid enum projection");
    }
    return obj->payload[index];
}

Value idem_tuple_get(Value value, size_t index)
{
    IdemSeqObj *obj;
    obj = (IdemSeqObj *)value.as.obj;
    if (obj == NULL || index >= obj->count) {
        idem_panic_str("invalid tuple projection");
    }
    return obj->items[index];
}

const IdemTypeDesc *idem_struct_type(Value value)
{
    IdemStructObj *obj;
    obj = (IdemStructObj *)value.as.obj;
    return obj != NULL ? obj->type : NULL;
}

const IdemTypeDesc *idem_enum_type(Value value)
{
    IdemEnumObj *obj;
    obj = (IdemEnumObj *)value.as.obj;
    return obj != NULL ? obj->type : NULL;
}

unsigned int idem_enum_variant(Value value)
{
    IdemEnumObj *obj;
    obj = (IdemEnumObj *)value.as.obj;
    return obj != NULL ? obj->variant : 0;
}

int idem_value_is_type(Value value, IdemTag tag, const IdemTypeDesc *type)
{
    if (value.tag != tag) {
        return 0;
    }
    switch (tag) {
    case IDEM_TAG_STRUCT:
        return idem_struct_type(value) == type;
    case IDEM_TAG_ENUM:
        return idem_enum_type(value) == type;
    case IDEM_TAG_ARRAY:
    case IDEM_TAG_LIST:
    case IDEM_TAG_TUPLE:
        return ((IdemSeqObj *)value.as.obj)->type == type;
    case IDEM_TAG_MAP:
        return ((IdemMapObj *)value.as.obj)->type == type;
    case IDEM_TAG_CLOSURE:
        return ((IdemClosureObj *)value.as.obj)->type == type;
    default:
        return 1;
    }
}

Value idem_value_call(Value function, Value *args, size_t argc)
{
    IdemClosureObj *obj;
    if (function.tag != IDEM_TAG_CLOSURE || function.as.obj == NULL) {
        idem_panic_str("value is not callable");
    }
    obj = (IdemClosureObj *)function.as.obj;
    return obj->fn(obj->env, args, argc);
}

/* ------------------------------------------------------------------ */
/* Literal comparisons                                                 */
/* ------------------------------------------------------------------ */

int idem_int_eq_lit(Value value, unsigned int w0, unsigned int w1,
                    unsigned int w2, unsigned int w3)
{
    IdemUInt bits;
    if (value.tag != IDEM_TAG_INT) {
        return 0;
    }
    bits = ((IdemUInt)w3 << 48) | ((IdemUInt)w2 << 32) |
           ((IdemUInt)w1 << 16) | (IdemUInt)w0;
    return (IdemUInt)value.as.i == bits;
}

int idem_float_eq_lit(Value value, double literal)
{
    return value.tag == IDEM_TAG_FLOAT && value.as.f == literal;
}

int idem_bool_eq_lit(Value value, int literal)
{
    return value.tag == IDEM_TAG_BOOL && value.as.i == (literal != 0);
}

int idem_char_eq_lit(Value value, IdemChar literal)
{
    return value.tag == IDEM_TAG_CHAR && value.as.c == literal;
}

static int idem_raw_eq_lit(IdemTag tag, Value value,
                           const char *data, size_t len)
{
    IdemStringObj *obj;
    if (value.tag != tag || value.as.obj == NULL) {
        return 0;
    }
    obj = (IdemStringObj *)value.as.obj;
    return obj->length == len &&
           (len == 0 || memcmp(obj->data, data, len) == 0);
}

int idem_string_eq_lit(Value value, const char *data, size_t len)
{
    return idem_raw_eq_lit(IDEM_TAG_STRING, value, data, len);
}

int idem_bytes_eq_lit(Value value, const char *data, size_t len)
{
    return idem_raw_eq_lit(IDEM_TAG_BYTES, value, data, len);
}

/* ------------------------------------------------------------------ */
/* Equality and ordering                                               */
/* ------------------------------------------------------------------ */

static int idem_seq_equal(IdemSeqObj *a, IdemSeqObj *b)
{
    size_t i;
    if (a->count != b->count) {
        return 0;
    }
    for (i = 0; i < a->count; i++) {
        if (!idem_value_equal(a->items[i], b->items[i])) {
            return 0;
        }
    }
    return 1;
}

static int idem_map_find(IdemMapObj *map, Value key)
{
    size_t i;
    for (i = 0; i < map->count; i++) {
        if (idem_value_equal(map->keys[i], key)) {
            return (int)i;
        }
    }
    return -1;
}

int idem_value_equal(Value a, Value b)
{
    size_t i;

    if (a.tag != b.tag) {
        return 0;
    }

    switch (a.tag) {
    case IDEM_TAG_UNIT:
        return 1;
    case IDEM_TAG_BOOL:
    case IDEM_TAG_INT:
        return a.as.i == b.as.i;
    case IDEM_TAG_FLOAT:
        return a.as.f == b.as.f;
    case IDEM_TAG_CHAR:
        return a.as.c == b.as.c;
    case IDEM_TAG_STRING:
    case IDEM_TAG_BYTES: {
        IdemStringObj *x;
        IdemStringObj *y;
        x = (IdemStringObj *)a.as.obj;
        y = (IdemStringObj *)b.as.obj;
        return x->length == y->length &&
               (x->length == 0 ||
                memcmp(x->data, y->data, x->length) == 0);
    }
    case IDEM_TAG_ARRAY:
    case IDEM_TAG_LIST:
    case IDEM_TAG_TUPLE: {
        IdemSeqObj *x;
        IdemSeqObj *y;
        x = (IdemSeqObj *)a.as.obj;
        y = (IdemSeqObj *)b.as.obj;
        if (x->type != y->type) {
            return 0;
        }
        return idem_seq_equal(x, y);
    }
    case IDEM_TAG_MAP: {
        IdemMapObj *x;
        IdemMapObj *y;
        x = (IdemMapObj *)a.as.obj;
        y = (IdemMapObj *)b.as.obj;
        if (x->type != y->type || x->count != y->count) {
            return 0;
        }
        for (i = 0; i < x->count; i++) {
            int index;
            index = idem_map_find(y, x->keys[i]);
            if (index < 0 ||
                !idem_value_equal(x->values[i], y->values[(size_t)index])) {
                return 0;
            }
        }
        return 1;
    }
    case IDEM_TAG_STRUCT: {
        IdemStructObj *x;
        IdemStructObj *y;
        x = (IdemStructObj *)a.as.obj;
        y = (IdemStructObj *)b.as.obj;
        if (x->type != y->type || x->count != y->count) {
            return 0;
        }
        for (i = 0; i < x->count; i++) {
            if (!idem_value_equal(x->fields[i], y->fields[i])) {
                return 0;
            }
        }
        return 1;
    }
    case IDEM_TAG_ENUM: {
        IdemEnumObj *x;
        IdemEnumObj *y;
        x = (IdemEnumObj *)a.as.obj;
        y = (IdemEnumObj *)b.as.obj;
        if (x->type != y->type || x->variant != y->variant ||
            x->count != y->count) {
            return 0;
        }
        for (i = 0; i < x->count; i++) {
            if (!idem_value_equal(x->payload[i], y->payload[i])) {
                return 0;
            }
        }
        return 1;
    }
    case IDEM_TAG_CLOSURE: {
        IdemClosureObj *x;
        IdemClosureObj *y;
        x = (IdemClosureObj *)a.as.obj;
        y = (IdemClosureObj *)b.as.obj;
        return x->type == y->type && x == y;
    }
    default:
        return 0;
    }
}

static int idem_raw_compare(const char *a, size_t a_len,
                            const char *b, size_t b_len)
{
    size_t n;
    int cmp;
    n = a_len < b_len ? a_len : b_len;
    cmp = n == 0 ? 0 : memcmp(a, b, n);
    if (cmp < 0) {
        return -1;
    }
    if (cmp > 0) {
        return 1;
    }
    if (a_len < b_len) {
        return -1;
    }
    if (a_len > b_len) {
        return 1;
    }
    return 0;
}

int idem_value_compare(Value a, Value b)
{
    size_t i;

    if (a.tag != b.tag) {
        if (a.tag < b.tag) {
            return -1;
        }
        return 1;
    }

    switch (a.tag) {
    case IDEM_TAG_UNIT:
        return 0;
    case IDEM_TAG_INT:
        if (a.as.i < b.as.i) return -1;
        if (a.as.i > b.as.i) return 1;
        return 0;
    case IDEM_TAG_FLOAT:
        if (a.as.f < b.as.f) return -1;
        if (a.as.f > b.as.f) return 1;
        return 0;
    case IDEM_TAG_CHAR:
        if (a.as.c < b.as.c) return -1;
        if (a.as.c > b.as.c) return 1;
        return 0;
    case IDEM_TAG_STRING:
    case IDEM_TAG_BYTES: {
        IdemStringObj *x;
        IdemStringObj *y;
        x = (IdemStringObj *)a.as.obj;
        y = (IdemStringObj *)b.as.obj;
        return idem_raw_compare(x->data, x->length, y->data, y->length);
    }
    case IDEM_TAG_TUPLE: {
        IdemSeqObj *x;
        IdemSeqObj *y;
        size_t n;
        x = (IdemSeqObj *)a.as.obj;
        y = (IdemSeqObj *)b.as.obj;
        n = x->count < y->count ? x->count : y->count;
        for (i = 0; i < n; i++) {
            int cmp;
            cmp = idem_value_compare(x->items[i], y->items[i]);
            if (cmp != 0) {
                return cmp;
            }
        }
        if (x->count < y->count) return -1;
        if (x->count > y->count) return 1;
        return 0;
    }
    default:
        return 0;
    }
}

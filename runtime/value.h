#ifndef IDEM_VALUE_H
#define IDEM_VALUE_H

#include <stddef.h>

/* ------------------------------------------------------------------ */
/* Basic types                                                         */
/* ------------------------------------------------------------------ */

typedef long IdemInt;
typedef unsigned long IdemUInt;
typedef unsigned long IdemChar;
typedef unsigned int IdemTag;

typedef enum IdemTagKind {
    IDEM_TAG_UNIT = 0,
    IDEM_TAG_BOOL,
    IDEM_TAG_INT,
    IDEM_TAG_FLOAT,
    IDEM_TAG_CHAR,
    IDEM_TAG_STRING,
    IDEM_TAG_BYTES,
    IDEM_TAG_ARRAY,
    IDEM_TAG_LIST,
    IDEM_TAG_MAP,
    IDEM_TAG_TUPLE,
    IDEM_TAG_STRUCT,
    IDEM_TAG_ENUM,
    IDEM_TAG_CLOSURE
} IdemTagKind;

typedef struct Value {
    IdemTag tag;
    union {
        IdemInt i;
        double f;
        IdemChar c;
        void *obj;
    } as;
} Value;

typedef Value (*IdemFnPtr)(Value *env, Value *args, size_t argc);

/* ------------------------------------------------------------------ */
/* Type descriptors                                                    */
/* ------------------------------------------------------------------ */

typedef struct IdemVariantDesc {
    const char *name;
    size_t payload_count;
} IdemVariantDesc;

typedef struct IdemTypeDesc {
    unsigned int id;
    unsigned int kind; /* 0 = struct, 1 = enum */
    const char *name;
    size_t field_count;
    const char *const *field_names;
    size_t variant_count;
    const IdemVariantDesc *variants;
} IdemTypeDesc;

#define IDEM_DESC_STRUCT 0
#define IDEM_DESC_ENUM 1
#define IDEM_DESC_STRUCTURAL 2

extern IdemTypeDesc idem_error_type;

/* ------------------------------------------------------------------ */
/* Runtime lifecycle and roots                                         */
/* ------------------------------------------------------------------ */

void idem_runtime_init(void);
void idem_gc_push_frame(Value *slots, size_t count);
void idem_gc_pop_frame(void);
void idem_gc_collect(void);

/* ------------------------------------------------------------------ */
/* Constructors                                                        */
/* ------------------------------------------------------------------ */

Value idem_unit(void);
Value idem_bool(int value);
Value idem_int(IdemInt value);
Value idem_int_lit(unsigned int w0, unsigned int w1,
                   unsigned int w2, unsigned int w3);
Value idem_float(double value);
Value idem_char(IdemChar codepoint);
Value idem_string(const char *data, size_t len);
Value idem_bytes(const char *data, size_t len);
Value idem_tuple(Value *items, size_t count, const IdemTypeDesc *type);
Value idem_array(Value *items, size_t count, const IdemTypeDesc *type);
Value idem_list(Value *items, size_t count, const IdemTypeDesc *type);
Value idem_map_lit(Value *keys, Value *values, size_t count,
                   const IdemTypeDesc *type);
Value idem_struct_new(const IdemTypeDesc *type, Value *fields,
                      size_t count);
Value idem_enum_new(const IdemTypeDesc *type, unsigned int variant,
                    Value *payload, size_t count);
Value idem_closure_new(IdemFnPtr fn, Value *env, size_t env_count,
                       const IdemTypeDesc *type);
Value idem_args_array(int argc, char **argv, const IdemTypeDesc *type,
                      const char *program);

/* ------------------------------------------------------------------ */
/* Accessors                                                           */
/* ------------------------------------------------------------------ */

int idem_truth(Value value);
Value idem_struct_get(Value value, size_t index);
Value idem_enum_get(Value value, size_t index);
Value idem_tuple_get(Value value, size_t index);
const IdemTypeDesc *idem_struct_type(Value value);
const IdemTypeDesc *idem_enum_type(Value value);
unsigned int idem_enum_variant(Value value);
int idem_value_is_type(Value value, IdemTag tag,
                       const IdemTypeDesc *type);
Value idem_value_call(Value function, Value *args, size_t argc);
int idem_value_equal(Value a, Value b);
int idem_value_compare(Value a, Value b);

/* ------------------------------------------------------------------ */
/* Literal comparisons                                                 */
/* ------------------------------------------------------------------ */

int idem_int_eq_lit(Value value, unsigned int w0, unsigned int w1,
                    unsigned int w2, unsigned int w3);
int idem_float_eq_lit(Value value, double literal);
int idem_bool_eq_lit(Value value, int literal);
int idem_char_eq_lit(Value value, IdemChar literal);
int idem_string_eq_lit(Value value, const char *data, size_t len);
int idem_bytes_eq_lit(Value value, const char *data, size_t len);

/* ------------------------------------------------------------------ */
/* Arithmetic and operators                                            */
/* ------------------------------------------------------------------ */

Value idem_int_add(Value a, Value b);
Value idem_int_sub(Value a, Value b);
Value idem_int_mul(Value a, Value b);
Value idem_int_div(Value a, Value b);
Value idem_int_mod(Value a, Value b);
Value idem_int_neg(Value a);
Value idem_int_and(Value a, Value b);
Value idem_int_or(Value a, Value b);
Value idem_int_xor(Value a, Value b);
Value idem_int_not(Value a);
Value idem_int_shl(Value a, Value b);
Value idem_int_shr(Value a, Value b);

Value idem_float_add(Value a, Value b);
Value idem_float_sub(Value a, Value b);
Value idem_float_mul(Value a, Value b);
Value idem_float_div(Value a, Value b);
Value idem_float_mod(Value a, Value b);
Value idem_float_neg(Value a);

/* ------------------------------------------------------------------ */
/* Built-ins                                                           */
/* ------------------------------------------------------------------ */

Value idem_print(Value value, int newline);
Value idem_read_line(void);
Value idem_read_file(Value path);
Value idem_write_file(Value path, Value contents);
Value idem_flush(void);
Value idem_exit(Value code);
Value idem_panic(Value message);
Value idem_len(Value value);
Value idem_get(Value collection, Value index);
Value idem_set(Value collection, Value index, Value value);
Value idem_remove(Value map, Value key);
Value idem_push(Value list, Value value);
Value idem_concat(Value a, Value b);
Value idem_join(Value collection, Value separator);
Value idem_has(Value map, Value key);
Value idem_range(Value start, Value end, const IdemTypeDesc *type);
Value idem_range_step(Value start, Value end, Value step,
                      const IdemTypeDesc *type);
Value idem_for_each(Value collection, Value function);
Value idem_sort(Value collection);
Value idem_sort_by(Value collection, Value comparator);
Value idem_contains(Value collection, Value needle);
Value idem_map(Value collection, Value function,
               const IdemTypeDesc *result_type);
Value idem_filter(Value collection, Value function);
Value idem_fold(Value collection, Value initial, Value function);
Value idem_sum_int(Value collection);
Value idem_sum_float(Value collection);
Value idem_keys(Value map, const IdemTypeDesc *result_type);
Value idem_values(Value map, const IdemTypeDesc *result_type);
Value idem_abs_int(Value value);
Value idem_abs_float(Value value);
Value idem_min_int(Value a, Value b);
Value idem_max_int(Value a, Value b);
Value idem_min_float(Value a, Value b);
Value idem_max_float(Value a, Value b);
Value idem_sqrt(Value value);
Value idem_floor(Value value);
Value idem_ceil(Value value);
Value idem_pow(Value a, Value b);
Value idem_random(void);
Value idem_now(void);
Value idem_byte_at(Value value, Value index);
Value idem_slice(Value value, Value start, Value end);
Value idem_compare(Value a, Value b);
Value idem_char_at(Value value, Value index);
Value idem_chars(Value value, const IdemTypeDesc *result_type);
Value idem_to_int(Value value);
Value idem_to_float(Value value);
Value idem_to_string(Value value);
Value idem_to_bytes(Value value);
Value idem_from_bytes(Value value);
Value idem_split(Value value, Value separator,
                 const IdemTypeDesc *type);
Value idem_trim(Value value);
Value idem_starts_with(Value value, Value prefix);
Value idem_ends_with(Value value, Value suffix);
Value idem_replace(Value value, Value old_value, Value new_value);

/* ------------------------------------------------------------------ */
/* Rendering and panic helpers                                         */
/* ------------------------------------------------------------------ */

void idem_panic_str(const char *message);
void idem_panic_oom(void);

#endif /* IDEM_VALUE_H */

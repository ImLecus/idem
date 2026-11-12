#include "internal.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

static IdemSeqObj *idem_as_seq(Value value, const char *message)
{
    if (value.tag != IDEM_TAG_ARRAY && value.tag != IDEM_TAG_LIST &&
        value.tag != IDEM_TAG_TUPLE) {
        idem_panic_str(message);
    }
    return (IdemSeqObj *)value.as.obj;
}

static IdemMapObj *idem_as_map(Value value)
{
    if (value.tag != IDEM_TAG_MAP) {
        idem_panic_str("expected a Map");
    }
    return (IdemMapObj *)value.as.obj;
}

static Value idem_seq_like(IdemTag tag, Value *items, size_t count,
                           const IdemTypeDesc *type)
{
    if (tag == IDEM_TAG_ARRAY) {
        return idem_array(items, count, type);
    }
    return idem_list(items, count, type);
}

Value idem_len(Value value)
{
    switch (value.tag) {
    case IDEM_TAG_STRING:
    case IDEM_TAG_BYTES:
        return idem_int((IdemInt)((IdemStringObj *)value.as.obj)->length);
    case IDEM_TAG_ARRAY:
    case IDEM_TAG_LIST:
    case IDEM_TAG_TUPLE:
        return idem_int((IdemInt)((IdemSeqObj *)value.as.obj)->count);
    case IDEM_TAG_MAP:
        return idem_int((IdemInt)((IdemMapObj *)value.as.obj)->count);
    default:
        idem_panic_str("len expects a collection or string");
        return idem_unit();
    }
}

Value idem_get(Value collection, Value index)
{
    if (collection.tag == IDEM_TAG_ARRAY ||
        collection.tag == IDEM_TAG_LIST) {
        IdemSeqObj *seq;
        IdemInt i;
        seq = idem_as_seq(collection, "expected an Array or List");
        if (index.tag != IDEM_TAG_INT) {
            idem_panic_str("index must be an Int");
        }
        i = index.as.i;
        if (i < 0 || (IdemUInt)i >= (IdemUInt)seq->count) {
            return idem_unit();
        }
        return seq->items[(size_t)i];
    }
    if (collection.tag == IDEM_TAG_MAP) {
        IdemMapObj *map;
        size_t i;
        map = idem_as_map(collection);
        for (i = 0; i < map->count; i++) {
            if (idem_value_equal(map->keys[i], index)) {
                return map->values[i];
            }
        }
        return idem_unit();
    }
    idem_panic_str("get expects an Array, List, or Map");
    return idem_unit();
}

Value idem_set(Value collection, Value index, Value value)
{
    if (collection.tag == IDEM_TAG_ARRAY) {
        IdemSeqObj *seq;
        Value *items;
        IdemInt i;

        seq = idem_as_seq(collection, "expected an Array");
        if (index.tag != IDEM_TAG_INT) {
            idem_panic_str("index must be an Int");
        }
        i = index.as.i;
        if (i < 0 || (IdemUInt)i >= (IdemUInt)seq->count) {
            idem_panic_str("array index out of bounds");
        }
        items = (Value *)malloc(sizeof(Value) * seq->count);
        if (items == NULL) {
            idem_panic_oom();
        }
        idem_copy_values(items, seq->items, seq->count);
        items[(size_t)i] = value;
        {
            Value result;
            result = idem_array(items, seq->count, seq->type);
            free(items);
            return result;
        }
    }

    if (collection.tag == IDEM_TAG_MAP) {
        IdemMapObj *map;
        Value *keys;
        Value *values;
        size_t i;
        size_t used;
        int found;

        map = idem_as_map(collection);
        keys = (Value *)malloc(sizeof(Value) * (map->count + 1));
        values = (Value *)malloc(sizeof(Value) * (map->count + 1));
        if (keys == NULL || values == NULL) {
            idem_panic_oom();
        }
        used = 0;
        found = 0;
        for (i = 0; i < map->count; i++) {
            keys[used] = map->keys[i];
            values[used] = map->values[i];
            if (!found && idem_value_equal(map->keys[i], index)) {
                values[used] = value;
                found = 1;
            }
            used += 1;
        }
        if (!found) {
            keys[used] = index;
            values[used] = value;
            used += 1;
        }
        {
            Value result;
            result = idem_map_lit(keys, values, used, map->type);
            free(keys);
            free(values);
            return result;
        }
    }

    idem_panic_str("set expects an Array or Map");
    return idem_unit();
}

Value idem_remove(Value map_value, Value key)
{
    IdemMapObj *map;
    Value *keys;
    Value *values;
    size_t i;
    size_t used;

    map = idem_as_map(map_value);
    keys = (Value *)malloc(sizeof(Value) * (map->count + 1));
    values = (Value *)malloc(sizeof(Value) * (map->count + 1));
    if (keys == NULL || values == NULL) {
        idem_panic_oom();
    }
    used = 0;
    for (i = 0; i < map->count; i++) {
        if (!idem_value_equal(map->keys[i], key)) {
            keys[used] = map->keys[i];
            values[used] = map->values[i];
            used += 1;
        }
    }
    {
        Value result;
        result = idem_map_lit(keys, values, used, map->type);
        free(keys);
        free(values);
        return result;
    }
}

Value idem_push(Value list, Value value)
{
    IdemSeqObj *seq;
    Value *items;
    Value result;

    if (list.tag != IDEM_TAG_LIST) {
        idem_panic_str("push expects a List");
    }
    seq = idem_as_seq(list, "expected a List");
    items = (Value *)malloc(sizeof(Value) * (seq->count + 1));
    if (items == NULL) {
        idem_panic_oom();
    }
    idem_copy_values(items, seq->items, seq->count);
    items[seq->count] = value;
    result = idem_list(items, seq->count + 1, seq->type);
    free(items);
    return result;
}

Value idem_concat(Value a, Value b)
{
    if (a.tag == IDEM_TAG_STRING || a.tag == IDEM_TAG_BYTES) {
        IdemStringObj *x;
        IdemStringObj *y;
        char *data;
        Value result;
        size_t total;

        if (a.tag != b.tag) {
            idem_panic_str("concat expects matching types");
        }
        x = (IdemStringObj *)a.as.obj;
        y = (IdemStringObj *)b.as.obj;
        total = x->length + y->length;
        data = (char *)malloc(total + 1);
        if (data == NULL) {
            idem_panic_oom();
        }
        memcpy(data, x->data, x->length);
        memcpy(data + x->length, y->data, y->length);
        data[total] = '\0';
        result = a.tag == IDEM_TAG_STRING
                     ? idem_string(data, total)
                     : idem_bytes(data, total);
        free(data);
        return result;
    }

    if (a.tag == IDEM_TAG_ARRAY || a.tag == IDEM_TAG_LIST) {
        IdemSeqObj *x;
        IdemSeqObj *y;
        Value *items;
        Value result;

        if (a.tag != b.tag) {
            idem_panic_str("concat expects matching types");
        }
        x = idem_as_seq(a, "expected a collection");
        y = idem_as_seq(b, "expected a collection");
        items = (Value *)malloc(sizeof(Value) * (x->count + y->count));
        if (items == NULL) {
            idem_panic_oom();
        }
        idem_copy_values(items, x->items, x->count);
        idem_copy_values(items + x->count, y->items, y->count);
        result = idem_seq_like(a.tag, items, x->count + y->count, x->type);
        free(items);
        return result;
    }

    idem_panic_str("concat expects String, Bytes, Array, or List");
    return idem_unit();
}

Value idem_join(Value collection, Value separator)
{    IdemSeqObj *seq;
    IdemStringObj *sep;
    size_t total;
    size_t position;
    size_t i;
    char *data;
    Value result;

    if (collection.tag != IDEM_TAG_ARRAY &&
        collection.tag != IDEM_TAG_LIST) {
        idem_panic_str("join expects an Array or List");
    }
    if (separator.tag != IDEM_TAG_STRING) {
        idem_panic_str("join expects a String separator");
    }

    seq = idem_as_seq(collection, "expected an Array or List");
    sep = (IdemStringObj *)separator.as.obj;

    total = 0;
    for (i = 0; i < seq->count; i++) {
        IdemStringObj *item;
        size_t extra;

        if (seq->items[i].tag != IDEM_TAG_STRING) {
            idem_panic_str("join expects String elements");
        }
        item = (IdemStringObj *)seq->items[i].as.obj;
        extra = item->length;
        if (i != 0) {
            if (sep->length > ((size_t)-1) - extra) {
                idem_panic_str("joined string is too large");
            }
            extra += sep->length;
        }
        if (extra > ((size_t)-1) - total) {
            idem_panic_str("joined string is too large");
        }
        total += extra;
    }

    if (total == (size_t)-1) {
        idem_panic_str("joined string is too large");
    }

    data = (char *)malloc(total + 1);
    if (data == NULL) {
        idem_panic_oom();
    }

    position = 0;
    for (i = 0; i < seq->count; i++) {
        IdemStringObj *item;

        item = (IdemStringObj *)seq->items[i].as.obj;
        if (i != 0) {
            memcpy(data + position, sep->data, sep->length);
            position += sep->length;
        }
        memcpy(data + position, item->data, item->length);
        position += item->length;
    }
    data[position] = '\0';

    result = idem_string(data, total);
    free(data);
    return result;
}

Value idem_has(Value map_value, Value key)
{
    IdemMapObj *map;
    size_t i;

    map = idem_as_map(map_value);
    for (i = 0; i < map->count; i++) {
        if (idem_value_equal(map->keys[i], key)) {
            return idem_bool(1);
        }
    }
    return idem_bool(0);
}

static int idem_raw_contains(const char *haystack, size_t haystack_len,
                             const char *needle, size_t needle_len)
{
    size_t i;

    if (needle_len == 0) {
        return 1;
    }
    if (needle_len > haystack_len) {
        return 0;
    }
    for (i = 0; i + needle_len <= haystack_len; i++) {
        if (memcmp(haystack + i, needle, needle_len) == 0) {
            return 1;
        }
    }
    return 0;
}

Value idem_contains(Value collection, Value needle)
{
    size_t i;

    if (collection.tag == IDEM_TAG_STRING ||
        collection.tag == IDEM_TAG_BYTES) {
        IdemStringObj *haystack;
        IdemStringObj *needle_obj;

        if (collection.tag != needle.tag) {
            idem_panic_str("contains expects matching types");
        }
        haystack = (IdemStringObj *)collection.as.obj;
        needle_obj = (IdemStringObj *)needle.as.obj;
        return idem_bool(idem_raw_contains(haystack->data, haystack->length,
                                           needle_obj->data,
                                           needle_obj->length));
    }

    if (collection.tag == IDEM_TAG_ARRAY ||
        collection.tag == IDEM_TAG_LIST) {
        IdemSeqObj *seq;

        seq = idem_as_seq(collection, "expected an Array or List");
        for (i = 0; i < seq->count; i++) {
            if (idem_value_equal(seq->items[i], needle)) {
                return idem_bool(1);
            }
        }
        return idem_bool(0);
    }

    idem_panic_str("contains expects String, Bytes, Array, or List");
    return idem_bool(0);
}

Value idem_for_each(Value collection, Value function)
{
    IdemSeqObj *seq;
    size_t i;

    if (collection.tag != IDEM_TAG_ARRAY &&
        collection.tag != IDEM_TAG_LIST) {
        idem_panic_str("forEach expects an Array or List");
    }
    seq = idem_as_seq(collection, "expected an Array or List");

    for (i = 0; i < seq->count; i++) {
        Value args[1];
        args[0] = seq->items[i];
        (void)idem_value_call(function, args, 1);
    }
    return idem_unit();
}

static int idem_sort_compare(Value a, Value b, Value comparator,
                             int use_comparator)
{
    if (use_comparator) {
        Value args[2];
        Value result;
        args[0] = a;
        args[1] = b;
        result = idem_value_call(comparator, args, 2);
        if (result.as.i < 0) {
            return -1;
        }
        if (result.as.i > 0) {
            return 1;
        }
        return 0;
    }
    return idem_value_compare(a, b);
}

static void idem_merge_sort(Value *items, Value *tmp, size_t count,
                            Value comparator, int use_comparator)
{
    size_t middle;
    size_t left;
    size_t right;
    size_t out;

    if (count < 2) {
        return;
    }

    middle = count / 2;
    idem_merge_sort(items, tmp, middle, comparator, use_comparator);
    idem_merge_sort(items + middle, tmp, count - middle, comparator,
                    use_comparator);

    left = 0;
    right = middle;
    out = 0;
    while (left < middle && right < count) {
        if (idem_sort_compare(items[right], items[left], comparator,
                              use_comparator) < 0) {
            tmp[out++] = items[right++];
        } else {
            tmp[out++] = items[left++];
        }
    }
    while (left < middle) {
        tmp[out++] = items[left++];
    }
    while (right < count) {
        tmp[out++] = items[right++];
    }
    idem_copy_values(items, tmp, count);
}

static Value idem_sort_impl(Value collection, Value comparator,
                            int use_comparator)
{
    IdemSeqObj *seq;
    Value *items;
    Value *tmp;
    Value result;

    if (collection.tag != IDEM_TAG_ARRAY &&
        collection.tag != IDEM_TAG_LIST) {
        idem_panic_str("sort expects an Array or List");
    }
    seq = idem_as_seq(collection, "expected an Array or List");

    if (seq->count < 2) {
        return idem_seq_like(collection.tag, seq->items, seq->count,
                             seq->type);
    }

    items = (Value *)malloc(sizeof(Value) * seq->count);
    tmp = (Value *)malloc(sizeof(Value) * seq->count);
    if (items == NULL || tmp == NULL) {
        free(items);
        free(tmp);
        idem_panic_oom();
    }

    idem_copy_values(items, seq->items, seq->count);
    idem_merge_sort(items, tmp, seq->count, comparator, use_comparator);
    result = idem_seq_like(collection.tag, items, seq->count, seq->type);
    free(items);
    free(tmp);
    return result;
}

Value idem_sort(Value collection)
{
    return idem_sort_impl(collection, idem_unit(), 0);
}

Value idem_sort_by(Value collection, Value comparator)
{
    return idem_sort_impl(collection, comparator, 1);
}

static Value idem_range_impl(Value start_value, Value end_value,
                             Value step_value, const IdemTypeDesc *type)
{
    IdemInt start;
    IdemInt end;
    IdemInt step;
    IdemInt current;
    size_t count;
    size_t i;
    Value *items;
    Value result;

    if (start_value.tag != IDEM_TAG_INT ||
        end_value.tag != IDEM_TAG_INT ||
        step_value.tag != IDEM_TAG_INT) {
        idem_panic_str("range expects Int arguments");
    }

    start = start_value.as.i;
    end = end_value.as.i;
    step = step_value.as.i;
    if (step == 0) {
        idem_panic_str("range step cannot be zero");
    }

    count = 0;
    current = start;
    while (step > 0 ? current < end : current > end) {
        count += 1;
        if (step > 0) {
            if (current > LONG_MAX - step) {
                break;
            }
        } else {
            if (current < LONG_MIN - step) {
                break;
            }
        }
        current += step;
    }

    if (count == 0) {
        return idem_array(NULL, 0, type);
    }
    if (count > ((size_t)-1) / sizeof(Value)) {
        idem_panic_str("range is too large");
    }

    items = (Value *)malloc(sizeof(Value) * count);
    if (items == NULL) {
        idem_panic_oom();
    }

    current = start;
    i = 0;
    while (i < count) {
        items[i] = idem_int(current);
        i += 1;
        if (i < count) {
            current += step;
        }
    }

    result = idem_array(items, count, type);
    free(items);
    return result;
}

Value idem_range(Value start, Value end, const IdemTypeDesc *type)
{
    return idem_range_impl(start, end, idem_int(1), type);
}

Value idem_range_step(Value start, Value end, Value step,
                      const IdemTypeDesc *type)
{
    return idem_range_impl(start, end, step, type);
}

Value idem_map(Value collection, Value function,
               const IdemTypeDesc *result_type)
{
    IdemSeqObj *seq;
    Value *items;
    Value tag_value;
    size_t i;
    Value result;

    if (collection.tag != IDEM_TAG_ARRAY &&
        collection.tag != IDEM_TAG_LIST) {
        idem_panic_str("map expects an Array or List");
    }
    seq = idem_as_seq(collection, "expected a collection");
    tag_value = collection;

    if (seq->count == 0) {
        return idem_seq_like(tag_value.tag, NULL, 0, result_type);
    }

    items = (Value *)malloc(sizeof(Value) * seq->count);
    if (items == NULL) {
        idem_panic_oom();
    }
    for (i = 0; i < seq->count; i++) {
        items[i] = idem_unit();
    }
    idem_gc_push_frame(items, seq->count);

    for (i = 0; i < seq->count; i++) {
        Value args[1];
        args[0] = seq->items[i];
        items[i] = idem_value_call(function, args, 1);
    }

    result = idem_seq_like(tag_value.tag, items, seq->count, result_type);
    idem_gc_pop_frame();
    free(items);
    return result;
}

Value idem_filter(Value collection, Value function)
{
    IdemSeqObj *seq;
    Value *items;
    size_t i;
    size_t used;
    Value result;

    if (collection.tag != IDEM_TAG_ARRAY &&
        collection.tag != IDEM_TAG_LIST) {
        idem_panic_str("filter expects an Array or List");
    }
    seq = idem_as_seq(collection, "expected a collection");

    if (seq->count == 0) {
        return idem_seq_like(collection.tag, NULL, 0, seq->type);
    }

    items = (Value *)malloc(sizeof(Value) * seq->count);
    if (items == NULL) {
        idem_panic_oom();
    }
    for (i = 0; i < seq->count; i++) {
        items[i] = idem_unit();
    }
    idem_gc_push_frame(items, seq->count);

    used = 0;
    for (i = 0; i < seq->count; i++) {
        Value args[1];
        Value keep;
        args[0] = seq->items[i];
        keep = idem_value_call(function, args, 1);
        if (idem_truth(keep)) {
            items[used] = seq->items[i];
            used += 1;
        }
    }

    result = idem_seq_like(collection.tag, items, used, seq->type);
    idem_gc_pop_frame();
    free(items);
    return result;
}

Value idem_fold(Value collection, Value initial, Value function)
{
    IdemSeqObj *seq;
    Value roots[1];
    size_t i;
    Value result;

    if (collection.tag != IDEM_TAG_ARRAY &&
        collection.tag != IDEM_TAG_LIST) {
        idem_panic_str("fold expects an Array or List");
    }
    seq = idem_as_seq(collection, "expected a collection");

    roots[0] = initial;
    idem_gc_push_frame(roots, 1);

    for (i = 0; i < seq->count; i++) {
        Value args[2];
        args[0] = roots[0];
        args[1] = seq->items[i];
        roots[0] = idem_value_call(function, args, 2);
    }

    result = roots[0];
    idem_gc_pop_frame();
    return result;
}

Value idem_sum_int(Value collection)
{
    IdemSeqObj *seq;
    Value total;
    size_t i;

    seq = idem_as_seq(collection, "sum expects an Array or List");
    total = idem_int(0);
    for (i = 0; i < seq->count; i++) {
        total = idem_int_add(total, seq->items[i]);
    }
    return total;
}

Value idem_sum_float(Value collection)
{
    IdemSeqObj *seq;
    double total;
    size_t i;

    seq = idem_as_seq(collection, "sum expects an Array or List");
    total = 0.0;
    for (i = 0; i < seq->count; i++) {
        total += seq->items[i].as.f;
    }
    return idem_float(total);
}

Value idem_keys(Value map_value, const IdemTypeDesc *result_type)
{
    IdemMapObj *map;
    Value result;

    map = idem_as_map(map_value);
    result = idem_array(map->keys, map->count, result_type);
    return result;
}

Value idem_values(Value map_value, const IdemTypeDesc *result_type)
{
    IdemMapObj *map;
    Value result;

    map = idem_as_map(map_value);
    result = idem_array(map->values, map->count, result_type);
    return result;
}

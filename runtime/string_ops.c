#include "internal.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

Value idem_byte_at(Value value, Value index)
{
    IdemStringObj *obj;
    IdemInt i;

    if (value.tag != IDEM_TAG_STRING && value.tag != IDEM_TAG_BYTES) {
        idem_panic_str("byteAt expects String or Bytes");
    }
    if (index.tag != IDEM_TAG_INT) {
        idem_panic_str("byteAt index must be an Int");
    }
    obj = (IdemStringObj *)value.as.obj;
    i = index.as.i;
    if (i < 0 || (IdemUInt)i >= (IdemUInt)obj->length) {
        return idem_unit();
    }
    return idem_int((IdemInt)(unsigned char)obj->data[(size_t)i]);
}

Value idem_slice(Value value, Value start, Value end)
{
    IdemStringObj *obj;
    IdemInt a;
    IdemInt b;
    Value result;

    if (value.tag != IDEM_TAG_STRING && value.tag != IDEM_TAG_BYTES) {
        idem_panic_str("slice expects String or Bytes");
    }
    if (start.tag != IDEM_TAG_INT || end.tag != IDEM_TAG_INT) {
        idem_panic_str("slice bounds must be Int");
    }
    obj = (IdemStringObj *)value.as.obj;
    a = start.as.i;
    b = end.as.i;
    if (a < 0 || b < a || (IdemUInt)b > (IdemUInt)obj->length) {
        idem_panic_str("slice range out of bounds");
    }
    if (value.tag == IDEM_TAG_STRING) {
        if (a < (IdemInt)obj->length &&
            (((unsigned char)obj->data[(size_t)a]) & 0xC0) == 0x80) {
            idem_panic_str("slice starts inside a UTF-8 sequence");
        }
        if (b < (IdemInt)obj->length &&
            (((unsigned char)obj->data[(size_t)b]) & 0xC0) == 0x80) {
            idem_panic_str("slice ends inside a UTF-8 sequence");
        }
        result = idem_string(obj->data + (size_t)a, (size_t)(b - a));
    } else {
        result = idem_bytes(obj->data + (size_t)a, (size_t)(b - a));
    }
    return result;
}

Value idem_compare(Value a, Value b)
{
    IdemStringObj *x;
    IdemStringObj *y;
    int cmp;

    if (a.tag != IDEM_TAG_STRING && a.tag != IDEM_TAG_BYTES) {
        idem_panic_str("compare expects String or Bytes");
    }
    if (a.tag != b.tag) {
        idem_panic_str("compare expects matching types");
    }
    x = (IdemStringObj *)a.as.obj;
    y = (IdemStringObj *)b.as.obj;
    cmp = idem_bytes_compare_raw(x->data, x->length, y->data, y->length);
    return idem_int((IdemInt)cmp);
}

Value idem_char_at(Value value, Value index)
{
    IdemStringObj *obj;
    IdemInt wanted;
    size_t pos;
    IdemInt current;

    if (value.tag != IDEM_TAG_STRING) {
        idem_panic_str("charAt expects a String");
    }
    if (index.tag != IDEM_TAG_INT) {
        idem_panic_str("charAt index must be an Int");
    }
    obj = (IdemStringObj *)value.as.obj;
    wanted = index.as.i;
    if (wanted < 0) {
        return idem_unit();
    }

    pos = 0;
    current = 0;
    while (pos < obj->length) {
        size_t consumed;
        unsigned long codepoint;
        if (!idem_utf8_decode((const unsigned char *)obj->data + pos,
                              obj->length - pos, &consumed,
                              &codepoint)) {
            return idem_unit();
        }
        if (current == wanted) {
            return idem_char(codepoint);
        }
        pos += consumed;
        current += 1;
    }
    return idem_unit();
}

Value idem_chars(Value value, const IdemTypeDesc *result_type)
{
    IdemStringObj *obj;
    Value *items;
    size_t pos;
    size_t count;
    Value result;

    if (value.tag != IDEM_TAG_STRING) {
        idem_panic_str("chars expects a String");
    }
    obj = (IdemStringObj *)value.as.obj;

    count = 0;
    pos = 0;
    while (pos < obj->length) {
        size_t consumed;
        unsigned long codepoint;
        if (!idem_utf8_decode((const unsigned char *)obj->data + pos,
                              obj->length - pos, &consumed,
                              &codepoint)) {
            break;
        }
        count += 1;
        pos += consumed;
    }

    if (count == 0) {
        return idem_array(NULL, 0, result_type);
    }

    items = (Value *)malloc(sizeof(Value) * count);
    if (items == NULL) {
        idem_panic_oom();
    }
    pos = 0;
    count = 0;
    while (pos < obj->length) {
        size_t consumed;
        unsigned long codepoint;
        if (!idem_utf8_decode((const unsigned char *)obj->data + pos,
                              obj->length - pos, &consumed,
                              &codepoint)) {
            break;
        }
        items[count] = idem_char(codepoint);
        count += 1;
        pos += consumed;
    }

    result = idem_array(items, count, result_type);
    free(items);
    return result;
}

static int idem_is_digit(char c)
{
    return c >= '0' && c <= '9';
}

static int idem_decimal_int_syntax(const char *text)
{
    const char *p;
    p = text;
    if (*p == '+' || *p == '-') {
        p += 1;
    }
    if (!idem_is_digit(*p)) {
        return 0;
    }
    while (idem_is_digit(*p)) {
        p += 1;
    }
    return *p == '\0';
}

static int idem_decimal_float_syntax(const char *text)
{
    const char *p;
    int digits;

    p = text;
    if (*p == '+' || *p == '-') {
        p += 1;
    }

    digits = 0;
    while (idem_is_digit(*p)) {
        digits += 1;
        p += 1;
    }

    if (*p == '.') {
        p += 1;
        while (idem_is_digit(*p)) {
            digits += 1;
            p += 1;
        }
    }

    if (digits == 0) {
        return 0;
    }

    if (*p == 'e' || *p == 'E') {
        int exponent_digits;
        p += 1;
        if (*p == '+' || *p == '-') {
            p += 1;
        }
        exponent_digits = 0;
        while (idem_is_digit(*p)) {
            exponent_digits += 1;
            p += 1;
        }
        if (exponent_digits == 0) {
            return 0;
        }
    }

    return *p == '\0';
}

Value idem_to_int(Value value)
{
    IdemStringObj *obj;
    char *text;
    char *end;
    long parsed;
    int ok;

    if (value.tag == IDEM_TAG_CHAR) {
        return idem_int((IdemInt)value.as.c);
    }
    if (value.tag == IDEM_TAG_FLOAT) {
        double f;
        f = value.as.f;
        if (!(f >= -9223372036854775808.0 &&
              f < 9223372036854775808.0)) {
            return idem_unit();
        }
        return idem_int((IdemInt)f);
    }
    if (value.tag != IDEM_TAG_STRING) {
        idem_panic_str("toInt expects a String, Float, or Char");
    }
    obj = (IdemStringObj *)value.as.obj;
    text = (char *)malloc(obj->length + 1);
    if (text == NULL) {
        idem_panic_oom();
    }
    memcpy(text, obj->data, obj->length);
    text[obj->length] = '\0';

    errno = 0;
    end = NULL;
    parsed = strtol(text, &end, 10);
    ok = idem_decimal_int_syntax(text) &&
         end != text && *end == '\0' && errno != ERANGE;
    free(text);

    if (!ok) {
        return idem_unit();
    }
    return idem_int((IdemInt)parsed);
}

Value idem_to_float(Value value)
{
    IdemStringObj *obj;
    char *text;
    char *end;
    double parsed;
    int ok;

    if (value.tag == IDEM_TAG_INT) {
        return idem_float((double)value.as.i);
    }
    if (value.tag != IDEM_TAG_STRING) {
        idem_panic_str("toFloat expects a String or Int");
    }
    obj = (IdemStringObj *)value.as.obj;
    text = (char *)malloc(obj->length + 1);
    if (text == NULL) {
        idem_panic_oom();
    }
    memcpy(text, obj->data, obj->length);
    text[obj->length] = '\0';

    errno = 0;
    end = NULL;
    parsed = strtod(text, &end);
    ok = idem_decimal_float_syntax(text) &&
         end != text && *end == '\0' && errno != ERANGE;
    free(text);

    if (!ok) {
        return idem_unit();
    }
    return idem_float(parsed);
}

Value idem_to_bytes(Value value)
{
    IdemStringObj *obj;
    if (value.tag != IDEM_TAG_STRING) {
        idem_panic_str("toBytes expects a String");
    }
    obj = (IdemStringObj *)value.as.obj;
    return idem_bytes(obj->data, obj->length);
}

Value idem_from_bytes(Value value)
{
    IdemStringObj *obj;
    if (value.tag != IDEM_TAG_BYTES) {
        idem_panic_str("fromBytes expects Bytes");
    }
    obj = (IdemStringObj *)value.as.obj;
    if (!idem_utf8_validate((const unsigned char *)obj->data,
                            obj->length)) {
        return idem_error_value(2, idem_unit());
    }
    return idem_string(obj->data, obj->length);
}

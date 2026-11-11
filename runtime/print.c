#include "internal.h"

#include <stdio.h>
#include <string.h>

static void idem_render_bytes(IdemBuf *buf, const char *data, size_t len)
{
    size_t i;
    char escape[8];

    idem_buf_puts(buf, "b\"");
    for (i = 0; i < len; i++) {
        unsigned char c;
        c = (unsigned char)data[i];
        if (c == '\\' || c == '"') {
            idem_buf_putc(buf, '\\');
            idem_buf_putc(buf, (char)c);
        } else if (c >= 0x20 && c <= 0x7E) {
            idem_buf_putc(buf, (char)c);
        } else {
            sprintf(escape, "\\x%02X", (unsigned int)c);
            idem_buf_puts(buf, escape);
        }
    }
    idem_buf_putc(buf, '"');
}

static void idem_render_seq(IdemBuf *buf, Value value,
                            const char *open, const char *close)
{
    IdemSeqObj *seq;
    size_t i;

    seq = (IdemSeqObj *)value.as.obj;
    idem_buf_puts(buf, open);
    for (i = 0; i < seq->count; i++) {
        if (i != 0) {
            idem_buf_puts(buf, ", ");
        }
        idem_render(buf, seq->items[i]);
    }
    idem_buf_puts(buf, close);
}

void idem_render(IdemBuf *buf, Value value)
{
    size_t i;

    switch (value.tag) {
    case IDEM_TAG_UNIT:
        idem_buf_puts(buf, "()");
        break;
    case IDEM_TAG_BOOL:
        idem_buf_puts(buf, value.as.i ? "true" : "false");
        break;
    case IDEM_TAG_INT:
        idem_buf_int(buf, value.as.i);
        break;
    case IDEM_TAG_FLOAT:
        idem_buf_float(buf, value.as.f);
        break;
    case IDEM_TAG_CHAR: {
        char encoded[4];
        size_t encoded_len;
        if (idem_utf8_encode(value.as.c, encoded, &encoded_len)) {
            idem_buf_write(buf, encoded, encoded_len);
        }
        break;
    }
    case IDEM_TAG_STRING: {
        IdemStringObj *obj;
        obj = (IdemStringObj *)value.as.obj;
        idem_buf_write(buf, obj->data, obj->length);
        break;
    }
    case IDEM_TAG_BYTES: {
        IdemStringObj *obj;
        obj = (IdemStringObj *)value.as.obj;
        idem_render_bytes(buf, obj->data, obj->length);
        break;
    }
    case IDEM_TAG_ARRAY:
        idem_render_seq(buf, value, "[", "]");
        break;
    case IDEM_TAG_LIST:
        idem_render_seq(buf, value, "List[", "]");
        break;
    case IDEM_TAG_TUPLE:
        idem_render_seq(buf, value, "(", ")");
        break;
    case IDEM_TAG_MAP: {
        IdemMapObj *map;
        map = (IdemMapObj *)value.as.obj;
        idem_buf_puts(buf, "Map{");
        if (map->count != 0) {
            idem_buf_putc(buf, ' ');
        }
        for (i = 0; i < map->count; i++) {
            if (i != 0) {
                idem_buf_puts(buf, ", ");
            }
            idem_render(buf, map->keys[i]);
            idem_buf_puts(buf, ": ");
            idem_render(buf, map->values[i]);
        }
        idem_buf_puts(buf, " }");
        break;
    }
    case IDEM_TAG_STRUCT: {
        IdemStructObj *st;
        st = (IdemStructObj *)value.as.obj;
        idem_buf_puts(buf, st->type != NULL ? st->type->name : "?");
        idem_buf_puts(buf, " {");
        if (st->count != 0) {
            idem_buf_putc(buf, ' ');
        }
        for (i = 0; i < st->count; i++) {
            if (i != 0) {
                idem_buf_puts(buf, ", ");
            }
            if (st->type != NULL && i < st->type->field_count) {
                idem_buf_puts(buf, st->type->field_names[i]);
            } else {
                idem_buf_int(buf, (IdemInt)i);
            }
            idem_buf_puts(buf, ": ");
            idem_render(buf, st->fields[i]);
        }
        idem_buf_puts(buf, " }");
        break;
    }
    case IDEM_TAG_ENUM: {
        IdemEnumObj *en;
        en = (IdemEnumObj *)value.as.obj;
        if (en->type != NULL && en->variant < en->type->variant_count) {
            idem_buf_puts(buf, en->type->variants[en->variant].name);
        } else {
            idem_buf_puts(buf, "?");
        }
        if (en->count != 0) {
            idem_buf_putc(buf, '(');
            for (i = 0; i < en->count; i++) {
                if (i != 0) {
                    idem_buf_puts(buf, ", ");
                }
                idem_render(buf, en->payload[i]);
            }
            idem_buf_putc(buf, ')');
        }
        break;
    }
    case IDEM_TAG_CLOSURE:
        idem_buf_puts(buf, "<function>");
        break;
    default:
        idem_buf_puts(buf, "<?>");
        break;
    }
}

Value idem_to_string(Value value)
{
    IdemBuf buf;
    Value result;

    idem_buf_init(&buf);
    idem_render(&buf, value);
    result = idem_string_from_buf(&buf);
    idem_buf_free(&buf);
    return result;
}

#include "internal.h"

#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* UTF-8                                                               */
/* ------------------------------------------------------------------ */

int idem_utf8_decode(const unsigned char *data, size_t len,
                     size_t *consumed, unsigned long *codepoint)
{
    unsigned char c;
    unsigned int need;
    unsigned long value;
    unsigned long minimum;
    unsigned int i;

    if (len == 0) {
        return 0;
    }

    c = data[0];
    if (c < 0x80) {
        *consumed = 1;
        *codepoint = c;
        return 1;
    }

    if ((c & 0xE0) == 0xC0) {
        need = 1;
        value = c & 0x1F;
        minimum = 0x80;
    } else if ((c & 0xF0) == 0xE0) {
        need = 2;
        value = c & 0x0F;
        minimum = 0x800;
    } else if ((c & 0xF8) == 0xF0) {
        need = 3;
        value = c & 0x07;
        minimum = 0x10000;
    } else {
        return 0;
    }

    if (len < (size_t)need + 1) {
        return 0;
    }

    for (i = 1; i <= need; i++) {
        if ((data[i] & 0xC0) != 0x80) {
            return 0;
        }
        value = (value << 6) | (data[i] & 0x3F);
    }

    if (value < minimum) {
        return 0;
    }
    if (value >= 0xD800 && value <= 0xDFFF) {
        return 0;
    }
    if (value > 0x10FFFFUL) {
        return 0;
    }

    *consumed = (size_t)need + 1;
    *codepoint = value;
    return 1;
}

int idem_utf8_validate(const unsigned char *data, size_t len)
{
    size_t pos;
    pos = 0;
    while (pos < len) {
        size_t consumed;
        unsigned long codepoint;
        if (!idem_utf8_decode(data + pos, len - pos, &consumed,
                              &codepoint)) {
            return 0;
        }
        pos += consumed;
    }
    return 1;
}

int idem_utf8_encode(unsigned long codepoint, char *out, size_t *out_len)
{
    if (codepoint <= 0x7F) {
        out[0] = (char)codepoint;
        *out_len = 1;
    } else if (codepoint <= 0x7FF) {
        out[0] = (char)(0xC0 | (codepoint >> 6));
        out[1] = (char)(0x80 | (codepoint & 0x3F));
        *out_len = 2;
    } else if (codepoint <= 0xFFFF) {
        out[0] = (char)(0xE0 | (codepoint >> 12));
        out[1] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        out[2] = (char)(0x80 | (codepoint & 0x3F));
        *out_len = 3;
    } else if (codepoint <= 0x10FFFF) {
        out[0] = (char)(0xF0 | (codepoint >> 18));
        out[1] = (char)(0x80 | ((codepoint >> 12) & 0x3F));
        out[2] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        out[3] = (char)(0x80 | (codepoint & 0x3F));
        *out_len = 4;
    } else {
        *out_len = 0;
        return 0;
    }
    return 1;
}

int idem_bytes_compare_raw(const char *a, size_t a_len,
                           const char *b, size_t b_len)
{
    size_t n;
    int cmp;
    n = a_len < b_len ? a_len : b_len;
    cmp = n == 0 ? 0 : memcmp(a, b, n);
    if (cmp < 0) return -1;
    if (cmp > 0) return 1;
    if (a_len < b_len) return -1;
    if (a_len > b_len) return 1;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Growable byte buffer                                                */
/* ------------------------------------------------------------------ */

void idem_buf_init(IdemBuf *buf)
{
    buf->data = NULL;
    buf->len = 0;
    buf->cap = 0;
}

void idem_buf_free(IdemBuf *buf)
{
    free(buf->data);
    buf->data = NULL;
    buf->len = 0;
    buf->cap = 0;
}

static void idem_buf_reserve(IdemBuf *buf, size_t extra)
{
    size_t need;
    size_t cap;
    char *data;

    need = buf->len + extra + 1;
    if (need <= buf->cap) {
        return;
    }

    cap = buf->cap == 0 ? 64 : buf->cap;
    while (cap < need) {
        cap *= 2;
    }

    data = (char *)realloc(buf->data, cap);
    if (data == NULL) {
        idem_panic_oom();
    }
    buf->data = data;
    buf->cap = cap;
}

void idem_buf_putc(IdemBuf *buf, char c)
{
    idem_buf_reserve(buf, 1);
    buf->data[buf->len] = c;
    buf->len += 1;
    buf->data[buf->len] = '\0';
}

void idem_buf_write(IdemBuf *buf, const char *data, size_t len)
{
    if (len == 0) {
        return;
    }
    idem_buf_reserve(buf, len);
    memcpy(buf->data + buf->len, data, len);
    buf->len += len;
    buf->data[buf->len] = '\0';
}

void idem_buf_puts(IdemBuf *buf, const char *text)
{
    idem_buf_write(buf, text, strlen(text));
}

void idem_buf_int(IdemBuf *buf, IdemInt value)
{
    char tmp[64];
    sprintf(tmp, "%ld", value);
    idem_buf_puts(buf, tmp);
}

void idem_buf_float(IdemBuf *buf, double value)
{
    char tmp[64];
    int precision;
    double back;

    if (value != value) {
        idem_buf_puts(buf, "NaN");
        return;
    }
    if (value > DBL_MAX) {
        idem_buf_puts(buf, "inf");
        return;
    }
    if (value < -DBL_MAX) {
        idem_buf_puts(buf, "-inf");
        return;
    }
    if (value == 0.0) {
        if (1.0 / value < 0.0) {
            idem_buf_puts(buf, "-0.0");
        } else {
            idem_buf_puts(buf, "0.0");
        }
        return;
    }

    for (precision = 1; precision <= 17; precision++) {
        sprintf(tmp, "%.*g", precision, value);
        back = strtod(tmp, NULL);
        if (back == value) {
            break;
        }
    }

    {
        char normalized[64];
        size_t in;
        size_t out;
        in = 0;
        out = 0;
        while (tmp[in] != '\0' && out + 1 < sizeof(normalized)) {
            if (tmp[in] == 'e' &&
                (tmp[in + 1] == '+' || tmp[in + 1] == '-')) {
                char sign;
                sign = tmp[in + 1];
                normalized[out++] = 'e';
                in += 2;
                if (sign == '-') {
                    normalized[out++] = '-';
                }
                while (tmp[in] == '0' && tmp[in + 1] != '\0') {
                    in += 1;
                }
                continue;
            }
            normalized[out++] = tmp[in++];
        }
        normalized[out] = '\0';
        idem_buf_puts(buf, normalized);
    }
}

Value idem_string_from_buf(IdemBuf *buf)
{
    return idem_string(buf->data != NULL ? buf->data : "", buf->len);
}

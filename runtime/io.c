#include "internal.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Value idem_print(Value value, int newline)
{
    IdemBuf buf;

    idem_buf_init(&buf);
    idem_render(&buf, value);
    if (buf.len != 0) {
        fwrite(buf.data, 1, buf.len, stdout);
    }
    if (newline) {
        fputc('\n', stdout);
    }
    idem_buf_free(&buf);
    return idem_unit();
}

Value idem_flush(void)
{
    fflush(stdout);
    return idem_unit();
}

Value idem_exit(Value code)
{
    int status;
    status = code.tag == IDEM_TAG_INT ? (int)(code.as.i & 0xFF) : 0;
    fflush(stdout);
    exit(status);
    return idem_unit();
}

Value idem_panic(Value message)
{
    IdemStringObj *obj;

    fflush(stdout);
    if (message.tag == IDEM_TAG_STRING && message.as.obj != NULL) {
        obj = (IdemStringObj *)message.as.obj;
        fprintf(stderr, "panic: ");
        fwrite(obj->data, 1, obj->length, stderr);
        fputc('\n', stderr);
    } else {
        fprintf(stderr, "panic\n");
    }
    exit(101);
    return idem_unit();
}

Value idem_read_line(void)
{
    size_t len;
    size_t cap;
    char *data;
    int c;
    Value result;

    len = 0;
    cap = 64;
    data = (char *)malloc(cap);
    if (data == NULL) {
        idem_panic_oom();
    }

    for (;;) {
        c = fgetc(stdin);
        if (c == EOF) {
            break;
        }
        if (c == '\n') {
            break;
        }
        if (len + 1 >= cap) {
            char *grown;
            cap *= 2;
            grown = (char *)realloc(data, cap);
            if (grown == NULL) {
                free(data);
                idem_panic_oom();
            }
            data = grown;
        }
        data[len++] = (char)c;
    }

    if (c == EOF && len == 0) {
        free(data);
        return idem_unit();
    }
    if (len != 0 && data[len - 1] == '\r') {
        len -= 1;
    }

    result = idem_string(data, len);
    free(data);
    return result;
}

static Value idem_error_from_errno(int error)
{
    if (error == ENOENT) {
        return idem_error_value(0, idem_unit());
    }
    if (error == EACCES || error == EPERM) {
        return idem_error_value(1, idem_unit());
    }
    return idem_error_io(strerror(error));
}

Value idem_read_file(Value path)
{
    IdemStringObj *path_obj;
    FILE *file;
    char *buffer;
    size_t len;
    size_t cap;
    Value result;

    if (path.tag != IDEM_TAG_STRING) {
        idem_panic_str("readFile expects a String path");
    }
    path_obj = (IdemStringObj *)path.as.obj;

    {
        char *name;
        name = (char *)malloc(path_obj->length + 1);
        if (name == NULL) {
            idem_panic_oom();
        }
        memcpy(name, path_obj->data, path_obj->length);
        name[path_obj->length] = '\0';
        file = fopen(name, "rb");
        if (file == NULL) {
            int error;
            error = errno;
            free(name);
            return idem_error_from_errno(error);
        }
        free(name);
    }

    len = 0;
    cap = 4096;
    buffer = (char *)malloc(cap);
    if (buffer == NULL) {
        fclose(file);
        idem_panic_oom();
    }

    for (;;) {
        size_t read_count;
        if (len == cap) {
            char *grown;
            cap *= 2;
            grown = (char *)realloc(buffer, cap);
            if (grown == NULL) {
                free(buffer);
                fclose(file);
                idem_panic_oom();
            }
            buffer = grown;
        }
        read_count = fread(buffer + len, 1, cap - len, file);
        len += read_count;
        if (read_count == 0) {
            break;
        }
    }

    if (ferror(file)) {
        int error;
        error = errno;
        free(buffer);
        fclose(file);
        return idem_error_from_errno(error);
    }
    fclose(file);

    if (!idem_utf8_validate((const unsigned char *)buffer, len)) {
        free(buffer);
        return idem_error_value(2, idem_unit());
    }

    result = idem_string(buffer, len);
    free(buffer);
    return result;
}

Value idem_write_file(Value path, Value contents)
{
    IdemStringObj *path_obj;
    IdemStringObj *data_obj;
    FILE *file;
    char *name;
    int failed;

    if (path.tag != IDEM_TAG_STRING || contents.tag != IDEM_TAG_STRING) {
        idem_panic_str("writeFile expects two Strings");
    }

    path_obj = (IdemStringObj *)path.as.obj;
    data_obj = (IdemStringObj *)contents.as.obj;

    name = (char *)malloc(path_obj->length + 1);
    if (name == NULL) {
        idem_panic_oom();
    }
    memcpy(name, path_obj->data, path_obj->length);
    name[path_obj->length] = '\0';

    file = fopen(name, "wb");
    if (file == NULL) {
        int error;
        error = errno;
        free(name);
        return idem_error_from_errno(error);
    }
    free(name);

    failed = 0;
    if (data_obj->length != 0) {
        if (fwrite(data_obj->data, 1, data_obj->length, file) !=
            data_obj->length) {
            failed = 1;
        }
    }
    if (fclose(file) != 0) {
        failed = 1;
    }

    if (failed) {
        return idem_error_io("write failed");
    }
    return idem_unit();
}

#include "codegen.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Output buffer                                                       */
/* ------------------------------------------------------------------ */

typedef struct CgBuf {
    char *data;
    size_t len;
    size_t cap;
} CgBuf;

static void *cg_alloc(Codegen *cg, size_t size)
{
    void *p;
    p = arena_alloc(cg->arena, size);
    if (p == NULL) {
        fprintf(stderr, "idem: out of memory\n");
        exit(1);
    }
    memset(p, 0, size);
    return p;
}

static void cg_error(Codegen *cg, const char *message)
{
    if (cg->had_error) {
        return;
    }
    cg->had_error = 1;
    strcpy(cg->error_message, message);
}

static void cg_buf_init(CgBuf *buf)
{
    buf->data = NULL;
    buf->len = 0;
    buf->cap = 0;
}

static void cg_buf_free(CgBuf *buf)
{
    free(buf->data);
    buf->data = NULL;
    buf->len = 0;
    buf->cap = 0;
}

static void cg_buf_reserve(CgBuf *buf, size_t extra)
{
    size_t need;
    size_t cap;
    char *data;

    need = buf->len + extra + 1;
    if (need <= buf->cap) {
        return;
    }
    cap = buf->cap == 0 ? 256 : buf->cap;
    while (cap < need) {
        cap *= 2;
    }
    data = (char *)realloc(buf->data, cap);
    if (data == NULL) {
        fprintf(stderr, "idem: out of memory\n");
        exit(1);
    }
    buf->data = data;
    buf->cap = cap;
}

static void cg_putc(CgBuf *buf, char c)
{
    cg_buf_reserve(buf, 1);
    buf->data[buf->len++] = c;
    buf->data[buf->len] = '\0';
}

static void cg_write(CgBuf *buf, const char *text, size_t len)
{
    if (len == 0) {
        return;
    }
    cg_buf_reserve(buf, len);
    memcpy(buf->data + buf->len, text, len);
    buf->len += len;
    buf->data[buf->len] = '\0';
}

static void cg_puts(CgBuf *buf, const char *text)
{
    cg_write(buf, text, strlen(text));
}

static void cg_printf(CgBuf *buf, const char *fmt, ...)
{
    char tmp[1024];
    va_list ap;
    va_start(ap, fmt);
    vsprintf(tmp, fmt, ap);
    va_end(ap);
    cg_puts(buf, tmp);
}

static void cg_indent(CgBuf *buf, int indent)
{
    int i;
    for (i = 0; i < indent; i++) {
        cg_puts(buf, "    ");
    }
}

static void cg_c_string(CgBuf *buf, const char *data, size_t len)
{
    size_t i;
    char escape[8];

    cg_putc(buf, '"');
    for (i = 0; i < len; i++) {
        unsigned char c;
        c = (unsigned char)data[i];
        if (c == '"' || c == '\\') {
            cg_putc(buf, '\\');
            cg_putc(buf, (char)c);
        } else if (c == '?') {
            cg_puts(buf, "\\?");
        } else if (c == '\n') {
            cg_puts(buf, "\\n");
        } else if (c == '\r') {
            cg_puts(buf, "\\r");
        } else if (c == '\t') {
            cg_puts(buf, "\\t");
        } else if (c >= 0x20 && c <= 0x7E) {
            cg_putc(buf, (char)c);
        } else {
            sprintf(escape, "\\%03o", (unsigned int)c);
            cg_puts(buf, escape);
        }
    }
    cg_putc(buf, '"');
}

static const char *cg_tag_name(TypeKind kind)
{
    switch (kind) {
    case TY_BOOL:     return "IDEM_TAG_BOOL";
    case TY_INT:      return "IDEM_TAG_INT";
    case TY_FLOAT:    return "IDEM_TAG_FLOAT";
    case TY_CHAR:     return "IDEM_TAG_CHAR";
    case TY_STRING:   return "IDEM_TAG_STRING";
    case TY_BYTES:    return "IDEM_TAG_BYTES";
    case TY_ARRAY:    return "IDEM_TAG_ARRAY";
    case TY_LIST:     return "IDEM_TAG_LIST";
    case TY_MAP:      return "IDEM_TAG_MAP";
    case TY_TUPLE:    return "IDEM_TAG_TUPLE";
    case TY_FUNCTION: return "IDEM_TAG_CLOSURE";
    case TY_STRUCT:   return "IDEM_TAG_STRUCT";
    case TY_ENUM:     return "IDEM_TAG_ENUM";
    case TY_UNIT:
    default:          return "IDEM_TAG_UNIT";
    }
}

static void cg_desc(Codegen *cg, CgBuf *buf, TypeInfo *info)
{
    if (info == cg->program->resolver->error_info) {
        cg_puts(buf, "&idem_error_type");
    } else {
        cg_printf(buf, "&idem_type_%lu", (unsigned long)info->id);
    }
}

static int cg_needs_structural_desc(Type *type)
{
    if (type == NULL) {
        return 0;
    }
    switch (type->kind) {
    case TY_ARRAY:
    case TY_LIST:
    case TY_MAP:
    case TY_TUPLE:
    case TY_FUNCTION:
    case TY_UNION:
        return 1;
    default:
        return 0;
    }
}

static void cg_type_ref(Codegen *cg, CgBuf *buf, Type *type)
{
    CgTypeDesc *desc;

    if (type == NULL) {
        cg_puts(buf, "NULL");
        return;
    }
    if (type->kind == TY_STRUCT || type->kind == TY_ENUM) {
        cg_desc(cg, buf, type->as.nominal.info);
        return;
    }
    if (!cg_needs_structural_desc(type)) {
        cg_puts(buf, "NULL");
        return;
    }

    for (desc = cg->type_descs; desc != NULL; desc = desc->next) {
        if (type_equal(desc->type, type)) {
            cg_printf(buf, "&idem_stype_%d", desc->id);
            return;
        }
    }

    desc = (CgTypeDesc *)cg_alloc(cg, sizeof(CgTypeDesc));
    desc->type = type;
    desc->id = cg->next_type_desc;
    cg->next_type_desc += 1;
    desc->next = cg->type_descs;
    cg->type_descs = desc;
    cg_printf(buf, "&idem_stype_%d", desc->id);
}

static void cg_emit_structural_descriptors(Codegen *cg, CgBuf *out)
{
    CgTypeDesc *desc;
    char name[512];

    cg_puts(out, "/* --- structural type descriptors --- */\n\n");
    for (desc = cg->type_descs; desc != NULL; desc = desc->next) {
        type_format(desc->type, name, sizeof(name));
        cg_printf(out, "const IdemTypeDesc idem_stype_%d = {\n",
                  desc->id);
        cg_printf(out, "    0U, IDEM_DESC_STRUCTURAL, ");
        cg_c_string(out, name, strlen(name));
        cg_puts(out, ", 0, NULL, 0, NULL\n};\n\n");
    }
}

static void cg_function_name(CgBuf *buf, IrFunction *function)
{
    if (function->is_lambda) {
        cg_printf(buf, "idem_lambda_%d", function->id);
    } else {
        cg_printf(buf, "idem_fn_%d_", function->id);
        cg_write(buf, function->name, function->name_len);
    }
}

static const char *cg_function_name_str(Codegen *cg, IrFunction *function)
{
    CgBuf buf;
    char *result;

    cg_buf_init(&buf);
    cg_function_name(&buf, function);
    result = (char *)cg_alloc(cg, buf.len + 1);
    memcpy(result, buf.data != NULL ? buf.data : "", buf.len);
    result[buf.len] = '\0';
    cg_buf_free(&buf);
    return result;
}

/* ------------------------------------------------------------------ */
/* Type descriptors                                                    */
/* ------------------------------------------------------------------ */

static void cg_emit_descriptors(Codegen *cg, CgBuf *out)
{
    Resolver *resolver;
    Module *module;
    Symbol *symbol;

    resolver = cg->program->resolver;
    cg_puts(out, "/* --- type descriptors --- */\n\n");

    for (module = resolver->modules; module != NULL; module = module->next) {
        if (module->failed) {
            continue;
        }
        for (symbol = module->symbols; symbol != NULL;
             symbol = symbol->next) {
            TypeInfo *info;
            TypeField *field;
            TypeVariant *variant;
            size_t field_count;
            size_t variant_count;

            if (symbol->kind != SYM_STRUCT && symbol->kind != SYM_ENUM) {
                continue;
            }
            info = symbol->as.nominal.info;
            if (info == resolver->error_info) {
                continue;
            }

            field_count = 0;
            for (field = info->fields; field != NULL; field = field->next) {
                field_count += 1;
            }
            variant_count = 0;
            for (variant = info->variants; variant != NULL;
                 variant = variant->next) {
                variant_count += 1;
            }

            if (symbol->kind == SYM_STRUCT && field_count != 0) {
                cg_printf(out, "const char *const "
                               "idem_field_names_%lu[] = {\n",
                          (unsigned long)info->id);
                for (field = info->fields; field != NULL;
                     field = field->next) {
                    cg_puts(out, "    ");
                    cg_c_string(out, field->name, field->name_len);
                    cg_puts(out, ",\n");
                }
                cg_puts(out, "};\n");
            }

            if (symbol->kind == SYM_ENUM && variant_count != 0) {
                cg_printf(out, "const IdemVariantDesc "
                               "idem_variants_%lu[] = {\n",
                          (unsigned long)info->id);
                for (variant = info->variants; variant != NULL;
                     variant = variant->next) {
                    size_t payload_count;
                    TypeList *payload;
                    payload_count = 0;
                    for (payload = variant->payload; payload != NULL;
                         payload = payload->next) {
                        payload_count += 1;
                    }
                    cg_puts(out, "    { ");
                    cg_c_string(out, variant->name, variant->name_len);
                    cg_printf(out, ", %lu },\n",
                              (unsigned long)payload_count);
                }
                cg_puts(out, "};\n");
            }

            cg_printf(out, "IdemTypeDesc idem_type_%lu = {\n",
                      (unsigned long)info->id);
            cg_printf(out, "    %luU, %s, ",
                      (unsigned long)info->id,
                      symbol->kind == SYM_STRUCT
                          ? "IDEM_DESC_STRUCT" : "IDEM_DESC_ENUM");
            cg_c_string(out, info->name, info->name_len);
            cg_printf(out, ", %lu, ", (unsigned long)field_count);
            if (field_count != 0) {
                cg_printf(out, "idem_field_names_%lu",
                          (unsigned long)info->id);
            } else {
                cg_puts(out, "NULL");
            }
            cg_printf(out, ", %lu, ", (unsigned long)variant_count);
            if (variant_count != 0) {
                cg_printf(out, "idem_variants_%lu",
                          (unsigned long)info->id);
            } else {
                cg_puts(out, "NULL");
            }
            cg_puts(out, "\n};\n\n");
        }
    }
}

/* ------------------------------------------------------------------ */
/* Pattern tests                                                       */
/* ------------------------------------------------------------------ */

static AstPattern *cg_find_pattern_field(AstPatternField *fields,
                                         const char *name, size_t len)
{
    AstPatternField *field;
    for (field = fields; field != NULL; field = field->next) {
        if (field->name_len == len &&
            memcmp(field->name, name, len) == 0) {
            return field->pattern;
        }
    }
    return NULL;
}

static int cg_variant_index(TypeInfo *info, const char *name, size_t len)
{
    TypeVariant *variant;
    int index;
    index = 0;
    for (variant = info->variants; variant != NULL;
         variant = variant->next) {
        if (variant->name_len == len &&
            memcmp(variant->name, name, len) == 0) {
            return index;
        }
        index += 1;
    }
    return -1;
}

static const char *cg_project(Codegen *cg, const char *function,
                              const char *value, int index)
{
    char *result;
    size_t size;

    size = strlen(function) + strlen(value) + 48;
    result = (char *)cg_alloc(cg, size);
    sprintf(result, "%s(%s, %d)", function, value, index);
    return result;
}

static void cg_float_literal(CgBuf *buf, double value)
{
    char tmp[64];
    size_t i;
    int has_marker;

    sprintf(tmp, "%.17g", value);
    has_marker = 0;
    for (i = 0; tmp[i] != '\0'; i++) {
        if (tmp[i] == '.' || tmp[i] == 'e' || tmp[i] == 'E') {
            has_marker = 1;
            break;
        }
    }
    cg_puts(buf, tmp);
    if (!has_marker) {
        cg_puts(buf, ".0");
    }
}

static void cg_int_parts(CgBuf *buf, u64 value)
{
    cg_printf(buf, "0x%04XU, 0x%04XU, 0x%04XU, 0x%04XU",
              (unsigned int)value.w[0], (unsigned int)value.w[1],
              (unsigned int)value.w[2], (unsigned int)value.w[3]);
}

static void cg_pattern_test(Codegen *cg, CgBuf *buf, AstPattern *pattern,
                            const char *value)
{
    if (pattern == NULL) {
        cg_puts(buf, "(1)");
        return;
    }

    switch (pattern->kind) {
    case PAT_WILDCARD:
        cg_puts(buf, "(1)");
        break;

    case PAT_VAR:
        if (!pattern->is_binding && pattern->sem_type != NULL &&
            pattern->sem_type->kind == TY_ENUM) {
            TypeInfo *info;
            int index;
            info = pattern->sem_type->as.nominal.info;
            index = cg_variant_index(info, pattern->as.var.name,
                                     pattern->as.var.name_len);
            if (index >= 0) {
                cg_puts(buf, "(");
                cg_puts(buf, value);
                cg_puts(buf, ".tag == IDEM_TAG_ENUM && idem_enum_type(");
                cg_puts(buf, value);
                cg_puts(buf, ") == ");
                cg_desc(cg, buf, info);
                cg_printf(buf, " && idem_enum_variant(%s) == %d)",
                          value, index);
                break;
            }
        }
        cg_puts(buf, "(1)");
        break;

    case PAT_UNIT:
        cg_putc(buf, '(');
        cg_puts(buf, value);
        cg_puts(buf, ".tag == IDEM_TAG_UNIT)");
        break;

    case PAT_LITERAL: {
        AstExpr *literal;
        literal = pattern->as.literal.literal;
        if (literal == NULL) {
            cg_puts(buf, "(1)");
            break;
        }
        switch (literal->kind) {
        case EXPR_INT:
            cg_puts(buf, "idem_int_eq_lit(");
            cg_puts(buf, value);
            cg_puts(buf, ", ");
            cg_int_parts(buf, literal->as.int_lit.value);
            cg_puts(buf, ")");
            break;
        case EXPR_FLOAT:
            cg_puts(buf, "idem_float_eq_lit(");
            cg_puts(buf, value);
            cg_puts(buf, ", ");
            cg_float_literal(buf, literal->as.float_lit.value);
            cg_puts(buf, ")");
            break;
        case EXPR_BOOL:
            cg_printf(buf, "idem_bool_eq_lit(%s, %d)", value,
                      literal->as.bool_lit.value ? 1 : 0);
            break;
        case EXPR_CHAR:
            cg_printf(buf, "idem_char_eq_lit(%s, 0x%lXUL)", value,
                      (unsigned long)literal->as.char_lit.codepoint);
            break;
        case EXPR_STRING:
            cg_puts(buf, "idem_string_eq_lit(");
            cg_puts(buf, value);
            cg_puts(buf, ", ");
            cg_c_string(buf, literal->as.str_lit.data,
                        literal->as.str_lit.len);
            cg_printf(buf, ", %lu)",
                      (unsigned long)literal->as.str_lit.len);
            break;
        case EXPR_BYTES:
            cg_puts(buf, "idem_bytes_eq_lit(");
            cg_puts(buf, value);
            cg_puts(buf, ", ");
            cg_c_string(buf, literal->as.str_lit.data,
                        literal->as.str_lit.len);
            cg_printf(buf, ", %lu)",
                      (unsigned long)literal->as.str_lit.len);
            break;
        default:
            cg_puts(buf, "(1)");
            break;
        }
        break;
    }

    case PAT_TUPLE: {
        AstPatternList *item;
        int index;
        int first;

        cg_puts(buf, "(");
        cg_puts(buf, value);
        cg_puts(buf, ".tag == IDEM_TAG_TUPLE");
        index = 0;
        first = 1;
        for (item = pattern->as.tuple.items; item != NULL;
             item = item->next) {
            const char *sub;
            sub = cg_project(cg, "idem_tuple_get", value, index);
            if (first) {
                cg_puts(buf, " && ");
                first = 0;
            } else {
                cg_puts(buf, " && ");
            }
            cg_pattern_test(cg, buf, item->pattern, sub);
            index += 1;
        }
        cg_putc(buf, ')');
        break;
    }

    case PAT_STRUCT: {
        TypeInfo *info;
        TypeField *field;
        int index;
        int first;

        if (pattern->sem_type == NULL ||
            pattern->sem_type->kind != TY_STRUCT) {
            cg_puts(buf, "(1)");
            break;
        }
        info = pattern->sem_type->as.nominal.info;
        cg_puts(buf, "(");
        cg_puts(buf, value);
        cg_puts(buf, ".tag == IDEM_TAG_STRUCT && idem_struct_type(");
        cg_puts(buf, value);
        cg_puts(buf, ") == ");
        cg_desc(cg, buf, info);
        index = 0;
        first = 1;
        for (field = info->fields; field != NULL; field = field->next) {
            AstPattern *sub;
            sub = cg_find_pattern_field(pattern->as.struct_.fields,
                                        field->name, field->name_len);
            if (sub != NULL) {
                const char *field_value;
                field_value = cg_project(cg, "idem_struct_get", value,
                                         index);
                cg_puts(buf, " && ");
                cg_pattern_test(cg, buf, sub, field_value);
                first = 0;
            }
            index += 1;
        }
        cg_putc(buf, ')');
        (void)first;
        break;
    }

    case PAT_ENUM: {
        TypeInfo *info;
        AstPatternList *item;
        int variant;
        int index;

        if (pattern->sem_type == NULL ||
            pattern->sem_type->kind != TY_ENUM) {
            cg_puts(buf, "(1)");
            break;
        }
        info = pattern->sem_type->as.nominal.info;
        variant = cg_variant_index(info, pattern->as.enum_.name,
                                   pattern->as.enum_.name_len);
        if (variant < 0) {
            cg_puts(buf, "(0)");
            break;
        }
        cg_puts(buf, "(");
        cg_puts(buf, value);
        cg_puts(buf, ".tag == IDEM_TAG_ENUM && idem_enum_type(");
        cg_puts(buf, value);
        cg_puts(buf, ") == ");
        cg_desc(cg, buf, info);
        cg_printf(buf, " && idem_enum_variant(%s) == %d", value,
                  variant);
        index = 0;
        for (item = pattern->as.enum_.payload; item != NULL;
             item = item->next) {
            const char *sub;
            sub = cg_project(cg, "idem_enum_get", value, index);
            cg_puts(buf, " && ");
            cg_pattern_test(cg, buf, item->pattern, sub);
            index += 1;
        }
        cg_putc(buf, ')');
        break;
    }

    case PAT_TYPE: {
        Type *member;
        member = pattern->sem_type;
        if (member == NULL) {
            cg_puts(buf, "(1)");
            break;
        }
        cg_puts(buf, "idem_value_is_type(");
        cg_puts(buf, value);
        cg_puts(buf, ", ");
        cg_puts(buf, cg_tag_name(member->kind));
        cg_puts(buf, ", ");
        cg_type_ref(cg, buf, member);
        cg_putc(buf, ')');
        break;
    }

    case PAT_ERROR:
        cg_puts(buf, "(0)");
        break;
    }
}

/* ------------------------------------------------------------------ */
/* Expressions                                                         */
/* ------------------------------------------------------------------ */

static int cg_name_eq(const char *name, size_t len, const char *literal)
{
    size_t n;
    n = strlen(literal);
    return n == len && memcmp(name, literal, len) == 0;
}

static void cg_builtin_call(Codegen *cg, CgBuf *buf, IrExpr *expr)
{
    const char *name;
    size_t len;
    int *args;

    (void)cg;
    name = expr->as.builtin.name;
    len = expr->as.builtin.name_len;
    args = expr->as.builtin.args;

    if (cg_name_eq(name, len, "print")) {
        cg_printf(buf, "idem_print(slots[%d], 0)", args[0]);
    } else if (cg_name_eq(name, len, "println")) {
        cg_printf(buf, "idem_print(slots[%d], 1)", args[0]);
    } else if (cg_name_eq(name, len, "readLine")) {
        cg_puts(buf, "idem_read_line()");
    } else if (cg_name_eq(name, len, "readFile")) {
        cg_printf(buf, "idem_read_file(slots[%d])", args[0]);
    } else if (cg_name_eq(name, len, "writeFile")) {
        cg_printf(buf, "idem_write_file(slots[%d], slots[%d])",
                  args[0], args[1]);
    } else if (cg_name_eq(name, len, "flush")) {
        cg_puts(buf, "idem_flush()");
    } else if (cg_name_eq(name, len, "exit")) {
        cg_printf(buf, "idem_exit(slots[%d])", args[0]);
    } else if (cg_name_eq(name, len, "panic")) {
        cg_printf(buf, "idem_panic(slots[%d])", args[0]);
    } else if (cg_name_eq(name, len, "len")) {
        cg_printf(buf, "idem_len(slots[%d])", args[0]);
    } else if (cg_name_eq(name, len, "get")) {
        cg_printf(buf, "idem_get(slots[%d], slots[%d])",
                  args[0], args[1]);
    } else if (cg_name_eq(name, len, "set")) {
        cg_printf(buf, "idem_set(slots[%d], slots[%d], slots[%d])",
                  args[0], args[1], args[2]);
    } else if (cg_name_eq(name, len, "remove")) {
        cg_printf(buf, "idem_remove(slots[%d], slots[%d])",
                  args[0], args[1]);
    } else if (cg_name_eq(name, len, "push")) {
        cg_printf(buf, "idem_push(slots[%d], slots[%d])",
                  args[0], args[1]);
    } else if (cg_name_eq(name, len, "concat")) {
        cg_printf(buf, "idem_concat(slots[%d], slots[%d])",
                  args[0], args[1]);
    } else if (cg_name_eq(name, len, "map")) {
        cg_printf(buf, "idem_map(slots[%d], slots[%d], ",
                  args[0], args[1]);
        cg_type_ref(cg, buf, expr->type);
        cg_putc(buf, ')');
    } else if (cg_name_eq(name, len, "filter")) {
        cg_printf(buf, "idem_filter(slots[%d], slots[%d])",
                  args[0], args[1]);
    } else if (cg_name_eq(name, len, "fold")) {
        cg_printf(buf, "idem_fold(slots[%d], slots[%d], slots[%d])",
                  args[0], args[1], args[2]);
    } else if (cg_name_eq(name, len, "sum")) {
        Type *collection;
        Type *element;
        collection = expr->as.builtin.arg_type;
        element = NULL;
        if (collection != NULL && collection->kind == TY_ARRAY) {
            element = collection->as.array.element;
        } else if (collection != NULL && collection->kind == TY_LIST) {
            element = collection->as.list.element;
        }
        if (element != NULL && element->kind == TY_FLOAT) {
            cg_printf(buf, "idem_sum_float(slots[%d])", args[0]);
        } else {
            cg_printf(buf, "idem_sum_int(slots[%d])", args[0]);
        }
    } else if (cg_name_eq(name, len, "keys")) {
        cg_printf(buf, "idem_keys(slots[%d], ", args[0]);
        cg_type_ref(cg, buf, expr->type);
        cg_putc(buf, ')');
    } else if (cg_name_eq(name, len, "values")) {
        cg_printf(buf, "idem_values(slots[%d], ", args[0]);
        cg_type_ref(cg, buf, expr->type);
        cg_putc(buf, ')');
    } else if (cg_name_eq(name, len, "abs")) {
        if (expr->as.builtin.arg_type != NULL &&
            expr->as.builtin.arg_type->kind == TY_FLOAT) {
            cg_printf(buf, "idem_abs_float(slots[%d])", args[0]);
        } else {
            cg_printf(buf, "idem_abs_int(slots[%d])", args[0]);
        }
    } else if (cg_name_eq(name, len, "min")) {
        if (expr->as.builtin.arg_type != NULL &&
            expr->as.builtin.arg_type->kind == TY_FLOAT) {
            cg_printf(buf, "idem_min_float(slots[%d], slots[%d])",
                      args[0], args[1]);
        } else {
            cg_printf(buf, "idem_min_int(slots[%d], slots[%d])",
                      args[0], args[1]);
        }
    } else if (cg_name_eq(name, len, "max")) {
        if (expr->as.builtin.arg_type != NULL &&
            expr->as.builtin.arg_type->kind == TY_FLOAT) {
            cg_printf(buf, "idem_max_float(slots[%d], slots[%d])",
                      args[0], args[1]);
        } else {
            cg_printf(buf, "idem_max_int(slots[%d], slots[%d])",
                      args[0], args[1]);
        }
    } else if (cg_name_eq(name, len, "sqrt")) {
        cg_printf(buf, "idem_sqrt(slots[%d])", args[0]);
    } else if (cg_name_eq(name, len, "floor")) {
        cg_printf(buf, "idem_floor(slots[%d])", args[0]);
    } else if (cg_name_eq(name, len, "ceil")) {
        cg_printf(buf, "idem_ceil(slots[%d])", args[0]);
    } else if (cg_name_eq(name, len, "pow")) {
        cg_printf(buf, "idem_pow(slots[%d], slots[%d])",
                  args[0], args[1]);
    } else if (cg_name_eq(name, len, "random")) {
        cg_puts(buf, "idem_random()");
    } else if (cg_name_eq(name, len, "now")) {
        cg_puts(buf, "idem_now()");
    } else if (cg_name_eq(name, len, "byteAt")) {
        cg_printf(buf, "idem_byte_at(slots[%d], slots[%d])",
                  args[0], args[1]);
    } else if (cg_name_eq(name, len, "slice")) {
        cg_printf(buf, "idem_slice(slots[%d], slots[%d], slots[%d])",
                  args[0], args[1], args[2]);
    } else if (cg_name_eq(name, len, "compare")) {
        cg_printf(buf, "idem_compare(slots[%d], slots[%d])",
                  args[0], args[1]);
    } else if (cg_name_eq(name, len, "charAt")) {
        cg_printf(buf, "idem_char_at(slots[%d], slots[%d])",
                  args[0], args[1]);
    } else if (cg_name_eq(name, len, "chars")) {
        cg_printf(buf, "idem_chars(slots[%d], ", args[0]);
        cg_type_ref(cg, buf, expr->type);
        cg_putc(buf, ')');
    } else if (cg_name_eq(name, len, "toInt")) {
        cg_printf(buf, "idem_to_int(slots[%d])", args[0]);
    } else if (cg_name_eq(name, len, "toFloat")) {
        cg_printf(buf, "idem_to_float(slots[%d])", args[0]);
    } else if (cg_name_eq(name, len, "toString")) {
        cg_printf(buf, "idem_to_string(slots[%d])", args[0]);
    } else if (cg_name_eq(name, len, "toBytes")) {
        cg_printf(buf, "idem_to_bytes(slots[%d])", args[0]);
    } else if (cg_name_eq(name, len, "fromBytes")) {
        cg_printf(buf, "idem_from_bytes(slots[%d])", args[0]);
    } else {
        cg_printf(buf, "idem_unit() /* unknown builtin %.*s */",
                  (int)len, name);
    }
}

static void cg_gen_expr(Codegen *cg, CgBuf *out, IrExpr *expr,
                        int dst, int indent)
{
    switch (expr->kind) {
    case IR_EXPR_UNIT:
        cg_indent(out, indent);
        cg_printf(out, "slots[%d] = idem_unit();\n", dst);
        break;
    case IR_EXPR_BOOL:
        cg_indent(out, indent);
        cg_printf(out, "slots[%d] = idem_bool(%d);\n", dst,
                  expr->as.bool_lit.value ? 1 : 0);
        break;
    case IR_EXPR_INT:
        cg_indent(out, indent);
        cg_printf(out, "slots[%d] = idem_int_lit(", dst);
        cg_int_parts(out, expr->as.int_lit.value);
        cg_puts(out, ");\n");
        break;
    case IR_EXPR_FLOAT:
        cg_indent(out, indent);
        cg_printf(out, "slots[%d] = idem_float(", dst);
        cg_float_literal(out, expr->as.float_lit.value);
        cg_puts(out, ");\n");
        break;
    case IR_EXPR_CHAR:
        cg_indent(out, indent);
        cg_printf(out, "slots[%d] = idem_char(0x%lXUL);\n", dst,
                  (unsigned long)expr->as.char_lit.codepoint);
        break;
    case IR_EXPR_STRING:
        cg_indent(out, indent);
        cg_printf(out, "slots[%d] = idem_string(", dst);
        cg_c_string(out, expr->as.str_lit.data, expr->as.str_lit.len);
        cg_printf(out, ", %lu);\n",
                  (unsigned long)expr->as.str_lit.len);
        break;
    case IR_EXPR_BYTES:
        cg_indent(out, indent);
        cg_printf(out, "slots[%d] = idem_bytes(", dst);
        cg_c_string(out, expr->as.str_lit.data, expr->as.str_lit.len);
        cg_printf(out, ", %lu);\n",
                  (unsigned long)expr->as.str_lit.len);
        break;
    case IR_EXPR_LOCAL:
        cg_indent(out, indent);
        cg_printf(out, "slots[%d] = slots[%d];\n", dst,
                  expr->as.local.slot);
        break;
    case IR_EXPR_FUNCTION:
        cg_indent(out, indent);
        cg_printf(out, "slots[%d] = idem_closure_new(%s, NULL, 0, ",
                  dst, cg_function_name_str(cg,
                                            expr->as.function.function));
        cg_type_ref(cg, out, expr->type);
        cg_puts(out, ");\n");
        break;
    case IR_EXPR_CLOSURE: {
        size_t i;
        cg_indent(out, indent);
        cg_puts(out, "{\n");
        if (expr->as.closure.capture_count != 0) {
            cg_indent(out, indent + 1);
            cg_printf(out, "Value captures[%lu];\n",
                      (unsigned long)expr->as.closure.capture_count);
            for (i = 0; i < expr->as.closure.capture_count; i++) {
                cg_indent(out, indent + 1);
                cg_printf(out, "captures[%lu] = slots[%d];\n",
                          (unsigned long)i,
                          expr->as.closure.captures[i]);
            }
        }
        cg_indent(out, indent + 1);
        cg_printf(out, "slots[%d] = idem_closure_new(%s, ", dst,
                  cg_function_name_str(cg,
                                       expr->as.closure.function));
        if (expr->as.closure.capture_count != 0) {
            cg_puts(out, "captures, ");
            cg_printf(out, "%lu, ",
                      (unsigned long)expr->as.closure.capture_count);
        } else {
            cg_puts(out, "NULL, 0, ");
        }
        cg_type_ref(cg, out, expr->type);
        cg_puts(out, ");\n");
        cg_indent(out, indent);
        cg_puts(out, "}\n");
        break;
    }
    case IR_EXPR_CALL: {
        size_t i;
        cg_indent(out, indent);
        cg_puts(out, "{\n");
        if (expr->as.call.argc != 0) {
            cg_indent(out, indent + 1);
            cg_printf(out, "Value call_args[%lu];\n",
                      (unsigned long)expr->as.call.argc);
            for (i = 0; i < expr->as.call.argc; i++) {
                cg_indent(out, indent + 1);
                cg_printf(out, "call_args[%lu] = slots[%d];\n",
                          (unsigned long)i, expr->as.call.args[i]);
            }
        }
        cg_indent(out, indent + 1);
        cg_printf(out, "slots[%d] = ", dst);
        if (expr->as.call.direct) {
            cg_printf(out, "%s(NULL, ",
                      cg_function_name_str(cg,
                                           expr->as.call.function));
        } else {
            cg_printf(out, "idem_value_call(slots[%d], ",
                      expr->as.call.callee);
        }
        if (expr->as.call.argc != 0) {
            cg_puts(out, "call_args, ");
            cg_printf(out, "%lu);\n",
                      (unsigned long)expr->as.call.argc);
        } else {
            cg_puts(out, "NULL, 0);\n");
        }
        cg_indent(out, indent);
        cg_puts(out, "}\n");
        break;
    }
    case IR_EXPR_BUILTIN:
        cg_indent(out, indent);
        cg_printf(out, "slots[%d] = ", dst);
        cg_builtin_call(cg, out, expr);
        cg_puts(out, ";\n");
        break;
    case IR_EXPR_STRUCT: {
        size_t i;
        cg_indent(out, indent);
        cg_puts(out, "{\n");
        if (expr->as.struct_lit.count != 0) {
            cg_indent(out, indent + 1);
            cg_printf(out, "Value fields[%lu];\n",
                      (unsigned long)expr->as.struct_lit.count);
            for (i = 0; i < expr->as.struct_lit.count; i++) {
                cg_indent(out, indent + 1);
                cg_printf(out, "fields[%lu] = slots[%d];\n",
                          (unsigned long)i,
                          expr->as.struct_lit.fields[i]);
            }
        }
        cg_indent(out, indent + 1);
        cg_printf(out, "slots[%d] = idem_struct_new(", dst);
        cg_desc(cg, out, expr->as.struct_lit.info);
        if (expr->as.struct_lit.count != 0) {
            cg_puts(out, ", fields, ");
            cg_printf(out, "%lu);\n",
                      (unsigned long)expr->as.struct_lit.count);
        } else {
            cg_puts(out, ", NULL, 0);\n");
        }
        cg_indent(out, indent);
        cg_puts(out, "}\n");
        break;
    }
    case IR_EXPR_ENUM: {
        size_t i;
        cg_indent(out, indent);
        cg_puts(out, "{\n");
        if (expr->as.enum_lit.count != 0) {
            cg_indent(out, indent + 1);
            cg_printf(out, "Value payload[%lu];\n",
                      (unsigned long)expr->as.enum_lit.count);
            for (i = 0; i < expr->as.enum_lit.count; i++) {
                cg_indent(out, indent + 1);
                cg_printf(out, "payload[%lu] = slots[%d];\n",
                          (unsigned long)i,
                          expr->as.enum_lit.payload[i]);
            }
        }
        cg_indent(out, indent + 1);
        cg_printf(out, "slots[%d] = idem_enum_new(", dst);
        cg_desc(cg, out, expr->as.enum_lit.info);
        cg_printf(out, ", %dU, ", expr->as.enum_lit.variant);
        if (expr->as.enum_lit.count != 0) {
            cg_puts(out, "payload, ");
            cg_printf(out, "%lu);\n",
                      (unsigned long)expr->as.enum_lit.count);
        } else {
            cg_puts(out, "NULL, 0);\n");
        }
        cg_indent(out, indent);
        cg_puts(out, "}\n");
        break;
    }
    case IR_EXPR_TUPLE:
    case IR_EXPR_ARRAY:
    case IR_EXPR_LIST: {
        size_t i;
        const char *constructor;
        if (expr->kind == IR_EXPR_TUPLE) {
            constructor = "idem_tuple";
        } else if (expr->kind == IR_EXPR_ARRAY) {
            constructor = "idem_array";
        } else {
            constructor = "idem_list";
        }
        cg_indent(out, indent);
        cg_puts(out, "{\n");
        if (expr->as.list_lit.count != 0) {
            cg_indent(out, indent + 1);
            cg_printf(out, "Value items[%lu];\n",
                      (unsigned long)expr->as.list_lit.count);
            for (i = 0; i < expr->as.list_lit.count; i++) {
                cg_indent(out, indent + 1);
                cg_printf(out, "items[%lu] = slots[%d];\n",
                          (unsigned long)i,
                          expr->as.list_lit.items[i]);
            }
        }
        cg_indent(out, indent + 1);
        cg_printf(out, "slots[%d] = %s(", dst, constructor);
        if (expr->as.list_lit.count != 0) {
            cg_puts(out, "items, ");
            cg_printf(out, "%lu, ",
                      (unsigned long)expr->as.list_lit.count);
        } else {
            cg_puts(out, "NULL, 0, ");
        }
        cg_type_ref(cg, out, expr->type);
        cg_puts(out, ");\n");
        cg_indent(out, indent);
        cg_puts(out, "}\n");
        break;
    }
    case IR_EXPR_MAP: {
        size_t i;
        cg_indent(out, indent);
        cg_puts(out, "{\n");
        if (expr->as.map_lit.count != 0) {
            cg_indent(out, indent + 1);
            cg_printf(out, "Value map_keys[%lu];\n",
                      (unsigned long)expr->as.map_lit.count);
            cg_indent(out, indent + 1);
            cg_printf(out, "Value map_values[%lu];\n",
                      (unsigned long)expr->as.map_lit.count);
            for (i = 0; i < expr->as.map_lit.count; i++) {
                cg_indent(out, indent + 1);
                cg_printf(out, "map_keys[%lu] = slots[%d];\n",
                          (unsigned long)i,
                          expr->as.map_lit.keys[i]);
                cg_indent(out, indent + 1);
                cg_printf(out, "map_values[%lu] = slots[%d];\n",
                          (unsigned long)i,
                          expr->as.map_lit.values[i]);
            }
        }
        cg_indent(out, indent + 1);
        cg_printf(out, "slots[%d] = idem_map_lit(", dst);
        if (expr->as.map_lit.count != 0) {
            cg_puts(out, "map_keys, map_values, ");
            cg_printf(out, "%lu, ",
                      (unsigned long)expr->as.map_lit.count);
        } else {
            cg_puts(out, "NULL, NULL, 0, ");
        }
        cg_type_ref(cg, out, expr->type);
        cg_puts(out, ");\n");
        cg_indent(out, indent);
        cg_puts(out, "}\n");
        break;
    }
    case IR_EXPR_FIELD:
        cg_indent(out, indent);
        cg_printf(out, "slots[%d] = idem_struct_get(slots[%d], %d);\n",
                  dst, expr->as.field.object, expr->as.field.index);
        break;
    case IR_EXPR_PROJECT: {
        const char *function;
        if (expr->as.project.kind == IR_PROJECT_TUPLE) {
            function = "idem_tuple_get";
        } else if (expr->as.project.kind == IR_PROJECT_STRUCT) {
            function = "idem_struct_get";
        } else {
            function = "idem_enum_get";
        }
        cg_indent(out, indent);
        cg_printf(out, "slots[%d] = %s(slots[%d], %d);\n", dst,
                  function, expr->as.project.object,
                  expr->as.project.index);
        break;
    }
    case IR_EXPR_INDEX:
        cg_indent(out, indent);
        cg_printf(out, "slots[%d] = idem_get(slots[%d], slots[%d]);\n",
                  dst, expr->as.index.object, expr->as.index.index);
        break;
    case IR_EXPR_BINARY: {
        AstBinaryOp op;
        Type *type;
        op = expr->as.binary.op;
        type = expr->as.binary.operand_type;
        cg_indent(out, indent);
        cg_printf(out, "slots[%d] = ", dst);
        if (op == BIN_EQ || op == BIN_NE) {
            cg_puts(out, "idem_bool(");
            if (op == BIN_NE) {
                cg_puts(out, "!");
            }
            cg_printf(out, "idem_value_equal(slots[%d], slots[%d]))",
                      expr->as.binary.left, expr->as.binary.right);
        } else if (op == BIN_LT || op == BIN_LE ||
                   op == BIN_GT || op == BIN_GE) {
            const char *symbol;
            symbol = op == BIN_LT ? "<" : op == BIN_LE ? "<="
                     : op == BIN_GT ? ">" : ">=";
            if (type != NULL && (type->kind == TY_INT ||
                                 type->kind == TY_FLOAT ||
                                 type->kind == TY_CHAR)) {
                const char *member;
                member = type->kind == TY_INT ? ".as.i"
                         : type->kind == TY_FLOAT ? ".as.f" : ".as.c";
                cg_printf(out, "idem_bool(slots[%d]%s %s slots[%d]%s)",
                          expr->as.binary.left, member, symbol,
                          expr->as.binary.right, member);
            } else {
                cg_printf(out,
                          "idem_bool(idem_value_compare(slots[%d], "
                          "slots[%d]) %s 0)",
                          expr->as.binary.left,
                          expr->as.binary.right, symbol);
            }
        } else if (op == BIN_ADD || op == BIN_SUB || op == BIN_MUL ||
                   op == BIN_DIV || op == BIN_MOD) {
            const char *name;
            if (type != NULL && type->kind == TY_FLOAT) {
                name = op == BIN_ADD ? "idem_float_add"
                       : op == BIN_SUB ? "idem_float_sub"
                       : op == BIN_MUL ? "idem_float_mul"
                       : op == BIN_DIV ? "idem_float_div"
                                       : "idem_float_mod";
            } else {
                name = op == BIN_ADD ? "idem_int_add"
                       : op == BIN_SUB ? "idem_int_sub"
                       : op == BIN_MUL ? "idem_int_mul"
                       : op == BIN_DIV ? "idem_int_div"
                                       : "idem_int_mod";
            }
            cg_printf(out, "%s(slots[%d], slots[%d])", name,
                      expr->as.binary.left, expr->as.binary.right);
        } else if (op == BIN_BIT_AND || op == BIN_BIT_OR ||
                   op == BIN_BIT_XOR || op == BIN_SHL ||
                   op == BIN_SHR) {
            const char *name;
            name = op == BIN_BIT_AND ? "idem_int_and"
                   : op == BIN_BIT_OR ? "idem_int_or"
                   : op == BIN_BIT_XOR ? "idem_int_xor"
                   : op == BIN_SHL ? "idem_int_shl" : "idem_int_shr";
            cg_printf(out, "%s(slots[%d], slots[%d])", name,
                      expr->as.binary.left, expr->as.binary.right);
        } else {
            cg_puts(out, "idem_unit()");
        }
        cg_puts(out, ";\n");
        break;
    }
    case IR_EXPR_UNARY: {
        AstUnaryOp op;
        Type *type;
        op = expr->as.unary.op;
        type = expr->as.unary.operand_type;
        cg_indent(out, indent);
        cg_printf(out, "slots[%d] = ", dst);
        if (op == UN_NOT) {
            cg_printf(out, "idem_bool(!idem_truth(slots[%d]))",
                      expr->as.unary.operand);
        } else if (op == UN_BIT_NOT) {
            cg_printf(out, "idem_int_not(slots[%d])",
                      expr->as.unary.operand);
        } else if (type != NULL && type->kind == TY_FLOAT) {
            cg_printf(out, "idem_float_neg(slots[%d])",
                      expr->as.unary.operand);
        } else {
            cg_printf(out, "idem_int_neg(slots[%d])",
                      expr->as.unary.operand);
        }
        cg_puts(out, ";\n");
        break;
    }
    }
}

/* ------------------------------------------------------------------ */
/* Statements                                                          */
/* ------------------------------------------------------------------ */

static void cg_gen_stmt(Codegen *cg, CgBuf *out, IrStmt *stmt, int indent)
{
    IrStmt *item;

    for (item = stmt; item != NULL; item = item->next) {
        switch (item->kind) {
        case IR_STMT_LET:
            cg_gen_expr(cg, out, item->as.let.value,
                        item->as.let.dst, indent);
            break;
        case IR_STMT_IF:
            cg_indent(out, indent);
            cg_printf(out, "if (idem_truth(slots[%d])) {\n",
                      item->as.if_.cond);
            cg_gen_stmt(cg, out, item->as.if_.then_branch, indent + 1);
            cg_indent(out, indent);
            cg_puts(out, "} else {\n");
            cg_gen_stmt(cg, out, item->as.if_.else_branch, indent + 1);
            cg_indent(out, indent);
            cg_puts(out, "}\n");
            break;
        case IR_STMT_BLOCK:
            cg_indent(out, indent);
            cg_puts(out, "{\n");
            cg_gen_stmt(cg, out, item->as.block.items, indent + 1);
            cg_indent(out, indent);
            cg_puts(out, "}\n");
            break;
        case IR_STMT_MATCH: {
            IrMatchArm *arm;
            int label;
            CgBuf test;

            label = cg->next_label;
            cg->next_label += 1;

            cg_indent(out, indent);
            cg_puts(out, "{\n");
            if (item->as.match.arms == NULL) {
                cg_indent(out, indent + 1);
                cg_puts(out,
                        "idem_panic_str(\"non-exhaustive match\");\n");
            } else {
                char subject[64];
                sprintf(subject, "slots[%d]", item->as.match.subject);
                for (arm = item->as.match.arms; arm != NULL;
                     arm = arm->next) {
                    cg_buf_init(&test);
                    cg_pattern_test(cg, &test, arm->pattern, subject);
                    cg_indent(out, indent + 1);
                    cg_puts(out, "if (");
                    cg_puts(out, test.data != NULL ? test.data : "1");
                    cg_puts(out, ") {\n");
                    cg_buf_free(&test);
                    cg_gen_stmt(cg, out, arm->bindings, indent + 2);
                    cg_gen_stmt(cg, out, arm->body, indent + 2);
                    cg_indent(out, indent + 2);
                    cg_printf(out, "goto idem_match_end_%d;\n", label);
                    cg_indent(out, indent + 1);
                    cg_puts(out, "}\n");
                }
                cg_indent(out, indent + 1);
                cg_puts(out,
                        "idem_panic_str(\"non-exhaustive match\");\n");
                cg_indent(out, indent);
                cg_printf(out, "idem_match_end_%d: ;\n", label);
            }
            cg_indent(out, indent);
            cg_puts(out, "}\n");
            break;
        }
        case IR_STMT_EXPR:
            /* Expression statements are not emitted by the lowering pass. */
            break;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Functions                                                           */
/* ------------------------------------------------------------------ */

static void cg_emit_prototypes(Codegen *cg, CgBuf *out)
{
    IrFunction *function;

    cg_puts(out, "/* --- function prototypes --- */\n\n");
    for (function = cg->program->functions; function != NULL;
         function = function->next) {
        cg_puts(out, "Value ");
        cg_function_name(out, function);
        cg_puts(out, "(Value *env, Value *args, size_t argc);\n");
    }
    cg_puts(out, "\n");
}

static void cg_emit_function(Codegen *cg, CgBuf *out,
                             IrFunction *function)
{
    int slots;
    int i;
    IrCapture *capture;

    slots = function->slot_count;
    if (slots < 1) {
        slots = 1;
    }

    cg_puts(out, "Value ");
    cg_function_name(out, function);
    cg_puts(out, "(Value *env, Value *args, size_t argc)\n{\n");
    cg_printf(out, "    Value slots[%d];\n", slots);
    cg_puts(out, "    size_t i;\n\n");
    cg_puts(out, "    (void)env;\n");
    cg_puts(out, "    (void)args;\n");
    cg_puts(out, "    (void)argc;\n");
    cg_printf(out, "    for (i = 0; i < %d; i++) {\n", slots);
    cg_puts(out, "        slots[i] = idem_unit();\n");
    cg_puts(out, "    }\n");
    cg_printf(out, "    idem_gc_push_frame(slots, %d);\n", slots);

    for (i = 0; i < function->param_count; i++) {
        cg_printf(out, "    slots[%d] = args[%d];\n", i, i);
    }

    i = 0;
    for (capture = function->captures; capture != NULL;
         capture = capture->next) {
        cg_printf(out, "    slots[%d] = env[%d];\n",
                  capture->slot, i);
        i += 1;
    }

    cg_puts(out, "\n");
    cg_gen_stmt(cg, out, function->body, 1);
    cg_puts(out, "\n    idem_gc_pop_frame();\n");
    cg_printf(out, "    return slots[%d];\n", function->result_slot);
    cg_puts(out, "}\n\n");
}

static void cg_emit_main(Codegen *cg, CgBuf *out)
{
    cg_puts(out, "int main(int argc, char **argv)\n{\n");
    cg_puts(out, "    Value args[1];\n");
    cg_puts(out, "    Value result;\n\n");
    cg_puts(out, "    idem_runtime_init();\n");
    cg_puts(out, "    args[0] = idem_args_array(argc, argv, ");
    if (cg->program->main_function != NULL &&
        cg->program->main_function->symbol != NULL) {
        TypeList *params;
        params = cg->program->main_function->symbol->as.fn.type
                     ->as.fn.params;
        cg_type_ref(cg, out, params != NULL ? params->type : NULL);
    } else {
        cg_puts(out, "NULL");
    }
    cg_puts(out, ", ");
    if (cg->program->resolver->root != NULL &&
        cg->program->resolver->root->path != NULL) {
        const char *path;
        path = cg->program->resolver->root->path;
        cg_c_string(out, path, strlen(path));
    } else {
        cg_c_string(out, "", 0);
    }
    cg_puts(out, ");\n");
    cg_puts(out, "    idem_gc_push_frame(args, 1);\n");
    cg_puts(out, "    result = ");
    cg_function_name(out, cg->program->main_function);
    cg_puts(out, "(NULL, args, 1);\n");
    cg_puts(out, "    (void)result;\n");
    cg_puts(out, "    idem_gc_pop_frame();\n");
    cg_puts(out, "    fflush(stdout);\n");
    cg_puts(out, "    return 0;\n}\n");
}

/* ------------------------------------------------------------------ */
/* Entry point                                                         */
/* ------------------------------------------------------------------ */

void codegen_init(Codegen *codegen, IrProgram *program, Arena *arena)
{
    memset(codegen, 0, sizeof(Codegen));
    codegen->program = program;
    codegen->arena = arena;
}

int codegen_emit(Codegen *codegen, const char *path)
{
    CgBuf out;
    CgBuf body;
    FILE *file;
    IrFunction *function;

    cg_buf_init(&out);
    cg_buf_init(&body);

    cg_puts(&body, "/* --- functions --- */\n\n");
    for (function = codegen->program->functions; function != NULL;
         function = function->next) {
        cg_emit_function(codegen, &body, function);
    }

    cg_puts(&body, "/* --- program entry --- */\n\n");
    cg_emit_main(codegen, &body);

    cg_puts(&out, "/* Generated by the Idem reference compiler. */\n");
    cg_puts(&out, "#include <stdio.h>\n");
    cg_puts(&out, "#include \"value.h\"\n\n");
    cg_emit_descriptors(codegen, &out);
    cg_emit_structural_descriptors(codegen, &out);
    cg_emit_prototypes(codegen, &out);
    cg_write(&out, body.data != NULL ? body.data : "", body.len);

    if (codegen->had_error) {
        cg_buf_free(&body);
        cg_buf_free(&out);
        return 0;
    }

    file = fopen(path, "wb");
    if (file == NULL) {
        cg_error(codegen, "cannot open output file");
        cg_buf_free(&body);
        cg_buf_free(&out);
        return 0;
    }
    if (out.len != 0 &&
        fwrite(out.data, 1, out.len, file) != out.len) {
        fclose(file);
        cg_error(codegen, "cannot write output file");
        cg_buf_free(&body);
        cg_buf_free(&out);
        return 0;
    }
    if (fclose(file) != 0) {
        cg_error(codegen, "cannot close output file");
        cg_buf_free(&body);
        cg_buf_free(&out);
        return 0;
    }

    cg_buf_free(&body);
    cg_buf_free(&out);
    return 1;
}

int codegen_had_error(const Codegen *codegen)
{
    return codegen->had_error;
}

const char *codegen_error_message(const Codegen *codegen)
{
    return codegen->error_message;
}

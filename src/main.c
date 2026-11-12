#if defined(__unix__) || defined(__APPLE__)
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__unix__) || defined(__APPLE__)
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "arena.h"
#include "ast.h"
#include "check.h"
#include "codegen.h"
#include "ir.h"
#include "lexer.h"
#include "platform.h"
#include "resolver.h"
#include "token.h"
#include "version.h"

#ifndef IDEM_RUNTIME_DIR
#define IDEM_RUNTIME_DIR "runtime"
#endif

static unsigned char *read_file(const char *path, size_t *out_len)
{
    FILE *f;
    long size;
    unsigned char *buf;
    size_t n;

    f = fopen(path, "rb");
    if (f == NULL) {
        fprintf(stderr, "idem: cannot open '%s'\n", path);
        return NULL;
    }

    if (fseek(f, 0L, SEEK_END) != 0) {
        fclose(f);
        fprintf(stderr, "idem: cannot seek '%s'\n", path);
        return NULL;
    }

    size = ftell(f);
    if (size < 0) {
        fclose(f);
        fprintf(stderr, "idem: cannot determine size of '%s'\n", path);
        return NULL;
    }

    rewind(f);

    buf = (unsigned char *)malloc((size_t)size + 1);
    if (buf == NULL) {
        fclose(f);
        fprintf(stderr, "idem: out of memory\n");
        return NULL;
    }

    n = fread(buf, 1, (size_t)size, f);
    fclose(f);

    if (n != (size_t)size) {
        free(buf);
        fprintf(stderr, "idem: cannot read '%s'\n", path);
        return NULL;
    }

    buf[n] = 0;
    *out_len = n;
    return buf;
}

static int dump_tokens(Lexer *lx)
{
    Token t;
    int errors;

    errors = 0;
    for (;;) {
        t = lexer_next(lx);

        printf("%lu:%lu: %s",
               (unsigned long)t.span.line,
               (unsigned long)t.span.column,
               token_kind_name(t.kind));

        switch (t.kind) {
        case TOK_IDENT:
            printf(" %.*s", (int)t.as.ident.len, t.as.ident.text);
            break;
        case TOK_INT:
            printf(" ");
            u64_print(t.as.int_lit.value);
            break;
        case TOK_FLOAT:
            printf(" %.17g", t.as.float_lit.value);
            break;
        case TOK_CHAR:
            printf(" U+%04lX", (unsigned long)t.as.char_lit.codepoint);
            break;
        case TOK_STRING:
        case TOK_BYTES:
            printf(" len=%lu", (unsigned long)t.as.str_lit.len);
            break;
        case TOK_ERROR:
            printf(" %s", t.as.error);
            break;
        default:
            break;
        }

        printf("\n");

        if (t.kind == TOK_ERROR) {
            errors += 1;
            break;
        }
        if (t.kind == TOK_EOF) {
            break;
        }
    }

    return errors != 0 ? 1 : 0;
}

static int print_file(const char *path)
{
    FILE *f;
    char buffer[4096];
    size_t n;

    f = fopen(path, "rb");
    if (f == NULL) {
        fprintf(stderr, "idem: cannot read generated C\n");
        return 0;
    }
    while ((n = fread(buffer, 1, sizeof(buffer), f)) != 0) {
        fwrite(buffer, 1, n, stdout);
    }
    fclose(f);
    return 1;
}

static char *copy_string(const char *text)
{
    size_t len;
    char *copy;

    len = strlen(text);
    copy = (char *)malloc(len + 1);
    if (copy == NULL) {
        return NULL;
    }
    memcpy(copy, text, len + 1);
    return copy;
}

static char *join_path(const char *dir, const char *name)
{
    size_t dir_len;
    size_t name_len;
    int needs_slash;
    char *path;

    dir_len = strlen(dir);
    name_len = strlen(name);
    if (dir_len > (size_t)-1 - name_len - 2) {
        return NULL;
    }
    needs_slash = dir_len > 0 && dir[dir_len - 1] != '/';
    path = (char *)malloc(dir_len + (needs_slash ? 1 : 0) + name_len + 1);
    if (path == NULL) {
        return NULL;
    }
    memcpy(path, dir, dir_len);
    if (needs_slash) {
        path[dir_len] = '/';
        dir_len += 1;
    }
    memcpy(path + dir_len, name, name_len + 1);
    return path;
}

static int file_exists(const char *path)
{
    FILE *f;

    f = fopen(path, "rb");
    if (f == NULL) {
        return 0;
    }
    fclose(f);
    return 1;
}

#if defined(__unix__) || defined(__APPLE__)
static char *path_dirname(const char *path)
{
    const char *slash;
    size_t len;
    char *dir;

    slash = strrchr(path, '/');
    if (slash == NULL) {
        return copy_string(".");
    }
    len = (size_t)(slash - path);
    if (len == 0) {
        len = 1;
    }
    dir = (char *)malloc(len + 1);
    if (dir == NULL) {
        return NULL;
    }
    memcpy(dir, path, len);
    dir[len] = '\0';
    return dir;
}
#endif

static int make_temp_dir(char **dir_out, char **c_out, char **exe_out)
{
#if defined(__unix__) || defined(__APPLE__)
    const char *base;
    char *template_path;
    char *dir;
    char *c_path;
    char *exe_path;
    size_t len;

    *dir_out = NULL;
    *c_out = NULL;
    *exe_out = NULL;

    base = getenv("TMPDIR");
    if (base == NULL || *base == '\0') {
        base = "/tmp";
    }

    len = strlen(base) + strlen("/idem-XXXXXX") + 1;
    template_path = (char *)malloc(len);
    if (template_path == NULL) {
        fprintf(stderr, "idem: out of memory\n");
        return 0;
    }
    strcpy(template_path, base);
    strcat(template_path, "/idem-XXXXXX");

    dir = mkdtemp(template_path);
    if (dir == NULL) {
        fprintf(stderr, "idem: cannot create a temporary directory in '%s'\n",
                base);
        free(template_path);
        return 0;
    }

    c_path = join_path(dir, "main.c");
    exe_path = join_path(dir, "idem-out");
    if (c_path == NULL || exe_path == NULL) {
        fprintf(stderr, "idem: out of memory\n");
        free(c_path);
        free(exe_path);
        rmdir(dir);
        free(dir);
        return 0;
    }

    *dir_out = dir;
    *c_out = c_path;
    *exe_out = exe_path;
    return 1;
#else
    *dir_out = NULL;
    *c_out = NULL;
    *exe_out = NULL;
    fprintf(stderr, "idem: temporary files require a POSIX platform\n");
    return 0;
#endif
}

static void cleanup_temp_dir(const char *dir, const char *c_path,
                             const char *exe_path, int keep)
{
    if (dir == NULL || keep) {
        return;
    }
    if (c_path != NULL) {
        remove(c_path);
    }
    if (exe_path != NULL) {
        remove(exe_path);
    }
#if defined(__unix__) || defined(__APPLE__)
    rmdir(dir);
#else
    (void)dir;
#endif
}

#if defined(__unix__) || defined(__APPLE__)
static char *find_executable(const char *argv0)
{
#if defined(__linux__)
    char *buffer;
    size_t capacity;
    ssize_t count;
#endif
    const char *path_env;
    const char *start;
    const char *end;
    size_t segment_len;
    char *candidate;
    char *resolved;

    if (argv0 == NULL) {
        return NULL;
    }

#if defined(__linux__)
    capacity = 256;
    for (;;) {
        buffer = (char *)malloc(capacity);
        if (buffer == NULL) {
            break;
        }
        count = readlink("/proc/self/exe", buffer, capacity - 1);
        if (count < 0) {
            free(buffer);
            break;
        }
        if ((size_t)count < capacity - 1) {
            buffer[count] = '\0';
            resolved = realpath(buffer, NULL);
            free(buffer);
            if (resolved != NULL) {
                return resolved;
            }
            break;
        }
        free(buffer);
        if (capacity > 65536) {
            break;
        }
        capacity *= 2;
    }
#endif

    if (strchr(argv0, '/') != NULL) {
        return realpath(argv0, NULL);
    }

    path_env = getenv("PATH");
    if (path_env == NULL) {
        return NULL;
    }

    start = path_env;
    for (;;) {
        end = strchr(start, ':');
        segment_len = end != NULL ? (size_t)(end - start) : strlen(start);
        if (segment_len == 0) {
            candidate = copy_string(argv0);
        } else {
            candidate = (char *)malloc(segment_len + 1 + strlen(argv0) + 1);
            if (candidate != NULL) {
                memcpy(candidate, start, segment_len);
                candidate[segment_len] = '/';
                strcpy(candidate + segment_len + 1, argv0);
            }
        }
        if (candidate != NULL) {
            if (access(candidate, X_OK) == 0) {
                resolved = realpath(candidate, NULL);
                free(candidate);
                if (resolved != NULL) {
                    return resolved;
                }
            } else {
                free(candidate);
            }
        }
        if (end == NULL) {
            break;
        }
        start = end + 1;
    }
    return NULL;
}
#endif

#if defined(__unix__) || defined(__APPLE__)
static char *runtime_dir_candidate(const char *exe_path)
{
    static const char *const layouts[2] = {
        "../lib/idem/runtime",
        "runtime"
    };
    char *dir;
    char *candidate;
    char *marker;
    int i;
    int ok;

    dir = path_dirname(exe_path);
    if (dir == NULL) {
        return NULL;
    }
    for (i = 0; i < 2; i++) {
        candidate = join_path(dir, layouts[i]);
        if (candidate == NULL) {
            continue;
        }
        marker = join_path(candidate, "value.h");
        if (marker == NULL) {
            free(candidate);
            continue;
        }
        ok = file_exists(marker);
        free(marker);
        if (ok) {
            free(dir);
            return candidate;
        }
        free(candidate);
    }
    free(dir);
    return NULL;
}
#endif

static char *resolve_runtime_dir(const char *argv0)
{
    const char *env;
#if defined(__unix__) || defined(__APPLE__)
    char *exe;
    char *candidate;
#else
    (void)argv0;
#endif

    env = getenv("IDEM_RUNTIME_DIR");
    if (env != NULL && *env != '\0') {
        return copy_string(env);
    }

#if defined(__unix__) || defined(__APPLE__)
    if (argv0 != NULL) {
        exe = find_executable(argv0);
        if (exe != NULL) {
            candidate = runtime_dir_candidate(exe);
            free(exe);
            if (candidate != NULL) {
                return candidate;
            }
        }
    }
#endif

    return copy_string(IDEM_RUNTIME_DIR);
}

typedef struct CommandBuf {
    char *data;
    size_t len;
    size_t cap;
    int failed;
} CommandBuf;

static void cb_init(CommandBuf *buf)
{
    buf->data = NULL;
    buf->len = 0;
    buf->cap = 0;
    buf->failed = 0;
}

static int cb_reserve(CommandBuf *buf, size_t extra)
{
    size_t need;
    size_t cap;
    char *grown;

    if (buf->failed) {
        return 0;
    }
    if (extra > (size_t)-1 - buf->len - 1) {
        buf->failed = 1;
        return 0;
    }
    need = buf->len + extra + 1;
    if (need <= buf->cap) {
        return 1;
    }

    cap = buf->cap != 0 ? buf->cap : 128;
    while (cap < need) {
        if (cap > (size_t)-1 / 2) {
            buf->failed = 1;
            return 0;
        }
        cap *= 2;
    }

    grown = (char *)realloc(buf->data, cap);
    if (grown == NULL) {
        buf->failed = 1;
        return 0;
    }
    buf->data = grown;
    buf->cap = cap;
    return 1;
}

static void cb_append_n(CommandBuf *buf, const char *text, size_t len)
{
    if (!cb_reserve(buf, len)) {
        return;
    }
    memcpy(buf->data + buf->len, text, len);
    buf->len += len;
    buf->data[buf->len] = '\0';
}

static void cb_append(CommandBuf *buf, const char *text)
{
    cb_append_n(buf, text, strlen(text));
}

static void cb_append_quoted(CommandBuf *buf, const char *text)
{
    const char *p;

    cb_append_n(buf, "'", 1);
    for (p = text; *p != '\0'; p++) {
        if (*p == '\'') {
            cb_append_n(buf, "'\\''", 4);
        } else {
            cb_append_n(buf, p, 1);
        }
    }
    cb_append_n(buf, "'", 1);
}

static void cb_free(CommandBuf *buf)
{
    free(buf->data);
    buf->data = NULL;
    buf->len = 0;
    buf->cap = 0;
}

#if defined(__unix__) || defined(__APPLE__)
static int command_status(int status)
{
#ifdef WIFEXITED
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    return 1;
#else
    return status == 0 ? 0 : 1;
#endif
}
#endif

static int compile_and_run(const char *c_path, const char *exe_path,
                           const char *runtime_dir, int argc, char **argv,
                           int first_arg)
{
    CommandBuf command;
    const char *cc;
    char *marker;
    int status;
#if defined(__unix__) || defined(__APPLE__)
    pid_t pid;
    char **child_argv;
    size_t child_count;
    int i;
#endif

    marker = join_path(runtime_dir, "value.h");
    if (marker == NULL) {
        fprintf(stderr, "idem: out of memory\n");
        return 1;
    }
    if (!file_exists(marker)) {
        fprintf(stderr, "idem: runtime directory not found: %s\n", runtime_dir);
        fprintf(stderr, "idem: set IDEM_RUNTIME_DIR to the directory "
                        "containing value.h\n");
        free(marker);
        return 1;
    }
    free(marker);

    cc = getenv("CC");
    if (cc == NULL || *cc == '\0') {
        cc = "cc";
    }

    cb_init(&command);
    cb_append(&command, cc);
    cb_append(&command, " -std=c89 -pedantic -Wall -Wextra -O2 -I");
    cb_append_quoted(&command, runtime_dir);
    cb_append(&command, " -o ");
    cb_append_quoted(&command, exe_path);
    cb_append(&command, " ");
    cb_append_quoted(&command, c_path);
    cb_append(&command, " ");
    cb_append_quoted(&command, runtime_dir);
    cb_append(&command, "/*.c -lm");

    if (command.failed) {
        cb_free(&command);
        fprintf(stderr, "idem: out of memory\n");
        return 1;
    }

    status = system(command.data);
    cb_free(&command);
    if (status != 0) {
        fprintf(stderr, "idem: C compilation failed\n");
        return 1;
    }

#if defined(__unix__) || defined(__APPLE__)
    child_count = (size_t)(argc - first_arg) + 2;
    child_argv = (char **)malloc(sizeof(char *) * child_count);
    if (child_argv == NULL) {
        fprintf(stderr, "idem: out of memory\n");
        return 1;
    }
    child_argv[0] = (char *)exe_path;
    for (i = first_arg; i < argc; i++) {
        child_argv[i - first_arg + 1] = argv[i];
    }
    child_argv[child_count - 1] = NULL;

    pid = fork();
    if (pid < 0) {
        fprintf(stderr, "idem: cannot fork\n");
        free(child_argv);
        return 1;
    }
    if (pid == 0) {
        execv(exe_path, child_argv);
        fprintf(stderr, "idem: cannot execute '%s'\n", exe_path);
        _exit(127);
    }
    free(child_argv);

    if (waitpid(pid, &status, 0) < 0) {
        fprintf(stderr, "idem: cannot wait for the program\n");
        return 1;
    }
    return command_status(status);
#else
    (void)argc;
    (void)argv;
    (void)first_arg;
    fprintf(stderr, "idem: running programs requires a POSIX platform\n");
    return 1;
#endif
}

static void print_usage(FILE *out)
{
    fprintf(out, "usage: idem [--tokens|--emit-c] <file.idem> [args...]\n");
    fprintf(out, "       idem --version\n");
    fprintf(out, "       idem --help\n");
    fprintf(out, "\n");
    fprintf(out, "Compiles an Idem source file to C89, builds it with the "
                 "system C compiler,\n");
    fprintf(out, "and runs the resulting program.\n");
    fprintf(out, "\n");
    fprintf(out, "Options:\n");
    fprintf(out, "  --tokens     print the token stream and exit\n");
    fprintf(out, "  --emit-c     print the generated C89 and exit\n");
    fprintf(out, "  --version    print version information and exit\n");
    fprintf(out, "  --help       print this help and exit\n");
    fprintf(out, "\n");
    fprintf(out, "Environment:\n");
    fprintf(out, "  CC                 C compiler command (default: cc)\n");
    fprintf(out, "  IDEM_RUNTIME_DIR   directory containing the Idem "
                 "runtime sources\n");
    fprintf(out, "  IDEM_KEEP_C        keep the generated C file and the "
                 "executable\n");
}

int main(int argc, char **argv)
{
    const char *path;
    int tokens_only;
    int emit_c;
    int first_arg;

    tokens_only = 0;
    emit_c = 0;
    first_arg = 2;

    if (argc >= 2 && strcmp(argv[1], "--version") == 0) {
        printf("idem %s (%s)\n", IDEM_VERSION, IDEM_LANGUAGE_VERSION);
        return 0;
    }
    if (argc >= 2 && strcmp(argv[1], "--help") == 0) {
        print_usage(stdout);
        return 0;
    }

    if (argc >= 3 && strcmp(argv[1], "--tokens") == 0) {
        tokens_only = 1;
        path = argv[2];
    } else if (argc >= 3 && strcmp(argv[1], "--emit-c") == 0) {
        emit_c = 1;
        path = argv[2];
    } else if (argc == 2 &&
               (strcmp(argv[1], "--tokens") == 0 ||
                strcmp(argv[1], "--emit-c") == 0)) {
        print_usage(stderr);
        return 2;
    } else if (argc >= 2) {
        path = argv[1];
        first_arg = 2;
    } else {
        print_usage(stderr);
        return 2;
    }

    if (tokens_only || emit_c) {
        unsigned char *src;
        size_t len;
        Arena arena;
        Lexer lx;
        Resolver resolver;
        TypeChecker checker;
        IrProgram program;
        Codegen codegen;
        char *temp_dir;
        char *c_path;
        char *exe_path;

        temp_dir = NULL;
        c_path = NULL;
        exe_path = NULL;

        src = read_file(path, &len);
        if (src == NULL) {
            return 1;
        }

        arena_init(&arena);

        if (tokens_only) {
            int status;
            lexer_init(&lx, path, src, len, &arena);
            status = dump_tokens(&lx);
            arena_free(&arena);
            free(src);
            return status;
        }

        resolver_init(&resolver, &arena);
        if (!resolver_resolve(&resolver, path)) {
            Span span;
            span = resolver_error_span(&resolver);
            fprintf(stderr, "%s:%lu:%lu: error: %s\n",
                    span.file != NULL ? span.file : path,
                    (unsigned long)span.line,
                    (unsigned long)span.column,
                    resolver_error_message(&resolver));
            arena_free(&arena);
            free(src);
            return 1;
        }

        checker_init(&checker, &resolver, &arena);
        if (!checker_check_program(&checker)) {
            Span span;
            span = checker_error_span(&checker);
            fprintf(stderr, "%s:%lu:%lu: error: %s\n",
                    span.file != NULL ? span.file : path,
                    (unsigned long)span.line,
                    (unsigned long)span.column,
                    checker_error_message(&checker));
            arena_free(&arena);
            free(src);
            return 1;
        }

        ir_init(&program, &resolver, &arena);
        if (!ir_lower(&program)) {
            Span span;
            span = ir_error_span(&program);
            fprintf(stderr, "%s:%lu:%lu: error: %s\n",
                    span.file != NULL ? span.file : path,
                    (unsigned long)span.line,
                    (unsigned long)span.column,
                    ir_error_message(&program));
            arena_free(&arena);
            free(src);
            return 1;
        }

        if (!make_temp_dir(&temp_dir, &c_path, &exe_path)) {
            arena_free(&arena);
            free(src);
            return 1;
        }

        codegen_init(&codegen, &program, &arena);
        if (!codegen_emit(&codegen, c_path)) {
            fprintf(stderr, "idem: %s\n", codegen_error_message(&codegen));
            cleanup_temp_dir(temp_dir, c_path, exe_path, 0);
            free(temp_dir);
            free(c_path);
            free(exe_path);
            arena_free(&arena);
            free(src);
            return 1;
        }

        if (emit_c) {
            int ok;
            ok = print_file(c_path);
            cleanup_temp_dir(temp_dir, c_path, exe_path, 0);
            free(temp_dir);
            free(c_path);
            free(exe_path);
            arena_free(&arena);
            free(src);
            return ok ? 0 : 1;
        }

        cleanup_temp_dir(temp_dir, c_path, exe_path, 0);
        free(temp_dir);
        free(c_path);
        free(exe_path);
        arena_free(&arena);
        free(src);
        return 0;
    }

    {
        unsigned char *src;
        size_t len;
        Arena arena;
        Resolver resolver;
        TypeChecker checker;
        IrProgram program;
        Codegen codegen;
        char *temp_dir;
        char *c_path;
        char *exe_path;
        char *runtime_dir;
        const char *keep;
        int keep_files;
        int status;

        temp_dir = NULL;
        c_path = NULL;
        exe_path = NULL;
        runtime_dir = NULL;

        src = read_file(path, &len);
        if (src == NULL) {
            return 1;
        }

        arena_init(&arena);
        resolver_init(&resolver, &arena);

        if (!resolver_resolve(&resolver, path)) {
            Span span;
            span = resolver_error_span(&resolver);
            fprintf(stderr, "%s:%lu:%lu: error: %s\n",
                    span.file != NULL ? span.file : path,
                    (unsigned long)span.line,
                    (unsigned long)span.column,
                    resolver_error_message(&resolver));
            arena_free(&arena);
            free(src);
            return 1;
        }

        checker_init(&checker, &resolver, &arena);
        if (!checker_check_program(&checker)) {
            Span span;
            span = checker_error_span(&checker);
            fprintf(stderr, "%s:%lu:%lu: error: %s\n",
                    span.file != NULL ? span.file : path,
                    (unsigned long)span.line,
                    (unsigned long)span.column,
                    checker_error_message(&checker));
            arena_free(&arena);
            free(src);
            return 1;
        }

        ir_init(&program, &resolver, &arena);
        if (!ir_lower(&program)) {
            Span span;
            span = ir_error_span(&program);
            fprintf(stderr, "%s:%lu:%lu: error: %s\n",
                    span.file != NULL ? span.file : path,
                    (unsigned long)span.line,
                    (unsigned long)span.column,
                    ir_error_message(&program));
            arena_free(&arena);
            free(src);
            return 1;
        }

        if (!make_temp_dir(&temp_dir, &c_path, &exe_path)) {
            arena_free(&arena);
            free(src);
            return 1;
        }

        runtime_dir = resolve_runtime_dir(argv[0]);
        if (runtime_dir == NULL) {
            fprintf(stderr, "idem: out of memory\n");
            cleanup_temp_dir(temp_dir, c_path, exe_path, 0);
            free(temp_dir);
            free(c_path);
            free(exe_path);
            arena_free(&arena);
            free(src);
            return 1;
        }

        codegen_init(&codegen, &program, &arena);
        if (!codegen_emit(&codegen, c_path)) {
            fprintf(stderr, "idem: %s\n", codegen_error_message(&codegen));
            cleanup_temp_dir(temp_dir, c_path, exe_path, 0);
            free(temp_dir);
            free(c_path);
            free(exe_path);
            free(runtime_dir);
            arena_free(&arena);
            free(src);
            return 1;
        }

        keep = getenv("IDEM_KEEP_C");
        keep_files = keep != NULL && *keep != '\0';
        if (keep_files) {
            fprintf(stderr, "idem: generated C at %s\n", c_path);
            fprintf(stderr, "idem: executable at %s\n", exe_path);
        }

        status = compile_and_run(c_path, exe_path, runtime_dir, argc, argv,
                                 first_arg);

        cleanup_temp_dir(temp_dir, c_path, exe_path, keep_files);
        free(temp_dir);
        free(c_path);
        free(exe_path);
        free(runtime_dir);

        arena_free(&arena);
        free(src);
        return status;
    }
}

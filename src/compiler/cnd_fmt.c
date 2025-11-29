#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "compiler.h"
#include "cnd_internal.h"

// --- Formatting Lexer ---

typedef enum {
    FMT_EOF,
    FMT_IDENTIFIER,
    FMT_NUMBER,
    FMT_STRING,
    FMT_LBRACE,    // {
    FMT_RBRACE,    // }
    FMT_LBRACKET,  // [
    FMT_RBRACKET,  // ]
    FMT_LPAREN,    // (
    FMT_RPAREN,    // )
    FMT_SEMICOLON, // ;
    FMT_COLON,     // :
    FMT_COMMA,     // ,
    FMT_AT,        // @
    FMT_COMMENT,   // // ...
    FMT_NEWLINE,   // \n
    FMT_WHITESPACE // spaces/tabs
} FmtTokenType;

typedef struct {
    FmtTokenType type;
    const char* start;
    int length;
} FmtToken;

typedef struct {
    const char* source;
    const char* current;
} FmtLexer;

static void fmt_init(FmtLexer* l, const char* source) {
    l->source = source;
    l->current = source;
}

static int is_alpha_f(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
static int is_digit_f(char c) { return c >= '0' && c <= '9'; }

static FmtToken fmt_next(FmtLexer* l) {
    FmtToken t;
    t.start = l->current;
    t.length = 1;
    
    char c = *l->current;
    if (c == '\0') {
        t.type = FMT_EOF;
        t.length = 0;
        return t;
    }

    l->current++;

    // Newlines
    if (c == '\n') {
        t.type = FMT_NEWLINE;
        return t;
    }

    // Whitespace (non-newline)
    if (c == ' ' || c == '\t' || c == '\r') {
        t.type = FMT_WHITESPACE;
        while (*l->current == ' ' || *l->current == '\t' || *l->current == '\r') {
            l->current++;
        }
        t.length = (int)(l->current - t.start);
        return t;
    }

    // Comments
    if (c == '/' && *l->current == '/') {
        t.type = FMT_COMMENT;
        while (*l->current != '\0' && *l->current != '\n') {
            l->current++;
        }
        t.length = (int)(l->current - t.start);
        return t;
    }

    switch (c) {
        case '{': t.type = FMT_LBRACE; return t;
        case '}': t.type = FMT_RBRACE; return t;
        case '[': t.type = FMT_LBRACKET; return t;
        case ']': t.type = FMT_RBRACKET; return t;
        case '(': t.type = FMT_LPAREN; return t;
        case ')': t.type = FMT_RPAREN; return t;
        case ';': t.type = FMT_SEMICOLON; return t;
        case ':': t.type = FMT_COLON; return t;
        case ',': t.type = FMT_COMMA; return t;
        case '@': t.type = FMT_AT; return t;
        case '"': {
            t.type = FMT_STRING;
            while (*l->current != '"' && *l->current != '\0') l->current++;
            if (*l->current == '"') l->current++;
            t.length = (int)(l->current - t.start);
            return t;
        }
    }

    if (is_alpha_f(c)) {
        t.type = FMT_IDENTIFIER;
        while (is_alpha_f(*l->current) || is_digit_f(*l->current)) {
            l->current++;
        }
        t.length = (int)(l->current - t.start);
        return t;
    }

    if (is_digit_f(c) || (c == '-' && is_digit_f(*l->current))) {
        t.type = FMT_NUMBER;
        while (is_digit_f(*l->current) || *l->current == 'x' || *l->current == 'X' || *l->current == '.' || (*l->current >= 'a' && *l->current <= 'f') || (*l->current >= 'A' && *l->current <= 'F')) {
            l->current++;
        }
        t.length = (int)(l->current - t.start);
        return t;
    }

    t.type = FMT_IDENTIFIER; 
    return t;
}

// --- Dynamic Buffer ---

typedef struct {
    char* data;
    size_t len;
    size_t cap;
} DynBuf;

static void db_init(DynBuf* db) {
    db->len = 0;
    db->cap = 1024;
    db->data = malloc(db->cap);
    db->data[0] = '\0';
}

static void db_append(DynBuf* db, const char* str, size_t len) {
    if (db->len + len + 1 > db->cap) {
        while (db->len + len + 1 > db->cap) db->cap *= 2;
        db->data = realloc(db->data, db->cap);
    }
    memcpy(db->data + db->len, str, len);
    db->len += len;
    db->data[db->len] = '\0';
}

static void db_append_str(DynBuf* db, const char* str) {
    db_append(db, str, strlen(str));
}

static void db_indent(DynBuf* db, int level) {
    for (int i = 0; i < level; i++) db_append_str(db, "    ");
}

// --- Formatter Logic ---

char* cnd_format_source(const char* source) {
    FmtLexer lexer;
    fmt_init(&lexer, source);

    DynBuf out;
    db_init(&out);

    int indent = 0;
    int newline_pending = 0;
    int space_pending = 0;
    int on_new_line = 1;

    FmtToken t;
    FmtToken prev = {FMT_EOF, NULL, 0};

    while (1) {
        t = fmt_next(&lexer);
        if (t.type == FMT_EOF) break;

        // Ignore input whitespace (except we track if separation needed)
        if (t.type == FMT_WHITESPACE) {
            if (prev.type != FMT_NEWLINE && prev.type != FMT_EOF) {
                space_pending = 1;
            }
            continue;
        }

        if (t.type == FMT_NEWLINE) {
            if (newline_pending < 2) newline_pending++;
            continue;
        }

        // Logic
        if (t.type == FMT_RBRACE) {
            indent--;
            if (indent < 0) indent = 0;
            newline_pending = 1;
        }

        // Apply newlines
        if (newline_pending > 0) {
            if (!on_new_line) {
                db_append_str(&out, "\n");
                if (newline_pending > 1) db_append_str(&out, "\n");
            }
            on_new_line = 1;
            newline_pending = 0;
            space_pending = 0;
        }

        // Apply Indent or Space
        if (on_new_line) {
            db_indent(&out, indent);
            on_new_line = 0;
        } else if (space_pending) {
            db_append_str(&out, " ");
            space_pending = 0;
        }

        if (t.type == FMT_LBRACE) {
            db_append_str(&out, " "); // Space before {
        }

        db_append(&out, t.start, t.length);

        if (t.type == FMT_LBRACE) {
            indent++;
            newline_pending = 1;
        } else if (t.type == FMT_RBRACE || t.type == FMT_SEMICOLON || t.type == FMT_COMMENT) {
            newline_pending = 1;
        } else if (t.type == FMT_COMMA) {
            space_pending = 1;
        }

        prev = t;
    }
    
    db_append_str(&out, "\n");
    return out.data;
}

int cnd_format_file(const char* in_path, const char* out_path) {
    FILE* f = fopen(in_path, "rb");
    if (!f) {
        fprintf(stderr, "Failed to open input file: %s\n", in_path);
        return 1;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char* source = (char*)malloc(size + 1);
    fread(source, 1, size, f);
    source[size] = '\0';
    fclose(f);

    char* formatted = cnd_format_source(source);
    free(source);

    if (!formatted) return 1;

    FILE* out = stdout;
    if (out_path) {
        out = fopen(out_path, "w");
        if (!out) {
            fprintf(stderr, "Failed to open output file: %s\n", out_path);
            free(formatted);
            return 1;
        }
    }

    fputs(formatted, out);
    
    if (out_path && out != stdout) fclose(out);
    free(formatted);
    return 0;
}

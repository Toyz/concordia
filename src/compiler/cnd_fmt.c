#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "../../include/compiler.h"
#include "cnd_internal.h"

// --- Formatting Lexer ---
// We need a lexer that returns comments and newlines to preserve structure

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
    FMT_WHITESPACE // spaces/tabs (for preservation if needed, but we usually normalize)
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
        // Simple skip for now, similar to main lexer but less strict validation
        while (is_digit_f(*l->current) || *l->current == 'x' || *l->current == 'X' || *l->current == '.' || (*l->current >= 'a' && *l->current <= 'f') || (*l->current >= 'A' && *l->current <= 'F')) {
            l->current++;
        }
        t.length = (int)(l->current - t.start);
        return t;
    }

    // Unknown char, treat as identifier/symbol for now to avoid crash
    t.type = FMT_IDENTIFIER; 
    return t;
}

// --- Formatter ---

static void print_indent(FILE* out, int level) {
    for (int i = 0; i < level; i++) fprintf(out, "    ");
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

    FILE* out = stdout;
    if (out_path) {
        out = fopen(out_path, "w");
        if (!out) {
            fprintf(stderr, "Failed to open output file: %s\n", out_path);
            free(source);
            return 1;
        }
    }

    FmtLexer lexer;
    fmt_init(&lexer, source);

    int indent = 0;
    int newline_pending = 0; // 0: no, 1: yes, 2: double newline
    int space_pending = 0;
    int on_new_line = 1; // Are we at the start of a line?

    FmtToken t;
    FmtToken prev = {FMT_EOF, NULL, 0};

    while (1) {
        t = fmt_next(&lexer);
        if (t.type == FMT_EOF) break;

        // Handle Whitespace/Newlines (Input)
        if (t.type == FMT_WHITESPACE) {
            // Ignore input whitespace, we generate our own
            // But if it's a comment following code, we might need a space
            if (prev.type != FMT_NEWLINE && prev.type != FMT_EOF) {
                space_pending = 1;
            }
            continue;
        }

        if (t.type == FMT_NEWLINE) {
            if (newline_pending < 2) newline_pending++;
            continue;
        }

        // Logic for outputting tokens
        
        // Dedent before closing brace
        if (t.type == FMT_RBRACE) {
            indent--;
            if (indent < 0) indent = 0;
            newline_pending = 1; // Force newline before }
        }

        // Apply pending newlines
        if (newline_pending > 0) {
            if (!on_new_line) {
                fprintf(out, "\n");
                if (newline_pending > 1) fprintf(out, "\n"); // Preserve blank lines
            }
            on_new_line = 1;
            newline_pending = 0;
            space_pending = 0;
        }

        // Apply Indent
        if (on_new_line) {
            print_indent(out, indent);
            on_new_line = 0;
        } else if (space_pending) {
            fprintf(out, " ");
            space_pending = 0;
        }

        // Special spacing rules
        if (t.type == FMT_LBRACE) {
            fprintf(out, " "); // Always space before {
        }

        // Print Token
        fwrite(t.start, 1, t.length, out);

        // Post-token rules
        if (t.type == FMT_LBRACE) {
            indent++;
            newline_pending = 1;
        } else if (t.type == FMT_RBRACE) {
            newline_pending = 1;
        } else if (t.type == FMT_SEMICOLON) {
            newline_pending = 1;
        } else if (t.type == FMT_COMMENT) {
            newline_pending = 1;
        } else if (t.type == FMT_COMMA) {
            space_pending = 1;
        } else if (t.type == FMT_AT) {
            // No space after @
        } else {
            // Default spacing between tokens?
            // e.g. "uint16 voltage" -> space needed
            // "packet Status" -> space needed
            // We rely on input whitespace setting 'space_pending' for now, 
            // but we should enforce it for keywords if we can identify them.
            // Since we don't parse keywords specifically in FmtLexer (just IDENTIFIER),
            // we rely on the fact that the user probably had a space there, 
            // OR we can check if next token is identifier/number.
            
            // Lookahead for spacing?
            // Or just set space_pending = 1 if next is identifier?
            // Let's rely on input whitespace for "intra-line" spacing for now,
            // as it's safer than guessing.
            // But we stripped input whitespace!
            // Wait, I added `if (t.type == FMT_WHITESPACE) space_pending = 1;`
            // So if the user had "uint16 voltage", we see WHITESPACE and set space_pending.
            // If user had "uint16voltage", we wouldn't.
            // This preserves necessary spaces.
        }

        prev = t;
    }
    
    fprintf(out, "\n"); // End with newline

    if (out_path && out != stdout) {
        fclose(out);
    }
    free(source);
    return 0;
}

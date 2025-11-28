#include "cnd_internal.h"

void lexer_init(Lexer* lexer, const char* source) {
    lexer->source = source;
    lexer->current = source;
    lexer->line = 1;
}

int is_alpha_c(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
int is_digit_c(char c) { return c >= '0' && c <= '9'; }
int is_hex_c(char c) { return is_digit_c(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }

static TokenType check_keyword(const char* start, int length) {
    struct { const char* name; TokenType type; } keywords[] = {
        {"struct", TOK_STRUCT},
        {"packet", TOK_PACKET},
        {"enum", TOK_ENUM},
        {"switch", TOK_SWITCH},
        {"case", TOK_CASE},
        {"default", TOK_DEFAULT},
        {"true", TOK_TRUE},
        {"false", TOK_FALSE},
        {NULL, TOK_ERROR}
    };
    for (int i = 0; keywords[i].name; i++) {
        size_t kw_len = strlen(keywords[i].name);
        if (length == (int)kw_len && strncmp(start, keywords[i].name, length) == 0) {
            return keywords[i].type;
        }
    }
    return TOK_IDENTIFIER;
}

Token lexer_next(Lexer* lexer) {
    while (*lexer->current != '\0') {
        char c = *lexer->current;

        if (c == ' ' || c == '\r' || c == '\t') { lexer->current++; continue; }
        if (c == '\n') { lexer->line++; lexer->current++; continue; }
        if (c == '/' && *(lexer->current + 1) == '/') {
            while (*lexer->current != '\0' && *lexer->current != '\n') lexer->current++;
            continue;
        }
        if (c == '/' && *(lexer->current + 1) == '*') {
            lexer->current += 2;
            while (*lexer->current != '\0') {
                if (*lexer->current == '\n') lexer->line++;
                if (*lexer->current == '*' && *(lexer->current + 1) == '/') {
                    lexer->current += 2;
                    break;
                }
                lexer->current++;
            }
            continue;
        }

        lexer->current++;
        Token token;
        token.start = lexer->current - 1;
        token.line = lexer->line;
        token.length = 1;

        switch (c) {
            case '{': token.type = TOK_LBRACE; return token;
            case '}': token.type = TOK_RBRACE; return token;
            case '[': token.type = TOK_LBRACKET; return token;
            case ']': token.type = TOK_RBRACKET; return token;
            case '(': token.type = TOK_LPAREN; return token;
            case ')': token.type = TOK_RPAREN; return token;
            case ';': token.type = TOK_SEMICOLON; return token;
            case ':': token.type = TOK_COLON; return token;
            case ',': token.type = TOK_COMMA; return token;
            case '@': token.type = TOK_AT; return token;
            case '=': token.type = TOK_EQUALS; return token;
            case '.': token.type = TOK_DOT; return token;
            case '"': {
                token.type = TOK_STRING;
                token.start = lexer->current;
                while (*lexer->current != '"' && *lexer->current != '\0') lexer->current++;
                token.length = (int)(lexer->current - token.start);
                if (*lexer->current == '"') lexer->current++;
                return token;
            }
        }

        if (is_alpha_c(c)) {
            token.type = TOK_IDENTIFIER;
            while (is_alpha_c(*lexer->current) || is_digit_c(*lexer->current)) {
                lexer->current++;
            }
            token.length = (int)(lexer->current - token.start);
            token.type = check_keyword(token.start, token.length);
            return token;
        }

        // Number Parsing (Hex, Float, Negative)
        if (is_digit_c(c) || (c == '-' && is_digit_c(*lexer->current))) {
            token.type = TOK_NUMBER;
            
            int is_hex = 0;
            // Check for Hex prefix 0x or -0x
            if (c == '0' && (*lexer->current == 'x' || *lexer->current == 'X')) {
                is_hex = 1;
                lexer->current++; // consume x
            } else if (c == '-' && *lexer->current == '0' && (*(lexer->current+1) == 'x' || *(lexer->current+1) == 'X')) {
                is_hex = 1;
                lexer->current += 2; // consume 0x
            }
            
            if (is_hex) {
                while (is_hex_c(*lexer->current)) lexer->current++;
            } else {
                // Decimal part
                while (is_digit_c(*lexer->current)) lexer->current++;
                // Fractional part
                if (*lexer->current == '.' && is_digit_c(*(lexer->current + 1))) {
                    lexer->current++;
                    while (is_digit_c(*lexer->current)) lexer->current++;
                }
            }
            
            token.length = (int)(lexer->current - token.start);
            return token;
        }
        
        
        token.type = TOK_ERROR;
        return token;
    }
    Token t = { TOK_EOF, lexer->current, 0, lexer->line };
    return t;
}

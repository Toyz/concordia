#include "cnd_internal.h"

// Precedence levels
typedef enum {
    PREC_NONE,
    PREC_OR,       // ||
    PREC_AND,      // &&
    PREC_BIT_OR,   // |
    PREC_BIT_XOR,  // ^
    PREC_BIT_AND,  // &
    PREC_EQUALITY, // == !=
    PREC_COMPARISON, // < > <= >=
    PREC_SHIFT,    // << >>
    PREC_TERM,     // + -
    PREC_FACTOR,   // * / %
    PREC_UNARY,    // ! - ~
    PREC_PRIMARY
} Precedence;

typedef enum {
    TYPE_INT,
    TYPE_FLOAT,
    TYPE_UNKNOWN
} ExprType;

typedef ExprType (*ParsePrefixFn)(Parser* p);
typedef ExprType (*ParseInfixFn)(Parser* p, ExprType left);

typedef struct {
    ParsePrefixFn prefix;
    ParseInfixFn infix;
    Precedence precedence;
} ParseRule;

static ParseRule* get_rule(TokenType type);
static ExprType parse_precedence(Parser* p, Precedence prec);
ExprType parse_primary(Parser* p);
static ExprType parse_unary(Parser* p);
static ExprType parse_binary(Parser* p, ExprType left);
static ExprType parse_grouping(Parser* p);
ExprType parse_expression(Parser* p);

static int is_float_literal(Token t) {
    for (int i = 0; i < t.length; i++) {
        if (t.start[i] == '.' || t.start[i] == 'e' || t.start[i] == 'E') return 1;
    }
    return 0;
}

static ExprType parse_unary(Parser* p) {
    TokenType op = p->current.type;
    advance(p);
    ExprType t = parse_precedence(p, PREC_UNARY);
    switch (op) {
        case TOK_BANG: buf_push(p->target, OP_LOG_NOT); return TYPE_INT;
        case TOK_TILDE: buf_push(p->target, OP_BIT_NOT); return TYPE_INT;
        case TOK_MINUS: 
            if (t == TYPE_FLOAT) { buf_push(p->target, OP_FNEG); return TYPE_FLOAT; }
            else { buf_push(p->target, OP_NEG); return TYPE_INT; }
        default: return TYPE_UNKNOWN;
    }
}

static ExprType parse_binary(Parser* p, ExprType left) {
    TokenType op = p->previous.type;
    ParseRule* rule = get_rule(op);
    ExprType right = parse_precedence(p, (Precedence)(rule->precedence + 1));
    
    int is_float_op = 0;
    if (left == TYPE_FLOAT || right == TYPE_FLOAT) {
        is_float_op = 1;
        if (left == TYPE_INT) {
            buf_push(p->target, OP_SWAP); buf_push(p->target, OP_ITOF); buf_push(p->target, OP_SWAP);
        } else if (right == TYPE_INT) {
            buf_push(p->target, OP_ITOF);
        }
    }

    switch (op) {
        case TOK_PIPE_PIPE: buf_push(p->target, OP_LOG_OR); return TYPE_INT;
        case TOK_AMP_AMP: buf_push(p->target, OP_LOG_AND); return TYPE_INT;
        case TOK_PIPE: buf_push(p->target, OP_BIT_OR); return TYPE_INT;
        case TOK_CARET: buf_push(p->target, OP_BIT_XOR); return TYPE_INT;
        case TOK_AMP: buf_push(p->target, OP_BIT_AND); return TYPE_INT;
        case TOK_EQ_EQ: buf_push(p->target, OP_EQ); return TYPE_INT;
        case TOK_BANG_EQ: buf_push(p->target, OP_NEQ); return TYPE_INT;
        case TOK_LT: buf_push(p->target, OP_LT); return TYPE_INT;
        case TOK_LT_EQ: buf_push(p->target, OP_LTE); return TYPE_INT;
        case TOK_GT: buf_push(p->target, OP_GT); return TYPE_INT;
        case TOK_GT_EQ: buf_push(p->target, OP_GTE); return TYPE_INT;
        case TOK_LSHIFT: buf_push(p->target, OP_SHL); return TYPE_INT;
        case TOK_RSHIFT: buf_push(p->target, OP_SHR); return TYPE_INT;
        
        case TOK_PLUS: 
            if (is_float_op) { buf_push(p->target, OP_FADD); return TYPE_FLOAT; }
            else { buf_push(p->target, OP_ADD); return TYPE_INT; }
        case TOK_MINUS: 
            if (is_float_op) { buf_push(p->target, OP_FSUB); return TYPE_FLOAT; }
            else { buf_push(p->target, OP_SUB); return TYPE_INT; }
        case TOK_STAR: 
            if (is_float_op) { buf_push(p->target, OP_FMUL); return TYPE_FLOAT; }
            else { buf_push(p->target, OP_MUL); return TYPE_INT; }
        case TOK_SLASH: 
            if (is_float_op) { buf_push(p->target, OP_FDIV); return TYPE_FLOAT; }
            else { buf_push(p->target, OP_DIV); return TYPE_INT; }
        case TOK_PERCENT: buf_push(p->target, OP_MOD); return TYPE_INT;
        default: return TYPE_UNKNOWN;
    }
}

static ExprType parse_grouping(Parser* p) {
    advance(p); // Consume (
    ExprType t = parse_expression(p);
    consume(p, TOK_RPAREN, "Expect ) after expression");
    return t;
}

static ParseRule rules[] = {
    [TOK_LPAREN]    = {parse_grouping, NULL, PREC_NONE},
    [TOK_MINUS]     = {parse_unary, parse_binary, PREC_TERM},
    [TOK_PLUS]      = {NULL, parse_binary, PREC_TERM},
    [TOK_SLASH]     = {NULL, parse_binary, PREC_FACTOR},
    [TOK_STAR]      = {NULL, parse_binary, PREC_FACTOR},
    [TOK_PERCENT]   = {NULL, parse_binary, PREC_FACTOR},
    [TOK_BANG]      = {parse_unary, NULL, PREC_NONE},
    [TOK_TILDE]     = {parse_unary, NULL, PREC_NONE},
    [TOK_PIPE_PIPE] = {NULL, parse_binary, PREC_OR},
    [TOK_AMP_AMP]   = {NULL, parse_binary, PREC_AND},
    [TOK_PIPE]      = {NULL, parse_binary, PREC_BIT_OR},
    [TOK_CARET]     = {NULL, parse_binary, PREC_BIT_XOR},
    [TOK_AMP]       = {NULL, parse_binary, PREC_BIT_AND},
    [TOK_EQ_EQ]     = {NULL, parse_binary, PREC_EQUALITY},
    [TOK_BANG_EQ]   = {NULL, parse_binary, PREC_EQUALITY},
    [TOK_LT]        = {NULL, parse_binary, PREC_COMPARISON},
    [TOK_LT_EQ]     = {NULL, parse_binary, PREC_COMPARISON},
    [TOK_GT]        = {NULL, parse_binary, PREC_COMPARISON},
    [TOK_GT_EQ]     = {NULL, parse_binary, PREC_COMPARISON},
    [TOK_LSHIFT]    = {NULL, parse_binary, PREC_SHIFT},
    [TOK_RSHIFT]    = {NULL, parse_binary, PREC_SHIFT},
    [TOK_NUMBER]    = {parse_primary, NULL, PREC_NONE},
    [TOK_TRUE]      = {parse_primary, NULL, PREC_NONE},
    [TOK_FALSE]     = {parse_primary, NULL, PREC_NONE},
    [TOK_SELF]      = {parse_primary, NULL, PREC_NONE},
    [TOK_IDENTIFIER]= {parse_primary, NULL, PREC_NONE},
};

static ParseRule* get_rule(TokenType type) {
    return &rules[type];
}

ExprType parse_expression(Parser* p) {
    return parse_precedence(p, PREC_OR);
}

static ExprType parse_precedence(Parser* p, Precedence prec) {
    advance(p);
    ParsePrefixFn prefix_rule = get_rule(p->previous.type)->prefix;
    if (prefix_rule == NULL) {
        parser_error(p, "Expect expression");
        return TYPE_UNKNOWN;
    }

    ExprType left = prefix_rule(p);

    while (prec <= get_rule(p->current.type)->precedence) {
        advance(p);
        ParseInfixFn infix_rule = get_rule(p->previous.type)->infix;
        left = infix_rule(p, left);
    }
    return left;
}

static char* append_doc(char* current, Token t);

void parser_error(Parser* p, const char* msg) {
    p->had_error = 1;
    p->error_count++;

    // Calculate column
    const char* line_start = p->current.start;
    while (line_start > p->lexer.source && *(line_start - 1) != '\n' && *(line_start - 1) != '\r') {
        line_start--;
    }
    int col = (int)(p->current.start - line_start) + 1;

    // Store error for LSP/API usage
    if ((size_t)p->error_count > p->error_cap) { // Allow growing
        size_t new_cap = (p->error_cap == 0) ? 8 : p->error_cap * 2;
        // Safety check
        if (new_cap > 1024) new_cap = 1024; // Cap max errors stored
        if ((size_t)p->error_count <= new_cap) {
            p->errors = realloc(p->errors, new_cap * sizeof(CompilerError));
            p->error_cap = new_cap;
        }
    }
    
    if (p->errors && (size_t)p->error_count <= p->error_cap) {
        CompilerError* err = &p->errors[p->error_count - 1];
        err->line = p->current.line;
        err->column = col;
        err->message = strdup(msg); // Make sure to free this later!
    }

    if (p->silent) return;

    if (p->json_output) {
        // JSON format: {"file": "...", "line": N, "message": "...", "token": "..."}
        // We use manual JSON construction to avoid dependency on cJSON in the compiler core
        printf("{\"file\": \"%s\", \"line\": %d, \"message\": \"%s\", \"token\": \"%.*s\"}\n",
            p->current_path, p->current.line, msg, p->current.length, p->current.start);
    } else {
        // Standard error format: file:line:col: error: message
        // With colors: Bold File, Red Error
        printf(COLOR_BOLD "%s:%d:%d: " COLOR_RED "error: " COLOR_RESET COLOR_BOLD "%s" COLOR_RESET "\n", 
            p->current_path, p->current.line, col, msg);
        
        // Print source line context
        const char* line_end = p->current.start;
        while (*line_end && *line_end != '\n' && *line_end != '\r') {
            line_end++;
        }
        
        int line_len = (int)(line_end - line_start);
        if (line_len > 0) {
            printf("    %.*s\n", line_len, line_start);
            printf("    ");
            for(int i=0; i<col-1; i++) {
                if (line_start[i] == '\t') printf("\t");
                else printf(" ");
            }
            
            // Print squiggles
            printf(COLOR_RED "^");
            int len = p->current.length;
            if (len < 1) len = 1;
            for(int i=1; i<len; i++) printf("~");
            printf(COLOR_RESET "\n");
        }
    }
}

void advance(Parser* p) {
    p->previous = p->current;
    for (;;) {
        p->current = lexer_next(&p->lexer);
        if (p->current.type != TOK_ERROR) break;
        parser_error(p, "Unexpected character");
    }
}

void consume(Parser* p, TokenType type, const char* msg) {
    if (p->current.type == type) {
        advance(p);
        return;
    }
    parser_error(p, msg);
}

int match_keyword(Token t, const char* kw) {
    if (t.type != TOK_IDENTIFIER) return 0;
    size_t len = strlen(kw);
    if (len != (size_t)t.length) return 0;
    return memcmp(t.start, kw, len) == 0;
}



void emit_range_check(Parser* p, uint8_t type_op, Token min_tok, Token max_tok) {
    // Validate range
    if (type_op == OP_IO_U8 || type_op == OP_IO_U16 || type_op == OP_IO_U32) {
        uint32_t min = parse_number_u32(min_tok.start, min_tok.length);
        uint32_t max = parse_number_u32(max_tok.start, max_tok.length);
        if (min > max) { parser_error(p, "Range min > max"); return; }
    } else if (type_op == OP_IO_I8 || type_op == OP_IO_I16 || type_op == OP_IO_I32 || type_op == OP_IO_I64) {
        int64_t min = parse_number_i64(min_tok.start, min_tok.length);
        int64_t max = parse_number_i64(max_tok.start, max_tok.length);
        if (min > max) { parser_error(p, "Range min > max"); return; }
    } else if (type_op == OP_IO_U64) {
        uint64_t min = (uint64_t)parse_number_i64(min_tok.start, min_tok.length);
        uint64_t max = (uint64_t)parse_number_i64(max_tok.start, max_tok.length);
        if (min > max) { parser_error(p, "Range min > max"); return; }
    } else if (type_op == OP_IO_F32 || type_op == OP_IO_F64) {
        double min = parse_number_double(min_tok.start, min_tok.length);
        double max = parse_number_double(max_tok.start, max_tok.length);
        if (min > max) { parser_error(p, "Range min > max"); return; }
    }

    buf_push(p->target, OP_RANGE_CHECK);
    buf_push(p->target, type_op);
    
    if (type_op == OP_IO_U8) {
        buf_push(p->target, (uint8_t)parse_number_u32(min_tok.start, min_tok.length));
        buf_push(p->target, (uint8_t)parse_number_u32(max_tok.start, max_tok.length));
    } else if (type_op == OP_IO_U16) {
        buf_push_u16(p->target, (uint16_t)parse_number_u32(min_tok.start, min_tok.length));
        buf_push_u16(p->target, (uint16_t)parse_number_u32(max_tok.start, max_tok.length));
    } else if (type_op == OP_IO_U32) {
        buf_push_u32(p->target, parse_number_u32(min_tok.start, min_tok.length));
        buf_push_u32(p->target, parse_number_u32(max_tok.start, max_tok.length));
    } else if (type_op == OP_IO_I8) {
        buf_push(p->target, (uint8_t)(int8_t)parse_number_i64(min_tok.start, min_tok.length));
        buf_push(p->target, (uint8_t)(int8_t)parse_number_i64(max_tok.start, max_tok.length));
    } else if (type_op == OP_IO_I16) {
        buf_push_u16(p->target, (uint16_t)(int16_t)parse_number_i64(min_tok.start, min_tok.length));
        buf_push_u16(p->target, (uint16_t)(int16_t)parse_number_i64(max_tok.start, max_tok.length));
    } else if (type_op == OP_IO_I32) {
        buf_push_u32(p->target, (uint32_t)(int32_t)parse_number_i64(min_tok.start, min_tok.length));
        buf_push_u32(p->target, (uint32_t)(int32_t)parse_number_i64(max_tok.start, max_tok.length));
    } else if (type_op == OP_IO_F32) {
        float fmin = (float)parse_number_double(min_tok.start, min_tok.length);
        float fmax = (float)parse_number_double(max_tok.start, max_tok.length);
        uint32_t imin, imax;
        memcpy(&imin, &fmin, 4);
        memcpy(&imax, &fmax, 4);
        buf_push_u32(p->target, imin);
        buf_push_u32(p->target, imax);
    } else if (type_op == OP_IO_U64) {
        buf_push_u64(p->target, (uint64_t)parse_number_i64(min_tok.start, min_tok.length));
        buf_push_u64(p->target, (uint64_t)parse_number_i64(max_tok.start, max_tok.length));
    } else if (type_op == OP_IO_I64) {
        buf_push_u64(p->target, (uint64_t)parse_number_i64(min_tok.start, min_tok.length));
        buf_push_u64(p->target, (uint64_t)parse_number_i64(max_tok.start, max_tok.length));
    } else if (type_op == OP_IO_F64) {
        double dmin = parse_number_double(min_tok.start, min_tok.length);
        double dmax = parse_number_double(max_tok.start, max_tok.length);
        uint64_t imin, imax;
        memcpy(&imin, &dmin, 8);
        memcpy(&imax, &dmax, 8);
        buf_push_u64(p->target, imin);
        buf_push_u64(p->target, imax);
    }
}

void parse_field(Parser* p, const char* doc); // Forward declaration
void parse_block(Parser* p); // Forward declaration

// Expression Parsing
ExprType parse_primary(Parser* p) {
    if (p->previous.type == TOK_NUMBER) {
        if (is_float_literal(p->previous)) {
            double val = parse_number_double(p->previous.start, p->previous.length);
            buf_push(p->target, OP_PUSH_IMM);
            uint64_t bits; memcpy(&bits, &val, 8);
            buf_push_u64(p->target, bits);
            return TYPE_FLOAT;
        } else {
            uint64_t val = (uint64_t)parse_number_i64(p->previous.start, p->previous.length);
            buf_push(p->target, OP_PUSH_IMM);
            buf_push_u64(p->target, val);
            return TYPE_INT;
        }
    } else if (p->previous.type == TOK_TRUE) {
        buf_push(p->target, OP_PUSH_IMM);
        buf_push_u64(p->target, 1);
        return TYPE_INT;
    } else if (p->previous.type == TOK_FALSE) {
        buf_push(p->target, OP_PUSH_IMM);
        buf_push_u64(p->target, 0);
        return TYPE_INT;
    } else if (p->previous.type == TOK_SELF) {
        buf_push(p->target, OP_DUP);
        return TYPE_UNKNOWN;
    } else if (p->previous.type == TOK_IDENTIFIER) {
        Token name = p->previous;
        
        if (match_keyword(name, "sin")) {
            consume(p, TOK_LPAREN, "Expect ("); 
            if (parse_expression(p) == TYPE_INT) buf_push(p->target, OP_ITOF);
            consume(p, TOK_RPAREN, "Expect )");
            buf_push(p->target, OP_SIN);
            return TYPE_FLOAT;
        } else if (match_keyword(name, "cos")) {
            consume(p, TOK_LPAREN, "Expect ("); 
            if (parse_expression(p) == TYPE_INT) buf_push(p->target, OP_ITOF);
            consume(p, TOK_RPAREN, "Expect )");
            buf_push(p->target, OP_COS);
            return TYPE_FLOAT;
        } else if (match_keyword(name, "tan")) {
            consume(p, TOK_LPAREN, "Expect ("); 
            if (parse_expression(p) == TYPE_INT) buf_push(p->target, OP_ITOF);
            consume(p, TOK_RPAREN, "Expect )");
            buf_push(p->target, OP_TAN);
            return TYPE_FLOAT;
        } else if (match_keyword(name, "sqrt")) {
            consume(p, TOK_LPAREN, "Expect ("); 
            if (parse_expression(p) == TYPE_INT) buf_push(p->target, OP_ITOF);
            consume(p, TOK_RPAREN, "Expect )");
            buf_push(p->target, OP_SQRT);
            return TYPE_FLOAT;
        } else if (match_keyword(name, "log")) {
            consume(p, TOK_LPAREN, "Expect ("); 
            if (parse_expression(p) == TYPE_INT) buf_push(p->target, OP_ITOF);
            consume(p, TOK_RPAREN, "Expect )");
            buf_push(p->target, OP_LOG);
            return TYPE_FLOAT;
        } else if (match_keyword(name, "abs")) {
            consume(p, TOK_LPAREN, "Expect ("); 
            if (parse_expression(p) == TYPE_INT) buf_push(p->target, OP_ITOF);
            consume(p, TOK_RPAREN, "Expect )");
            buf_push(p->target, OP_ABS);
            return TYPE_FLOAT;
        } else if (match_keyword(name, "pow")) {
            consume(p, TOK_LPAREN, "Expect ("); 
            if (parse_expression(p) == TYPE_INT) buf_push(p->target, OP_ITOF);
            consume(p, TOK_COMMA, "Expect ,");
            if (parse_expression(p) == TYPE_INT) buf_push(p->target, OP_ITOF);
            consume(p, TOK_RPAREN, "Expect )");
            buf_push(p->target, OP_POW);
            return TYPE_FLOAT;
        } else if (match_keyword(name, "float")) {
            consume(p, TOK_LPAREN, "Expect ("); 
            parse_expression(p); // Ignore type, force ITOF
            consume(p, TOK_RPAREN, "Expect )");
            buf_push(p->target, OP_ITOF);
            return TYPE_FLOAT;
        } else if (match_keyword(name, "int")) {
            consume(p, TOK_LPAREN, "Expect ("); 
            parse_expression(p); // Ignore type, force FTOI
            consume(p, TOK_RPAREN, "Expect )");
            buf_push(p->target, OP_FTOI);
            return TYPE_INT;
        } else {
            uint16_t key_id = strtab_add(&p->strtab, name.start, name.length);
            buf_push(p->target, OP_LOAD_CTX);
            buf_push_u16(p->target, key_id);
            return TYPE_UNKNOWN;
        }
    }
    return TYPE_UNKNOWN;
}

void parse_if(Parser* p) {
    consume(p, TOK_LPAREN, "Expect ( after if");
    parse_expression(p);
    consume(p, TOK_RPAREN, "Expect ) after if condition");
    
    size_t jump_false_loc = buf_current_offset(p->target);
    buf_push(p->target, OP_JUMP_IF_NOT);
    buf_push_u32(p->target, 0); // Placeholder
    
    parse_block(p);
    
    size_t jump_end_loc = buf_current_offset(p->target);
    buf_push(p->target, OP_JUMP);
    buf_push_u32(p->target, 0); // Placeholder
    
    // Patch jump_false
    size_t else_start = buf_current_offset(p->target);
    int32_t false_offset = (int32_t)(else_start - (jump_false_loc + 1 + 4));
    buf_write_u32_at(p->target, jump_false_loc + 1, (uint32_t)false_offset);
    
    if (p->current.type == TOK_ELSE) {
        advance(p);
        if (p->current.type == TOK_IF) {
            advance(p);
            parse_if(p); // recursive else if
        } else {
            parse_block(p);
        }
    }
    
    // Patch jump_end
    size_t end_loc = buf_current_offset(p->target);
    int32_t end_offset = (int32_t)(end_loc - (jump_end_loc + 1 + 4));
    buf_write_u32_at(p->target, jump_end_loc + 1, (uint32_t)end_offset);
}

typedef struct {
    uint64_t val;
    int32_t offset;
} SwitchCase;

void parse_switch(Parser* p) {
    consume(p, TOK_LPAREN, "Expect ( after switch");
    Token field_tok = p->current;
    consume(p, TOK_IDENTIFIER, "Expect discriminator field name");
    if (p->verbose) printf("  [Switch] Discriminator: '%.*s'\n", field_tok.length, field_tok.start);
    uint16_t key_id = strtab_add(&p->strtab, field_tok.start, field_tok.length);
    consume(p, TOK_RPAREN, "Expect )");
    consume(p, TOK_LBRACE, "Expect {");

    size_t switch_instr_loc = buf_current_offset(p->target);
    buf_push(p->target, OP_SWITCH);
    buf_push_u16(p->target, key_id);
    buf_push_u32(p->target, 0); // Placeholder for Table Offset

    // Code block starts here
    size_t code_start_loc = buf_current_offset(p->target);

    SwitchCase* cases = NULL;
    size_t case_count = 0;
    size_t case_cap = 0;
    
    size_t* jump_fixups = NULL;
    size_t jump_count = 0;
    size_t jump_cap = 0;

    int32_t default_offset = -1; // -1 indicates no default

    while (p->current.type != TOK_RBRACE && p->current.type != TOK_EOF && !p->had_error) {
        if (p->current.type == TOK_CASE) {
            advance(p); // consume case
            
            uint64_t val = 0;
            if (p->current.type == TOK_NUMBER) {
                val = (uint64_t)parse_number_i64(p->current.start, p->current.length);
                advance(p);
            } else if (p->current.type == TOK_IDENTIFIER) {
                // Enum.Value syntax
                Token enum_name = p->current;
                advance(p);
                if (p->current.type == TOK_DOT) {
                    advance(p); // Consume .
                    Token val_name = p->current;
                    consume(p, TOK_IDENTIFIER, "Expect Enum Value Name");
                    
                    EnumDef* edef = enum_reg_find(&p->enums, enum_name.start, enum_name.length);
                    if (!edef) {
                        parser_error(p, "Enum not found");
                    } else {
                        int found = 0;
                        for(size_t i=0; i<edef->count; i++) {
                            if (strlen(edef->values[i].name) == (size_t)val_name.length &&
                                strncmp(edef->values[i].name, val_name.start, val_name.length) == 0) {
                                val = (uint64_t)edef->values[i].value;
                                found = 1;
                                break;
                            }
                        }
                        if (!found) parser_error(p, "Enum value not found");
                    }
                } else {
                    parser_error(p, "Expect . after Enum name (Enum.Value)");
                }
            }
            
            consume(p, TOK_COLON, "Expect : after case value");
            
            // Register Case
            // Check for duplicates
            for(size_t i=0; i<case_count; i++) {
                if (cases[i].val == val) {
                    parser_error(p, "Duplicate case value");
                }
            }

            if (case_count >= case_cap) {
                case_cap = (case_cap == 0) ? 8 : case_cap * 2;
                cases = realloc(cases, case_cap * sizeof(SwitchCase));
            }
            cases[case_count].val = val;
            cases[case_count].offset = (int32_t)(buf_current_offset(p->target) - code_start_loc);
            case_count++;
            
            // Parse body
            if (p->current.type == TOK_LBRACE) {
                parse_block(p);
            } else {
                parse_field(p, NULL);
            }
            
            // Emit Jump to end (break)
            size_t jump_loc = buf_current_offset(p->target);
            buf_push(p->target, OP_JUMP);
            buf_push_u32(p->target, 0); // Placeholder
            
            if (jump_count >= jump_cap) {
                jump_cap = (jump_cap == 0) ? 8 : jump_cap * 2;
                jump_fixups = realloc(jump_fixups, jump_cap * sizeof(size_t));
            }
            jump_fixups[jump_count++] = jump_loc;

        } else if (p->current.type == TOK_DEFAULT) {
            advance(p); consume(p, TOK_COLON, "Expect :");
            default_offset = (int32_t)(buf_current_offset(p->target) - code_start_loc);
            
            if (p->current.type == TOK_LBRACE) {
                parse_block(p);
            } else {
                parse_field(p, NULL);
            }
            
            // Emit Jump to end (break)
            size_t jump_loc = buf_current_offset(p->target);
            buf_push(p->target, OP_JUMP);
            buf_push_u32(p->target, 0); // Placeholder
            
            if (jump_count >= jump_cap) {
                jump_cap = (jump_cap == 0) ? 8 : jump_cap * 2;
                jump_fixups = realloc(jump_fixups, jump_cap * sizeof(size_t));
            }
            jump_fixups[jump_count++] = jump_loc;
        } else {
            parser_error(p, "Expect case or default");
            advance(p);
        }
    }
    consume(p, TOK_RBRACE, "Expect }");
    
    // Analyze cases for Jump Table optimization
    int use_jump_table = 0;
    uint64_t min_val = UINT64_MAX;
    uint64_t max_val = 0;
    
    if (case_count > 3) { // Only optimize if enough cases
        min_val = cases[0].val;
        max_val = cases[0].val;
        for(size_t i=1; i<case_count; i++) {
            if (cases[i].val < min_val) min_val = cases[i].val;
            if (cases[i].val > max_val) max_val = cases[i].val;
        }
        
        uint64_t range = max_val - min_val;
        // Heuristic: Range must be reasonable (e.g. < 256) and density > 50%
        // Or just small range. Let's say range < 256 is always good.
        if (range < 256) {
            use_jump_table = 1;
        }
    }

    // Emit Table
    size_t table_start = buf_current_offset(p->target);
    size_t table_end = 0;
    
    if (use_jump_table) {
        // Patch Opcode
        buf_write_u8_at(p->target, switch_instr_loc, OP_SWITCH_TABLE);
        
        // Table Layout: Min(8), Max(8), Default(4), [Offset(4)] * (Range+1)
        buf_push_u64(p->target, min_val);
        buf_push_u64(p->target, max_val);
        
        // We need to know table_end to calculate default_offset if it's missing
        size_t range = (size_t)(max_val - min_val);
        size_t table_size = 8 + 8 + 4 + (range + 1) * 4;
        table_end = table_start + table_size;
        
        if (default_offset == -1) {
            default_offset = (int32_t)(table_end - code_start_loc);
        }
        buf_push_u32(p->target, (uint32_t)default_offset);
        
        // Fill table
        for (size_t i = 0; i <= range; i++) {
            uint64_t current_val = min_val + i;
            int32_t target = default_offset;
            // Find case (linear search is fine for compile time)
            for (size_t j = 0; j < case_count; j++) {
                if (cases[j].val == current_val) {
                    target = cases[j].offset;
                    break;
                }
            }
            buf_push_u32(p->target, (uint32_t)target);
        }
        
    } else {
        // Sparse Table Layout: Count(2), Default(4), [Val(8), Offset(4)] * Count
        buf_push_u16(p->target, (uint16_t)case_count);
        
        size_t table_size = 2 + 4 + case_count * 12;
        table_end = table_start + table_size;
        
        if (default_offset == -1) {
            default_offset = (int32_t)(table_end - code_start_loc);
        }
        buf_push_u32(p->target, (uint32_t)default_offset);
        
        for(size_t i=0; i<case_count; i++) {
            buf_push_u64(p->target, cases[i].val);
            buf_push_u32(p->target, (uint32_t)cases[i].offset);
        }
    }
    
    // Fixup Jumps to point to AFTER the table
    for(size_t i=0; i<jump_count; i++) {
        size_t jump_instr_end = jump_fixups[i] + 1 + 4;
        int32_t off = (int32_t)(table_end - jump_instr_end);
        buf_write_u32_at(p->target, jump_fixups[i] + 1, (uint32_t)off);
    }
    
    // Fixup SWITCH instruction
    size_t switch_instr_end = switch_instr_loc + 1 + 2 + 4;
    uint32_t table_rel_offset = (uint32_t)(table_start - switch_instr_end);
    buf_write_u32_at(p->target, switch_instr_loc + 3, table_rel_offset);
    
    if(cases) free(cases);
    if(jump_fixups) free(jump_fixups);
}

void parse_field(Parser* p, const char* doc) {
    (void)doc;
    if (p->had_error) return;
    
    // Consume doc comments (currently ignored for fields, but prevents syntax error)
    while (p->current.type == TOK_DOC_COMMENT) {
        advance(p);
    }
    
    if (p->current.type == TOK_SWITCH) {
        advance(p); // consume switch (was type_tok in previous logic, but here we check before)
        parse_switch(p);
        return;
    }

    if (p->current.type == TOK_IF) {
        advance(p);
        parse_if(p);
        return;
    }

    // Decorator State Variables
    uint32_t array_fixed_count = 0;
    int has_fixed_array_count = 0;
    uint64_t const_val = 0;
    int has_const = 0;
    int is_big_endian_field = 0;
    int is_standalone_op = 0;
    
    int has_range = 0;
    Token range_min_tok = {0};
    Token range_max_tok = {0};
    
    // Transform state
    cnd_trans_t trans_type = CND_TRANS_NONE;
    double trans_scale = 1.0;
    double trans_offset = 0.0;
    int64_t trans_int_val = 0;
    double poly_coeffs[16];
    uint8_t poly_count = 0;
    
    double spline_points[32]; // x0, y0, x1, y1...
    uint8_t spline_count = 0; // Number of points (pairs)

    // CRC State
    int has_crc = 0;
    uint32_t crc_width = 0;
    uint32_t crc_poly = 0;
    uint32_t crc_init = 0;
    uint32_t crc_xor = 0;
    uint8_t crc_flags = 0;

    int has_expr = 0;
    int has_eof = 0;
    int has_count_ref = 0;
    uint16_t count_ref_id = 0;

    // Jump Patching State for @depends_on
    // size_t jump_patches[8]; // Max 8 pending jumps
    // int jump_count = 0;
    
    while (p->current.type == TOK_AT) {
        advance(p); // Consume the '@'
        Token dec_name_token = p->current; // Save token of decorator name
        if (p->verbose) printf("    [Decorator] @%.*s\n", dec_name_token.length, dec_name_token.start);
        consume(p, TOK_IDENTIFIER, "Expect decorator name");
        
        if (match_keyword(dec_name_token, "big_endian") || match_keyword(dec_name_token, "be")) {
            is_big_endian_field = 1;
        } else if (match_keyword(dec_name_token, "little_endian") || match_keyword(dec_name_token, "le")) {
             is_big_endian_field = 0;
        } else if (match_keyword(dec_name_token, "fill")) {
            is_standalone_op = 1;
            uint8_t fill_val = 0;
            if (p->current.type == TOK_LPAREN) {
                advance(p);
                Token num = p->current;
                consume(p, TOK_NUMBER, "Expect fill bit value (0 or 1)");
                fill_val = (uint8_t)parse_number_u32(num.start, num.length);
                if (fill_val > 1) {
                    parser_error(p, "Fill bit must be 0 or 1");
                }
                consume(p, TOK_RPAREN, "Expect )");
            }
            buf_push(p->target, OP_ALIGN_FILL);
            buf_push(p->target, fill_val);
            
            // Reset bit count on fill (aligns to byte)
            if (p->in_bit_mode && p->is_bit_count_valid) {
                p->current_bit_count = 0;
            }
        } else if (match_keyword(dec_name_token, "crc_refin")) {
            crc_flags |= 1;
        } else if (match_keyword(dec_name_token, "crc_refout")) {
            crc_flags |= 2;
        } else if (match_keyword(dec_name_token, "optional")) {
            buf_push(p->target, OP_MARK_OPTIONAL);
        } else if (match_keyword(dec_name_token, "eof")) {
            has_eof = 1;
        } else { 
            consume(p, TOK_LPAREN, "Expect ("); 
            if (match_keyword(dec_name_token, "count") || match_keyword(dec_name_token, "len")) {
                if (p->current.type == TOK_IDENTIFIER) {
                    Token ref_tok = p->current;
                    advance(p);
                    // Verify that the referenced field exists (simple check: must be in string table already?)
                    // Actually, we can't easily check existence here because fields might be defined later?
                    // But for array counts, the length field usually comes BEFORE the array.
                    // Let's just ensure it's a valid identifier for now.
                    count_ref_id = strtab_add(&p->strtab, ref_tok.start, ref_tok.length);
                    has_count_ref = 1;
                } else if (p->current.type == TOK_NUMBER) {
                    Token num = p->current; 
                    advance(p);
                    array_fixed_count = parse_number_u32(num.start, num.length); has_fixed_array_count = 1;
                } else {
                    parser_error(p, "Expect number or variable name for count");
                }
            } else if (match_keyword(dec_name_token, "const") || match_keyword(dec_name_token, "match")) {
                Token num = p->current; consume(p, TOK_NUMBER, "Expect const/match value");
                const_val = (uint64_t)parse_number_i64(num.start, num.length); has_const = 1;
            } else if (match_keyword(dec_name_token, "pad")) {
                is_standalone_op = 1;
                Token num = p->current; consume(p, TOK_NUMBER, "Expect pad bits");
                uint32_t pad_bits = parse_number_u32(num.start, num.length); buf_push(p->target, OP_ALIGN_PAD); buf_push(p->target, (uint8_t)pad_bits);
                if (p->in_bit_mode && p->is_bit_count_valid) {
                    p->current_bit_count += pad_bits;
                }
            } else if (match_keyword(dec_name_token, "range")) {
                range_min_tok = p->current; if (range_min_tok.type == TOK_NUMBER) advance(p); else consume(p, TOK_NUMBER, "Expect min");
                consume(p, TOK_COMMA, "Expect ,");
                range_max_tok = p->current; if (range_max_tok.type == TOK_NUMBER) advance(p); else consume(p, TOK_NUMBER, "Expect max");
                has_range = 1;
            } else if (match_keyword(dec_name_token, "crc")) {
                Token num = p->current; consume(p, TOK_NUMBER, "Expect width");
                crc_width = parse_number_u32(num.start, num.length); has_crc = 1;
                if (crc_width == 16) { crc_poly = 0x1021; crc_init = 0xFFFF; crc_xor = 0; crc_flags = 0; } 
                else if (crc_width == 32) { crc_poly = 0x04C11DB7; crc_init = 0xFFFFFFFF; crc_xor = 0xFFFFFFFF; crc_flags = 3; }
            } else if (match_keyword(dec_name_token, "crc_poly")) {
                Token num = p->current; consume(p, TOK_NUMBER, "Expect poly"); crc_poly = parse_number_u32(num.start, num.length);
            } else if (match_keyword(dec_name_token, "crc_init")) {
                Token num = p->current; consume(p, TOK_NUMBER, "Expect init"); crc_init = parse_number_u32(num.start, num.length);
            } else if (match_keyword(dec_name_token, "crc_xor")) {
                Token num = p->current; consume(p, TOK_NUMBER, "Expect xor"); crc_xor = parse_number_u32(num.start, num.length);
            } else if (match_keyword(dec_name_token, "scale")) {
                Token num = p->current; if (num.type == TOK_NUMBER) advance(p); else consume(p, TOK_NUMBER, "Expect scale factor");
                trans_type = CND_TRANS_SCALE_F64; trans_scale = parse_number_double(num.start, num.length);
            } else if (match_keyword(dec_name_token, "offset")) {
                Token num = p->current; if (num.type == TOK_NUMBER) advance(p); else consume(p, TOK_NUMBER, "Expect offset value");
                if (trans_type != CND_TRANS_SCALE_F64) { trans_type = CND_TRANS_SCALE_F64; trans_scale = 1.0; } trans_offset = parse_number_double(num.start, num.length);
            } else if (match_keyword(dec_name_token, "mul")) {
                Token num = p->current; if (num.type == TOK_NUMBER) advance(p); else consume(p, TOK_NUMBER, "Expect mul value");
                trans_type = CND_TRANS_MUL_I64; trans_int_val = parse_number_i64(num.start, num.length);
            } else if (match_keyword(dec_name_token, "div")) {
                Token num = p->current; if (num.type == TOK_NUMBER) advance(p); else consume(p, TOK_NUMBER, "Expect div value");
                trans_type = CND_TRANS_DIV_I64; trans_int_val = parse_number_i64(num.start, num.length);
            } else if (match_keyword(dec_name_token, "add")) {
                Token num = p->current; if (num.type == TOK_NUMBER) advance(p); else consume(p, TOK_NUMBER, "Expect add value");
                trans_type = CND_TRANS_ADD_I64; trans_int_val = parse_number_i64(num.start, num.length);
            } else if (match_keyword(dec_name_token, "sub")) {
                Token num = p->current; if (num.type == TOK_NUMBER) advance(p); else consume(p, TOK_NUMBER, "Expect sub value");
                trans_type = CND_TRANS_SUB_I64; trans_int_val = parse_number_i64(num.start, num.length);
            } else if (match_keyword(dec_name_token, "poly")) {
                trans_type = CND_TRANS_POLY;
                while (true) {
                    Token num = p->current;
                    if (num.type == TOK_NUMBER) {
                        advance(p);
                        if (poly_count < 16) {
                            poly_coeffs[poly_count++] = parse_number_double(num.start, num.length);
                        } else {
                            parser_error(p, "Too many polynomial coefficients (max 16)");
                        }
                    } else {
                        consume(p, TOK_NUMBER, "Expect coefficient");
                    }
                    if (p->current.type == TOK_COMMA) {
                        advance(p);
                    } else {
                        break;
                    }
                }
            } else if (match_keyword(dec_name_token, "spline")) {
                trans_type = CND_TRANS_SPLINE;
                while (true) {
                    Token num = p->current;
                    if (num.type == TOK_NUMBER) {
                        advance(p);
                        if (spline_count < 16) { // 16 pairs = 32 doubles
                            spline_points[spline_count * 2] = parse_number_double(num.start, num.length);
                            consume(p, TOK_COMMA, "Expect comma between x and y");
                            Token y_tok = p->current;
                            consume(p, TOK_NUMBER, "Expect y value");
                            spline_points[spline_count * 2 + 1] = parse_number_double(y_tok.start, y_tok.length);
                            spline_count++;
                        } else {
                            parser_error(p, "Too many spline points (max 16)");
                        }
                    } else {
                        consume(p, TOK_NUMBER, "Expect x value");
                    }
                    if (p->current.type == TOK_COMMA) {
                        advance(p);
                    } else {
                        break;
                    }
                }
            } else if (match_keyword(dec_name_token, "expr")) {
                parse_expression(p);
                has_expr = 1;
            } else { 
                parser_error(p, "Unknown decorator");
                while (p->current.type != TOK_RPAREN && p->current.type != TOK_EOF) advance(p);
            }
            consume(p, TOK_RPAREN, "Expect )");
        }
    } 

    if (is_standalone_op && (p->current.type == TOK_SEMICOLON || p->current.type == TOK_RBRACE)) {
        if (p->current.type == TOK_SEMICOLON) advance(p);
        return;
    }
    
    // Emit CRC
    if (has_crc) {
        // Do nothing here, wait until type is parsed
    }
    
    // Emit Transform Op
    if (trans_type == CND_TRANS_SCALE_F64) {
        buf_push(p->target, OP_SCALE_LIN);
        buf_push_u64(p->target, *(uint64_t*)&trans_scale);
        buf_push_u64(p->target, *(uint64_t*)&trans_offset);
    } else if (trans_type == CND_TRANS_MUL_I64) {
        buf_push(p->target, OP_TRANS_MUL); buf_push_u64(p->target, (uint64_t)trans_int_val);
    } else if (trans_type == CND_TRANS_DIV_I64) {
        buf_push(p->target, OP_TRANS_DIV); buf_push_u64(p->target, (uint64_t)trans_int_val);
    } else if (trans_type == CND_TRANS_ADD_I64) {
        buf_push(p->target, OP_TRANS_ADD); buf_push_u64(p->target, (uint64_t)trans_int_val);
    } else if (trans_type == CND_TRANS_SUB_I64) {
        buf_push(p->target, OP_TRANS_SUB); buf_push_u64(p->target, (uint64_t)trans_int_val);
    } else if (trans_type == CND_TRANS_POLY) {
        buf_push(p->target, OP_TRANS_POLY);
        buf_push(p->target, poly_count);
        for (int i = 0; i < poly_count; i++) {
            buf_push_u64(p->target, *(uint64_t*)&poly_coeffs[i]);
        }
    } else if (trans_type == CND_TRANS_SPLINE) {
        buf_push(p->target, OP_TRANS_SPLINE);
        buf_push(p->target, spline_count);
        for (int i = 0; i < spline_count * 2; i++) {
            buf_push_u64(p->target, *(uint64_t*)&spline_points[i]);
        }
    }

    Token type_tok = p->current;
    consume(p, TOK_IDENTIFIER, "Expect field type");

    Token name_tok = p->current;
    if (name_tok.type == TOK_SELF) {
        parser_error(p, "Cannot use 'self' as field name");
        return;
    }
    consume(p, TOK_IDENTIFIER, "Expect field name");
    
    // Check for field name collision
    size_t old_field_count = p->current_struct_fields.count;
    strtab_add(&p->current_struct_fields, name_tok.start, name_tok.length);
    if (p->current_struct_fields.count == old_field_count) {
        parser_error(p, "Field name collision");
    }

    uint16_t key_id = strtab_add(&p->strtab, name_tok.start, name_tok.length);

    uint8_t bit_width = 0;
    if (p->current.type == TOK_COLON) {
        advance(p); consume(p, TOK_NUMBER, "Expect bit width");
        bit_width = (uint8_t)parse_number_u32(p->previous.start, p->previous.length);
    }

    int is_array_field = 0; 
    int is_variable_array = 0;
    uint8_t arr_prefix_op = OP_NOOP;
    uint8_t str_prefix_op = OP_NOOP;
    uint16_t max_len = 255;
    int has_until = 0;
    
    if (p->current.type == TOK_LBRACKET) {
        advance(p); is_array_field = 1;
        if (p->current.type == TOK_RBRACKET) { advance(p); is_variable_array = 1; } 
        else {
            Token num = p->current; consume(p, TOK_NUMBER, "Expect array size");
            array_fixed_count = parse_number_u32(num.start, num.length); has_fixed_array_count = 1;
            consume(p, TOK_RBRACKET, "Expect ]");
        }
    } else {
        // printf("PARSER: Field '%.*s' is NOT array\n", name_tok.length, name_tok.start);
    }
    
    if (p->verbose) {
        printf("  [Field] Name: '%.*s', Type: '%.*s'", name_tok.length, name_tok.start, type_tok.length, type_tok.start);
        if (bit_width > 0) printf(", Bits: %d", bit_width);
        if (is_array_field) {
            if (has_fixed_array_count) printf(", Array[%d]", array_fixed_count);
            else printf(", Array[]");
        }
        printf("\n");
    }
    
    if (match_keyword(p->current, "prefix")) {
        advance(p); Token ptype = p->current; consume(p, TOK_IDENTIFIER, "Expect prefix type");
        if (is_variable_array && !has_fixed_array_count && !has_count_ref) {
            if (match_keyword(ptype, "uint8") || match_keyword(ptype, "u8")) arr_prefix_op = OP_ARR_PRE_U8;
            else if (match_keyword(ptype, "uint16") || match_keyword(ptype, "u16")) arr_prefix_op = OP_ARR_PRE_U16;
            else if (match_keyword(ptype, "uint32") || match_keyword(ptype, "u32")) arr_prefix_op = OP_ARR_PRE_U32;
            else parser_error(p, "Invalid prefix type for variable array");
        } else if (match_keyword(type_tok, "string")) {
            if (match_keyword(ptype, "uint8") || match_keyword(ptype, "u8")) str_prefix_op = OP_STR_PRE_U8;
            else if (match_keyword(ptype, "uint16") || match_keyword(ptype, "u16")) str_prefix_op = OP_STR_PRE_U16;
            else if (match_keyword(ptype, "uint32") || match_keyword(ptype, "u32")) str_prefix_op = OP_STR_PRE_U32;
            else parser_error(p, "Invalid prefix type for string");
        } else { parser_error(p, "Prefix keyword used for non-variable-array/non-string type"); }
    } else if (match_keyword(type_tok, "string")) {
        if (match_keyword(p->current, "until")) { advance(p); consume(p, TOK_NUMBER, "Expect val"); has_until = 1; }
        if (match_keyword(p->current, "max")) { advance(p); Token max_tok = p->current; consume(p, TOK_NUMBER, "Expect max length"); max_len = (uint16_t)parse_number_u32(max_tok.start, max_tok.length); }
    }

    if (is_array_field && match_keyword(type_tok, "string")) {
        if (str_prefix_op == OP_NOOP && !has_until && !has_fixed_array_count && !has_count_ref) {
            parser_error(p, "String arrays must specify 'prefix' or 'until' or be fixed/dynamic count");
        }
    }

    if (is_big_endian_field) { buf_push(p->target, OP_SET_ENDIAN_BE); }

    if (arr_prefix_op != OP_NOOP) {
        buf_push(p->target, arr_prefix_op); buf_push_u16(p->target, key_id);
    } else if (has_eof && is_array_field) {
        buf_push(p->target, OP_ARR_EOF); buf_push_u16(p->target, key_id);
    } else if (has_count_ref && is_array_field) {
        buf_push(p->target, OP_ARR_DYNAMIC); buf_push_u16(p->target, key_id);
        buf_push_u16(p->target, count_ref_id);
    } else if (has_fixed_array_count && is_array_field) {
        buf_push(p->target, OP_ARR_FIXED); buf_push_u16(p->target, key_id);
        buf_push_u32(p->target, array_fixed_count);
    } 
    
    if (str_prefix_op != OP_NOOP) {
        buf_push(p->target, str_prefix_op); buf_push_u16(p->target, key_id);
    }

    if (has_const) {
        uint8_t type_op = OP_IO_U16;
        int is_signed = 0;
        int width = 0; // 1, 2, 4, 8
        int found = 0;

        #define CHECK_TYPE(t1, t2, t3, op, w, s) \
            if (!found && (match_keyword(type_tok, t1) || (t2 && match_keyword(type_tok, t2)) || (t3 && match_keyword(type_tok, t3)))) { \
                type_op = op; width = w; is_signed = s; found = 1; \
            }

        CHECK_TYPE("uint8", "byte", "u8", OP_IO_U8, 1, 0)
        CHECK_TYPE("int8", "i8", NULL, OP_IO_I8, 1, 1)
        CHECK_TYPE("uint16", "u16", NULL, OP_IO_U16, 2, 0)
        CHECK_TYPE("int16", "i16", NULL, OP_IO_I16, 2, 1)
        CHECK_TYPE("uint32", "u32", NULL, OP_IO_U32, 4, 0)
        CHECK_TYPE("int32", "i32", NULL, OP_IO_I32, 4, 1)
        CHECK_TYPE("uint64", "u64", NULL, OP_IO_U64, 8, 0)
        CHECK_TYPE("int64", "i64", NULL, OP_IO_I64, 8, 1)

        if (!found) {
             parser_error(p, "Const not supported for this type");
        }
        #undef CHECK_TYPE

        // Validation
        if (is_signed) {
            int64_t val_i64 = (int64_t)const_val;
            int64_t min = 0, max = 0;
            if (width == 1) { min = -128; max = 127; }
            else if (width == 2) { min = -32768; max = 32767; }
            else if (width == 4) { min = -2147483648LL; max = 2147483647LL; }
            else { min = INT64_MIN; max = INT64_MAX; }
            
            if (val_i64 < min || val_i64 > max) {
                parser_error(p, "Const value out of range for signed type");
            }
        } else {
            uint64_t val_u64 = const_val;
            uint64_t max = 0;
            if (width == 1) max = 255;
            else if (width == 2) max = 65535;
            else if (width == 4) max = 4294967295ULL;
            else max = UINT64_MAX;
            
            if (val_u64 > max) {
                parser_error(p, "Const value out of range for unsigned type");
            }
        }
        
        buf_push(p->target, OP_CONST_CHECK); 
        buf_push_u16(p->target, key_id);
        buf_push(p->target, type_op);
        
        if (type_op == OP_IO_U8 || type_op == OP_IO_I8) buf_push(p->target, (uint8_t)const_val);
        else if (type_op == OP_IO_U16 || type_op == OP_IO_I16) buf_push_u16(p->target, (uint16_t)const_val);
        else if (type_op == OP_IO_U32 || type_op == OP_IO_I32) buf_push_u32(p->target, (uint32_t)const_val);
        else if (type_op == OP_IO_U64 || type_op == OP_IO_I64) buf_push_u64(p->target, const_val);
        
        if (has_range) { emit_range_check(p, type_op, range_min_tok, range_max_tok); }
    } else {
        StructDef* sdef = reg_find(&p->registry, type_tok.start, type_tok.length);
        EnumDef* edef = enum_reg_find(&p->enums, type_tok.start, type_tok.length);
        
        if (sdef) {
            if (p->current_struct_name && 
                type_tok.length == p->current_struct_name_len && 
                strncmp(type_tok.start, p->current_struct_name, type_tok.length) == 0) {
                parser_error(p, "Recursive struct definition detected");
                return;
            }
            
            if (trans_type != CND_TRANS_NONE) { parser_error(p, "Cannot apply scale/transform to struct field"); }
            if (has_range) { parser_error(p, "Cannot apply range check to struct field"); }
            if (has_crc) { parser_error(p, "Cannot apply CRC to struct field"); }
            if (bit_width > 0) { parser_error(p, "Bitfields not supported for struct fields"); }

            buf_push(p->target, OP_ENTER_STRUCT); buf_push_u16(p->target, key_id);
            buf_append(p->target, sdef->bytecode.data, sdef->bytecode.size);
            buf_push(p->target, OP_EXIT_STRUCT);
        } else if (edef) {
            if (trans_type != CND_TRANS_NONE) { parser_error(p, "Cannot apply scale/transform to enum field"); }
            if (has_crc) { parser_error(p, "Cannot apply CRC to enum field"); }
            if (bit_width > 0) { parser_error(p, "Bitfields not supported for enum fields"); }
            
            buf_push(p->target, edef->underlying_type); buf_push_u16(p->target, key_id);
            
            // Emit Enum Validation
            buf_push(p->target, OP_ENUM_CHECK);
            buf_push(p->target, edef->underlying_type);
            buf_push_u16(p->target, (uint16_t)edef->count);
            for (size_t i = 0; i < edef->count; i++) {
                int64_t val = edef->values[i].value;
                if (edef->underlying_type == OP_IO_U8 || edef->underlying_type == OP_IO_I8) buf_push(p->target, (uint8_t)val);
                else if (edef->underlying_type == OP_IO_U16 || edef->underlying_type == OP_IO_I16) buf_push_u16(p->target, (uint16_t)val);
                else if (edef->underlying_type == OP_IO_U32 || edef->underlying_type == OP_IO_I32) buf_push_u32(p->target, (uint32_t)val);
                else if (edef->underlying_type == OP_IO_U64 || edef->underlying_type == OP_IO_I64) buf_push_u64(p->target, (uint64_t)val);
            }

            if (has_range) { emit_range_check(p, edef->underlying_type, range_min_tok, range_max_tok); }
        }
        else if (match_keyword(type_tok, "string")) {
            if (trans_type != CND_TRANS_NONE) { parser_error(p, "Cannot apply scale/offset/transform to string"); }
            if (has_range) { parser_error(p, "Cannot apply range check to string"); }
            if (has_crc) { parser_error(p, "Cannot apply CRC to string"); }
            if (bit_width > 0) { parser_error(p, "Bitfields only supported for integer types"); }

            if (str_prefix_op == OP_NOOP) { 
                if (is_array_field && !has_until) {
                    parser_error(p, "String array must specify 'prefix' or 'until'");
                }
                buf_push(p->target, OP_STR_NULL); buf_push_u16(p->target, key_id); buf_push_u16(p->target, max_len);
            }
        } else {
            uint8_t op = OP_NOOP;
            
            if (p->in_bit_mode && bit_width == 0) {
                 if (match_keyword(type_tok, "uint8") || match_keyword(type_tok, "byte") || match_keyword(type_tok, "u8") ||
                     match_keyword(type_tok, "int8") || match_keyword(type_tok, "i8")) bit_width = 8;
                 else if (match_keyword(type_tok, "uint16") || match_keyword(type_tok, "u16") ||
                          match_keyword(type_tok, "int16") || match_keyword(type_tok, "i16")) bit_width = 16;
                 else if (match_keyword(type_tok, "uint32") || match_keyword(type_tok, "u32") ||
                          match_keyword(type_tok, "int32") || match_keyword(type_tok, "i32")) bit_width = 32;
                 else if (match_keyword(type_tok, "uint64") || match_keyword(type_tok, "u64") ||
                          match_keyword(type_tok, "int64") || match_keyword(type_tok, "i64")) bit_width = 64;
                 else if (match_keyword(type_tok, "bool")) bit_width = 1;
            }

            if (bit_width > 0) {
                 if (match_keyword(type_tok, "uint8") || match_keyword(type_tok, "byte") || match_keyword(type_tok, "u8") ||
                     match_keyword(type_tok, "uint16") || match_keyword(type_tok, "u16") ||
                     match_keyword(type_tok, "uint32") || match_keyword(type_tok, "u32") ||
                     match_keyword(type_tok, "uint64") || match_keyword(type_tok, "u64")) {
                     op = OP_IO_BIT_U;
                 }
                 else if (match_keyword(type_tok, "int8") || match_keyword(type_tok, "i8") ||
                          match_keyword(type_tok, "int16") || match_keyword(type_tok, "i16") ||
                          match_keyword(type_tok, "int32") || match_keyword(type_tok, "i32") ||
                          match_keyword(type_tok, "int64") || match_keyword(type_tok, "i64")) {
                     op = OP_IO_BIT_I;
                 }
                 else if (match_keyword(type_tok, "bool")) {
                     op = OP_IO_BIT_BOOL;
                 }
                 else { parser_error(p, "Bitfields only supported for integer/bool types"); return; }
                 
                 buf_push(p->target, op); buf_push_u16(p->target, key_id); buf_push(p->target, bit_width);
                 
                 if (p->in_bit_mode && p->is_bit_count_valid) {
                     p->current_bit_count += bit_width;
                 }
            } else {
                if (p->in_bit_mode) {
                    parser_error(p, "Only integer and bool types allowed in unaligned_bytes struct");
                    return;
                }

                if (match_keyword(type_tok, "uint8") || match_keyword(type_tok, "byte") || match_keyword(type_tok, "u8")) op = OP_IO_U8;
                else if (match_keyword(type_tok, "uint16") || match_keyword(type_tok, "u16")) op = OP_IO_U16;
                else if (match_keyword(type_tok, "uint32") || match_keyword(type_tok, "u32")) op = OP_IO_U32;
                else if (match_keyword(type_tok, "uint64") || match_keyword(type_tok, "u64")) op = OP_IO_U64;
                else if (match_keyword(type_tok, "int8") || match_keyword(type_tok, "i8")) op = OP_IO_I8;
                else if (match_keyword(type_tok, "int16") || match_keyword(type_tok, "i16")) op = OP_IO_I16;
                else if (match_keyword(type_tok, "int32") || match_keyword(type_tok, "i32")) op = OP_IO_I32;
                else if (match_keyword(type_tok, "int64") || match_keyword(type_tok, "i64")) op = OP_IO_I64;
                else if (match_keyword(type_tok, "float") || match_keyword(type_tok, "f32")) op = OP_IO_F32;
                else if (match_keyword(type_tok, "double") || match_keyword(type_tok, "f64")) op = OP_IO_F64;
                else if (match_keyword(type_tok, "bool")) op = OP_IO_BOOL;
                else { parser_error(p, "Unknown type"); return; }
                
                if (has_crc) {
                     if (crc_width == 16) {
                         if (op != OP_IO_U16) parser_error(p, "CRC16 requires uint16 type");
                         buf_push(p->target, OP_CRC_16);
                         buf_push_u16(p->target, (uint16_t)crc_poly);
                         buf_push_u16(p->target, (uint16_t)crc_init);
                         buf_push_u16(p->target, (uint16_t)crc_xor);
                         buf_push(p->target, crc_flags);
                     } else if (crc_width == 32) {
                         if (op != OP_IO_U32) parser_error(p, "CRC32 requires uint32 type");
                         buf_push(p->target, OP_CRC_32);
                         buf_push_u32(p->target, crc_poly);
                         buf_push_u32(p->target, crc_init);
                         buf_push_u32(p->target, crc_xor);
                         buf_push(p->target, crc_flags);
                     }
                } else if (has_expr) {
                    buf_push(p->target, OP_DUP);
                    buf_push(p->target, OP_STORE_CTX); buf_push_u16(p->target, key_id);
                    buf_push(p->target, OP_EMIT); buf_push(p->target, op);
                } else {
                    buf_push(p->target, op); buf_push_u16(p->target, key_id);
                    if (has_range) { emit_range_check(p, op, range_min_tok, range_max_tok); }
                }
            }
        }
    }
    
    if (is_array_field) { buf_push(p->target, OP_ARR_END); }

    consume(p, TOK_SEMICOLON, "Expect ; after field");

    if (is_big_endian_field) { buf_push(p->target, OP_SET_ENDIAN_LE); }
}

void parse_block(Parser* p) {
    consume(p, TOK_LBRACE, "Expect {");
    StringBuilder doc_sb;
    sb_init(&doc_sb);
    
    while (p->current.type != TOK_RBRACE && p->current.type != TOK_EOF && !p->had_error) {
        if (p->current.type == TOK_DOC_COMMENT) {
            if (doc_sb.length > 0) sb_append_c(&doc_sb, '\n');
            sb_append_n(&doc_sb, p->current.start, p->current.length);
            advance(p);
            continue;
        }
        
        char* doc_str = doc_sb.length > 0 ? sb_build(&doc_sb) : NULL;
        parse_field(p, doc_str);
        if (doc_str) free(doc_str);
        sb_reset(&doc_sb);
    }
    consume(p, TOK_RBRACE, "Expect }");
    sb_free(&doc_sb);
}

void parse_enum(Parser* p, const char* doc) {
    Token name = p->current; consume(p, TOK_IDENTIFIER, "Expect enum name");
    
    // Check for name collisions
    if (enum_reg_find(&p->enums, name.start, name.length)) {
        parser_error(p, "Enum name already defined");
    }
    if (reg_find(&p->registry, name.start, name.length)) {
        parser_error(p, "Name collision with Struct");
    }

    if (p->verbose) printf("[VERBOSE] Parsing enum '%.*s'\n", name.length, name.start);
    EnumDef* def = enum_reg_add(&p->enums, name.start, name.length, name.line, p->current_path, doc);
    
    // Init value name tracking
    StringTable value_names;
    strtab_init(&value_names);

    // Optional underlying type: enum MyEnum : uint8 { ... }
    if (p->current.type == TOK_COLON) {
        advance(p);
        Token type_tok = p->current;
        consume(p, TOK_IDENTIFIER, "Expect underlying type");
        
        if (match_keyword(type_tok, "uint8") || match_keyword(type_tok, "u8")) def->underlying_type = OP_IO_U8;
        else if (match_keyword(type_tok, "int8") || match_keyword(type_tok, "i8")) def->underlying_type = OP_IO_I8;
        else if (match_keyword(type_tok, "uint16") || match_keyword(type_tok, "u16")) def->underlying_type = OP_IO_U16;
        else if (match_keyword(type_tok, "int16") || match_keyword(type_tok, "i16")) def->underlying_type = OP_IO_I16;
        else if (match_keyword(type_tok, "uint32") || match_keyword(type_tok, "u32")) def->underlying_type = OP_IO_U32;
        else if (match_keyword(type_tok, "int32") || match_keyword(type_tok, "i32")) def->underlying_type = OP_IO_I32;
        else if (match_keyword(type_tok, "uint64") || match_keyword(type_tok, "u64")) def->underlying_type = OP_IO_U64;
        else if (match_keyword(type_tok, "int64") || match_keyword(type_tok, "i64")) def->underlying_type = OP_IO_I64;
        else parser_error(p, "Invalid underlying type for enum");
    }

    consume(p, TOK_LBRACE, "Expect {");
    
    int64_t next_val = 0;
    StringBuilder val_doc_sb;
    sb_init(&val_doc_sb);
    
    while (p->current.type != TOK_RBRACE && p->current.type != TOK_EOF && !p->had_error) {
        if (p->current.type == TOK_DOC_COMMENT) {
            if (val_doc_sb.length > 0) sb_append_c(&val_doc_sb, '\n');
            sb_append_n(&val_doc_sb, p->current.start, p->current.length);
            advance(p);
            continue;
        }

        Token val_name = p->current;
        consume(p, TOK_IDENTIFIER, "Expect enum value name");
        
        // Check for duplicate name
        size_t old_count = value_names.count;
        strtab_add(&value_names, val_name.start, val_name.length);
        if (value_names.count == old_count) {
            parser_error(p, "Duplicate enum value name");
        }

        int64_t val = next_val;
        if (p->current.type == TOK_EQUALS) {
            advance(p);
            Token num = p->current;
            consume(p, TOK_NUMBER, "Expect enum value");
            val = parse_number_i64(num.start, num.length);
        }
        
        // Check for duplicate value
        for (size_t i = 0; i < def->count; i++) {
            if (def->values[i].value == val) {
                parser_error(p, "Duplicate enum value");
            }
        }

        // Store value
        if (def->count >= def->capacity) {
            def->capacity = (def->capacity == 0) ? 8 : def->capacity * 2;
            def->values = realloc(def->values, def->capacity * sizeof(EnumValue));
        }
        def->values[def->count].name = malloc(val_name.length + 1);
        memcpy(def->values[def->count].name, val_name.start, val_name.length);
        def->values[def->count].name[val_name.length] = '\0';
        def->values[def->count].value = val;
        def->values[def->count].doc_comment = val_doc_sb.length > 0 ? sb_build(&val_doc_sb) : NULL; // Transfer ownership
        sb_reset(&val_doc_sb); // Reset for next
        def->count++;
        
        next_val = val + 1;
        
        if (p->current.type == TOK_COMMA) advance(p);
    }
    sb_free(&val_doc_sb);
    consume(p, TOK_RBRACE, "Expect }");
    
    strtab_free(&value_names);
}

void parse_struct(Parser* p, const char* doc) {
    Token name = p->current; consume(p, TOK_IDENTIFIER, "Expect struct name");
    
    // Check for name collisions
    if (reg_find(&p->registry, name.start, name.length)) {
        parser_error(p, "Struct name already defined");
    }
    if (enum_reg_find(&p->enums, name.start, name.length)) {
        parser_error(p, "Name collision with Enum");
    }

    if (p->verbose) printf("[VERBOSE] Parsing struct '%.*s'\n", name.length, name.start);
    StructDef* def = reg_add(&p->registry, name.start, name.length, name.line, p->current_path, doc);
    
    // Init field tracking for collision detection
    strtab_init(&p->current_struct_fields);

    const char* prev_name = p->current_struct_name;
    int prev_len = p->current_struct_name_len;
    p->current_struct_name = name.start;
    p->current_struct_name_len = name.length;

    Buffer* prev = p->target; p->target = &def->bytecode;

    int was_in_bit_mode = p->in_bit_mode;
    int prev_bit_count = p->current_bit_count;
    int prev_bit_valid = p->is_bit_count_valid;

    if (p->pending_unaligned) {
        buf_push(p->target, OP_ENTER_BIT_MODE);
        buf_push(p->target, OP_SET_ENDIAN_BE);
        p->in_bit_mode = 1;
        p->pending_unaligned = 0;
        p->current_bit_count = 0;
        p->is_bit_count_valid = 1;
    }

    parse_block(p);

    if (p->in_bit_mode && !was_in_bit_mode) {
        if (p->is_bit_count_valid && (p->current_bit_count % 8 != 0)) {
            char msg[256];
            // Use a simpler name for error message
            char struct_name[64];
            int len = p->current_struct_name_len < 63 ? p->current_struct_name_len : 63;
            memcpy(struct_name, p->current_struct_name, len);
            struct_name[len] = '\0';
            
            snprintf(msg, sizeof(msg), "Unaligned struct '%s' must end on byte boundary (current bits: %d). Use @fill.", 
                     struct_name, p->current_bit_count);
            parser_error(p, msg);
        }
        buf_push(p->target, OP_EXIT_BIT_MODE);
        p->in_bit_mode = 0;
    }
    p->in_bit_mode = was_in_bit_mode;
    p->current_bit_count = prev_bit_count;
    p->is_bit_count_valid = prev_bit_valid;

    p->target = prev;

    p->current_struct_name = prev_name;
    p->current_struct_name_len = prev_len;
    
    // Free field tracking
    strtab_free(&p->current_struct_fields);
}

void parse_packet(Parser* p, const char* doc) {
    (void)doc;
    // TODO: Store doc comment for packet?
    Token name = p->current; consume(p, TOK_IDENTIFIER, "Expect packet name");
    
    // Check for name collisions
    if (reg_find(&p->registry, name.start, name.length)) {
        parser_error(p, "Packet name collides with Struct");
    }
    if (enum_reg_find(&p->enums, name.start, name.length)) {
        parser_error(p, "Packet name collides with Enum");
    }

    if (p->verbose) printf("[VERBOSE] Parsing packet '%.*s'\n", name.length, name.start);
    
    // Emit META_NAME with placeholder key
    size_t meta_loc = buf_current_offset(p->target);
    buf_push(p->target, OP_META_NAME);
    buf_push_u16(p->target, 0xFFFF); // Placeholder
    
    if (p->current.type == TOK_EQUALS) {
        advance(p); // Consume '='
        Token struct_name = p->current;
        consume(p, TOK_IDENTIFIER, "Expect struct name");
        consume(p, TOK_SEMICOLON, "Expect ;");
        
        StructDef* sdef = reg_find(&p->registry, struct_name.start, struct_name.length);
        if (!sdef) {
            parser_error(p, "Struct not found");
        } else {
            // Inline the struct's bytecode
            buf_append(p->target, sdef->bytecode.data, sdef->bytecode.size);
        }
    } else {
        strtab_init(&p->current_struct_fields);
        parse_block(p);
        strtab_free(&p->current_struct_fields);
    }
    
    // Add name to string table AFTER fields, so fields get lower IDs (preserving compatibility/tests)
    uint16_t key_id = strtab_add(&p->strtab, name.start, name.length);
    
    // Patch the key
    buf_write_u16_at(p->target, meta_loc + 1, key_id);
}

// Helper to resolve path relative to base
char* resolve_path(const char* base, const char* rel) {
    // Find last slash in base
    const char* last_slash = strrchr(base, '/');
    const char* last_backslash = strrchr(base, '\\');
    const char* slash = (last_slash > last_backslash) ? last_slash : last_backslash;
    
    size_t base_len = 0;
    if (slash) {
        base_len = slash - base + 1;
    }
    
    size_t rel_len = strlen(rel);
    char* path = (char*)malloc(base_len + rel_len + 1);
    if (base_len > 0) memcpy(path, base, base_len);
    memcpy(path + base_len, rel, rel_len);
    path[base_len + rel_len] = '\0';
    return path;
}

void parse_import(Parser* p) {
    consume(p, TOK_LPAREN, "Expect ( after @import");
    Token path_tok = p->current;
    consume(p, TOK_STRING, "Expect file path string");
    consume(p, TOK_RPAREN, "Expect ) after import path");
    
    // Extract string content (Lexer already removed quotes)
    char rel_path[256];
    int len = path_tok.length;
    if (len >= 256) { parser_error(p, "Import path too long"); return; }
    memcpy(rel_path, path_tok.start, len);
    rel_path[len] = '\0';
    
    char* full_path = resolve_path(p->current_path, rel_path);
    
    // Check if already imported
    for (size_t i = 0; i < p->imports.count; i++) {
        if (strcmp(p->imports.strings[i], full_path) == 0) {
            free(full_path);
            return; // Already imported
        }
    }
    
    // Add to imports
    strtab_add(&p->imports, full_path, (int)strlen(full_path));
    
    // Read file
    FILE* f = fopen(full_path, "rb");
    if (!f) {
        char msg[512];
        snprintf(msg, sizeof(msg), "Could not open imported file: %s", full_path);
        parser_error(p, msg);
        free(full_path);
        return;
    }
    
    fseek(f, 0, SEEK_END); long size = ftell(f); fseek(f, 0, SEEK_SET);
    char* source = (char*)malloc(size + 1); fread(source, 1, size, f); source[size] = '\0';
    fclose(f);
    
    // Save state
    Lexer old_lexer = p->lexer;
    Token old_cur = p->current;
    Token old_prev = p->previous;
    const char* old_path = p->current_path;
    
    // Switch state
    lexer_init(&p->lexer, source);
    p->current_path = full_path;
    
    advance(p);
    p->import_depth++;
    parse_top_level(p);
    p->import_depth--;
    
    // Restore state
    p->lexer = old_lexer;
    p->current = old_cur;
    p->previous = old_prev;
    p->current_path = old_path;
    
    free(source);
    free(full_path);
}

void parse_top_level(Parser* p) {
    StringBuilder doc_sb;
    sb_init(&doc_sb);

    while (p->current.type != TOK_EOF && !p->had_error) {
        if (p->current.type == TOK_DOC_COMMENT) {
            if (doc_sb.length > 0) sb_append_c(&doc_sb, '\n');
            sb_append_n(&doc_sb, p->current.start, p->current.length);
            advance(p);
            continue;
        }

        // Handle Decorators (Stackable)
        while (p->current.type == TOK_AT) {
            advance(p); 
            Token dec = p->current; consume(p, TOK_IDENTIFIER, "Expect decorator name");
            
            if (match_keyword(dec, "version")) {
                consume(p, TOK_LPAREN, "Expect ("); Token ver = p->current; consume(p, TOK_NUMBER, "Expect version number");
                uint32_t v = parse_number_u32(ver.start, ver.length); buf_push(p->target, OP_META_VERSION); buf_push(p->target, (uint8_t)v);
                consume(p, TOK_RPAREN, "Expect )");
            } else if (match_keyword(dec, "import")) {
                parse_import(p);
            } else if (match_keyword(dec, "big_endian")) {
                buf_push(p->target, OP_SET_ENDIAN_BE);
            } else if (match_keyword(dec, "little_endian") || match_keyword(dec, "le")) {
                buf_push(p->target, OP_SET_ENDIAN_LE);
            } else if (match_keyword(dec, "unaligned_bytes")) {
                p->pending_unaligned = 1;
            } else {
                if (p->current.type == TOK_LPAREN) {
                    consume(p, TOK_LPAREN, "Expect (");
                    while (p->current.type != TOK_RPAREN && p->current.type != TOK_EOF) advance(p);
                    consume(p, TOK_RPAREN, "Expect )");
                }
            }
        }

        char* doc_str = doc_sb.length > 0 ? sb_build(&doc_sb) : NULL;

        if (p->current.type == TOK_STRUCT) {
            advance(p); parse_struct(p, doc_str);
            sb_reset(&doc_sb);
        } else if (p->current.type == TOK_ENUM) {
            advance(p); parse_enum(p, doc_str);
            sb_reset(&doc_sb);
        } else if (p->current.type == TOK_PACKET) {
            if (p->packet_count > 0) {
                parser_error(p, "Only one packet definition allowed per file");
            }
            p->packet_count++;
            advance(p); parse_packet(p, doc_str);
            sb_reset(&doc_sb); // Packet doesn't store doc yet but consume it
        } else if (p->current.type == TOK_SEMICOLON) {
            advance(p); // Ignore top-level semicolons
            sb_reset(&doc_sb); // Orphaned comment
        } else if (p->current.type != TOK_EOF) {
            parser_error(p, "Unexpected token");
            sb_reset(&doc_sb);
        }
        
        if (doc_str) free(doc_str);
    }
    sb_free(&doc_sb);

    if (p->packet_count == 0 && p->import_depth == 0) {
        parser_error(p, "No packet definition found. A .cnd file must contain exactly one 'packet Name { ... }' block.");
    }
}

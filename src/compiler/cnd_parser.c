#include "cnd_internal.h"

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

uint32_t parse_number(Token t) {
    const char* s = t.start;
    int len = t.length;
    uint32_t res = 0;
    
    // Handle hex
    if (len > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        for (int i = 2; i < len; i++) {
            char c = s[i];
            res <<= 4;
            if (c >= '0' && c <= '9') res |= (c - '0');
            else if (c >= 'a' && c <= 'f') res |= (c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') res |= (c - 'A' + 10);
        }
    } else {
        for (int i = 0; i < len; i++) {
            if (s[i] >= '0' && s[i] <= '9') {
                res = res * 10 + (s[i] - '0');
            }
        }
    }
    return res;
}

int64_t parse_int64(Token t) {
    const char* s = t.start;
    const char* end = s + t.length;
    if (s == end) return 0;
    
    int neg = 0;
    if (*s == '-') {
        neg = 1;
        s++;
    } else if (*s == '+') {
        s++;
    }
    
    uint64_t res = 0;
    if (end - s > 2 && *s == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
        while (s < end) {
            uint8_t c = (uint8_t)*s++;
            // Branchless hex conversion: (c & 0xF) + 9 * (c >> 6)
            // Works for '0'-'9', 'a'-'f', 'A'-'F'
            res = (res << 4) | ((c & 0xF) + 9 * (c >> 6));
        }
    } else {
        while (s < end) {
            char c = *s++;
            if (c >= '0' && c <= '9') {
                res = res * 10 + (c - '0');
            }
        }
    }
    
    return neg ? -(int64_t)res : (int64_t)res;
}

double parse_double(Token t) {
    char buf[64];
    if (t.length >= 63) return 0.0;
    memcpy(buf, t.start, t.length);
    buf[t.length] = '\0';
    return strtod(buf, NULL);
}

void emit_range_check(Parser* p, uint8_t type_op, Token min_tok, Token max_tok) {
    // Validate range
    if (type_op == OP_IO_U8 || type_op == OP_IO_U16 || type_op == OP_IO_U32) {
        uint32_t min = parse_number(min_tok);
        uint32_t max = parse_number(max_tok);
        if (min > max) { parser_error(p, "Range min > max"); return; }
    } else if (type_op == OP_IO_I8 || type_op == OP_IO_I16 || type_op == OP_IO_I32 || type_op == OP_IO_I64) {
        int64_t min = parse_int64(min_tok);
        int64_t max = parse_int64(max_tok);
        if (min > max) { parser_error(p, "Range min > max"); return; }
    } else if (type_op == OP_IO_U64) {
        uint64_t min = (uint64_t)parse_int64(min_tok);
        uint64_t max = (uint64_t)parse_int64(max_tok);
        if (min > max) { parser_error(p, "Range min > max"); return; }
    } else if (type_op == OP_IO_F32 || type_op == OP_IO_F64) {
        double min = parse_double(min_tok);
        double max = parse_double(max_tok);
        if (min > max) { parser_error(p, "Range min > max"); return; }
    }

    buf_push(p->target, OP_RANGE_CHECK);
    buf_push(p->target, type_op);
    
    if (type_op == OP_IO_U8) {
        buf_push(p->target, (uint8_t)parse_number(min_tok));
        buf_push(p->target, (uint8_t)parse_number(max_tok));
    } else if (type_op == OP_IO_U16) {
        buf_push_u16(p->target, (uint16_t)parse_number(min_tok));
        buf_push_u16(p->target, (uint16_t)parse_number(max_tok));
    } else if (type_op == OP_IO_U32) {
        buf_push_u32(p->target, parse_number(min_tok));
        buf_push_u32(p->target, parse_number(max_tok));
    } else if (type_op == OP_IO_I8) {
        buf_push(p->target, (uint8_t)(int8_t)parse_int64(min_tok));
        buf_push(p->target, (uint8_t)(int8_t)parse_int64(max_tok));
    } else if (type_op == OP_IO_I16) {
        buf_push_u16(p->target, (uint16_t)(int16_t)parse_int64(min_tok));
        buf_push_u16(p->target, (uint16_t)(int16_t)parse_int64(max_tok));
    } else if (type_op == OP_IO_I32) {
        buf_push_u32(p->target, (uint32_t)(int32_t)parse_int64(min_tok));
        buf_push_u32(p->target, (uint32_t)(int32_t)parse_int64(max_tok));
    } else if (type_op == OP_IO_F32) {
        float fmin = (float)parse_double(min_tok);
        float fmax = (float)parse_double(max_tok);
        uint32_t imin, imax;
        memcpy(&imin, &fmin, 4);
        memcpy(&imax, &fmax, 4);
        buf_push_u32(p->target, imin);
        buf_push_u32(p->target, imax);
    } else if (type_op == OP_IO_U64) {
        buf_push_u64(p->target, (uint64_t)parse_int64(min_tok));
        buf_push_u64(p->target, (uint64_t)parse_int64(max_tok));
    } else if (type_op == OP_IO_I64) {
        buf_push_u64(p->target, (uint64_t)parse_int64(min_tok));
        buf_push_u64(p->target, (uint64_t)parse_int64(max_tok));
    } else if (type_op == OP_IO_F64) {
        double dmin = parse_double(min_tok);
        double dmax = parse_double(max_tok);
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
void parse_expression(Parser* p);

void parse_primary(Parser* p) {
    if (p->current.type == TOK_NUMBER) {
        uint64_t val = (uint64_t)parse_int64(p->current);
        advance(p);
        buf_push(p->target, OP_PUSH_IMM);
        buf_push_u64(p->target, val);
    } else if (p->current.type == TOK_TRUE) {
        advance(p);
        buf_push(p->target, OP_PUSH_IMM);
        buf_push_u64(p->target, 1);
    } else if (p->current.type == TOK_FALSE) {
        advance(p);
        buf_push(p->target, OP_PUSH_IMM);
        buf_push_u64(p->target, 0);
    } else if (p->current.type == TOK_IDENTIFIER) {
        Token name = p->current;
        advance(p);
        uint16_t key_id = strtab_add(&p->strtab, name.start, name.length);
        buf_push(p->target, OP_LOAD_CTX);
        buf_push_u16(p->target, key_id);
    } else if (p->current.type == TOK_LPAREN) {
        advance(p);
        parse_expression(p);
        consume(p, TOK_RPAREN, "Expect ) after expression");
    } else {
        parser_error(p, "Expect expression");
    }
}

void parse_unary(Parser* p) {
    if (p->current.type == TOK_BANG) {
        advance(p);
        parse_unary(p);
        buf_push(p->target, OP_LOG_NOT);
    } else if (p->current.type == TOK_TILDE) {
        advance(p);
        parse_unary(p);
        buf_push(p->target, OP_BIT_NOT);
    } else {
        parse_primary(p);
    }
}

void parse_shift(Parser* p) {
    parse_unary(p);
    while (p->current.type == TOK_LSHIFT || p->current.type == TOK_RSHIFT) {
        TokenType op = p->current.type;
        advance(p);
        parse_unary(p);
        if (op == TOK_LSHIFT) buf_push(p->target, OP_SHL);
        else buf_push(p->target, OP_SHR);
    }
}

void parse_comparison(Parser* p) {
    parse_shift(p);
    while (p->current.type == TOK_GT || p->current.type == TOK_GT_EQ ||
           p->current.type == TOK_LT || p->current.type == TOK_LT_EQ) {
        TokenType op = p->current.type;
        advance(p);
        parse_shift(p);
        if (op == TOK_GT) buf_push(p->target, OP_GT);
        else if (op == TOK_GT_EQ) buf_push(p->target, OP_GTE);
        else if (op == TOK_LT) buf_push(p->target, OP_LT);
        else if (op == TOK_LT_EQ) buf_push(p->target, OP_LTE);
    }
}

void parse_equality(Parser* p) {
    parse_comparison(p);
    while (p->current.type == TOK_EQ_EQ || p->current.type == TOK_BANG_EQ) {
        TokenType op = p->current.type;
        advance(p);
        parse_comparison(p);
        if (op == TOK_EQ_EQ) buf_push(p->target, OP_EQ);
        else buf_push(p->target, OP_NEQ);
    }
}

void parse_bit_and(Parser* p) {
    parse_equality(p);
    while (p->current.type == TOK_AMP) {
        advance(p);
        parse_equality(p);
        buf_push(p->target, OP_BIT_AND);
    }
}

void parse_bit_xor(Parser* p) {
    parse_bit_and(p);
    while (p->current.type == TOK_CARET) {
        advance(p);
        parse_bit_and(p);
        buf_push(p->target, OP_BIT_XOR);
    }
}

void parse_bit_or(Parser* p) {
    parse_bit_xor(p);
    while (p->current.type == TOK_PIPE) {
        advance(p);
        parse_bit_xor(p);
        buf_push(p->target, OP_BIT_OR);
    }
}

void parse_logic_and(Parser* p) {
    parse_bit_or(p);
    while (p->current.type == TOK_AMP_AMP) {
        advance(p);
        parse_bit_or(p);
        buf_push(p->target, OP_LOG_AND);
    }
}

void parse_logic_or(Parser* p) {
    parse_logic_and(p);
    while (p->current.type == TOK_PIPE_PIPE) {
        advance(p);
        parse_logic_and(p);
        buf_push(p->target, OP_LOG_OR);
    }
}

void parse_expression(Parser* p) {
    parse_logic_or(p);
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
                val = (uint64_t)parse_int64(p->current);
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
    
    // Emit Table
    size_t table_start = buf_current_offset(p->target);
    buf_push_u16(p->target, (uint16_t)case_count);
    
    // Calculate default offset
    // Default offset is relative to code_start_loc.
    // If no default case provided, we want to skip the whole switch block?
    // i.e. jump to table_end?
    // But we don't know table_end yet.
    // If default_offset is -1, we will patch it later or calculate it if possible.
    // Actually, we can calculate table size!
    // Table Size = 2 (count) + 4 (def) + count * (8+4).
    size_t table_size = 2 + 4 + case_count * 12;
    size_t table_end = table_start + table_size;
    
    if (default_offset == -1) {
        default_offset = (int32_t)(table_end - code_start_loc);
    }
    buf_push_u32(p->target, (uint32_t)default_offset);
    
    for(size_t i=0; i<case_count; i++) {
        buf_push_u64(p->target, cases[i].val);
        buf_push_u32(p->target, (uint32_t)cases[i].offset);
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
                fill_val = (uint8_t)parse_number(num);
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
        } else { 
            consume(p, TOK_LPAREN, "Expect ("); 
            if (match_keyword(dec_name_token, "count") || match_keyword(dec_name_token, "len")) {
                Token num = p->current; consume(p, TOK_NUMBER, "Expect count number");
                array_fixed_count = parse_number(num); has_fixed_array_count = 1;
            } else if (match_keyword(dec_name_token, "const") || match_keyword(dec_name_token, "match")) {
                Token num = p->current; consume(p, TOK_NUMBER, "Expect const/match value");
                const_val = (uint64_t)parse_int64(num); has_const = 1;
            } else if (match_keyword(dec_name_token, "pad")) {
                is_standalone_op = 1;
                Token num = p->current; consume(p, TOK_NUMBER, "Expect pad bits");
                uint32_t pad_bits = parse_number(num); buf_push(p->target, OP_ALIGN_PAD); buf_push(p->target, (uint8_t)pad_bits);
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
                crc_width = parse_number(num); has_crc = 1;
                if (crc_width == 16) { crc_poly = 0x1021; crc_init = 0xFFFF; crc_xor = 0; crc_flags = 0; } 
                else if (crc_width == 32) { crc_poly = 0x04C11DB7; crc_init = 0xFFFFFFFF; crc_xor = 0xFFFFFFFF; crc_flags = 3; }
            } else if (match_keyword(dec_name_token, "crc_poly")) {
                Token num = p->current; consume(p, TOK_NUMBER, "Expect poly"); crc_poly = parse_number(num);
            } else if (match_keyword(dec_name_token, "crc_init")) {
                Token num = p->current; consume(p, TOK_NUMBER, "Expect init"); crc_init = parse_number(num);
            } else if (match_keyword(dec_name_token, "crc_xor")) {
                Token num = p->current; consume(p, TOK_NUMBER, "Expect xor"); crc_xor = parse_number(num);
            } else if (match_keyword(dec_name_token, "scale")) {
                Token num = p->current; if (num.type == TOK_NUMBER) advance(p); else consume(p, TOK_NUMBER, "Expect scale factor");
                trans_type = CND_TRANS_SCALE_F64; trans_scale = parse_double(num);
            } else if (match_keyword(dec_name_token, "offset")) {
                Token num = p->current; if (num.type == TOK_NUMBER) advance(p); else consume(p, TOK_NUMBER, "Expect offset value");
                if (trans_type != CND_TRANS_SCALE_F64) { trans_type = CND_TRANS_SCALE_F64; trans_scale = 1.0; } trans_offset = parse_double(num);
            } else if (match_keyword(dec_name_token, "mul")) {
                Token num = p->current; if (num.type == TOK_NUMBER) advance(p); else consume(p, TOK_NUMBER, "Expect mul value");
                trans_type = CND_TRANS_MUL_I64; trans_int_val = parse_int64(num);
            } else if (match_keyword(dec_name_token, "div")) {
                Token num = p->current; if (num.type == TOK_NUMBER) advance(p); else consume(p, TOK_NUMBER, "Expect div value");
                trans_type = CND_TRANS_DIV_I64; trans_int_val = parse_int64(num);
            } else if (match_keyword(dec_name_token, "add")) {
                Token num = p->current; if (num.type == TOK_NUMBER) advance(p); else consume(p, TOK_NUMBER, "Expect add value");
                trans_type = CND_TRANS_ADD_I64; trans_int_val = parse_int64(num);
            } else if (match_keyword(dec_name_token, "sub")) {
                Token num = p->current; if (num.type == TOK_NUMBER) advance(p); else consume(p, TOK_NUMBER, "Expect sub value");
                trans_type = CND_TRANS_SUB_I64; trans_int_val = parse_int64(num);
            } else if (match_keyword(dec_name_token, "poly")) {
                trans_type = CND_TRANS_POLY;
                while (true) {
                    Token num = p->current;
                    if (num.type == TOK_NUMBER) {
                        advance(p);
                        if (poly_count < 16) {
                            poly_coeffs[poly_count++] = parse_double(num);
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
                            spline_points[spline_count * 2] = parse_double(num);
                            consume(p, TOK_COMMA, "Expect comma between x and y");
                            Token y_tok = p->current;
                            consume(p, TOK_NUMBER, "Expect y value");
                            spline_points[spline_count * 2 + 1] = parse_double(y_tok);
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
    consume(p, TOK_IDENTIFIER, "Expect field name");
    uint16_t key_id = strtab_add(&p->strtab, name_tok.start, name_tok.length);

    uint8_t bit_width = 0;
    if (p->current.type == TOK_COLON) {
        advance(p); consume(p, TOK_NUMBER, "Expect bit width");
        bit_width = (uint8_t)parse_number(p->previous);
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
            array_fixed_count = parse_number(num); has_fixed_array_count = 1;
            consume(p, TOK_RBRACKET, "Expect ]");
        }
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
        if (is_variable_array && !has_fixed_array_count) {
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
        if (match_keyword(p->current, "max")) { advance(p); Token max_tok = p->current; consume(p, TOK_NUMBER, "Expect max length"); max_len = (uint16_t)parse_number(max_tok); }
    }

    if (is_array_field && match_keyword(type_tok, "string")) {
        if (str_prefix_op == OP_NOOP && !has_until) {
            parser_error(p, "String arrays must specify 'prefix' or 'until'");
        }
    }

    if (is_big_endian_field) { buf_push(p->target, OP_SET_ENDIAN_BE); }

    if (arr_prefix_op != OP_NOOP) {
        buf_push(p->target, arr_prefix_op); buf_push_u16(p->target, key_id);
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
    char* pending_doc = NULL;
    while (p->current.type != TOK_RBRACE && p->current.type != TOK_EOF && !p->had_error) {
        if (p->current.type == TOK_DOC_COMMENT) {
            pending_doc = append_doc(pending_doc, p->current);
            advance(p);
            continue;
        }
        parse_field(p, pending_doc);
        if (pending_doc) { free(pending_doc); pending_doc = NULL; }
    }
    consume(p, TOK_RBRACE, "Expect }");
    if (pending_doc) free(pending_doc);
}

void parse_enum(Parser* p, const char* doc) {
    Token name = p->current; consume(p, TOK_IDENTIFIER, "Expect enum name");
    if (p->verbose) printf("[VERBOSE] Parsing enum '%.*s'\n", name.length, name.start);
    EnumDef* def = enum_reg_add(&p->enums, name.start, name.length, name.line, p->current_path, doc);
    
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
    char* val_doc = NULL;
    
    while (p->current.type != TOK_RBRACE && p->current.type != TOK_EOF && !p->had_error) {
        if (p->current.type == TOK_DOC_COMMENT) {
            val_doc = append_doc(val_doc, p->current);
            advance(p);
            continue;
        }

        Token val_name = p->current;
        consume(p, TOK_IDENTIFIER, "Expect enum value name");
        
        int64_t val = next_val;
        if (p->current.type == TOK_EQUALS) {
            advance(p);
            Token num = p->current;
            consume(p, TOK_NUMBER, "Expect enum value");
            val = parse_int64(num);
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
        def->values[def->count].doc_comment = val_doc; // Transfer ownership
        val_doc = NULL; // Reset for next
        def->count++;
        
        next_val = val + 1;
        
        if (p->current.type == TOK_COMMA) advance(p);
        if (val_doc) { free(val_doc); val_doc = NULL; } // Should be NULL already, but safety
    }
    consume(p, TOK_RBRACE, "Expect }");
    if(val_doc) free(val_doc);
}

void parse_struct(Parser* p, const char* doc) {
    Token name = p->current; consume(p, TOK_IDENTIFIER, "Expect struct name");
    if (p->verbose) printf("[VERBOSE] Parsing struct '%.*s'\n", name.length, name.start);
    StructDef* def = reg_add(&p->registry, name.start, name.length, name.line, p->current_path, doc);
    
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
            char msg[128];
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
}

void parse_packet(Parser* p, const char* doc) {
    (void)doc;
    // TODO: Store doc comment for packet?
    Token name = p->current; consume(p, TOK_IDENTIFIER, "Expect packet name");
    if (p->verbose) printf("[VERBOSE] Parsing packet '%.*s'\n", name.length, name.start);
    
    // Emit META_NAME with placeholder key
    size_t meta_loc = buf_current_offset(p->target);
    buf_push(p->target, OP_META_NAME);
    buf_push_u16(p->target, 0xFFFF); // Placeholder
    
    parse_block(p);
    
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
    parse_top_level(p);
    
    // Restore state
    p->lexer = old_lexer;
    p->current = old_cur;
    p->previous = old_prev;
    p->current_path = old_path;
    
    free(source);
    free(full_path);
}

// Helper to append doc comments
static char* append_doc(char* current, Token t) {
    size_t len = t.length;
    size_t current_len = current ? strlen(current) : 0;
    char* new_doc = realloc(current, current_len + len + 2); // +1 for space/newline, +1 for null
    if (current_len > 0) {
        new_doc[current_len] = '\n'; // Join with newline
        memcpy(new_doc + current_len + 1, t.start, len);
        new_doc[current_len + 1 + len] = '\0';
    } else {
        memcpy(new_doc, t.start, len);
        new_doc[len] = '\0';
    }
    return new_doc;
}

void parse_top_level(Parser* p) {
    char* pending_doc = NULL;

    while (p->current.type != TOK_EOF && !p->had_error) {
        if (p->current.type == TOK_DOC_COMMENT) {
            pending_doc = append_doc(pending_doc, p->current);
            advance(p);
            continue;
        }

        // Handle Decorators (Stackable)
        while (p->current.type == TOK_AT) {
            advance(p); 
            Token dec = p->current; consume(p, TOK_IDENTIFIER, "Expect decorator name");
            
            if (match_keyword(dec, "version")) {
                consume(p, TOK_LPAREN, "Expect ("); Token ver = p->current; consume(p, TOK_NUMBER, "Expect version number");
                uint32_t v = parse_number(ver); buf_push(p->target, OP_META_VERSION); buf_push(p->target, (uint8_t)v);
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

        if (p->current.type == TOK_STRUCT) {
            advance(p); parse_struct(p, pending_doc);
            if(pending_doc) { free(pending_doc); pending_doc = NULL; }
        } else if (p->current.type == TOK_ENUM) {
            advance(p); parse_enum(p, pending_doc);
            if(pending_doc) { free(pending_doc); pending_doc = NULL; }
        } else if (p->current.type == TOK_PACKET) {
            if (p->packet_count > 0) {
                parser_error(p, "Only one packet definition allowed per file");
            }
            p->packet_count++;
            advance(p); parse_packet(p, pending_doc);
            if(pending_doc) { free(pending_doc); pending_doc = NULL; } // Packet doesn't store doc yet but consume it
        } else if (p->current.type == TOK_SEMICOLON) {
            advance(p); // Ignore top-level semicolons
            if(pending_doc) { free(pending_doc); pending_doc = NULL; } // Orphaned comment
        } else if (p->current.type != TOK_EOF) {
            parser_error(p, "Unexpected token");
            if(pending_doc) { free(pending_doc); pending_doc = NULL; }
        }
    }
    if(pending_doc) free(pending_doc);
}

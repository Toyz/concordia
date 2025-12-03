#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>

#define MAX_LINE 1024

// Acronyms to keep uppercase
const char *KEEP_UPPER[] = {
    "OOB",
    "CRC",
    "ID",
    "VM",
    NULL
};

// Helper to convert SCREAMING_SNAKE_CASE to PascalCase
void to_pascal_case(const char *input, char *output, const char *strip_prefix, const char *add_prefix) {
    size_t i = 0, j = 0;
    int capitalize_next = 1;
    
    if (add_prefix) {
        strcpy(output, add_prefix);
        j = strlen(add_prefix);
    }

    // Skip prefix if it matches
    if (strip_prefix && strncmp(input, strip_prefix, strlen(strip_prefix)) == 0) {
        i += strlen(strip_prefix);
    }

    char buffer[256];
    int buf_idx = 0;

    for (; input[i]; i++) {
        if (input[i] == '_') {
            // Flush buffer
            buffer[buf_idx] = '\0';
            if (buf_idx > 0) {
                // Check if buffer is in KEEP_UPPER
                bool kept = false;
                for (int k = 0; KEEP_UPPER[k]; k++) {
                    if (strcmp(buffer, KEEP_UPPER[k]) == 0) {
                        strcpy(&output[j], KEEP_UPPER[k]);
                        j += strlen(KEEP_UPPER[k]);
                        kept = true;
                        break;
                    }
                }
                if (!kept) {
                    // PascalCase the buffer
                    output[j++] = toupper(buffer[0]);
                    for (int k = 1; k < buf_idx; k++) {
                        output[j++] = tolower(buffer[k]);
                    }
                }
            }
            buf_idx = 0;
            capitalize_next = 1;
        } else {
            buffer[buf_idx++] = input[i];
        }
    }
    
    // Flush last part
    buffer[buf_idx] = '\0';
    if (buf_idx > 0) {
        bool kept = false;
        for (int k = 0; KEEP_UPPER[k]; k++) {
            if (strcmp(buffer, KEEP_UPPER[k]) == 0) {
                strcpy(&output[j], KEEP_UPPER[k]);
                j += strlen(KEEP_UPPER[k]);
                kept = true;
                break;
            }
        }
        if (!kept) {
            output[j++] = toupper(buffer[0]);
            for (int k = 1; k < buf_idx; k++) {
                output[j++] = tolower(buffer[k]);
            }
        }
    }
    output[j] = '\0';
}

typedef struct {
    char name[256];
    char value[256];
} ConstEntry;

static ConstEntry ops[256];
static int op_count = 0;

static ConstEntry errs[256];
static int err_count = 0;

static ConstEntry modes[256];
static int mode_count = 0;

static ConstEntry trans[256];
static int trans_count = 0;

static ConstEntry others[256];
static int other_count = 0;

void process_line(char *line) {
    char name[256];
    char value[256];
    char go_name[256];

    // Strip comments
    char *comment = strstr(line, "//");
    if (comment) {
        *comment = '\0';
    }

    // Handle #define OP_...
    if (sscanf(line, "#define OP_%s %s", name, value) == 2) {
        char full_name[256];
        sprintf(full_name, "OP_%s", name);
        to_pascal_case(full_name, go_name, "OP_", "Op");
        
        strcpy(ops[op_count].name, go_name);
        strcpy(ops[op_count].value, value);
        op_count++;
        return;
    }

    // Handle Enum values
    char *ptr = line;
    while (*ptr && isspace(*ptr)) ptr++;
    
    if (strncmp(ptr, "CND_", 4) == 0) {
        char *eq = strchr(ptr, '=');
        char *comma = strchr(ptr, ',');
        
        // Determine name end
        char *name_end = ptr;
        while (*name_end && (isalnum(*name_end) || *name_end == '_')) name_end++;
        
        size_t len = name_end - ptr;
        strncpy(name, ptr, len);
        name[len] = '\0';

        // Determine value
        static int last_val = -1;
        static char last_prefix[256] = "";
        
        // Check if we switched enum blocks (heuristic: prefix change)
        // Actually, C enums reset to 0 if not specified, but usually they are in a block.
        // We can just rely on the fact that if '=' is present, we parse it.
        // If not, we increment last_val.
        
        if (eq) {
            char *val_start = eq + 1;
            while (*val_start && isspace(*val_start)) val_start++;
            char *val_end = val_start;
            while (*val_end && (isalnum(*val_end) || *val_end == 'x' || *val_end == '-')) val_end++;
            
            char val_str[256];
            strncpy(val_str, val_start, val_end - val_start);
            val_str[val_end - val_start] = '\0';
            
            // Parse value to int for tracking
            last_val = (int)strtol(val_str, NULL, 0);
            strcpy(value, val_str);
        } else {
            // Implicit increment
            last_val++;
            sprintf(value, "%d", last_val);
        }

        if (strncmp(name, "CND_ERR_", 8) == 0) {
            to_pascal_case(name, go_name, "CND_ERR_", "Err");
            strcpy(errs[err_count].name, go_name);
            strcpy(errs[err_count].value, value);
            err_count++;
        } else if (strncmp(name, "CND_MODE_", 9) == 0) {
            to_pascal_case(name, go_name, "CND_MODE_", "Mode");
            strcpy(modes[mode_count].name, go_name);
            strcpy(modes[mode_count].value, value);
            mode_count++;
        } else if (strncmp(name, "CND_TRANS_", 10) == 0) {
            to_pascal_case(name, go_name, "CND_TRANS_", "Trans");
            strcpy(trans[trans_count].name, go_name);
            strcpy(trans[trans_count].value, value);
            trans_count++;
        } else if (strncmp(name, "CND_LE", 6) == 0 || strncmp(name, "CND_BE", 6) == 0) {
             // Special case for Endianness
             to_pascal_case(name, go_name, "CND_", "");
             strcpy(others[other_count].name, go_name);
             strcpy(others[other_count].value, value);
             other_count++;
        }
    }
}

void generate_go() {
    printf("// Code generated by gen_bindings.c; DO NOT EDIT.\n");
    printf("package concordia\n\n");
    
    printf("type Error int\n");
    printf("type Mode int\n");
    printf("type Trans int\n");
    printf("type OpCode uint8\n\n");

    if (err_count > 0) {
        printf("const (\n");
        for (int i = 0; i < err_count; i++) {
            printf("\t%s Error = %s\n", errs[i].name, errs[i].value);
        }
        printf(")\n\n");
    }

    if (mode_count > 0) {
        printf("const (\n");
        for (int i = 0; i < mode_count; i++) {
            printf("\t%s Mode = %s\n", modes[i].name, modes[i].value);
        }
        printf(")\n\n");
    }

    if (trans_count > 0) {
        printf("const (\n");
        for (int i = 0; i < trans_count; i++) {
            printf("\t%s Trans = %s\n", trans[i].name, trans[i].value);
        }
        printf(")\n\n");
    }

    if (op_count > 0) {
        printf("const (\n");
        for (int i = 0; i < op_count; i++) {
            printf("\t%s OpCode = %s\n", ops[i].name, ops[i].value);
        }
        printf(")\n\n");
    }

    if (other_count > 0) {
        printf("const (\n");
        for (int i = 0; i < other_count; i++) {
            printf("\t%s = %s\n", others[i].name, others[i].value);
        }
        printf(")\n");
    }
}

void generate_python() {
    printf("# Code generated by gen_bindings.c; DO NOT EDIT.\n");
    printf("from enum import IntEnum\n\n");

    if (err_count > 0) {
        printf("class Error(IntEnum):\n");
        for (int i = 0; i < err_count; i++) {
            printf("    %s = %s\n", errs[i].name, errs[i].value);
        }
        printf("\n");
    }

    if (mode_count > 0) {
        printf("class Mode(IntEnum):\n");
        for (int i = 0; i < mode_count; i++) {
            printf("    %s = %s\n", modes[i].name, modes[i].value);
        }
        printf("\n");
    }

    if (trans_count > 0) {
        printf("class Trans(IntEnum):\n");
        for (int i = 0; i < trans_count; i++) {
            printf("    %s = %s\n", trans[i].name, trans[i].value);
        }
        printf("\n");
    }

    if (op_count > 0) {
        printf("class OpCode(IntEnum):\n");
        for (int i = 0; i < op_count; i++) {
            printf("    %s = %s\n", ops[i].name, ops[i].value);
        }
        printf("\n");
    }
}

void generate_ts() {
    printf("// Code generated by gen_bindings.c; DO NOT EDIT.\n\n");

    if (err_count > 0) {
        printf("export enum Error {\n");
        for (int i = 0; i < err_count; i++) {
            printf("    %s = %s,\n", errs[i].name, errs[i].value);
        }
        printf("}\n\n");
    }

    if (mode_count > 0) {
        printf("export enum Mode {\n");
        for (int i = 0; i < mode_count; i++) {
            printf("    %s = %s,\n", modes[i].name, modes[i].value);
        }
        printf("}\n\n");
    }

    if (trans_count > 0) {
        printf("export enum Trans {\n");
        for (int i = 0; i < trans_count; i++) {
            printf("    %s = %s,\n", trans[i].name, trans[i].value);
        }
        printf("}\n\n");
    }

    if (op_count > 0) {
        printf("export enum OpCode {\n");
        for (int i = 0; i < op_count; i++) {
            printf("    %s = %s,\n", ops[i].name, ops[i].value);
        }
        printf("}\n\n");
    }
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <concordia.h> [lang]\n", argv[0]);
        fprintf(stderr, "Languages: go (default), python, ts\n");
        return 1;
    }

    FILE *fp = fopen(argv[1], "r");
    if (!fp) {
        perror("fopen");
        return 1;
    }

    char line[MAX_LINE];
    while (fgets(line, sizeof(line), fp)) {
        process_line(line);
    }
    fclose(fp);

    const char *lang = "go";
    if (argc >= 3) {
        lang = argv[2];
    }

    if (strcmp(lang, "go") == 0) {
        generate_go();
    } else if (strcmp(lang, "python") == 0) {
        generate_python();
    } else if (strcmp(lang, "ts") == 0) {
        generate_ts();
    } else {
        fprintf(stderr, "Unknown language: %s\n", lang);
        return 1;
    }

    return 0;
}

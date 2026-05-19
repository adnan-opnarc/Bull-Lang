/*
 * Bull Compiler - Error Reporting Implementation
 * Format:
 *   [1]file.bl-->
 *      31| print(\n)
 *         ^^^^^ {print(const *char,....);}
 *      31| print(\n)
 *                 ^(expected ';')
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "error.h"
#include "lexer.h"
#include "token.h"
#include "color.h"

/* Extract a specific line from the source (1-indexed) */
static char *get_source_line(const char *source, int target_line) {
    if (!source) return NULL;
    
    int current_line = 1;
    const char *line_start = source;
    const char *p = source;
    
    while (*p) {
        if (current_line == target_line) {
            const char *end = p;
            while (*end && *end != '\n' && *end != '\r') end++;
            int len = end - line_start;
            char *line = malloc(len + 1);
            strncpy(line, line_start, len);
            line[len] = '\0';
            return line;
        }
        
        if (*p == '\n') {
            current_line++;
            line_start = p + 1;
        }
        p++;
    }
    
    if (current_line == target_line) {
        int len = strlen(line_start);
        char *line = malloc(len + 1);
        strcpy(line, line_start);
        return line;
    }
    
    return NULL;
}

/* Get token signature string */
static const char* token_signature(TokenType type, const char *text) {
    static char buffer[128];
    switch (type) {
        case TOKEN_PRINT: return "print(const *char,....);";
        case TOKEN_INPUT: return "input(const *char,....);";
        case TOKEN_LPAREN: return "(expr)";
        case TOKEN_RPAREN: return ")";
        case TOKEN_LBRACE: return "{...}";
        case TOKEN_RBRACE: return "}";
        case TOKEN_LBRACE2: return "{{...}}";
        case TOKEN_RBRACE2: return "}}";
        case TOKEN_LBRACKET: return "[...]";
        case TOKEN_RBRACKET: return "]";
        case TOKEN_SEMICOLON: return ";";
        case TOKEN_COMMA: return ",";
        case TOKEN_DOT: return ".";
        case TOKEN_COLON: return ":";
        case TOKEN_EQ: return "=";
        case TOKEN_PLUS: return "+";
        case TOKEN_MINUS: return "-";
        case TOKEN_STAR: return "*";
        case TOKEN_SLASH: return "/";
        case TOKEN_MOD: return "%";
        case TOKEN_AMP: return "&";
        case TOKEN_PIPE: return "|";
        case TOKEN_CARET: return "^";
        case TOKEN_TILDE: return "~";
        case TOKEN_BANG: return "!";
        case TOKEN_LT: return "<";
        case TOKEN_GT: return ">";
        case TOKEN_PLUS_PLUS: return "++";
        case TOKEN_MINUS_MINUS: return "--";
        case TOKEN_ARROW: return "->";
        case TOKEN_EQ_EQ: return "==";
        case TOKEN_BANG_EQ: return "!=";
        case TOKEN_LT_EQ: return "<=";
        case TOKEN_GT_EQ: return ">=";
        case TOKEN_AMP_AMP: return "&&";
        case TOKEN_PIPE_PIPE: return "||";
        case TOKEN_LT_LT: return "<<";
        case TOKEN_GT_GT: return ">>";
        case TOKEN_RETURN: return "return";
        case TOKEN_IF: return "if (expr) stmt";
        case TOKEN_ELSE: return "else";
        case TOKEN_WHILE: return "while (expr) stmt";
        case TOKEN_FOR: return "for (init; cond; inc) stmt";
        case TOKEN_CLASS: return "class Name { ... }";
        case TOKEN_STRUCT: return "struct Name { ... }";
        case TOKEN_INT: return "int";
        case TOKEN_BOOL: return "bool";
        case TOKEN_GLASS: return "glass";
        case TOKEN_MATRIX: return "matrix";
        case TOKEN_VAR: return "var";
        case TOKEN_TRUE: return "true";
        case TOKEN_FALSE: return "false";
        case TOKEN_NULL: return "null";
        case TOKEN_THIS: return "this";
        case TOKEN_NEW: return "new";
        case TOKEN_USING: return "using";
        case TOKEN_IDENTIFIER: return text ? text : "identifier";
        case TOKEN_NUMBER: return text ? text : "number";
        case TOKEN_STRING: return text ? "\"string\"" : "\"\"";
        case TOKEN_EOF: return "end of file";
        default:
            if (text) {
                strncpy(buffer, text, sizeof(buffer)-1);
                buffer[sizeof(buffer)-1] = '\0';
                return buffer;
            }
            return "token";
    }
}

/* Print formatted error */
void report_error(Lexer *lexer, Token *token, const char *expected) {
    const char *src = lexer->source;
    char *line_str = get_source_line(src, token->line);
    if (!line_str) return;
    
    // Trim trailing whitespace
    int len = strlen(line_str);
    while (len > 0 && (line_str[len-1] == ' ' || line_str[len-1] == '\t')) {
        line_str[--len] = '\0';
    }
    
    // Calculate prefix width: "    %d| " = 4 + digits(line) + 2
    int line_digits = snprintf(NULL, 0, "%d", token->line);
    int prefix_width = 4 + line_digits + 2;
    
    // Line 1: source code
    fprintf(stderr, "    " COLOR_GREEN "%d" COLOR_RESET "| %s\n", token->line, line_str);
    
    // Line 2: caret under token, with signature
    int col = token->column;
    if (col < 1) col = 1;
    const char *sig = token_signature(token->type, token->text);
    
    // Print spaces to token column
    for (int i = 0; i < prefix_width - 1 + col - 1; i++) fputc(' ', stderr);
    // Print carets matching token text length
    int token_text_len = token->text ? strlen(token->text) : 1;
    int caret_count = token_text_len;
    if (caret_count < 1) caret_count = 1;
    if (caret_count > 30) caret_count = 30;
    for (int i = 0; i < caret_count; i++) fputc('^', stderr);
    fprintf(stderr, " " COLOR_ORANGE "{%s}" COLOR_RESET "\n", sig);
    
    fprintf(stderr, "    " COLOR_GREEN "%d" COLOR_RESET "| %s\n", token->line, line_str);
    
    // Line 4: caret pointing to expected token location
    for (int i = 0; i < prefix_width - 1 + col - 1; i++) fputc(' ', stderr);
    fprintf(stderr, COLOR_RED "^(expected '%s')" COLOR_RESET "\n", expected);
    
    free(line_str);
    fflush(stderr);
}

void report_lexer_error(Lexer *lexer, Token *token, const char *reason) {
    const char *src = lexer->source;
    char *line_str = get_source_line(src, token->line);
    if (!line_str) return;
    
    int len = strlen(line_str);
    while (len > 0 && (line_str[len-1] == ' ' || line_str[len-1] == '\t')) {
        line_str[--len] = '\0';
    }
    
    fprintf(stderr, "    " COLOR_GREEN "%d" COLOR_RESET "| %s\n", token->line, line_str);
    
    int line_digits = snprintf(NULL, 0, "%d", token->line);
    int col = token->column;
    if (col < 1) col = 1;
    for (int i = 0; i < 4 + line_digits + 2 - 1 + col - 1; i++) fputc(' ', stderr);
    fprintf(stderr, COLOR_RED "^ %s" COLOR_RESET "\n", reason);
    
    free(line_str);
    fflush(stderr);
}

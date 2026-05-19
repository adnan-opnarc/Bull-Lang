/*
 * Bull Compiler - Lexer
 * Tokenizes Bull source code into tokens for the parser
 */

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include "lexer.h"
#include "token.h"

static const char *keywords[] = {
    "print", "input", "class", "struct", "int", "s8", "s16", "s32", "s64",
    "u8", "u16", "u32", "u64", "glass", "bool", "matrix", "array", "map",
    "call", "rc", "kernif", "kernelse", "else", "if", "return",
    "true", "false", "this", "var", "new", "len", "null", "while", "for",
    "read", "write", "open", "char", "exl", "using", NULL
};

static TokenType get_keyword_type(const char *word) {
    for (int i = 0; keywords[i] != NULL; i++) {
        if (strcmp(word, keywords[i]) == 0) {
            switch (i) {
                case 0: return TOKEN_PRINT;
                case 1: return TOKEN_INPUT;
                case 2: return TOKEN_CLASS;
                case 3: return TOKEN_STRUCT;
                case 4: return TOKEN_INT;
                case 5: return TOKEN_S8;
                case 6: return TOKEN_S16;
                case 7: return TOKEN_S32;
                case 8: return TOKEN_S64;
                case 9: return TOKEN_U8;
                case 10: return TOKEN_U16;
                case 11: return TOKEN_U32;
                case 12: return TOKEN_U64;
                case 13: return TOKEN_GLASS;
                case 14: return TOKEN_BOOL;
                case 15: return TOKEN_MATRIX;
                case 16: return TOKEN_ARRAY;
                case 17: return TOKEN_MAP;
                case 18: return TOKEN_CALL;
                case 19: return TOKEN_RC;
                case 20: return TOKEN_KERNIF;
                case 21: return TOKEN_KERNELSE;
                case 22: return TOKEN_ELSE;
                case 23: return TOKEN_IF;
                case 24: return TOKEN_RETURN;
                case 25: return TOKEN_TRUE;
                case 26: return TOKEN_FALSE;
                case 27: return TOKEN_THIS;
                case 28: return TOKEN_VAR;
                case 29: return TOKEN_NEW;
                case 30: return TOKEN_LEN;
                case 31: return TOKEN_NULL;
                case 32: return TOKEN_WHILE;
                 case 33: return TOKEN_FOR;
                 case 34: return TOKEN_READ;
                 case 35: return TOKEN_WRITE;
                 case 36: return TOKEN_OPEN;
                   case 37: return TOKEN_CHAR;
                   case 38: return TOKEN_EXL;
                   case 39: return TOKEN_USING;
                  default: return TOKEN_IDENTIFIER;
            }
        }
    }
    return TOKEN_IDENTIFIER;
}

Lexer *lexer_new(const char *source, const char *filename) {
    Lexer *lexer = calloc(1, sizeof(Lexer));
    lexer->source = strdup(source);
    lexer->filename = strdup(filename);
    lexer->pos = 0;
    lexer->len = strlen(source);
    lexer->line = 1;
    lexer->column = 1;
    return lexer;
}

void lexer_free(Lexer *lexer) {
    if (lexer) {
        free(lexer->source);
        free(lexer->filename);
        free(lexer);
    }
}

static char lexer_peek(Lexer *lexer) {
    if (lexer->pos >= lexer->len) return '\0';
    return lexer->source[lexer->pos];
}

static char lexer_peek_next(Lexer *lexer) {
    if (lexer->pos + 1 >= lexer->len) return '\0';
    return lexer->source[lexer->pos + 1];
}

static char lexer_advance(Lexer *lexer) {
    if (lexer->pos >= lexer->len) return '\0';
    char c = lexer->source[lexer->pos++];
    if (c == '\n') {
        lexer->line++;
        lexer->column = 1;
    } else {
        lexer->column++;
    }
    return c;
}

static bool lexer_match(Lexer *lexer, char expected) {
    if (lexer_peek(lexer) != expected) return false;
    lexer_advance(lexer);
    return true;
}

static void skip_whitespace(Lexer *lexer) {
    while (1) {
        char c = lexer_peek(lexer);
        if (c == ' ' || c == '\t' || c == '\r') {
            lexer_advance(lexer);
        } else if (c == '\n') {
            lexer_advance(lexer);
        } else {
            break;
        }
    }
}

static void skip_comment(Lexer *lexer) {
    char c = lexer_peek(lexer);
    if (c == '/' && lexer_peek_next(lexer) == '/') {
        while (lexer_peek(lexer) != '\n' && lexer_peek(lexer) != '\0') {
            lexer_advance(lexer);
        }
    } else if (c == '/' && lexer_peek_next(lexer) == '*') {
        lexer_advance(lexer);
        lexer_advance(lexer);
        while (1) {
            c = lexer_peek(lexer);
            if (c == '\0') break;
            if (c == '*' && lexer_peek_next(lexer) == '/') {
                lexer_advance(lexer);
                lexer_advance(lexer);
                break;
            }
            lexer_advance(lexer);
        }
    }
}

static Token *make_token(Lexer *lexer, TokenType type, const char *text) {
    Token *token = calloc(1, sizeof(Token));
    token->type = type;
    token->line = lexer->line;
    token->column = lexer->column;
    if (text) {
        token->text = strdup(text);
    } else {
        token->text = NULL;
    }
    return token;
}

static int is_hex_digit(char c) {
    return isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static Token *read_char(Lexer *lexer) {
    unsigned char value = 0;

    if (lexer_peek(lexer) == '\0') {
        return make_token(lexer, TOKEN_ERROR, "Unterminated character literal");
    }

    if (lexer_peek(lexer) == '\\') {
        lexer_advance(lexer);
        char esc = lexer_peek(lexer);
        if (esc == '\0') {
            return make_token(lexer, TOKEN_ERROR, "Unterminated character literal");
        }
        lexer_advance(lexer);
        switch (esc) {
            case 'n': value = '\n'; break;
            case 't': value = '\t'; break;
            case 'r': value = '\r'; break;
            case '0': value = '\0'; break;
            case '\\': value = '\\'; break;
            case '\'': value = '\''; break;
            default:  value = esc; break;
        }
    } else {
        value = lexer_advance(lexer);
    }

    if (!lexer_match(lexer, '\'')) {
        return make_token(lexer, TOKEN_ERROR, "Expected closing single quote");
    }

    char num_str[12];
    snprintf(num_str, sizeof(num_str), "%d", value);
    return make_token(lexer, TOKEN_NUMBER, num_str);
}

static Token *read_number(Lexer *lexer) {
    int start = lexer->pos - 1;
    
    // Check for hex literal: 0x or 0X
    if (lexer->source[start] == '0' && (lexer_peek(lexer) == 'x' || lexer_peek(lexer) == 'X')) {
        lexer_advance(lexer); // consume 'x' or 'X'
        while (is_hex_digit(lexer_peek(lexer))) {
            lexer_advance(lexer);
        }
    } else {
        while (isdigit(lexer_peek(lexer))) {
            lexer_advance(lexer);
        }
        if (lexer_peek(lexer) == '.' && isdigit(lexer_peek_next(lexer))) {
            lexer_advance(lexer);
            while (isdigit(lexer_peek(lexer))) {
                lexer_advance(lexer);
            }
        }
    }
    
    int length = lexer->pos - start;
    char *num_str = malloc(length + 1);
    strncpy(num_str, lexer->source + start, length);
    num_str[length] = '\0';
    
    Token *token = make_token(lexer, TOKEN_NUMBER, num_str);
    free(num_str);
    return token;
}

static Token *read_string(Lexer *lexer) {
    // Opening quote already consumed by lexer_next_token
    char *buffer = NULL;
    int buf_len = 0;
    int buf_cap = 0;
    
    while (lexer_peek(lexer) != '"' && lexer_peek(lexer) != '\0') {
        char c = lexer_peek(lexer);
        if (c == '\\') {
            lexer_advance(lexer);
            char esc = lexer_peek(lexer);
            if (esc == '\0') break;
            lexer_advance(lexer);
            char decoded;
            switch (esc) {
                case 'n': decoded = '\n'; break;
                case 't': decoded = '\t'; break;
                case 'r': decoded = '\r'; break;
                case '\\': decoded = '\\'; break;
                case '"': decoded = '"'; break;
                default:  decoded = esc; break;
            }
            if (buf_len + 1 >= buf_cap) {
                buf_cap = buf_cap * 2 + 16;
                buffer = realloc(buffer, buf_cap);
            }
            buffer[buf_len++] = decoded;
        } else {
            lexer_advance(lexer);
            if (buf_len + 1 >= buf_cap) {
                buf_cap = buf_cap * 2 + 16;
                buffer = realloc(buffer, buf_cap);
            }
            buffer[buf_len++] = c;
        }
    }
    
    if (lexer_peek(lexer) == '"') {
        lexer_advance(lexer);
    }
    
    if (!buffer) {
        buffer = malloc(1);
        buffer[0] = '\0';
    } else {
        buffer[buf_len] = '\0';
    }
    
    
    Token *token = make_token(lexer, TOKEN_STRING, buffer);
    free(buffer);
    return token;
}

static Token *read_identifier(Lexer *lexer) {
    int start = lexer->pos - 1;
    while (isalnum(lexer_peek(lexer)) || lexer_peek(lexer) == '_') {
        lexer_advance(lexer);
    }
    
    int length = lexer->pos - start;
    char *id = malloc(length + 1);
    strncpy(id, lexer->source + start, length);
    id[length] = '\0';
    
    TokenType type = get_keyword_type(id);
    Token *token = make_token(lexer, type, id);
    free(id);
    return token;
}

Token *lexer_next_token(Lexer *lexer) {
    skip_whitespace(lexer);
    skip_comment(lexer);
    skip_whitespace(lexer);
    
    char c = lexer_peek(lexer);
    if (c == '\0') {
        return make_token(lexer, TOKEN_EOF, NULL);
    }
    
    c = lexer_advance(lexer);
    
    // Single character tokens
    if (c == '(') return make_token(lexer, TOKEN_LPAREN, "(");
    if (c == ')') return make_token(lexer, TOKEN_RPAREN, ")");
    if (c == '{') {
        if (lexer_match(lexer, '{')) return make_token(lexer, TOKEN_LBRACE2, "{{");
        return make_token(lexer, TOKEN_LBRACE, "{");
    }
    if (c == '}') {
        if (lexer_match(lexer, '}')) return make_token(lexer, TOKEN_RBRACE2, "}}");
        return make_token(lexer, TOKEN_RBRACE, "}");
    }
    if (c == '[') return make_token(lexer, TOKEN_LBRACKET, "[");
    if (c == ']') return make_token(lexer, TOKEN_RBRACKET, "]");
    if (c == ',') return make_token(lexer, TOKEN_COMMA, ",");
    if (c == ';') return make_token(lexer, TOKEN_SEMICOLON, ";");
    if (c == '.') return make_token(lexer, TOKEN_DOT, ".");
    
    // Multi-character tokens
    if (c == '+') {
        if (lexer_match(lexer, '+')) return make_token(lexer, TOKEN_PLUS_PLUS, "++");
        if (lexer_match(lexer, '=')) return make_token(lexer, TOKEN_PLUS_EQ, "+=");
        return make_token(lexer, TOKEN_PLUS, "+");
    }
    if (c == '-') {
        if (lexer_match(lexer, '-')) return make_token(lexer, TOKEN_MINUS_MINUS, "--");
        if (lexer_match(lexer, '=')) return make_token(lexer, TOKEN_MINUS_EQ, "-=");
        if (lexer_match(lexer, '>')) return make_token(lexer, TOKEN_ARROW, "->");
        return make_token(lexer, TOKEN_MINUS, "-");
    }
    if (c == '*') {
        if (lexer_match(lexer, '=')) return make_token(lexer, TOKEN_STAR_EQ, "*=");
        return make_token(lexer, TOKEN_STAR, "*");
    }
    if (c == '/') {
        if (lexer_match(lexer, '=')) return make_token(lexer, TOKEN_SLASH_EQ, "/=");
        return make_token(lexer, TOKEN_SLASH, "/");
    }
    if (c == '%') {
        if (lexer_match(lexer, '=')) return make_token(lexer, TOKEN_MOD_EQ, "%=");
        return make_token(lexer, TOKEN_MOD, "%");
    }
    if (c == '=') {
        if (lexer_match(lexer, '=')) return make_token(lexer, TOKEN_EQ_EQ, "==");
        return make_token(lexer, TOKEN_EQ, "=");
    }
    if (c == '!') {
        if (lexer_match(lexer, '=')) return make_token(lexer, TOKEN_BANG_EQ, "!=");
        return make_token(lexer, TOKEN_BANG, "!");
    }
    if (c == '<') {
        if (lexer_match(lexer, '=')) return make_token(lexer, TOKEN_LT_EQ, "<=");
        if (lexer_match(lexer, '<')) return make_token(lexer, TOKEN_LT_LT, "<<");
        return make_token(lexer, TOKEN_LT, "<");
    }
    if (c == '>') {
        if (lexer_match(lexer, '=')) return make_token(lexer, TOKEN_GT_EQ, ">=");
        if (lexer_match(lexer, '>')) return make_token(lexer, TOKEN_GT_GT, ">>");
        return make_token(lexer, TOKEN_GT, ">");
    }
    if (c == '&') {
        if (lexer_match(lexer, '&')) return make_token(lexer, TOKEN_AMP_AMP, "&&");
        if (lexer_match(lexer, '=')) return make_token(lexer, TOKEN_AMP_EQ, "&=");
        return make_token(lexer, TOKEN_AMP, "&");
    }
    if (c == '|') {
        if (lexer_match(lexer, '|')) return make_token(lexer, TOKEN_PIPE_PIPE, "||");
        if (lexer_match(lexer, '=')) return make_token(lexer, TOKEN_PIPE_EQ, "|=");
        return make_token(lexer, TOKEN_PIPE, "|");
    }
    if (c == '^') {
        if (lexer_match(lexer, '=')) return make_token(lexer, TOKEN_CARET_EQ, "^=");
        return make_token(lexer, TOKEN_CARET, "^");
    }
    if (c == '~') return make_token(lexer, TOKEN_TILDE, "~");
    if (c == ':') return make_token(lexer, TOKEN_COLON, ":");
    if (c == '?') return make_token(lexer, TOKEN_QUESTION, "?");
    
    // Character literals
    if (c == '\'') {
        return read_char(lexer);
    }
    
    // Numbers
    if (isdigit(c)) {
        return read_number(lexer);
    }
    
    // Strings
    if (c == '"') {
        return read_string(lexer);
    }
    
    // Identifiers and keywords
    if (isalpha(c) || c == '_') {
        return read_identifier(lexer);
    }
    
    // Unknown character
    return make_token(lexer, TOKEN_ERROR, "Unknown character");
}

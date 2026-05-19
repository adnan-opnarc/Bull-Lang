/*
 * Bull Compiler - Lexer Header
 * Lexical analyzer for the Bull programming language
 */

#ifndef BULL_LEXER_H
#define BULL_LEXER_H

#include "token.h"

typedef struct {
    char *source;
    char *filename;
    int pos;
    int len;
    int line;
    int column;
} Lexer;

Lexer *lexer_new(const char *source, const char *filename);
void lexer_free(Lexer *lexer);
Token *lexer_next_token(Lexer *lexer);

#endif // BULL_LEXER_H

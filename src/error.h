/*
 * Bull Compiler - Error Reporting
 * Provides formatted error output matching the style:
 *   [1]file.bl-->
 *      31| print(\n) --------- using stdio
 *           ^^^^^ {print(const *char,....);}
 *      31| print(\n) --------- token found
 *               ^(expected ';')
 */

#ifndef ERROR_H
#define ERROR_H

#include "lexer.h"
#include "token.h"

/* Print a syntax error */
void report_error(Lexer *lexer, Token *token, const char *expected);

/* Print lexer unknown token error */
void report_lexer_error(Lexer *lexer, Token *token, const char *reason);

#endif
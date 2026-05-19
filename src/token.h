/*
 * Bull Compiler - Token Definitions
 * Defines token types and structures for the lexer and parser
 */

#ifndef BULL_TOKEN_H
#define BULL_TOKEN_H

typedef enum {
    // Single-character tokens
    TOKEN_LPAREN,      // (
    TOKEN_RPAREN,      // )
    TOKEN_LBRACE,      // {
    TOKEN_RBRACE,      // }
    TOKEN_LBRACE2,     // {{
    TOKEN_RBRACE2,     // }}
    TOKEN_LBRACKET,    // [
    TOKEN_RBRACKET,    // ]
    TOKEN_COMMA,       // ,
    TOKEN_SEMICOLON,   // ;
    TOKEN_DOT,         // .
    TOKEN_COLON,       // :
    TOKEN_QUESTION,    // ?
    
    // One or two character tokens
    TOKEN_PLUS,        // +
    TOKEN_MINUS,       // -
    TOKEN_STAR,        // *
    TOKEN_SLASH,       // /
    TOKEN_MOD,         // %
    TOKEN_AMP,         // &
    TOKEN_PIPE,        // |
    TOKEN_CARET,       // ^
    TOKEN_TILDE,       // ~
    TOKEN_BANG,        // !
    TOKEN_LT,          // <
    TOKEN_GT,          // >
    TOKEN_EQ,          // =
    
    TOKEN_PLUS_PLUS,   // ++
    TOKEN_MINUS_MINUS, // --
    TOKEN_PLUS_EQ,     // +=
    TOKEN_MINUS_EQ,    // -=
    TOKEN_STAR_EQ,     // *=
    TOKEN_SLASH_EQ,    // /=
    TOKEN_MOD_EQ,      // %=
    TOKEN_AMP_EQ,      // &=
    TOKEN_PIPE_EQ,     // |=
    TOKEN_CARET_EQ,    // ^=
    
    TOKEN_EQ_EQ,       // ==
    TOKEN_BANG_EQ,     // !=
    TOKEN_LT_EQ,       // <=
    TOKEN_GT_EQ,       // >=
    TOKEN_AMP_AMP,     // &&
    TOKEN_PIPE_PIPE,   // ||
    TOKEN_LT_LT,       // <<
    TOKEN_GT_GT,       // >>
    TOKEN_ARROW,       // ->
    
    // Literals
    TOKEN_IDENTIFIER,  // variable names
    TOKEN_NUMBER,      // numeric literals
    TOKEN_STRING,      // string literals
    
    // Keywords
    TOKEN_PRINT,       // print
    TOKEN_INPUT,       // input
    TOKEN_CLASS,       // class
    TOKEN_STRUCT,      // struct
    TOKEN_INT,         // int
    TOKEN_S8,          // s8 (signed 8-bit)
    TOKEN_S16,         // s16 (signed 16-bit)
    TOKEN_S32,         // s32 (signed 32-bit)
    TOKEN_S64,         // s64 (signed 64-bit)
    TOKEN_U8,          // u8 (unsigned 8-bit)
    TOKEN_U16,         // u16 (unsigned 16-bit)
    TOKEN_U32,         // u32 (unsigned 32-bit)
    TOKEN_U64,         // u64 (unsigned 64-bit)
    TOKEN_GLASS,       // glass (void)
    TOKEN_BOOL,        // bool
    TOKEN_MATRIX,      // matrix
    TOKEN_ARRAY,       // array
    TOKEN_MAP,         // map
    TOKEN_CHAR,        // char
    TOKEN_CALL,        // call
    TOKEN_RC,          // rc (return code)
    TOKEN_KERNIF,      // kernif
    TOKEN_KERNELSE,    // kernelse
    TOKEN_ELSE,        // else
    TOKEN_IF,          // if
    TOKEN_RETURN,      // return
    TOKEN_TRUE,        // true
    TOKEN_FALSE,       // false
    TOKEN_THIS,        // this
    TOKEN_VAR,         // var
    TOKEN_NEW,         // new
    TOKEN_LEN,         // len
    TOKEN_NULL,        // null
    TOKEN_WHILE,       // while
    TOKEN_FOR,         // for
    TOKEN_READ,        // read
    TOKEN_WRITE,       // write
    TOKEN_OPEN,        // open
    TOKEN_USING,       // using
    TOKEN_EXL,         // exl
    
    TOKEN_EOF,         // end of file
    TOKEN_ERROR        // error
} TokenType;

typedef struct {
    TokenType type;
    int line;
    int column;
    char *text;
} Token;

#endif // BULL_TOKEN_H

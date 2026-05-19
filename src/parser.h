/*
 * Bull Compiler - Parser Header
 * Parser for the Bull programming language
 */

#ifndef BULL_PARSER_H
#define BULL_PARSER_H

#include "token.h"
#include "lexer.h"

typedef enum {
    NODE_PROGRAM,
    NODE_FUNCTION,
    NODE_CLASS,
    NODE_STRUCT,
    NODE_BLOCK,
    NODE_EXPRESSION_STMT,
    NODE_RETURN_STMT,
    NODE_IF_STMT,
    NODE_WHILE_STMT,
    NODE_FOR_STMT,
    NODE_VAR_DECL,
    NODE_ASSIGNMENT,
    NODE_BINARY_OP,
    NODE_UNARY_OP,
    NODE_CALL,
    NODE_MEMBER_ACCESS,
    NODE_INDEX,
    NODE_IDENTIFIER,
    NODE_NUMBER,
    NODE_STRING,
    NODE_BOOLEAN,
    NODE_ARRAY_LITERAL,
    NODE_STRUCT_LITERAL
} NodeType;

typedef struct ASTNode {
    NodeType type;
    int line;
    int column;
    union {
        char *identifier;
        double number;
        char *string;
        int boolean;
        struct {
            char *name;
            struct ASTNode **params;
            int param_count;
            struct ASTNode *body;
            char *return_type;
            int is_exl;
          } function;
        struct {
            struct ASTNode **stmts;
            int stmt_count;
        } block;
        struct {
            struct ASTNode *condition;
            struct ASTNode *then_branch;
            struct ASTNode *else_branch;
            int is_kern;
        } if_stmt;
        struct {
            struct ASTNode *init;
            struct ASTNode *condition;
            struct ASTNode *increment;
            struct ASTNode *body;
        } for_loop;
        struct {
            struct ASTNode *value;
        } return_stmt;
        struct {
            char *var_type;
            char *var_name;
            struct ASTNode *init;
            int array_size;
        } var_decl;
        struct {
            char *op;
            struct ASTNode *left;
            struct ASTNode *right;
        } binary_op;
        struct {
            char *op;
            struct ASTNode *operand;
        } unary_op;
        struct {
            struct ASTNode *callee;
            struct ASTNode **args;
            int arg_count;
        } call;
        struct {
            struct ASTNode *target;
            struct ASTNode *value;
        } assign;
        struct {
            struct ASTNode *expr;
        } expr_stmt;
        struct {
            char *struct_type;
            struct {
                char *name;
                struct ASTNode *value;
            } *fields;
            int field_count;
        } struct_literal;
        struct {
            struct ASTNode **elements;
            int elem_count;
        } array_literal;
        struct {
            char *name;
            struct ASTNode **fields;
            int field_count;
            struct ASTNode **methods;
            int method_count;
        } class_decl;
        struct {
            struct ASTNode *object;
            char *member;
        } member_access;
        struct {
            struct ASTNode *object;
            struct ASTNode *index;
        } array_index;
        struct {
            char *name;
            struct ASTNode *value;
        } this_field;
     } data;
} ASTNode;

typedef struct {
    Lexer *lexer;
    Token *current;
    Token *previous;
    int had_error;
    int panic_mode;
    struct ASTNode *root;
} Parser;

Parser *parser_new(Lexer *lexer);
void parser_free(Parser *parser);
ASTNode *parser_parse(Parser *parser);

#endif // BULL_PARSER_H

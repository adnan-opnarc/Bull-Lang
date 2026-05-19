/*
 * Bull Compiler - Parser
 * Parses tokens into an abstract syntax tree (AST)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "token.h"
#include "lexer.h"
#include "parser.h"
#include "error.h"

static ASTNode *create_node(Parser *p, NodeType type) {
    ASTNode *node = calloc(1, sizeof(ASTNode));
    node->type = type;
    node->line = p->previous ? p->previous->line : 1;
    node->column = p->previous ? p->previous->column : 1;
    return node;
}

static void advance(Parser *parser) {
    parser->previous = parser->current;
    parser->current = lexer_next_token(parser->lexer);
}

static bool match(Parser *parser, TokenType type) {
    if (parser->current->type == type) {
        advance(parser);
        return true;
    }
    return false;
}

static bool consume(Parser *parser, TokenType type, const char *expected) {
    if (parser->current->type == type) {
        advance(parser);
        return true;
    }
    report_error(parser->lexer, parser->current, expected);
    parser->had_error = 1;
    return false;
}

// Forward declarations
static ASTNode *parse_expression(Parser *parser);
static ASTNode *parse_statement(Parser *parser);
static ASTNode *parse_block(Parser *parser);
static ASTNode *parse_atom(Parser *parser);
static ASTNode *parse_postfix(Parser *parser);
static ASTNode *parse_var_decl(Parser *parser, TokenType type);
static ASTNode *parse_function(Parser *parser);
static ASTNode *parse_exl_function(Parser *parser);
static int parse_type_size(Parser *parser);
static ASTNode *parse_if_stmt(Parser *parser);
static ASTNode *parse_while_stmt(Parser *parser);
static ASTNode *parse_for_stmt(Parser *parser);
static ASTNode *parse_class_body(Parser *parser, ASTNode *node);
static ASTNode *parse_array_literal(Parser *parser);
static int is_type_token(TokenType t);

static ASTNode *parse_atom(Parser *parser) {
    Token *tok = parser->current;
    
    // Accept any non-special token as identifier for now
    if (tok->type == TOKEN_NUMBER) {
        advance(parser);
        ASTNode *node = create_node(parser, NODE_NUMBER);
        node->data.number = strtod(tok->text, NULL);
        return node;
    }
    if (tok->type == TOKEN_STRING) {
        advance(parser);
        ASTNode *node = create_node(parser, NODE_STRING);
        node->data.string = strdup(tok->text);
        return node;
    }
    if (tok->type == TOKEN_TRUE) {
        advance(parser);
        ASTNode *node = create_node(parser, NODE_BOOLEAN);
        node->data.boolean = 1;
        return node;
    }
    if (tok->type == TOKEN_FALSE) {
        advance(parser);
        ASTNode *node = create_node(parser, NODE_BOOLEAN);
        node->data.boolean = 0;
        return node;
    }
    if (tok->type == TOKEN_LBRACE2) {
        return parse_array_literal(parser);
    }
    
    if (tok->type == TOKEN_LPAREN) {
        advance(parser);
        ASTNode *expr = parse_expression(parser);
    if (!consume(parser, TOKEN_RPAREN, ")")) {
        return NULL;
    }
    return expr;
    }
    
    if (tok->type == TOKEN_THIS) {
        advance(parser);
        ASTNode *node = create_node(parser, NODE_IDENTIFIER);
        node->data.identifier = strdup("this");
        return node;
    }
    
    // Identifier, print, input, keywords - treat as identifier
    if (tok->type == TOKEN_IDENTIFIER || tok->type == TOKEN_PRINT || 
        tok->type == TOKEN_INPUT || tok->type >= TOKEN_CLASS) {
        advance(parser);
        ASTNode *node = create_node(parser, NODE_IDENTIFIER);
        node->data.identifier = strdup(tok->text);
        return node;
    }
    
        report_error(parser->lexer, tok, "valid expression");
        parser->had_error = 1;
    return NULL;
}

static ASTNode *parse_array_literal(Parser *parser) {
    advance(parser);
    ASTNode *node = create_node(parser, NODE_ARRAY_LITERAL);
    node->data.array_literal.elements = NULL;
    node->data.array_literal.elem_count = 0;
    if (!match(parser, TOKEN_RBRACE2)) {
        do {
            ASTNode *elem = parse_expression(parser);
            node->data.array_literal.elem_count++;
            node->data.array_literal.elements = realloc(node->data.array_literal.elements,
                node->data.array_literal.elem_count * sizeof(ASTNode*));
            node->data.array_literal.elements[node->data.array_literal.elem_count-1] = elem;
        } while (match(parser, TOKEN_COMMA));
        if (!consume(parser, TOKEN_RBRACE2, "}}")) {
            return NULL;
        }
    }
    return node;
}

static ASTNode *parse_postfix(Parser *parser) {
    ASTNode *expr = parse_atom(parser);
    
    // Function call and array index postfix
    for (;;) {
        if (match(parser, TOKEN_LPAREN)) {
            ASTNode *call_node = create_node(parser, NODE_CALL);
            call_node->data.call.callee = expr;
            call_node->data.call.arg_count = 0;
            call_node->data.call.args = NULL;
            
            if (!match(parser, TOKEN_RPAREN)) {
                do {
                    ASTNode *arg = parse_expression(parser);
                    call_node->data.call.arg_count++;
                    call_node->data.call.args = realloc(call_node->data.call.args, call_node->data.call.arg_count * sizeof(ASTNode*));
                    call_node->data.call.args[call_node->data.call.arg_count-1] = arg;
                } while (match(parser, TOKEN_COMMA));
                if (!consume(parser, TOKEN_RPAREN, ")")) {
                    return NULL;
                }
            }
            expr = call_node;
        } else if (match(parser, TOKEN_LBRACE2)) {
            ASTNode *index_node = create_node(parser, NODE_INDEX);
            index_node->data.array_index.object = expr;
            index_node->data.array_index.index = parse_expression(parser);
            if (!consume(parser, TOKEN_RBRACE2, "}}")) {
                return NULL;
            }
            expr = index_node;
        } else if (match(parser, TOKEN_DOT)) {
            ASTNode *member_node = create_node(parser, NODE_MEMBER_ACCESS);
            member_node->data.member_access.object = expr;
            if (parser->current->type == TOKEN_IDENTIFIER) {
                advance(parser);
                member_node->data.member_access.member = strdup(parser->previous->text);
            } else {
                report_error(parser->lexer, parser->current, "member name");
                parser->had_error = 1;
                return NULL;
            }
            expr = member_node;
        } else {
            break;
        }
    }
    
    return expr;
}

static ASTNode *parse_unary(Parser *parser) {
    if (match(parser, TOKEN_MINUS) || match(parser, TOKEN_BANG) || 
        match(parser, TOKEN_PLUS_PLUS) || match(parser, TOKEN_MINUS_MINUS) ||
        match(parser, TOKEN_TILDE)) {
        TokenType op = parser->previous->type;
        ASTNode *node = create_node(parser, NODE_UNARY_OP);
        char *op_str;
        switch (op) {
            case TOKEN_MINUS: op_str = "-"; break;
            case TOKEN_BANG: op_str = "!"; break;
            case TOKEN_PLUS_PLUS: op_str = "++"; break;
            case TOKEN_MINUS_MINUS: op_str = "--"; break;
            case TOKEN_TILDE: op_str = "~"; break;
            default: op_str = "";
        }
        node->data.unary_op.op = strdup(op_str);
        node->data.unary_op.operand = parse_unary(parser);
        return node;
    }
    return parse_postfix(parser);
}

static ASTNode *parse_multiplicative(Parser *parser) {
    ASTNode *left = parse_unary(parser);
    
    while (match(parser, TOKEN_STAR) || match(parser, TOKEN_SLASH) || 
           match(parser, TOKEN_MOD)) {
        ASTNode *node = create_node(parser, NODE_BINARY_OP);
        TokenType op = parser->previous->type;
        char *op_str = (op == TOKEN_STAR) ? "*" : (op == TOKEN_SLASH) ? "/" : "%";
        node->data.binary_op.op = strdup(op_str);
        node->data.binary_op.left = left;
        node->data.binary_op.right = parse_unary(parser);
        left = node;
    }
    
    return left;
}

static ASTNode *parse_additive(Parser *parser) {
    ASTNode *left = parse_multiplicative(parser);
    
    while (match(parser, TOKEN_PLUS) || match(parser, TOKEN_MINUS)) {
        ASTNode *node = create_node(parser, NODE_BINARY_OP);
        TokenType op = parser->previous->type;
        char *op_str = (op == TOKEN_PLUS) ? "+" : "-";
        node->data.binary_op.op = strdup(op_str);
        node->data.binary_op.left = left;
        node->data.binary_op.right = parse_multiplicative(parser);
        left = node;
    }
    
    return left;
}

static ASTNode *parse_shift(Parser *parser) {
    ASTNode *left = parse_additive(parser);
    while (match(parser, TOKEN_LT_LT) || match(parser, TOKEN_GT_GT)) {
        ASTNode *node = create_node(parser, NODE_BINARY_OP);
        TokenType op = parser->previous->type;
        node->data.binary_op.op = strdup((op == TOKEN_LT_LT) ? "<<" : ">>");
        node->data.binary_op.left = left;
        node->data.binary_op.right = parse_additive(parser);
        left = node;
    }
    return left;
}

static ASTNode *parse_comparison(Parser *parser) {
    ASTNode *left = parse_shift(parser);
    
    while (match(parser, TOKEN_LT) || match(parser, TOKEN_GT) || 
           match(parser, TOKEN_LT_EQ) || match(parser, TOKEN_GT_EQ)) {
        ASTNode *node = create_node(parser, NODE_BINARY_OP);
        TokenType op = parser->previous->type;
        char *op_str;
        switch (op) {
            case TOKEN_LT: op_str = "<"; break;
            case TOKEN_GT: op_str = ">"; break;
            case TOKEN_LT_EQ: op_str = "<="; break;
            case TOKEN_GT_EQ: op_str = ">="; break;
            default: op_str = "";
        }
        node->data.binary_op.op = strdup(op_str);
        node->data.binary_op.left = left;
        node->data.binary_op.right = parse_additive(parser);
        left = node;
    }
    
    return left;
}

static ASTNode *parse_equality(Parser *parser) {
    ASTNode *left = parse_comparison(parser);
    
    while (match(parser, TOKEN_EQ_EQ) || match(parser, TOKEN_BANG_EQ)) {
        ASTNode *node = create_node(parser, NODE_BINARY_OP);
        TokenType op = parser->previous->type;
        node->data.binary_op.op = strdup((op == TOKEN_EQ_EQ) ? "==" : "!=");
        node->data.binary_op.left = left;
        node->data.binary_op.right = parse_comparison(parser);
        left = node;
    }
    
    return left;
}

static ASTNode *parse_bitwise_and(Parser *parser) {
    ASTNode *left = parse_equality(parser);
    while (match(parser, TOKEN_AMP)) {
        ASTNode *node = create_node(parser, NODE_BINARY_OP);
        node->data.binary_op.op = strdup("&");
        node->data.binary_op.left = left;
        node->data.binary_op.right = parse_equality(parser);
        left = node;
    }
    return left;
}

static ASTNode *parse_bitwise_xor(Parser *parser) {
    ASTNode *left = parse_bitwise_and(parser);
    while (match(parser, TOKEN_CARET)) {
        ASTNode *node = create_node(parser, NODE_BINARY_OP);
        node->data.binary_op.op = strdup("^");
        node->data.binary_op.left = left;
        node->data.binary_op.right = parse_bitwise_and(parser);
        left = node;
    }
    return left;
}

static ASTNode *parse_bitwise_or(Parser *parser) {
    ASTNode *left = parse_bitwise_xor(parser);
    while (match(parser, TOKEN_PIPE)) {
        ASTNode *node = create_node(parser, NODE_BINARY_OP);
        node->data.binary_op.op = strdup("|");
        node->data.binary_op.left = left;
        node->data.binary_op.right = parse_bitwise_xor(parser);
        left = node;
    }
    return left;
}

static ASTNode *parse_logical_and(Parser *parser) {
    ASTNode *left = parse_bitwise_or(parser);
    
    while (match(parser, TOKEN_AMP_AMP)) {
        ASTNode *node = create_node(parser, NODE_BINARY_OP);
        node->data.binary_op.op = strdup("&&");
        node->data.binary_op.left = left;
        node->data.binary_op.right = parse_bitwise_or(parser);
        left = node;
    }
    
    return left;
}

static ASTNode *parse_logical_or(Parser *parser) {
    ASTNode *left = parse_logical_and(parser);
    
    while (match(parser, TOKEN_PIPE_PIPE)) {
        ASTNode *node = create_node(parser, NODE_BINARY_OP);
        node->data.binary_op.op = strdup("||");
        node->data.binary_op.left = left;
        node->data.binary_op.right = parse_logical_and(parser);
        left = node;
    }
    
    return left;
}

static int is_assignable(ASTNode *expr) {
    return expr->type == NODE_IDENTIFIER ||
           expr->type == NODE_MEMBER_ACCESS ||
           expr->type == NODE_INDEX;
}

static ASTNode *parse_assignment(Parser *parser) {
    ASTNode *expr = parse_logical_or(parser);
    if (match(parser, TOKEN_EQ)) {
        if (!is_assignable(expr)) {
            report_error(parser->lexer, parser->current, "assignable expression");
            parser->had_error = 1;
            return expr;
        }
        ASTNode *node = create_node(parser, NODE_ASSIGNMENT);
        node->data.assign.target = expr;
        node->data.assign.value = parse_assignment(parser);
        return node;
    }
    if (match(parser, TOKEN_PLUS_EQ) || match(parser, TOKEN_MINUS_EQ) ||
        match(parser, TOKEN_STAR_EQ) || match(parser, TOKEN_SLASH_EQ) ||
        match(parser, TOKEN_MOD_EQ) || match(parser, TOKEN_AMP_EQ) ||
        match(parser, TOKEN_PIPE_EQ) || match(parser, TOKEN_CARET_EQ)) {
        if (!is_assignable(expr)) {
            report_error(parser->lexer, parser->current, "assignable expression");
            parser->had_error = 1;
            return expr;
        }
        TokenType op = parser->previous->type;
        const char *op_str;
        switch (op) {
            case TOKEN_PLUS_EQ:  op_str = "+"; break;
            case TOKEN_MINUS_EQ: op_str = "-"; break;
            case TOKEN_STAR_EQ:  op_str = "*"; break;
            case TOKEN_SLASH_EQ: op_str = "/"; break;
            case TOKEN_MOD_EQ:   op_str = "%"; break;
            case TOKEN_AMP_EQ:   op_str = "&"; break;
            case TOKEN_PIPE_EQ:  op_str = "|"; break;
            case TOKEN_CARET_EQ: op_str = "^"; break;
            default:             op_str = ""; break;
        }
        ASTNode *rhs = parse_assignment(parser);
        ASTNode *bin = create_node(parser, NODE_BINARY_OP);
        bin->data.binary_op.op = strdup(op_str);
        bin->data.binary_op.left = expr;
        bin->data.binary_op.right = rhs;
        ASTNode *node = create_node(parser, NODE_ASSIGNMENT);
        node->data.assign.target = expr;
        node->data.assign.value = bin;
        return node;
    }
    return expr;
}

static ASTNode *parse_expression(Parser *parser) {
    return parse_assignment(parser);
}

static ASTNode *parse_var_decl(Parser *parser, TokenType type) {
    ASTNode *node = create_node(parser, NODE_VAR_DECL);
    
    // Determine type from token
    switch (type) {
        case TOKEN_INT: node->data.var_decl.var_type = strdup("int"); break;
        case TOKEN_S8: node->data.var_decl.var_type = strdup("s8"); break;
        case TOKEN_S16: node->data.var_decl.var_type = strdup("s16"); break;
        case TOKEN_S32: node->data.var_decl.var_type = strdup("s32"); break;
        case TOKEN_S64: node->data.var_decl.var_type = strdup("s64"); break;
        case TOKEN_U8: node->data.var_decl.var_type = strdup("u8"); break;
        case TOKEN_U16: node->data.var_decl.var_type = strdup("u16"); break;
        case TOKEN_U32: node->data.var_decl.var_type = strdup("u32"); break;
        case TOKEN_U64: node->data.var_decl.var_type = strdup("u64"); break;
        case TOKEN_BOOL: node->data.var_decl.var_type = strdup("bool"); break;
        case TOKEN_GLASS: node->data.var_decl.var_type = strdup("glass"); break;
        case TOKEN_MATRIX: node->data.var_decl.var_type = strdup("matrix"); break;
        case TOKEN_CHAR: {
            int char_size = parse_type_size(parser);
            if (char_size == 1)
                node->data.var_decl.var_type = strdup("char");
            else {
                char size_str[16];
                snprintf(size_str, sizeof(size_str), "char%d", char_size);
                node->data.var_decl.var_type = strdup(size_str);
            }
            break;
        }
        case TOKEN_VAR: node->data.var_decl.var_type = strdup("var"); break;
        default: node->data.var_decl.var_type = strdup("var"); break;
    }
    
    // Variable name
    if (parser->current->type == TOKEN_IDENTIFIER) {
        advance(parser);
        node->data.var_decl.var_name = strdup(parser->previous->text);
    } else {
        report_error(parser->lexer, parser->current, "identifier");
        parser->had_error = 1;
        free(node);
        return NULL;
    }
    
    // Array size {{N}}
    if (match(parser, TOKEN_LBRACE2)) {
        if (parser->current->type == TOKEN_NUMBER) {
            advance(parser);
            node->data.var_decl.array_size = (int)strtod(parser->previous->text, NULL);
        } else {
            report_error(parser->lexer, parser->current, "array size");
            parser->had_error = 1;
        }
        consume(parser, TOKEN_RBRACE2, "}}");
    }
    
    // Optional initializer
    if (match(parser, TOKEN_EQ)) {
        node->data.var_decl.init = parse_expression(parser);
    }
    
    consume(parser, TOKEN_SEMICOLON, ";");
    return node;
}

static ASTNode *parse_if_stmt(Parser *parser) {
    ASTNode *node = create_node(parser, NODE_IF_STMT);
    int is_kern = parser->previous->type == TOKEN_KERNIF;
    node->data.if_stmt.is_kern = is_kern;
    
    if (!consume(parser, TOKEN_LPAREN, "(")) {
        free(node);
        return NULL;
    }
    node->data.if_stmt.condition = parse_expression(parser);
    if (!node->data.if_stmt.condition) {
        free(node);
        return NULL;
    }
    if (!consume(parser, TOKEN_RPAREN, ")")) {
        free(node);
        return NULL;
    }
    
    node->data.if_stmt.then_branch = parse_statement(parser);
    if (!node->data.if_stmt.then_branch) {
        free(node);
        return NULL;
    }
    
    if (match(parser, TOKEN_KERNELSE) || match(parser, TOKEN_ELSE)) {
        node->data.if_stmt.else_branch = parse_statement(parser);
        if (!node->data.if_stmt.else_branch) {
            free(node);
            return NULL;
        }
        // kernelse forces the whole node to be kernel-level
        if (parser->previous->type == TOKEN_KERNELSE)
            node->data.if_stmt.is_kern = 1;
    }
    
    return node;
}

static ASTNode *parse_while_stmt(Parser *parser) {
    ASTNode *node = create_node(parser, NODE_WHILE_STMT);
    
    if (!consume(parser, TOKEN_LPAREN, "(")) {
        free(node);
        return NULL;
    }
    node->data.if_stmt.condition = parse_expression(parser);
    if (!node->data.if_stmt.condition) {
        free(node);
        return NULL;
    }
    if (!consume(parser, TOKEN_RPAREN, ")")) {
        free(node);
        return NULL;
    }
    
    node->data.if_stmt.then_branch = parse_statement(parser);
    if (!node->data.if_stmt.then_branch) {
        free(node);
        return NULL;
    }
    
    return node;
}

static ASTNode *parse_for_stmt(Parser *parser) {
    ASTNode *node = create_node(parser, NODE_FOR_STMT);
    node->data.for_loop.init = NULL;
    node->data.for_loop.condition = NULL;
    node->data.for_loop.increment = NULL;
    node->data.for_loop.body = NULL;
    
    if (!consume(parser, TOKEN_LPAREN, "(")) {
        free(node);
        return NULL;
    }
    // Init
    if (parser->current->type != TOKEN_SEMICOLON) {
        TokenType decl_type = 0;
        if (is_type_token(parser->current->type)) {
            if (match(parser, TOKEN_INT)) decl_type = TOKEN_INT;
            else if (match(parser, TOKEN_S8)) decl_type = TOKEN_S8;
            else if (match(parser, TOKEN_S16)) decl_type = TOKEN_S16;
            else if (match(parser, TOKEN_S32)) decl_type = TOKEN_S32;
            else if (match(parser, TOKEN_S64)) decl_type = TOKEN_S64;
            else if (match(parser, TOKEN_U8)) decl_type = TOKEN_U8;
            else if (match(parser, TOKEN_U16)) decl_type = TOKEN_U16;
            else if (match(parser, TOKEN_U32)) decl_type = TOKEN_U32;
            else if (match(parser, TOKEN_U64)) decl_type = TOKEN_U64;
            else if (match(parser, TOKEN_BOOL)) decl_type = TOKEN_BOOL;
            else if (match(parser, TOKEN_GLASS)) decl_type = TOKEN_GLASS;
            else if (match(parser, TOKEN_MATRIX)) decl_type = TOKEN_MATRIX;
            else if (match(parser, TOKEN_VAR)) decl_type = TOKEN_VAR;
            else if (match(parser, TOKEN_CHAR)) decl_type = TOKEN_CHAR;
        }
        if (decl_type)
            node->data.for_loop.init = parse_var_decl(parser, decl_type);
        else
            node->data.for_loop.init = parse_expression(parser);
        if (!node->data.for_loop.init) {
            free(node);
            return NULL;
        }
    }
    consume(parser, TOKEN_SEMICOLON, ";");
    
    // Condition
    if (parser->current->type != TOKEN_SEMICOLON) {
        node->data.for_loop.condition = parse_expression(parser);
        if (!node->data.for_loop.condition) {
            free(node);
            return NULL;
        }
    }
    consume(parser, TOKEN_SEMICOLON, ";");
    
    // Increment
    if (parser->current->type != TOKEN_RPAREN) {
        node->data.for_loop.increment = parse_expression(parser);
        if (!node->data.for_loop.increment) {
            free(node);
            return NULL;
        }
    }
    if (!consume(parser, TOKEN_RPAREN, ")")) {
        free(node);
        return NULL;
    }
    
    node->data.for_loop.body = parse_statement(parser);
    if (!node->data.for_loop.body) {
        free(node);
        return NULL;
    }
    
    return node;
}

static ASTNode *parse_return_stmt(Parser *parser) {
    ASTNode *node = create_node(parser, NODE_RETURN_STMT);
    
    // Optional return value
    if (parser->current->type != TOKEN_SEMICOLON) {
        node->data.return_stmt.value = parse_expression(parser);
        if (!node->data.return_stmt.value) {
            free(node);
            return NULL;
        }
    } else {
        node->data.return_stmt.value = NULL;
    }
    
    consume(parser, TOKEN_SEMICOLON, ";");
    return node;
}

static ASTNode *parse_expression_stmt(Parser *parser) {
    ASTNode *node = create_node(parser, NODE_EXPRESSION_STMT);
    
    TokenType t = parser->current->type;
    if (t != TOKEN_IDENTIFIER && t != TOKEN_NUMBER && t != TOKEN_STRING && 
        t != TOKEN_LPAREN && t != TOKEN_MINUS && t != TOKEN_PLUS_PLUS &&
        t != TOKEN_MINUS_MINUS && t != TOKEN_BANG &&
        t != TOKEN_PRINT && t != TOKEN_INPUT &&
        t != TOKEN_THIS && t != TOKEN_LBRACE2 &&
        t != TOKEN_TRUE && t != TOKEN_FALSE) {
        report_error(parser->lexer, parser->current, "expression");
        free(node);
        return NULL;
    }
    
    node->data.expr_stmt.expr = parse_expression(parser);
    if (!node->data.expr_stmt.expr) {
        free(node);
        return NULL;
    }
    
    if (!consume(parser, TOKEN_SEMICOLON, ";")) {
        free(node);
        return NULL;
    }
    return node;
}

static ASTNode *parse_statement(Parser *parser) {
    // Handle special keywords that are not type tokens
    if (match(parser, TOKEN_EXL)) {
        return parse_exl_function(parser);
    }
    
    // Check for variable declaration (but NOT function definition)
    TokenType current_type = parser->current->type;
    TokenType decl_type = 0;
    
    if (is_type_token(current_type)) {
        // Peek ahead to distinguish var decl from function: TYPE ID ( = fn
        // We save and restore lexer position to peek without consuming
        Lexer *lex = parser->lexer;
        int saved_pos = lex->pos;
        int saved_line = lex->line;
        int saved_col = lex->column;
        Token *peek1 = lexer_next_token(lex);
        if (peek1 && peek1->type == TOKEN_LT) {
            free(peek1);
            peek1 = lexer_next_token(lex);
            if (peek1 && peek1->type == TOKEN_NUMBER) {
                free(peek1);
                peek1 = lexer_next_token(lex);
                if (peek1 && peek1->type == TOKEN_GT) {
                    free(peek1);
                    peek1 = lexer_next_token(lex);
                }
            }
        }
        int is_fn = 0;
        if (peek1 && peek1->type == TOKEN_IDENTIFIER) {
            Token *peek2 = lexer_next_token(lex);
            if (peek2 && peek2->type == TOKEN_LPAREN)
                is_fn = 1;
            free(peek2);
        }
        free(peek1);
        lex->pos = saved_pos;
        lex->line = saved_line;
        lex->column = saved_col;
        
        if (is_fn) {
            return parse_function(parser);
        }
        
        // It's a var decl - consume the type
        if (match(parser, TOKEN_INT)) decl_type = TOKEN_INT;
        else if (match(parser, TOKEN_S8)) decl_type = TOKEN_S8;
        else if (match(parser, TOKEN_S16)) decl_type = TOKEN_S16;
        else if (match(parser, TOKEN_S32)) decl_type = TOKEN_S32;
        else if (match(parser, TOKEN_S64)) decl_type = TOKEN_S64;
        else if (match(parser, TOKEN_U8)) decl_type = TOKEN_U8;
        else if (match(parser, TOKEN_U16)) decl_type = TOKEN_U16;
        else if (match(parser, TOKEN_U32)) decl_type = TOKEN_U32;
        else if (match(parser, TOKEN_U64)) decl_type = TOKEN_U64;
        else if (match(parser, TOKEN_BOOL)) decl_type = TOKEN_BOOL;
        else if (match(parser, TOKEN_GLASS)) decl_type = TOKEN_GLASS;
        else if (match(parser, TOKEN_MATRIX)) decl_type = TOKEN_MATRIX;
        else if (match(parser, TOKEN_VAR)) decl_type = TOKEN_VAR;
        else if (match(parser, TOKEN_CHAR)) decl_type = TOKEN_CHAR;
        
        if (decl_type != 0) {
            return parse_var_decl(parser, decl_type);
        }
    } else {
        if (match(parser, TOKEN_INT)) decl_type = TOKEN_INT;
        else if (match(parser, TOKEN_S8)) decl_type = TOKEN_S8;
        else if (match(parser, TOKEN_S16)) decl_type = TOKEN_S16;
        else if (match(parser, TOKEN_S32)) decl_type = TOKEN_S32;
        else if (match(parser, TOKEN_S64)) decl_type = TOKEN_S64;
        else if (match(parser, TOKEN_U8)) decl_type = TOKEN_U8;
        else if (match(parser, TOKEN_U16)) decl_type = TOKEN_U16;
        else if (match(parser, TOKEN_U32)) decl_type = TOKEN_U32;
        else if (match(parser, TOKEN_U64)) decl_type = TOKEN_U64;
        else if (match(parser, TOKEN_BOOL)) decl_type = TOKEN_BOOL;
        else if (match(parser, TOKEN_GLASS)) decl_type = TOKEN_GLASS;
        else if (match(parser, TOKEN_MATRIX)) decl_type = TOKEN_MATRIX;
        else if (match(parser, TOKEN_VAR)) decl_type = TOKEN_VAR;
        else if (match(parser, TOKEN_CHAR)) decl_type = TOKEN_CHAR;
        
        if (decl_type != 0) {
            return parse_var_decl(parser, decl_type);
        }
    }
    
    // User-defined type variable declaration: TypeName var_name;
    if (parser->current->type == TOKEN_IDENTIFIER) {
        Lexer *lex = parser->lexer;
        int saved_pos = lex->pos;
        int saved_line = lex->line;
        int saved_col = lex->column;
        Token *peek1 = lexer_next_token(lex);
        int is_var = 0;
        if (peek1 && peek1->type == TOKEN_IDENTIFIER) {
            Token *peek2 = lexer_next_token(lex);
            if (peek2 && peek2->type != TOKEN_LPAREN) {
                is_var = 1;
            }
            free(peek2);
        }
        free(peek1);
        
        if (is_var) {
            lex->pos = saved_pos;
            lex->line = saved_line;
            lex->column = saved_col;
            
            advance(parser); // consume type name
            char *type_name = strdup(parser->previous->text);
            
            ASTNode *node = create_node(parser, NODE_VAR_DECL);
            node->data.var_decl.var_type = type_name;
            node->data.var_decl.array_size = 0;
            node->data.var_decl.init = NULL;
            
            if (parser->current->type == TOKEN_IDENTIFIER) {
                advance(parser);
                node->data.var_decl.var_name = strdup(parser->previous->text);
            } else {
                report_error(parser->lexer, parser->current, "variable name");
                parser->had_error = 1;
                free(node);
                return NULL;
            }
            
            // Array size {{N}}
            if (match(parser, TOKEN_LBRACE2)) {
                if (parser->current->type == TOKEN_NUMBER) {
                    advance(parser);
                    node->data.var_decl.array_size = (int)strtod(parser->previous->text, NULL);
                } else {
                    report_error(parser->lexer, parser->current, "array size");
                    parser->had_error = 1;
                }
                consume(parser, TOKEN_RBRACE2, "}}");
            }
            
            if (match(parser, TOKEN_EQ)) {
                node->data.var_decl.init = parse_expression(parser);
            }
            
            consume(parser, TOKEN_SEMICOLON, ";");
            return node;
        }
        
        lex->pos = saved_pos;
        lex->line = saved_line;
        lex->column = saved_col;
    }
    
    if (match(parser, TOKEN_IF) || match(parser, TOKEN_KERNIF)) {
        return parse_if_stmt(parser);
    }
    
    if (match(parser, TOKEN_RETURN)) {
        return parse_return_stmt(parser);
    }
    
    if (match(parser, TOKEN_WHILE)) {
        return parse_while_stmt(parser);
    }
    
    if (match(parser, TOKEN_FOR)) {
        return parse_for_stmt(parser);
    }
    
    if (match(parser, TOKEN_LBRACE)) {
        return parse_block(parser);
    }
    
    if (match(parser, TOKEN_CLASS)) {
        ASTNode *node = create_node(parser, NODE_CLASS);
        if (parser->current->type == TOKEN_IDENTIFIER) {
            advance(parser);
            node->data.class_decl.name = strdup(parser->previous->text);
        } else {
            report_error(parser->lexer, parser->current, "class name");
            parser->had_error = 1;
            free(node);
            return NULL;
        }
        if (!consume(parser, TOKEN_LBRACE, "{")) {
            free(node);
            return NULL;
        }
        return parse_class_body(parser, node);
    }
    
    if (match(parser, TOKEN_STRUCT)) {
        ASTNode *node = create_node(parser, NODE_STRUCT);
        if (parser->current->type == TOKEN_IDENTIFIER) {
            advance(parser);
            node->data.class_decl.name = strdup(parser->previous->text);
        } else {
            report_error(parser->lexer, parser->current, "struct name");
            parser->had_error = 1;
            free(node);
            return NULL;
        }
        if (!consume(parser, TOKEN_LBRACE, "{")) {
            free(node);
            return NULL;
        }
        return parse_class_body(parser, node);
    }
    
    return parse_expression_stmt(parser);
}

static ASTNode *parse_block(Parser *parser) {
    ASTNode *node = create_node(parser, NODE_BLOCK);
    node->data.block.stmts = NULL;
    node->data.block.stmt_count = 0;
    
    // We already consumed '{' before entering this function
    // Now consume everything until '}'
    while (parser->current->type != TOKEN_RBRACE && parser->current->type != TOKEN_EOF) {
        ASTNode *stmt = parse_statement(parser);
        if (!stmt) {
            free(node);
            return NULL;
        }
        node->data.block.stmt_count++;
        node->data.block.stmts = realloc(node->data.block.stmts, node->data.block.stmt_count * sizeof(ASTNode*));
        node->data.block.stmts[node->data.block.stmt_count-1] = stmt;
    }
    
    if (!consume(parser, TOKEN_RBRACE, "}")) {
        free(node);
        return NULL;
    }
    return node;
}

static ASTNode *parse_class_body(Parser *parser, ASTNode *node) {
    node->data.class_decl.fields = NULL;
    node->data.class_decl.field_count = 0;
    node->data.class_decl.methods = NULL;
    node->data.class_decl.method_count = 0;

    while (parser->current->type != TOKEN_RBRACE && parser->current->type != TOKEN_EOF) {
        TokenType t = parser->current->type;
        if (is_type_token(t) || t == TOKEN_GLASS || t == TOKEN_EXL) {
            Lexer *lex = parser->lexer;
            int saved_pos = lex->pos;
            int saved_line = lex->line;
            int saved_col = lex->column;

            if (t == TOKEN_EXL) {
                advance(parser);
                t = parser->current->type;
                if (!is_type_token(t) && t != TOKEN_GLASS) {
                    lex->pos = saved_pos;
                    lex->line = saved_line;
                    lex->column = saved_col;
                    report_error(parser->lexer, parser->current, "type");
                    parser->had_error = 1;
                    free(node);
                    return NULL;
                }
            }

            Token *peek1 = lexer_next_token(lex);
            int is_fn = 0;
            if (peek1 && peek1->type == TOKEN_IDENTIFIER) {
                Token *peek2 = lexer_next_token(lex);
                if (peek2 && peek2->type == TOKEN_LPAREN)
                    is_fn = 1;
                free(peek2);
            }
            free(peek1);
            lex->pos = saved_pos;
            lex->line = saved_line;
            lex->column = saved_col;

            if (is_fn) {
                ASTNode *method = parse_function(parser);
                if (!method) {
                    free(node);
                    return NULL;
                }
                node->data.class_decl.method_count++;
                node->data.class_decl.methods = realloc(node->data.class_decl.methods,
                    node->data.class_decl.method_count * sizeof(ASTNode*));
                node->data.class_decl.methods[node->data.class_decl.method_count-1] = method;
            } else {
                ASTNode *field = parse_statement(parser);
                if (!field) {
                    free(node);
                    return NULL;
                }
                node->data.class_decl.field_count++;
                node->data.class_decl.fields = realloc(node->data.class_decl.fields,
                    node->data.class_decl.field_count * sizeof(ASTNode*));
                node->data.class_decl.fields[node->data.class_decl.field_count-1] = field;
            }
        } else {
            ASTNode *stmt = parse_statement(parser);
            if (!stmt) {
                free(node);
                return NULL;
            }
            node->data.class_decl.method_count++;
            node->data.class_decl.methods = realloc(node->data.class_decl.methods,
                node->data.class_decl.method_count * sizeof(ASTNode*));
            node->data.class_decl.methods[node->data.class_decl.method_count-1] = stmt;
        }
    }

    if (!consume(parser, TOKEN_RBRACE, "}")) {
        free(node);
        return NULL;
    }
    return node;
}

static int parse_type_size(Parser *parser) {
    if (parser->current->type == TOKEN_LT) {
        advance(parser);
        if (parser->current->type != TOKEN_NUMBER) {
            report_error(parser->lexer, parser->current, "number");
            return 1;
        }
        int size = atoi(parser->current->text);
        advance(parser);
        if (parser->current->type != TOKEN_GT) {
            report_error(parser->lexer, parser->current, "'>'");
            return 1;
        }
        advance(parser);
        return size < 1 ? 1 : size;
    }
    return 1;
}

static ASTNode *parse_param(Parser *parser) {
    // Parse a single function parameter: <type> <name>
    TokenType ptype = parser->current->type;
    if (ptype != TOKEN_INT && ptype != TOKEN_BOOL && ptype != TOKEN_GLASS &&
        ptype != TOKEN_MATRIX && ptype != TOKEN_VAR &&
        ptype != TOKEN_S8 && ptype != TOKEN_S16 && ptype != TOKEN_S32 && ptype != TOKEN_S64 &&
        ptype != TOKEN_U8 && ptype != TOKEN_U16 && ptype != TOKEN_U32 && ptype != TOKEN_U64 &&
        ptype != TOKEN_CHAR) {
        return NULL;
    }
    advance(parser);
    
    ASTNode *node = create_node(parser, NODE_VAR_DECL);
    switch (ptype) {
        case TOKEN_INT:    node->data.var_decl.var_type = strdup("int"); break;
        case TOKEN_S8:     node->data.var_decl.var_type = strdup("s8"); break;
        case TOKEN_S16:    node->data.var_decl.var_type = strdup("s16"); break;
        case TOKEN_S32:    node->data.var_decl.var_type = strdup("s32"); break;
        case TOKEN_S64:    node->data.var_decl.var_type = strdup("s64"); break;
        case TOKEN_U8:     node->data.var_decl.var_type = strdup("u8"); break;
        case TOKEN_U16:    node->data.var_decl.var_type = strdup("u16"); break;
        case TOKEN_U32:    node->data.var_decl.var_type = strdup("u32"); break;
        case TOKEN_U64:    node->data.var_decl.var_type = strdup("u64"); break;
        case TOKEN_BOOL:   node->data.var_decl.var_type = strdup("bool"); break;
        case TOKEN_GLASS:  node->data.var_decl.var_type = strdup("glass"); break;
        case TOKEN_MATRIX: node->data.var_decl.var_type = strdup("matrix"); break;
        case TOKEN_CHAR: {
            int char_size = parse_type_size(parser);
            if (char_size == 1)
                node->data.var_decl.var_type = strdup("char");
            else {
                char size_str[16];
                snprintf(size_str, sizeof(size_str), "char%d", char_size);
                node->data.var_decl.var_type = strdup(size_str);
            }
            break;
        }
        default:           node->data.var_decl.var_type = strdup("var"); break;
    }
    
    if (parser->current->type == TOKEN_IDENTIFIER) {
        advance(parser);
        node->data.var_decl.var_name = strdup(parser->previous->text);
    } else {
        report_error(parser->lexer, parser->current, "identifier");
        parser->had_error = 1;
        free(node);
        return NULL;
    }
    
    node->data.var_decl.init = NULL;
    return node;
}

static ASTNode *parse_function(Parser *parser) {
    ASTNode *node = create_node(parser, NODE_FUNCTION);
    
     // Return type
     if (match(parser, TOKEN_INT)) node->data.function.return_type = strdup("int");
     else if (match(parser, TOKEN_S8)) node->data.function.return_type = strdup("s8");
     else if (match(parser, TOKEN_S16)) node->data.function.return_type = strdup("s16");
     else if (match(parser, TOKEN_S32)) node->data.function.return_type = strdup("s32");
     else if (match(parser, TOKEN_S64)) node->data.function.return_type = strdup("s64");
     else if (match(parser, TOKEN_U8)) node->data.function.return_type = strdup("u8");
     else if (match(parser, TOKEN_U16)) node->data.function.return_type = strdup("u16");
     else if (match(parser, TOKEN_U32)) node->data.function.return_type = strdup("u32");
     else if (match(parser, TOKEN_U64)) node->data.function.return_type = strdup("u64");
     else if (match(parser, TOKEN_GLASS)) node->data.function.return_type = strdup("glass");
     else if (match(parser, TOKEN_BOOL)) node->data.function.return_type = strdup("bool");
     else if (match(parser, TOKEN_CHAR)) {
          int char_size = parse_type_size(parser);
          if (char_size == 1)
              node->data.function.return_type = strdup("char");
          else {
               char size_str[16];
               snprintf(size_str, sizeof(size_str), "char%d", char_size);
               node->data.function.return_type = strdup(size_str);
          }
      }
      else if (match(parser, TOKEN_EXL)) node->data.function.return_type = strdup("exl");
     else node->data.function.return_type = strdup("glass");
    
    // Function name
    if (match(parser, TOKEN_IDENTIFIER)) {
        node->data.function.name = strdup(parser->previous->text);
    }
    
    // Parameters
    node->data.function.params = NULL;
    node->data.function.param_count = 0;
    if (!consume(parser, TOKEN_LPAREN, "(")) {
        free(node);
        return NULL;
    }
    if (parser->current->type != TOKEN_RPAREN) {
        do {
            ASTNode *param = parse_param(parser);
            if (!param) {
                free(node);
                return NULL;
            }
            node->data.function.param_count++;
            node->data.function.params = realloc(node->data.function.params,
                node->data.function.param_count * sizeof(ASTNode*));
            node->data.function.params[node->data.function.param_count - 1] = param;
        } while (match(parser, TOKEN_COMMA));
    }
    consume(parser, TOKEN_RPAREN, ")");
    
     // Body
     node->data.function.body = parse_statement(parser);
     if (!node->data.function.body) {
         free(node);
         return NULL;
     }
      return node;
 }

static ASTNode *parse_exl_function(Parser *parser) {
    ASTNode *node = create_node(parser, NODE_FUNCTION);
    
    // For exl functions, they are for temporary RAM highlighting
    node->data.function.is_exl = 1;
    node->data.function.return_type = strdup("glass");
    
    // Function name
    if (match(parser, TOKEN_IDENTIFIER)) {
        node->data.function.name = strdup(parser->previous->text);
    }
    
    // Parameters
    node->data.function.params = NULL;
    node->data.function.param_count = 0;
    if (!consume(parser, TOKEN_LPAREN, "(")) {
        free(node);
        return NULL;
    }
    if (parser->current->type != TOKEN_RPAREN) {
        do {
            ASTNode *param = parse_param(parser);
            if (!param) {
                free(node);
                return NULL;
            }
            node->data.function.param_count++;
            node->data.function.params = realloc(node->data.function.params,
                node->data.function.param_count * sizeof(ASTNode*));
            node->data.function.params[node->data.function.param_count - 1] = param;
        } while (match(parser, TOKEN_COMMA));
    }
    consume(parser, TOKEN_RPAREN, ")");
    
    // Body
    node->data.function.body = parse_statement(parser);
    if (!node->data.function.body) {
        free(node);
        return NULL;
    }
    
    return node;
}



 Parser *parser_new(Lexer *lexer) {
 
    Parser *parser = calloc(1, sizeof(Parser));
    parser->lexer = lexer;
    advance(parser);
    return parser;
}

static void free_ast_node(ASTNode *node) {
    if (!node) return;
    switch (node->type) {
        case NODE_FUNCTION:
            free(node->data.function.name);
            free(node->data.function.return_type);
            for (int i = 0; i < node->data.function.param_count; i++)
                free_ast_node(node->data.function.params[i]);
            free(node->data.function.params);
            free_ast_node(node->data.function.body);
            break;
        case NODE_BLOCK:
        case NODE_PROGRAM:
            for (int i = 0; i < node->data.block.stmt_count; i++)
                free_ast_node(node->data.block.stmts[i]);
            free(node->data.block.stmts);
            break;
        case NODE_IF_STMT:
            free_ast_node(node->data.if_stmt.condition);
            free_ast_node(node->data.if_stmt.then_branch);
            free_ast_node(node->data.if_stmt.else_branch);
            break;
        case NODE_FOR_STMT:
            free_ast_node(node->data.for_loop.init);
            free_ast_node(node->data.for_loop.condition);
            free_ast_node(node->data.for_loop.increment);
            free_ast_node(node->data.for_loop.body);
            break;
        case NODE_WHILE_STMT:
            free_ast_node(node->data.if_stmt.condition);
            free_ast_node(node->data.if_stmt.then_branch);
            break;
        case NODE_RETURN_STMT:
            free_ast_node(node->data.return_stmt.value);
            break;
        case NODE_VAR_DECL:
            free(node->data.var_decl.var_type);
            free(node->data.var_decl.var_name);
            free_ast_node(node->data.var_decl.init);
            break;
        case NODE_ASSIGNMENT:
            free_ast_node(node->data.assign.target);
            free_ast_node(node->data.assign.value);
            break;
        case NODE_BINARY_OP:
            free(node->data.binary_op.op);
            free_ast_node(node->data.binary_op.left);
            free_ast_node(node->data.binary_op.right);
            break;
        case NODE_UNARY_OP:
            free(node->data.unary_op.op);
            free_ast_node(node->data.unary_op.operand);
            break;
        case NODE_CALL:
            free_ast_node(node->data.call.callee);
            for (int i = 0; i < node->data.call.arg_count; i++)
                free_ast_node(node->data.call.args[i]);
            free(node->data.call.args);
            break;
        case NODE_EXPRESSION_STMT:
            free_ast_node(node->data.expr_stmt.expr);
            break;
        case NODE_CLASS:
            free(node->data.class_decl.name);
            for (int i = 0; i < node->data.class_decl.field_count; i++)
                free_ast_node(node->data.class_decl.fields[i]);
            free(node->data.class_decl.fields);
            for (int i = 0; i < node->data.class_decl.method_count; i++)
                free_ast_node(node->data.class_decl.methods[i]);
            free(node->data.class_decl.methods);
            break;
        case NODE_MEMBER_ACCESS:
            free_ast_node(node->data.member_access.object);
            free(node->data.member_access.member);
            break;
        case NODE_INDEX:
            free_ast_node(node->data.array_index.object);
            free_ast_node(node->data.array_index.index);
            break;
        case NODE_ARRAY_LITERAL:
            for (int i = 0; i < node->data.array_literal.elem_count; i++)
                free_ast_node(node->data.array_literal.elements[i]);
            free(node->data.array_literal.elements);
            break;
        case NODE_IDENTIFIER:
            free(node->data.identifier);
            break;
        case NODE_STRING:
            free(node->data.string);
            break;
        default:
            break;
    }
    free(node);
}

void parser_free(Parser *parser) {
    if (!parser) return;
    if (parser->current) free(parser->current);
    if (parser->previous) free(parser->previous);
    if (parser->root) free_ast_node(parser->root);
    free(parser);
}

static int is_type_token(TokenType t) {
    return t == TOKEN_INT || t == TOKEN_BOOL || t == TOKEN_GLASS ||
           t == TOKEN_MATRIX || t == TOKEN_VAR ||
           t == TOKEN_S8 || t == TOKEN_S16 || t == TOKEN_S32 || t == TOKEN_S64 ||
           t == TOKEN_U8 || t == TOKEN_U16 || t == TOKEN_U32 || t == TOKEN_U64 ||
           t == TOKEN_ARRAY || t == TOKEN_MAP || t == TOKEN_STRUCT ||
           t == TOKEN_CHAR;
}

ASTNode *parser_parse(Parser *parser) {
    ASTNode *program = create_node(parser, NODE_PROGRAM);
    program->data.block.stmts = NULL;
    program->data.block.stmt_count = 0;
    parser->root = program;
    
    while (parser->current->type != TOKEN_EOF) {
        if (parser->current->type == TOKEN_USING) {
            advance(parser);
            if (parser->current->type == TOKEN_IDENTIFIER) {
                advance(parser);
            }
            continue;
        }
        
        ASTNode *stmt = parse_statement(parser);
        if (!stmt) {
            fprintf(stderr, "Parsing stopped due to errors\n");
            return NULL;
        }
        
        program->data.block.stmt_count++;
        program->data.block.stmts = realloc(program->data.block.stmts,
            program->data.block.stmt_count * sizeof(ASTNode*));
        program->data.block.stmts[program->data.block.stmt_count - 1] = stmt;
    }
    
    return program;
}

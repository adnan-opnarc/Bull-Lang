/*
 * Bull Compiler - Code Generation
 * LLVM IR generation for Bull programs
 */

#ifndef BULL_CODEGEN_H
#define BULL_CODEGEN_H

#include "parser.h"
#include <llvm-c/Core.h>
#include <llvm-c/ExecutionEngine.h>
#include <llvm-c/Target.h>
#include <llvm-c/Analysis.h>
#include <llvm-c/BitWriter.h>

typedef struct {
    LLVMContextRef context;
    LLVMModuleRef module;
    LLVMBuilderRef builder;
    LLVMPassManagerRef pass_manager;
    LLVMTargetMachineRef target_machine;
    char *target_triple;
    // Built-in functions
    LLVMValueRef printf_func;
    LLVMTypeRef printf_type;
    LLVMValueRef scanf_func;
    LLVMTypeRef scanf_type;
    // Local variable tracking per function
    struct {
        char **names;
        LLVMValueRef *allocs;
        char **types;
        int count;
        int capacity;
    } locals;
    // Current function's return type (set during codegen_function)
    LLVMTypeRef current_return_type;
    // Class type tracking
    struct {
        char **names;
        LLVMTypeRef *struct_types;
        char ***field_names;
        int *field_counts;
        int count;
        int capacity;
    } classes;
    // 'this' pointer for current method (NULL if not in a method)
    LLVMValueRef this_ptr;
    // Current class struct type (NULL if not in a method)
    LLVMTypeRef current_class_type;
    // Method function type lookup: mangled name -> function type
    struct {
        char **names;
        LLVMTypeRef *types;
        int count;
        int capacity;
    } method_types;
} CodeGenContext;

 CodeGenContext *codegen_new(void);
 void codegen_free(CodeGenContext *ctx);
 void codegen_build_ir(CodeGenContext *ctx, ASTNode *ast);
 int codegen_generate(CodeGenContext *ctx, ASTNode *ast, const char *output_file);
 int codegen_emit_object(CodeGenContext *ctx, const char *output_file);
 int codegen_emit_executable(CodeGenContext *ctx, const char *output_file);

#endif // BULL_CODEGEN_H

/*
 * Bull Compiler - LLVM Code Generation
 * Generates LLVM IR from Bull AST
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "parser.h"
#include "codegen.h"
#include "color.h"

// Initialize LLVM native target once
static void llvm_init_native_target(void) {
    // Initialize the native target - required before creating target machine
    LLVMInitializeNativeTarget();
    LLVMInitializeNativeAsmParser();
    LLVMInitializeNativeAsmPrinter();
}

static CodeGenContext *global_ctx = NULL;

// Forward declarations for local variable handling
static void codegen_init_locals(CodeGenContext *ctx);
static void codegen_register_local(CodeGenContext *ctx, const char *name, LLVMValueRef alloca, const char *type_str);
static LLVMValueRef codegen_lookup_local(CodeGenContext *ctx, const char *name);
static void codegen_var_decl(CodeGenContext *ctx, ASTNode *node);

CodeGenContext *codegen_new(void) {
    // Ensure native target is initialized
    llvm_init_native_target();
    
    CodeGenContext *ctx = calloc(1, sizeof(CodeGenContext));
    global_ctx = ctx;
    
    ctx->context = LLVMContextCreate();
    ctx->module = LLVMModuleCreateWithNameInContext("bull_module", ctx->context);
    ctx->builder = LLVMCreateBuilderInContext(ctx->context);
    ctx->pass_manager = LLVMCreatePassManager();
    
    // Set target triple
    ctx->target_triple = LLVMGetDefaultTargetTriple();
    LLVMSetTarget(ctx->module, ctx->target_triple);
    
    // Get target from triple
    LLVMTargetRef target = NULL;
    char *target_name = NULL;
    if (LLVMGetTargetFromTriple(ctx->target_triple, &target, &target_name) != 0) {
        fprintf(stderr, COLOR_RED "Error:" COLOR_RESET " No target available for triple '%s'\n", ctx->target_triple);
        free(ctx->target_triple);
        return ctx;
    }
    
    // Create target machine with default options
    ctx->target_machine = LLVMCreateTargetMachine(
        target,
        ctx->target_triple,
        "generic",      // CPU
        "",             // Features
        LLVMCodeGenLevelDefault,  // Optimization level
        LLVMRelocDefault,         // Relocation model
        LLVMCodeModelDefault      // Code model
    );
    
    free(target_name);
    
    // Set x86_64 System V ABI data layout (hardcoded for now)
    LLVMSetDataLayout(ctx->module, "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128");
    
    // printf(const char *format, ...) -> int (varargs)
    LLVMTypeRef printf_params[] = { LLVMPointerType(LLVMInt8TypeInContext(ctx->context), 0) };
    LLVMTypeRef printf_type = LLVMFunctionType(LLVMInt32TypeInContext(ctx->context), printf_params, 1, 1);
    LLVMValueRef printf_decl = LLVMAddFunction(ctx->module, "printf", printf_type);
    ctx->printf_func = printf_decl;
    ctx->printf_type = printf_type;
    
    // scanf(const char *format, ...) -> int (varargs)
    LLVMTypeRef scanf_params[] = { LLVMPointerType(LLVMInt8TypeInContext(ctx->context), 0) };
    LLVMTypeRef scanf_type = LLVMFunctionType(LLVMInt32TypeInContext(ctx->context), scanf_params, 1, 1);
    LLVMValueRef scanf_decl = LLVMAddFunction(ctx->module, "scanf", scanf_type);
    ctx->scanf_func = scanf_decl;
    ctx->scanf_type = scanf_type;
    
    // Initialize local variable table
    codegen_init_locals(ctx);
    
    // Initialize class table
    ctx->classes.names = NULL;
    ctx->classes.struct_types = NULL;
    ctx->classes.field_names = NULL;
    ctx->classes.field_counts = NULL;
    ctx->classes.count = 0;
    ctx->classes.capacity = 0;
    
    // Initialize this pointer
    ctx->this_ptr = NULL;
    
    // Initialize method type lookup
    ctx->method_types.names = NULL;
    ctx->method_types.types = NULL;
    ctx->method_types.count = 0;
    ctx->method_types.capacity = 0;
    
    return ctx;
}

static void codegen_init_locals(CodeGenContext *ctx) {
    // Initialize local variable tracking (used per function)
    ctx->locals.names = NULL;
    ctx->locals.allocs = NULL;
    ctx->locals.types = NULL;
    ctx->locals.count = 0;
    ctx->locals.capacity = 0;
}

// Register a local variable
static void codegen_register_local(CodeGenContext *ctx, const char *name, LLVMValueRef alloca, const char *type_str) {
    if (ctx->locals.count >= ctx->locals.capacity) {
        ctx->locals.capacity = ctx->locals.capacity ? ctx->locals.capacity * 2 : 4;
        ctx->locals.names = realloc(ctx->locals.names, ctx->locals.capacity * sizeof(char*));
        ctx->locals.allocs = realloc(ctx->locals.allocs, ctx->locals.capacity * sizeof(LLVMValueRef));
        ctx->locals.types = realloc(ctx->locals.types, ctx->locals.capacity * sizeof(char*));
    }
    ctx->locals.names[ctx->locals.count] = strdup(name);
    ctx->locals.allocs[ctx->locals.count] = alloca;
    ctx->locals.types[ctx->locals.count] = type_str ? strdup(type_str) : NULL;
    ctx->locals.count++;
}

// Lookup local variable alloca
static LLVMTypeRef codegen_lookup_method_type(CodeGenContext *ctx, const char *mangled_name) {
    for (int i = 0; i < ctx->method_types.count; i++) {
        if (strcmp(ctx->method_types.names[i], mangled_name) == 0)
            return ctx->method_types.types[i];
    }
    return NULL;
}

static void codegen_register_method_type(CodeGenContext *ctx, const char *mangled_name, LLVMTypeRef func_type) {
    if (ctx->method_types.count >= ctx->method_types.capacity) {
        ctx->method_types.capacity = ctx->method_types.capacity ? ctx->method_types.capacity * 2 : 16;
        ctx->method_types.names = realloc(ctx->method_types.names, ctx->method_types.capacity * sizeof(char *));
        ctx->method_types.types = realloc(ctx->method_types.types, ctx->method_types.capacity * sizeof(LLVMTypeRef));
    }
    ctx->method_types.names[ctx->method_types.count] = strdup(mangled_name);
    ctx->method_types.types[ctx->method_types.count] = func_type;
    ctx->method_types.count++;
}

static LLVMValueRef codegen_lookup_local(CodeGenContext *ctx, const char *name) {
    for (int i = 0; i < ctx->locals.count; i++) {
        if (strcmp(ctx->locals.names[i], name) == 0) {
            return ctx->locals.allocs[i];
        }
    }
    return NULL;
}

static const char *codegen_lookup_local_type(CodeGenContext *ctx, const char *name) {
    for (int i = 0; i < ctx->locals.count; i++) {
        if (strcmp(ctx->locals.names[i], name) == 0) {
            return ctx->locals.types[i];
        }
    }
    return NULL;
}

static LLVMValueRef codegen_expr(CodeGenContext *ctx, ASTNode *node);
static void codegen_stmt(CodeGenContext *ctx, ASTNode *node);

static LLVMTypeRef type_str_to_llvm(CodeGenContext *ctx, const char *type_str) {
    if (strcmp(type_str, "int") == 0 || strcmp(type_str, "s64") == 0 || strcmp(type_str, "u64") == 0)
        return LLVMInt64TypeInContext(ctx->context);
    if (strcmp(type_str, "bool") == 0)
        return LLVMInt1TypeInContext(ctx->context);
    if (strcmp(type_str, "s8") == 0 || strcmp(type_str, "u8") == 0)
        return LLVMInt8TypeInContext(ctx->context);
    if (strcmp(type_str, "s16") == 0 || strcmp(type_str, "u16") == 0)
        return LLVMInt16TypeInContext(ctx->context);
    if (strcmp(type_str, "s32") == 0 || strcmp(type_str, "u32") == 0)
        return LLVMInt32TypeInContext(ctx->context);
    if (strncmp(type_str, "char", 4) == 0) {
        int size = 1;
        if (type_str[4] != '\0') size = atoi(type_str + 4);
        switch (size) {
            case 1:  return LLVMInt8TypeInContext(ctx->context);
            case 2:  return LLVMInt16TypeInContext(ctx->context);
            case 4:  return LLVMInt32TypeInContext(ctx->context);
            default: return LLVMInt8TypeInContext(ctx->context);
        }
    }
    if (strcmp(type_str, "glass") == 0)
        return LLVMVoidTypeInContext(ctx->context);
    // Check if it's a class/struct type
    for (int i = 0; i < ctx->classes.count; i++) {
        if (strcmp(ctx->classes.names[i], type_str) == 0)
            return LLVMPointerType(ctx->classes.struct_types[i], 0);
    }
    return LLVMInt64TypeInContext(ctx->context);
}

static void codegen_register_class(CodeGenContext *ctx, ASTNode *node) {
    const char *name = node->data.class_decl.name;
    
    // Count actual fields from var_decl nodes
    int field_count = 0;
    for (int i = 0; i < node->data.class_decl.field_count; i++) {
        if (node->data.class_decl.fields[i]->type == NODE_VAR_DECL)
            field_count++;
    }
    
    // Build LLVM struct type
    LLVMTypeRef *field_types = NULL;
    char **field_names = NULL;
    if (field_count > 0) {
        field_types = malloc(field_count * sizeof(LLVMTypeRef));
        field_names = malloc(field_count * sizeof(char *));
        int fi = 0;
        for (int i = 0; i < node->data.class_decl.field_count; i++) {
            if (node->data.class_decl.fields[i]->type == NODE_VAR_DECL) {
                ASTNode *f = node->data.class_decl.fields[i];
                field_types[fi] = type_str_to_llvm(ctx, f->data.var_decl.var_type);
                field_names[fi] = strdup(f->data.var_decl.var_name);
                fi++;
            }
        }
    }
    
    LLVMTypeRef struct_type = LLVMStructCreateNamed(ctx->context, name);
    if (field_count > 0) {
        LLVMStructSetBody(struct_type, field_types, field_count, 0);
        free(field_types);
    }
    
    // Store in class table
    if (ctx->classes.count >= ctx->classes.capacity) {
        ctx->classes.capacity = ctx->classes.capacity ? ctx->classes.capacity * 2 : 8;
        ctx->classes.names = realloc(ctx->classes.names, ctx->classes.capacity * sizeof(char *));
        ctx->classes.struct_types = realloc(ctx->classes.struct_types, ctx->classes.capacity * sizeof(LLVMTypeRef));
        ctx->classes.field_names = realloc(ctx->classes.field_names, ctx->classes.capacity * sizeof(char **));
        ctx->classes.field_counts = realloc(ctx->classes.field_counts, ctx->classes.capacity * sizeof(int));
    }
    int idx = ctx->classes.count++;
    ctx->classes.names[idx] = strdup(name);
    ctx->classes.struct_types[idx] = struct_type;
    ctx->classes.field_names[idx] = field_names ? field_names : NULL;
    ctx->classes.field_counts[idx] = field_count;
    
    // Emit methods as functions with implicit 'this' parameter
    for (int i = 0; i < node->data.class_decl.method_count; i++) {
        ASTNode *method = node->data.class_decl.methods[i];
        if (method->type == NODE_FUNCTION) {
            char *mangled = malloc(strlen(name) + strlen(method->data.function.name) + 2);
            sprintf(mangled, "%s_%s", name, method->data.function.name);
            
            char *orig_name = method->data.function.name;
            method->data.function.name = mangled;
            
            // Build param types with implicit 'this' (pointer to struct)
            int pc = method->data.function.param_count;
            LLVMTypeRef *param_types = malloc((pc + 1) * sizeof(LLVMTypeRef));
            param_types[0] = LLVMPointerType(struct_type, 0);
            
            for (int j = 0; j < pc; j++) {
                ASTNode *p = method->data.function.params[j];
                if (p->type == NODE_VAR_DECL)
                    param_types[j + 1] = type_str_to_llvm(ctx, p->data.var_decl.var_type);
                else
                    param_types[j + 1] = LLVMInt64TypeInContext(ctx->context);
            }
            
            // Build function type and create function
            LLVMTypeRef ret_type = type_str_to_llvm(ctx, method->data.function.return_type);
            LLVMTypeRef ft = LLVMFunctionType(ret_type, param_types, pc + 1, 0);
            LLVMValueRef fn = LLVMAddFunction(ctx->module, mangled, ft);
            codegen_register_method_type(ctx, mangled, ft);
            free(param_types);
            
            // Create entry block
            codegen_init_locals(ctx);
            LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(ctx->context, fn, "entry");
            LLVMPositionBuilderAtEnd(ctx->builder, entry);
            
            // Store 'this' pointer
            LLVMValueRef this_alloca = LLVMBuildAlloca(ctx->builder, LLVMPointerType(struct_type, 0), "this.ptr");
            LLVMValueRef this_param = LLVMGetParam(fn, 0);
            LLVMBuildStore(ctx->builder, this_param, this_alloca);
            codegen_register_local(ctx, "this", this_alloca, node->data.class_decl.name);
            ctx->this_ptr = this_alloca;
            
            // Store other params
            for (int j = 0; j < pc; j++) {
                ASTNode *p = method->data.function.params[j];
                const char *pname = p->data.var_decl.var_name;
                const char *ptype = p->data.var_decl.var_type;
                LLVMValueRef param_val = LLVMGetParam(fn, j + 1);
                LLVMValueRef alloca = LLVMBuildAlloca(ctx->builder, LLVMTypeOf(param_val), pname);
                LLVMBuildStore(ctx->builder, param_val, alloca);
                codegen_register_local(ctx, pname, alloca, ptype);
            }
            
            ctx->current_return_type = ret_type;
            ctx->current_class_type = struct_type;
            if (method->data.function.body)
                codegen_stmt(ctx, method->data.function.body);
            ctx->current_return_type = NULL;
            ctx->current_class_type = NULL;
            ctx->this_ptr = NULL;
            
            if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(ctx->builder))) {
                if (LLVMGetTypeKind(ret_type) == LLVMVoidTypeKind)
                    LLVMBuildRetVoid(ctx->builder);
                else
                    LLVMBuildRet(ctx->builder, LLVMConstInt(ret_type, 0, 0));
            }
            
            free(method->data.function.name);
            method->data.function.name = orig_name;
        }
    }
}

void codegen_free(CodeGenContext *ctx) {
    if (ctx) {
        if (global_ctx == ctx) global_ctx = NULL;
        if (ctx->target_machine) {
            LLVMDisposeTargetMachine(ctx->target_machine);
        }
        if (ctx->target_triple) {
            free(ctx->target_triple);
        }
        for (int i = 0; i < ctx->locals.count; i++) {
            free(ctx->locals.names[i]);
        }
        free(ctx->locals.names);
        free(ctx->locals.allocs);
        for (int i = 0; i < ctx->classes.count; i++) {
            free(ctx->classes.names[i]);
            if (ctx->classes.field_names[i]) {
                for (int j = 0; j < ctx->classes.field_counts[i]; j++)
                    free(ctx->classes.field_names[i][j]);
                free(ctx->classes.field_names[i]);
            }
        }
        free(ctx->classes.names);
        free(ctx->classes.struct_types);
        free(ctx->classes.field_names);
        free(ctx->classes.field_counts);
        LLVMDisposeBuilder(ctx->builder);
        LLVMDisposeModule(ctx->module);
        LLVMContextDispose(ctx->context);
        LLVMDisposePassManager(ctx->pass_manager);
        free(ctx);
    }
}


static LLVMValueRef codegen_number(CodeGenContext *ctx, ASTNode *node) {
    return LLVMConstInt(LLVMInt64TypeInContext(ctx->context), 
                        (unsigned long long)node->data.number, 1);
}

static LLVMValueRef codegen_boolean(CodeGenContext *ctx, ASTNode *node) {
    return LLVMConstInt(LLVMInt1TypeInContext(ctx->context), 
                        node->data.boolean, 0);
}

static LLVMValueRef codegen_string(CodeGenContext *ctx, ASTNode *node) {
    return LLVMBuildGlobalStringPtr(ctx->builder, node->data.string, "str");
}

static LLVMValueRef codegen_identifier(CodeGenContext *ctx, ASTNode *node) {
    const char *name = node->data.identifier;
    
    // Check local variables first
    LLVMValueRef local = codegen_lookup_local(ctx, name);
    if (local) {
        // Load the value from the alloca
        LLVMTypeRef elem_type = LLVMGetAllocatedType(local);
        return LLVMBuildLoad2(ctx->builder, elem_type, local, name);
    }
    
    // Built-in functions
    if (strcmp(name, "print") == 0) {
        return ctx->printf_func;
    }
    if (strcmp(name, "input") == 0) {
        return ctx->scanf_func;
    }
    
    LLVMValueRef val = LLVMGetNamedFunction(ctx->module, name);
    if (!val) {
        val = LLVMGetNamedGlobal(ctx->module, name);
    }
    return val;
}

static void codegen_var_decl(CodeGenContext *ctx, ASTNode *node) {
    const char *var_name = node->data.var_decl.var_name;
    const char *type_str = node->data.var_decl.var_type;
    
    LLVMTypeRef var_type = type_str_to_llvm(ctx, type_str);
    
    if (node->data.var_decl.array_size > 0) {
        LLVMTypeRef arr_type = LLVMArrayType(var_type, node->data.var_decl.array_size);
        LLVMValueRef alloca = LLVMBuildAlloca(ctx->builder, arr_type, var_name);
        codegen_register_local(ctx, var_name, alloca, type_str);
    } else {
        LLVMValueRef alloca = LLVMBuildAlloca(ctx->builder, var_type, var_name);
        codegen_register_local(ctx, var_name, alloca, type_str);
        if (node->data.var_decl.init) {
            LLVMValueRef init_val = codegen_expr(ctx, node->data.var_decl.init);
            if (init_val) {
                LLVMBuildStore(ctx->builder, init_val, alloca);
            }
        }
    }
}

static LLVMValueRef codegen_binary_op(CodeGenContext *ctx, ASTNode *node) {
    LLVMValueRef left = codegen_expr(ctx, node->data.binary_op.left);
    LLVMValueRef right = codegen_expr(ctx, node->data.binary_op.right);
    
    if (!left || !right) return NULL;
    
    if (strcmp(node->data.binary_op.op, "+") == 0) {
        return LLVMBuildAdd(ctx->builder, left, right, "addtmp");
    } else if (strcmp(node->data.binary_op.op, "-") == 0) {
        return LLVMBuildSub(ctx->builder, left, right, "subtmp");
    } else if (strcmp(node->data.binary_op.op, "*") == 0) {
        return LLVMBuildMul(ctx->builder, left, right, "multmp");
    } else if (strcmp(node->data.binary_op.op, "/") == 0) {
        return LLVMBuildSDiv(ctx->builder, left, right, "divtmp");
    } else if (strcmp(node->data.binary_op.op, "==") == 0) {
        return LLVMBuildICmp(ctx->builder, LLVMIntEQ, left, right, "cmptmp");
    } else if (strcmp(node->data.binary_op.op, "!=") == 0) {
        return LLVMBuildICmp(ctx->builder, LLVMIntNE, left, right, "cmptmp");
    } else if (strcmp(node->data.binary_op.op, "<") == 0) {
        return LLVMBuildICmp(ctx->builder, LLVMIntSLT, left, right, "cmptmp");
    } else if (strcmp(node->data.binary_op.op, ">") == 0) {
        return LLVMBuildICmp(ctx->builder, LLVMIntSGT, left, right, "cmptmp");
    } else if (strcmp(node->data.binary_op.op, "<=") == 0) {
        return LLVMBuildICmp(ctx->builder, LLVMIntSLE, left, right, "cmptmp");
    } else if (strcmp(node->data.binary_op.op, ">=") == 0) {
        return LLVMBuildICmp(ctx->builder, LLVMIntSGE, left, right, "cmptmp");
    } else if (strcmp(node->data.binary_op.op, "%") == 0) {
        return LLVMBuildSRem(ctx->builder, left, right, "modtmp");
    } else if (strcmp(node->data.binary_op.op, "&") == 0) {
        return LLVMBuildAnd(ctx->builder, left, right, "andtmp");
    } else if (strcmp(node->data.binary_op.op, "|") == 0) {
        return LLVMBuildOr(ctx->builder, left, right, "ortmp");
    } else if (strcmp(node->data.binary_op.op, "^") == 0) {
        return LLVMBuildXor(ctx->builder, left, right, "xortmp");
    } else if (strcmp(node->data.binary_op.op, "<<") == 0) {
        return LLVMBuildShl(ctx->builder, left, right, "shltmp");
    } else if (strcmp(node->data.binary_op.op, ">>") == 0) {
        return LLVMBuildAShr(ctx->builder, left, right, "ashtmp");
    }
    
    return NULL;
}

static LLVMValueRef codegen_call(CodeGenContext *ctx, ASTNode *node) {
    // Special handling for built-in syscall (kernel syscall via inline asm)
    if (node->data.call.callee->type == NODE_IDENTIFIER &&
        strcmp(node->data.call.callee->data.identifier, "syscall") == 0) {

        int ac = node->data.call.arg_count;
        if (ac < 1) {
            fprintf(stderr, COLOR_RED "Error:" COLOR_RESET " syscall expects at least 1 argument\n");
            return NULL;
        }

        LLVMValueRef *sys_args = malloc(ac * sizeof(LLVMValueRef));
        for (int i = 0; i < ac; i++)
            sys_args[i] = codegen_expr(ctx, node->data.call.args[i]);

        LLVMTypeRef *param_types = malloc(ac * sizeof(LLVMTypeRef));
        for (int i = 0; i < ac; i++)
            param_types[i] = LLVMInt64TypeInContext(ctx->context);

        LLVMTypeRef asm_fn_type = LLVMFunctionType(
            LLVMInt64TypeInContext(ctx->context), param_types, ac, 0);
        free(param_types);

        char constraints[128] = "={rax},";
        char *p = constraints + 7;
        const char *regs[] = {"{rax}", "{rdi}", "{rsi}", "{rdx}", "{r10}", "{r8}", "{r9}"};

        int max_args = ac < 7 ? ac : 7;
        for (int i = 0; i < max_args; i++)
            p += snprintf(p, 16, "%s,", regs[i]);
        if (p > constraints) p[-1] = '\0';

        LLVMValueRef asm_val = LLVMGetInlineAsm(
            asm_fn_type, "syscall", 7,
            constraints, strlen(constraints),
            1, 0, LLVMInlineAsmDialectATT, 0);

        LLVMValueRef result = LLVMBuildCall2(ctx->builder, asm_fn_type,
                                              asm_val, sys_args, ac, "sysret");
        free(sys_args);
        return result;
    }

    // Handle method calls: object.method(args...)
    if (node->data.call.callee->type == NODE_MEMBER_ACCESS) {
        ASTNode *access = node->data.call.callee;
        if (access->data.member_access.object->type == NODE_IDENTIFIER) {
            const char *obj_name = access->data.member_access.object->data.identifier;
            const char *method_name = access->data.member_access.member;
            
            LLVMValueRef obj_ptr = codegen_lookup_local(ctx, obj_name);
            if (!obj_ptr) {
                fprintf(stderr, COLOR_RED "Error:" COLOR_RESET " undefined object '%s'\n", obj_name);
                return NULL;
            }
            
            const char *obj_type = codegen_lookup_local_type(ctx, obj_name);
            if (!obj_type) {
                fprintf(stderr, COLOR_RED "Error:" COLOR_RESET " unknown type for '%s'\n", obj_name);
                return NULL;
            }
            
            int class_idx = -1;
            for (int i = 0; i < ctx->classes.count; i++) {
                if (strcmp(ctx->classes.names[i], obj_type) == 0) {
                    class_idx = i;
                    break;
                }
            }
            if (class_idx < 0) {
                fprintf(stderr, COLOR_RED "Error:" COLOR_RESET " unknown class '%s'\n", obj_type);
                return NULL;
            }
            
            // Load struct pointer from alloca (type is struct**)
            LLVMTypeRef obj_alloca_type = LLVMGetAllocatedType(obj_ptr);
            LLVMValueRef obj = LLVMBuildLoad2(ctx->builder, obj_alloca_type, obj_ptr, obj_name);
            
            char mangled[256];
            snprintf(mangled, sizeof(mangled), "%s_%s", obj_type, method_name);
            
            LLVMValueRef fn = LLVMGetNamedFunction(ctx->module, mangled);
            if (!fn) {
                fprintf(stderr, COLOR_RED "Error:" COLOR_RESET " unknown method '%s' for class '%s'\n",
                    method_name, obj_type);
                return NULL;
            }
            
            int ac = node->data.call.arg_count;
            LLVMValueRef *args = malloc((ac + 1) * sizeof(LLVMValueRef));
            args[0] = obj;
            for (int i = 0; i < ac; i++) {
                args[i + 1] = codegen_expr(ctx, node->data.call.args[i]);
                if (!args[i + 1]) { free(args); return NULL; }
            }
            
            LLVMTypeRef fn_type = codegen_lookup_method_type(ctx, mangled);
            if (!fn_type) {
                fprintf(stderr, COLOR_RED "Error:" COLOR_RESET " function type not found for '%s'\n", mangled);
                free(args);
                return NULL;
            }
            const char *call_name = "";
            if (LLVMGetTypeKind(LLVMGetReturnType(fn_type)) != LLVMVoidTypeKind)
                call_name = "calltmp";
            LLVMValueRef result = LLVMBuildCall2(ctx->builder, fn_type, fn, args, ac + 1, call_name);
            free(args);
            return result;
        }
        fprintf(stderr, COLOR_RED "Error:" COLOR_RESET " method call on non-identifier object\n");
        return NULL;
    }
    
    LLVMValueRef callee = codegen_expr(ctx, node->data.call.callee);
    if (!callee) {
        if (node->data.call.callee->type == NODE_IDENTIFIER) {
            const char *name = node->data.call.callee->data.identifier;
            LLVMTypeRef *arg_types = malloc(node->data.call.arg_count * sizeof(LLVMTypeRef));
            for (int i = 0; i < node->data.call.arg_count; i++)
                arg_types[i] = LLVMInt64TypeInContext(ctx->context);
            LLVMTypeRef ft = LLVMFunctionType(
                LLVMInt64TypeInContext(ctx->context),
                arg_types, node->data.call.arg_count, 0);
            free(arg_types);
            callee = LLVMAddFunction(ctx->module, name, ft);
        } else {
            fprintf(stderr, COLOR_RED "Error:" COLOR_RESET " Unknown function in call\n");
            return NULL;
        }
    }
    //fprintf(stderr, "DEBUG call: callee=%p, printf_func=%p, scanf_func=%p\n",
           // (void*)callee, (void*)ctx->printf_func, (void*)ctx->scanf_func);
    
    // Special handling for built-in scanf (input)
    if (callee == ctx->scanf_func) {
        if (node->data.call.arg_count != 1) {
            fprintf(stderr, COLOR_RED "Error:" COLOR_RESET " input expects exactly one argument\n");
            return NULL;
        }
        ASTNode *arg = node->data.call.args[0];
        if (arg->type != NODE_IDENTIFIER) {
            fprintf(stderr, COLOR_RED "Error:" COLOR_RESET " input argument must be a variable\n");
            return NULL;
        }
        LLVMValueRef alloca = codegen_lookup_local(ctx, arg->data.identifier);
        if (!alloca) {
            fprintf(stderr, COLOR_RED "Error:" COLOR_RESET " undefined variable '%s' for input\n", arg->data.identifier);
            return NULL;
        }
        const char *fmt = "%lld";
        LLVMValueRef fmt_str = LLVMBuildGlobalStringPtr(ctx->builder, fmt, "inpfmt");
        LLVMValueRef args[2] = { fmt_str, alloca };
        return LLVMBuildCall2(ctx->builder, ctx->scanf_type, ctx->scanf_func, args, 2, "calltmp");
    }
    
    // Special handling for built-in printf (print) - auto-format non-string args
    if (callee == ctx->printf_func) {
        int ac = node->data.call.arg_count;
        if (ac < 1) {
            fprintf(stderr, COLOR_RED "Error:" COLOR_RESET " print expects at least one argument\n");
            return NULL;
        }
        LLVMValueRef *pargs = malloc((ac + 1) * sizeof(LLVMValueRef));
        for (int i = 0; i < ac; i++)
            pargs[i] = codegen_expr(ctx, node->data.call.args[i]);
        if (pargs[0] && LLVMGetTypeKind(LLVMTypeOf(pargs[0])) != LLVMPointerTypeKind) {
            memmove(&pargs[1], pargs, ac * sizeof(LLVMValueRef));
            pargs[0] = LLVMBuildGlobalStringPtr(ctx->builder, "%lld\n", "printfmt");
            LLVMValueRef r = LLVMBuildCall2(ctx->builder, ctx->printf_type, ctx->printf_func, pargs, ac + 1, "calltmp");
            free(pargs);
            return r;
        }
        LLVMValueRef r = LLVMBuildCall2(ctx->builder, ctx->printf_type, ctx->printf_func, pargs, ac, "calltmp");
        free(pargs);
        return r;
    }
    
    LLVMValueRef *args = NULL;
    if (node->data.call.arg_count > 0) {
        args = malloc(node->data.call.arg_count * sizeof(LLVMValueRef));
        for (int i = 0; i < node->data.call.arg_count; i++) {
            args[i] = codegen_expr(ctx, node->data.call.args[i]);
            if (!args[i]) { free(args); return NULL; }
        }
    }
    
    LLVMTypeRef *arg_types = malloc(node->data.call.arg_count * sizeof(LLVMTypeRef));
    for (int i = 0; i < node->data.call.arg_count; i++)
        arg_types[i] = LLVMTypeOf(args[i]);
    LLVMTypeRef call_ft = LLVMFunctionType(
        LLVMInt64TypeInContext(ctx->context),
        arg_types, node->data.call.arg_count, 0);
    free(arg_types);
    LLVMValueRef result = LLVMBuildCall2(ctx->builder, call_ft,
                                         callee, args, node->data.call.arg_count, "calltmp");
    if (args) free(args);
    return result;
}

static LLVMValueRef codegen_expr(CodeGenContext *ctx, ASTNode *node) {
    if (!node) return NULL;
    
    switch (node->type) {
        case NODE_NUMBER:
            return codegen_number(ctx, node);
        case NODE_BOOLEAN:
            return codegen_boolean(ctx, node);
        case NODE_STRING:
            return codegen_string(ctx, node);
        case NODE_IDENTIFIER:
            return codegen_identifier(ctx, node);
        case NODE_BINARY_OP:
            return codegen_binary_op(ctx, node);
        case NODE_CALL:
            return codegen_call(ctx, node);
        case NODE_UNARY_OP: {
            const char *op = node->data.unary_op.op;
            LLVMValueRef operand = codegen_expr(ctx, node->data.unary_op.operand);
            if (!operand) return NULL;
            if (strcmp(op, "-") == 0)
                return LLVMBuildNeg(ctx->builder, operand, "negtmp");
            if (strcmp(op, "!") == 0) {
                LLVMValueRef zero = LLVMConstInt(LLVMTypeOf(operand), 0, 1);
                return LLVMBuildICmp(ctx->builder, LLVMIntEQ, operand, zero, "nottmp");
            }
            if (strcmp(op, "~") == 0)
                return LLVMBuildNot(ctx->builder, operand, "nottmp");
            if (strcmp(op, "++") == 0 || strcmp(op, "--") == 0) {
                if (node->data.unary_op.operand->type == NODE_IDENTIFIER) {
                    const char *name = node->data.unary_op.operand->data.identifier;
                    LLVMValueRef ptr = codegen_lookup_local(ctx, name);
                    if (ptr) {
                        LLVMValueRef val = LLVMBuildLoad2(ctx->builder, LLVMGetAllocatedType(ptr), ptr, name);
                        LLVMValueRef one = LLVMConstInt(LLVMTypeOf(val), 1, 1);
                        LLVMValueRef result = (strcmp(op, "++") == 0)
                            ? LLVMBuildAdd(ctx->builder, val, one, "inctmp")
                            : LLVMBuildSub(ctx->builder, val, one, "dectmp");
                        LLVMBuildStore(ctx->builder, result, ptr);
                        return result;
                    }
                }
                return operand;
            }
            return operand;
        }
        case NODE_MEMBER_ACCESS: {
            ASTNode *obj = node->data.member_access.object;
            const char *member = node->data.member_access.member;
            if (obj->type == NODE_IDENTIFIER && strcmp(obj->data.identifier, "this") == 0) {
                if (!ctx->current_class_type) {
                    fprintf(stderr, COLOR_RED "Error:" COLOR_RESET "'this' used outside of class method\n");
                    return NULL;
                }
                LLVMValueRef this_alloca = codegen_lookup_local(ctx, "this");
                if (!this_alloca) {
                    fprintf(stderr, COLOR_RED "Error:" COLOR_RESET "'this' not available\n");
                    return NULL;
                }
                LLVMValueRef this_val = LLVMBuildLoad2(ctx->builder,
                    LLVMPointerType(ctx->current_class_type, 0),
                    this_alloca, "this");
                int field_idx = -1;
                for (int ci = 0; ci < ctx->classes.count; ci++) {
                    for (int fj = 0; fj < ctx->classes.field_counts[ci]; fj++) {
                        if (strcmp(ctx->classes.field_names[ci][fj], member) == 0) {
                            field_idx = fj;
                            break;
                        }
                    }
                    if (field_idx >= 0) break;
                }
                if (field_idx >= 0) {
                    LLVMValueRef indices[] = {
                        LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, 0),
                        LLVMConstInt(LLVMInt32TypeInContext(ctx->context), field_idx, 0)
                    };
                    LLVMValueRef gep = LLVMBuildGEP2(ctx->builder,
                        ctx->current_class_type, this_val,
                        indices, 2, member);
                    LLVMTypeRef field_type = LLVMStructGetTypeAtIndex(
                        ctx->current_class_type, field_idx);
                    return LLVMBuildLoad2(ctx->builder, field_type, gep, member);
                }
                fprintf(stderr, COLOR_RED "Error:" COLOR_RESET " unknown field '%s'\n", member);
                return NULL;
            }
            fprintf(stderr, COLOR_RED "Error:" COLOR_RESET " member access not supported for this expression\n");
            return NULL;
        }
        case NODE_INDEX: {
            ASTNode *arr_node = node->data.array_index.object;
            ASTNode *idx_node = node->data.array_index.index;
            const char *arr_name = (arr_node->type == NODE_IDENTIFIER)
                ? arr_node->data.identifier : "arr";
            LLVMValueRef arr_ptr = codegen_lookup_local(ctx, arr_name);
            LLVMValueRef idx_val = codegen_expr(ctx, idx_node);
            if (!arr_ptr || !idx_val) {
                if (!arr_ptr)
                    fprintf(stderr, COLOR_RED "Error:" COLOR_RESET " undefined array '%s'\n", arr_name);
                return NULL;
            }
            LLVMTypeRef allocated = LLVMGetAllocatedType(arr_ptr);
            if (LLVMGetTypeKind(allocated) == LLVMArrayTypeKind) {
                LLVMTypeRef elem_type = LLVMGetElementType(allocated);
                LLVMValueRef indices[] = {
                    LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, 0),
                    idx_val
                };
                LLVMValueRef elem_ptr = LLVMBuildGEP2(ctx->builder, allocated, arr_ptr, indices, 2, "arr_elem");
                return LLVMBuildLoad2(ctx->builder, elem_type, elem_ptr, "arr_val");
            }
            if (LLVMGetTypeKind(allocated) == LLVMPointerTypeKind) {
                LLVMTypeRef elem_type = LLVMGetElementType(allocated);
                LLVMValueRef loaded_ptr = LLVMBuildLoad2(ctx->builder, allocated, arr_ptr, arr_name);
                LLVMValueRef elem_ptr = LLVMBuildGEP2(ctx->builder, elem_type, loaded_ptr, &idx_val, 1, "arr_elem");
                return LLVMBuildLoad2(ctx->builder, elem_type, elem_ptr, "arr_val");
            }
            LLVMTypeRef elem_type = allocated;
            LLVMValueRef ptr = LLVMBuildGEP2(ctx->builder, elem_type, arr_ptr, &idx_val, 1, "arr_elem");
            return LLVMBuildLoad2(ctx->builder, elem_type, ptr, "arr_val");
        }
        case NODE_ARRAY_LITERAL: {
            int count = node->data.array_literal.elem_count;
            LLVMTypeRef elem_type = LLVMInt64TypeInContext(ctx->context);
            LLVMTypeRef arr_type = LLVMArrayType(elem_type, count > 0 ? count : 1);
            LLVMValueRef alloca = LLVMBuildAlloca(ctx->builder, arr_type, "arrlit");
            for (int i = 0; i < count; i++) {
                LLVMValueRef val = codegen_expr(ctx, node->data.array_literal.elements[i]);
                if (val) {
                    LLVMValueRef indices[] = {
                        LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, 0),
                        LLVMConstInt(LLVMInt32TypeInContext(ctx->context), i, 0)
                    };
                    LLVMValueRef ptr = LLVMBuildGEP2(ctx->builder, arr_type, alloca, indices, 2, "arr_el");
                    LLVMBuildStore(ctx->builder, val, ptr);
                }
            }
            return LLVMBuildLoad2(ctx->builder, arr_type, alloca, "arrlit_val");
        }
        case NODE_ASSIGNMENT: {
            ASTNode *target = node->data.assign.target;
            LLVMValueRef val = codegen_expr(ctx, node->data.assign.value);
            if (!val) return NULL;
            if (target->type == NODE_IDENTIFIER) {
                const char *name = target->data.identifier;
                LLVMValueRef ptr = codegen_lookup_local(ctx, name);
                if (!ptr) {
                    fprintf(stderr, COLOR_RED "Error:" COLOR_RESET " undefined variable '%s'\n", name);
                    return NULL;
                }
                LLVMBuildStore(ctx->builder, val, ptr);
            } else if (target->type == NODE_MEMBER_ACCESS) {
                if (target->data.member_access.object->type == NODE_IDENTIFIER &&
                    strcmp(target->data.member_access.object->data.identifier, "this") == 0) {
                    if (!ctx->current_class_type) {
                        fprintf(stderr, COLOR_RED "Error:" COLOR_RESET "'this' used outside of class method\n");
                        return NULL;
                    }
                    LLVMValueRef this_alloca = codegen_lookup_local(ctx, "this");
                    if (!this_alloca) return NULL;
                    LLVMValueRef this_val = LLVMBuildLoad2(ctx->builder,
                        LLVMPointerType(ctx->current_class_type, 0),
                        this_alloca, "this");
                    const char *member = target->data.member_access.member;
                    int field_idx = -1;
                    for (int ci = 0; ci < ctx->classes.count; ci++) {
                        for (int fj = 0; fj < ctx->classes.field_counts[ci]; fj++) {
                            if (strcmp(ctx->classes.field_names[ci][fj], member) == 0) {
                                field_idx = fj;
                                break;
                            }
                        }
                        if (field_idx >= 0) break;
                    }
                    if (field_idx >= 0) {
                        LLVMValueRef indices[] = {
                            LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, 0),
                            LLVMConstInt(LLVMInt32TypeInContext(ctx->context), field_idx, 0)
                        };
                        LLVMValueRef gep = LLVMBuildGEP2(ctx->builder,
                            ctx->current_class_type, this_val,
                            indices, 2, member);
                        LLVMBuildStore(ctx->builder, val, gep);
                    }
                }
            } else if (target->type == NODE_INDEX) {
                const char *arr_name = (target->data.array_index.object->type == NODE_IDENTIFIER)
                    ? target->data.array_index.object->data.identifier : "arr";
                LLVMValueRef arr_ptr = codegen_lookup_local(ctx, arr_name);
                LLVMValueRef idx_val = codegen_expr(ctx, target->data.array_index.index);
                if (arr_ptr && idx_val) {
                    LLVMTypeRef allocated = LLVMGetAllocatedType(arr_ptr);
                    if (LLVMGetTypeKind(allocated) == LLVMArrayTypeKind) {
                        LLVMValueRef indices[] = {
                            LLVMConstInt(LLVMInt32TypeInContext(ctx->context), 0, 0),
                            idx_val
                        };
                        LLVMValueRef elem_ptr = LLVMBuildGEP2(ctx->builder, allocated, arr_ptr, indices, 2, "arr_elem");
                        LLVMBuildStore(ctx->builder, val, elem_ptr);
                    } else {
                        LLVMValueRef loaded_ptr = LLVMBuildLoad2(ctx->builder, allocated, arr_ptr, arr_name);
                        LLVMTypeRef elem_type = LLVMGetElementType(allocated);
                        LLVMValueRef elem_ptr = LLVMBuildGEP2(ctx->builder, elem_type, loaded_ptr, &idx_val, 1, "arr_elem");
                        LLVMBuildStore(ctx->builder, val, elem_ptr);
                    }
                }
            }
            return val;
        }
        default:
            return NULL;
    }
}

static void codegen_while_stmt(CodeGenContext *ctx, ASTNode *node) {
    LLVMValueRef function = LLVMGetBasicBlockParent(
        LLVMGetInsertBlock(ctx->builder));

    LLVMBasicBlockRef header_bb = LLVMAppendBasicBlockInContext(
        ctx->context, function, "while_header");
    LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(
        ctx->context, function, "while_body");
    LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(
        ctx->context, function, "while_end");

    LLVMBuildBr(ctx->builder, header_bb);
    LLVMPositionBuilderAtEnd(ctx->builder, header_bb);

    LLVMValueRef cond_val = codegen_expr(ctx, node->data.if_stmt.condition);
    if (!cond_val) {
        cond_val = LLVMConstInt(LLVMInt1TypeInContext(ctx->context), 1, 0);
    }
    LLVMValueRef zero = LLVMConstInt(LLVMTypeOf(cond_val), 0, 1);
    LLVMValueRef cmp = LLVMBuildICmp(ctx->builder, LLVMIntNE, cond_val, zero, "whilecond");
    LLVMBuildCondBr(ctx->builder, cmp, body_bb, end_bb);

    LLVMPositionBuilderAtEnd(ctx->builder, body_bb);
    if (node->data.if_stmt.then_branch) {
        codegen_stmt(ctx, node->data.if_stmt.then_branch);
    }
    if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(ctx->builder))) {
        LLVMBuildBr(ctx->builder, header_bb);
    }

    LLVMPositionBuilderAtEnd(ctx->builder, end_bb);
}

static void codegen_if_stmt(CodeGenContext *ctx, ASTNode *node) {
    LLVMValueRef cond_val = codegen_expr(ctx, node->data.if_stmt.condition);
    if (!cond_val) return;
    
    int is_kern = node->data.if_stmt.is_kern;
    LLVMValueRef function = LLVMGetBasicBlockParent(
        LLVMGetInsertBlock(ctx->builder));
    
    const char *then_name = is_kern ? "kern_then" : "then";
    const char *else_name = is_kern ? "kern_else" : "else";
    const char *merge_name = is_kern ? "kern_ifcont" : "ifcont";
    
    LLVMBasicBlockRef then_bb = LLVMAppendBasicBlockInContext(
        ctx->context, function, then_name);
    LLVMBasicBlockRef else_bb = LLVMAppendBasicBlockInContext(
        ctx->context, function, else_name);
    LLVMBasicBlockRef merge_bb = LLVMAppendBasicBlockInContext(
        ctx->context, function, merge_name);
    
    // Convert condition to boolean by comparing with 0
    LLVMValueRef zero = LLVMConstInt(
        LLVMTypeOf(cond_val), 0, 1);
    LLVMValueRef cmp = LLVMBuildICmp(ctx->builder, LLVMIntNE, 
                                    cond_val, zero, "ifcond");
    
    LLVMBuildCondBr(ctx->builder, cmp, then_bb, else_bb);
    
    // Then block
    LLVMPositionBuilderAtEnd(ctx->builder, then_bb);
    codegen_stmt(ctx, node->data.if_stmt.then_branch);
    if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(ctx->builder))) {
        LLVMBuildBr(ctx->builder, merge_bb);
    }
    
    // Else block
    LLVMPositionBuilderAtEnd(ctx->builder, else_bb);
    if (node->data.if_stmt.else_branch) {
        codegen_stmt(ctx, node->data.if_stmt.else_branch);
    }
    if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(ctx->builder))) {
        LLVMBuildBr(ctx->builder, merge_bb);
    }
    
    // Merge block
    LLVMPositionBuilderAtEnd(ctx->builder, merge_bb);
}

static char *create_valid_name(const char *name) {
    char *valid = strdup(name);
    for (int i = 0; valid[i]; i++) {
        if (valid[i] == '.') valid[i] = '_';
    }
    return valid;
}

static void codegen_function(CodeGenContext *ctx, ASTNode *node) {
    char *func_name = create_valid_name(node->data.function.name);
    
    LLVMTypeRef return_type = LLVMVoidTypeInContext(ctx->context);
    if (node->data.function.return_type) {
        return_type = type_str_to_llvm(ctx, node->data.function.return_type);
    }
    // Override void main to return i32 (C ABI expects int main())
    if (strcmp(func_name, "main") == 0 && LLVMGetTypeKind(return_type) == LLVMVoidTypeKind) {
        return_type = LLVMInt32TypeInContext(ctx->context);
    }
    
    int pc = node->data.function.param_count;
    LLVMTypeRef *param_types = NULL;
    if (pc > 0) {
        param_types = malloc(pc * sizeof(LLVMTypeRef));
        for (int i = 0; i < pc; i++) {
            ASTNode *p = node->data.function.params[i];
            if (p->type == NODE_VAR_DECL)
                param_types[i] = type_str_to_llvm(ctx, p->data.var_decl.var_type);
            else
                param_types[i] = LLVMInt64TypeInContext(ctx->context);
        }
    }
    
    LLVMTypeRef func_type = LLVMFunctionType(return_type, param_types, pc, 0);
    LLVMValueRef function = LLVMAddFunction(ctx->module, func_name, func_type);
    free(param_types);
    
    // Reset local variable tracking per function
    codegen_init_locals(ctx);
    
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(
        ctx->context, function, "entry");
    LLVMPositionBuilderAtEnd(ctx->builder, entry);
    
    // Create allocas for parameters, store incoming values, register as locals
    for (int i = 0; i < pc; i++) {
        ASTNode *p = node->data.function.params[i];
        const char *pname = p->data.var_decl.var_name;
        LLVMValueRef param_val = LLVMGetParam(function, i);
        LLVMValueRef alloca = LLVMBuildAlloca(ctx->builder,
            LLVMTypeOf(param_val), pname);
        LLVMBuildStore(ctx->builder, param_val, alloca);
        codegen_register_local(ctx, pname, alloca, p->data.var_decl.var_type);
    }
    
    // Set current return type for this function (used by return statements)
    ctx->current_return_type = return_type;
    
    codegen_stmt(ctx, node->data.function.body);
    
    // Clear current return type after function body
    ctx->current_return_type = NULL;
    
    // Add default return if needed
    if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(ctx->builder))) {
        if (LLVMGetTypeKind(return_type) == LLVMVoidTypeKind) {
            LLVMBuildRetVoid(ctx->builder);
        } else {
            LLVMBuildRet(ctx->builder, LLVMConstInt(return_type, 0, 0));
        }
    }
    
    free(func_name);
}
static void codegen_for_stmt(CodeGenContext *ctx, ASTNode *node) {
    LLVMValueRef function = LLVMGetBasicBlockParent(LLVMGetInsertBlock(ctx->builder));
    LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(ctx->context, function, "for_cond");
    LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(ctx->context, function, "for_body");
    LLVMBasicBlockRef inc_bb = LLVMAppendBasicBlockInContext(ctx->context, function, "for_inc");
    LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(ctx->context, function, "for_end");

    if (node->data.for_loop.init)
        codegen_stmt(ctx, node->data.for_loop.init);
    LLVMBuildBr(ctx->builder, cond_bb);

    LLVMPositionBuilderAtEnd(ctx->builder, cond_bb);
    LLVMValueRef cond_val = NULL;
    if (node->data.for_loop.condition) {
        cond_val = codegen_expr(ctx, node->data.for_loop.condition);
    }
    if (!cond_val)
        cond_val = LLVMConstInt(LLVMInt1TypeInContext(ctx->context), 1, 0);
    LLVMValueRef zero = LLVMConstInt(LLVMTypeOf(cond_val), 0, 1);
    LLVMValueRef cmp = LLVMBuildICmp(ctx->builder, LLVMIntNE, cond_val, zero, "forcond");
    LLVMBuildCondBr(ctx->builder, cmp, body_bb, end_bb);

    LLVMPositionBuilderAtEnd(ctx->builder, body_bb);
    if (node->data.for_loop.body)
        codegen_stmt(ctx, node->data.for_loop.body);
    if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(ctx->builder)))
        LLVMBuildBr(ctx->builder, inc_bb);

    LLVMPositionBuilderAtEnd(ctx->builder, inc_bb);
    if (node->data.for_loop.increment)
        codegen_expr(ctx, node->data.for_loop.increment);
    LLVMBuildBr(ctx->builder, cond_bb);

    LLVMPositionBuilderAtEnd(ctx->builder, end_bb);
}

static void codegen_return_stmt(CodeGenContext *ctx, ASTNode *node) {
    LLVMTypeRef ret_type = ctx->current_return_type;
    if (!ret_type) {
        // Fallback: deduce from current function
        LLVMValueRef current_fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(ctx->builder));
        LLVMTypeRef func_type = LLVMGetElementType(LLVMTypeOf(current_fn));
        ret_type = LLVMGetReturnType(func_type);
    }
    
    if (node->data.return_stmt.value && LLVMGetTypeKind(ret_type) != LLVMVoidTypeKind) {
        LLVMValueRef ret_val = codegen_expr(ctx, node->data.return_stmt.value);
        if (!ret_val) return;
        if (LLVMGetTypeKind(ret_type) == LLVMIntegerTypeKind &&
            LLVMGetTypeKind(LLVMTypeOf(ret_val)) == LLVMIntegerTypeKind &&
            LLVMGetIntTypeWidth(LLVMTypeOf(ret_val)) > LLVMGetIntTypeWidth(ret_type)) {
            ret_val = LLVMBuildTrunc(ctx->builder, ret_val, ret_type, "mainret");
        }
        LLVMBuildRet(ctx->builder, ret_val);
    } else {
        LLVMBuildRetVoid(ctx->builder);
    }
}

static void codegen_stmt(CodeGenContext *ctx, ASTNode *node) {
    if (!node) return;
    
    switch (node->type) {
        case NODE_FUNCTION:
            codegen_function(ctx, node);
            break;
        case NODE_IF_STMT:
            codegen_if_stmt(ctx, node);
            break;
        case NODE_WHILE_STMT:
            codegen_while_stmt(ctx, node);
            break;
        case NODE_RETURN_STMT:
            codegen_return_stmt(ctx, node);
            break;
        case NODE_FOR_STMT:
            codegen_for_stmt(ctx, node);
            break;
        case NODE_EXPRESSION_STMT:
            codegen_expr(ctx, node->data.expr_stmt.expr);
            break;
        case NODE_VAR_DECL:
            codegen_var_decl(ctx, node);
            break;
        case NODE_CALL:
            codegen_call(ctx, node);
            break;
        case NODE_BLOCK:
        case NODE_PROGRAM:
            for (int i = 0; i < node->data.block.stmt_count; i++) {
                codegen_stmt(ctx, node->data.block.stmts[i]);
            }
            break;
        case NODE_CLASS:
        case NODE_STRUCT:
            codegen_register_class(ctx, node);
            break;
        default:
            break;
    }
}

int codegen_generate(CodeGenContext *ctx, ASTNode *ast, const char *output_file) {
    codegen_stmt(ctx, ast);
    
    // Verify module
    char *error = NULL;
    if (LLVMVerifyModule(ctx->module, LLVMReturnStatusAction, &error)) {
        fprintf(stderr, COLOR_RED "Module verification failed: %s\n" COLOR_RESET, error);
        LLVMDisposeMessage(error);
        return -1;
    }
    
    // Run optimization passes
    LLVMRunPassManager(ctx->pass_manager, ctx->module);
    
    // Write bitcode
    //fprintf(stderr, "Debug: Writing bitcode to '%s'\n", output_file);
    if (LLVMWriteBitcodeToFile(ctx->module, output_file)) {
        fprintf(stderr, COLOR_RED "Error:" COLOR_RESET " LLVMWriteBitcodeToFile failed for '%s'\n", output_file);
        return -1;
    }
   // fprintf(stderr, "Debug: Successfully wrote bitcode to '%s'\n", output_file);
    
    return 0;
}

// Public wrapper to build IR from AST (must be called before emission)
void codegen_build_ir(CodeGenContext *ctx, ASTNode *ast) {
    if (!ctx || !ast) return;
    codegen_stmt(ctx, ast);
}

// Emit object file (.o) directly using LLVM target machine
int codegen_emit_object(CodeGenContext *ctx, const char *output_file) {
    // Verify module
    char *error = NULL;
    if (LLVMVerifyModule(ctx->module, LLVMReturnStatusAction, &error)) {
        fprintf(stderr, COLOR_RED "Module verification failed: %s\n" COLOR_RESET, error);
        LLVMDisposeMessage(error);
        return -1;
    }
    
    // Run optimization passes (currently none)
    LLVMRunPassManager(ctx->pass_manager, ctx->module);
    
    if (!ctx->target_machine) {
        fprintf(stderr, COLOR_RED "Error:" COLOR_RESET " Target machine not initialized\n");
        return -1;
    }
    
    char *err_msg = NULL;
    if (LLVMTargetMachineEmitToFile(ctx->target_machine, ctx->module,
                                     output_file, LLVMObjectFile, &err_msg)) {
        fprintf(stderr, COLOR_RED "Error emitting object file: %s\n" COLOR_RESET, err_msg);
        LLVMDisposeMessage(err_msg);
        return -1;
    }
    
    return 0;
}

// Emit complete executable by emitting object then linking with gcc
int codegen_emit_executable(CodeGenContext *ctx, const char *output_file) {
    // Create temporary object filename
    char obj_file[512];
    snprintf(obj_file, sizeof(obj_file), "%s.o", output_file);
    
    // Emit object file
    if (codegen_emit_object(ctx, obj_file) != 0) {
        return -1;
    }
    
    // Link object file into executable using gcc with -no-pie for non-PIC code
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "gcc -no-pie -o %s %s 2>&1", output_file, obj_file);
    
    int rc = system(cmd);
    if (rc != 0) {
        fprintf(stderr, COLOR_RED "Linking failed (gcc exit code %d)\n" COLOR_RESET, rc);
        remove(obj_file);
        return -1;
    }
    
    // Clean up temporary object file
    remove(obj_file);
    return 0;
}

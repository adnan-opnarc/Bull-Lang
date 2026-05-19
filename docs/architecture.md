# Bull Compiler Architecture

## Pipeline

```
Source (.bl) → Lexer → Tokens → Parser → AST → Codegen → LLVM IR → Object
```

## Component Map

```
src/
├── lexer.c          # Tokenizes source into tokens
├── parser.c         # Builds AST from token stream
├── codegen.c        # LLVM IR generation from AST
├── toml.c           # TOML config parser (bullfc.toml)
├── bullc.c          # Compiler driver / main
├── bullfc.c         # Project manager
├── bpkg.c           # Package manager
├── lexer.h          # Lexer API
├── parser.h         # Parser types / AST node definitions
├── codegen.h        # Codegen context / API
├── token.h          # Token type definitions
└── toml.h           # TOML parser API
```

## Lexer (`lexer.c`)

- Single-pass, character-by-character tokenizer
- Produces tokens with line/column tracking
- Recognizes: identifiers, keywords, numbers (decimal + hex), strings, operators, comments, `{{`/`}}` tokens
- `read_number` supports `0x`/`0X` hex prefix
- `read_string` captures literal characters (no escape interpretation)
- Crate directives `using <name>` are tokenized as `TOKEN_USING` + `TOKEN_IDENTIFIER`
- `{{` and `}}` are tokenized as `TOKEN_LBRACE2` and `TOKEN_RBRACE2` for array syntax

## Parser (`parser.c`)

- Recursive descent, one-token lookahead
- Precedence climbing for expressions
- Function detection via non-destructive peek: `TYPE IDENTIFIER LPAREN` → function; otherwise → var decl
- User-defined type detection: `IDENTIFIER IDENTIFIER not-LPAREN` → var decl of class type

### AST Node Types
| Node | Purpose |
|------|---------|
| `NODE_PROGRAM` | Top-level container (multiple functions/statements) |
| `NODE_FUNCTION` | Function definition |
| `NODE_VAR_DECL` | Variable declaration with optional init and array size |
| `NODE_BINARY_OP` | Binary expression |
| `NODE_UNARY_OP` | Unary expression (`-`, `!`, `++`, `--`) |
| `NODE_CALL` | Function call |
| `NODE_NUMBER` | Integer literal |
| `NODE_BOOLEAN` | Boolean literal |
| `NODE_STRING` | String literal |
| `NODE_IDENTIFIER` | Variable/function name reference |
| `NODE_IF_STMT` | If/else with `is_kern` flag |
| `NODE_RETURN_STMT` | Return statement |
| `NODE_EXPRESSION_STMT` | Expression used as statement |
| `NODE_BLOCK` | Block of statements |
| `NODE_CLASS` | Class definition with fields and methods |
| `NODE_INDEX` | Array index access `arr{{expr}}` |
| `NODE_MEMBER_ACCESS` | Member access `object.field` |
| `NODE_ARRAY_LITERAL` | Array literal `{{1, 2, 3}}` |
| `NODE_ASSIGNMENT` | Assignment with LHS target |

## Codegen (`codegen.c`)

- Uses LLVM-C API to build IR in memory
- **Functions**: LLVM `define` with allocas for parameters + entry block
- **Variables**: alloca + store at declaration, load at reference
- **Arrays**: `LLVMArrayType` alloca for fixed-size arrays; GEP with 2 indices (`[0, index]`) for access
- **Calls**: `LLVMBuildCall2` for function calls; auto-declares unknown functions
- **Classes**: Struct type via `LLVMStructCreateNamed`; methods get mangled names (`Class_method`) with implicit `this` pointer
- **Method calls**: Dispatch via `object.method(args)` using local variable type tracking and mangled function lookup
- **Built-ins**: `print` → `printf`, `input` → `scanf`, `syscall` → inline asm
- **Inline asm**: Uses `={rax}` constraint format (LLVM 22 compatible)
- **kernif/kernelse**: Block names `kern_then`, `kern_else`, `kern_ifcont`
- **Local scope**: Reset per function via `codegen_init_locals(ctx)`
- **Type tracking**: Locals table stores type strings for method dispatch

### Type Mapping
| Bull | LLVM |
|------|------|
| `int` | `i64` |
| `bool` | `i1` |
| `glass` | `void` |
| `int{{N}}` | `[N x i64]` |
| User class | pointer to `%classname` struct |

### Emission
- Bitcode via `LLVMWriteBitcodeToFile`
- Object via `LLVMTargetMachineEmitToFile`
- Executable: emit `.o` → link with `gcc -no-pie`
- Archive: emit `.o` → `ar rcs`

## LLVM Compatibility Notes

- LLVM 22 only — `LLVMBuildCall2` (not `LLVMBuildCall`)
- `LLVMTypeOf` on a function returns pointer type (opaque `ptr`), not function type — use stored function types for calls
- Inline asm constraints use `{rax}`/`{rdi}`/etc. (LLVM 22 rejects `=a`/`0` format)
- Target triple: `x86_64-redhat-linux-gnu`; data layout: `e-m:e...S128`
- Opaque pointers: `LLVMGetElementType` on opaque pointers returns garbage; store types explicitly

## Crate System

```
bullfc.toml [crates] ──→ bpkg install ──→ git clone ──→ bullc -ar ──→ .a
                                                              ↓
                                              bullc -exe ──→ gcc link ──→ executable
```

- Source files use `using <name>` to import a crate
- Crates listed in `[crates]` are cloned from a configurable git mirror
- Each crate is compiled to a static archive via `bullc -ar`
- Final executable links against all `crates/*/build/*.a` archives
- The `-i` flag automates: `bpkg install` → compile → link

# Bull Programming Language

A systems programming language designed for kernel-level development with the Ark kernel.

## Components

- **bullc** - The Bull compiler (written in C with LLVM backend)
- **bullfc** - Project manager and build configuration tool  
- **bpkg** - Package/crate manager
- **bullv** - Version control and build artifact manager (git-like snapshots)

## Language Features

- Simple, clean syntax inspired by C
- Direct integration with Ark kernel
- LLVM-based code generation to x86_64
- Crate-based package system using `using <name>` syntax
- OOP support with classes, fields, methods, and `this`
- Fixed-size arrays with `{{ }}` syntax
- Procedural with class-based OOP

## Quick Start

```bash
# Build the compiler
make

# Initialize a new Bull project
./bullfc myproject
cd myproject

# Edit src/main.bl
cat src/main.bl

# Compile
../bullc -- bullfc.toml

# Run (with qemu or direct execution)
# bullc -- bullfc.toml -r
```

## Language Syntax

See `docs/language-spec.md` for full language specification.

## Example

```bull
using standard

class Calculator {
    int last_result;
    
    glass init() {
        this.last_result = 0;
    }
    
    int add(int a, int b) {
        this.last_result = a + b;
        return this.last_result;
    }
}

int main() {
    Calculator calc;
    calc.init();
    
    int result = calc.add(5, 3);
    print(result);
    
    return 0;
}
```

## Building

```bash
make              # Build compiler and tools
make test         # Run basic tests
make clean        # Remove build artifacts
make install      # Install to /usr/local/bin
```

## Requirements

- GCC or Clang
- LLVM 22+ (for code generation)
- Linux x86_64

## License

(c) 2026 adnan-opnarc

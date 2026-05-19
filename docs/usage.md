# Bull Compiler Usage Guide

## Quick Start

```bash
# Build the compiler
make

# Create a new project
./bullfc myproject
cd myproject

# Edit source
# src/main.bl:

# using standard
# int main() {
#     print("Hello, Bull!\n");
#     return 0;
# }

# Compile to executable
../bullc -exe -- bullfc.toml

# Run
./build/demo
```

## Compilation Examples

### Single file → executable
```bash
bullc -exe hello.bl -o hello
./hello
```

### Single file → object
```bash
bullc -obj io.bl -o io.o
```

### Crate project → executable
```bash
# From project root with bullfc.toml
bullc -i -exe -- bullfc.toml
```

### Multiple source files
```bash
bullc -exe -- bullfc.toml
```

The config file's `[srcs]` section lists all source files:
```toml
[srcs]
main = "src/main.bl"
helper = "src/helper.bl"
```

## Crate Usage

### Install a crate
```bash
bpkg install standard
# or install all from bullfc.toml
bpkg install
```

### List installed crates
```bash
bpkg list
```

### Use a crate in your project
```toml
[crates]
standard = "0.1"
```

Then in your source:
```bull
using standard

int main() {
    sys_write(1, msg, 5);
    return 0;
}
```

## Working with the Standard Library

The standard crate must be built before use:

```bash
# Build standard library
cd standard
../bullc -ar -- bullfc.toml -o build/standard.a

# Copy to your project
mkdir -p ../myproject/crates/standard/build
cp build/standard.a ../myproject/crates/standard/build/
```

## Debugging

The compiler emits debug output to stderr:
- `DEBUG call: callee=0x...` — function call resolution
- `DEBUG var_decl: name=..., type=...` — variable declaration
- `Debug: Writing bitcode to '...'` — output path

To see generated LLVM IR:
```bash
bullc file.bl -o /tmp/out.bc
llvm-dis-22 /tmp/out.bc -o /tmp/out.ll
cat /tmp/out.ll
```

## Environment

- **LLVM**: Requires LLVM 22. The Makefile uses `llvm-config` to locate it.
- **Linker**: Uses `gcc` with `-no-pie` for final executable linking.
- **OS**: Linux x86_64 (targets System V ABI).

## Make Targets

| Target | Description |
|--------|-------------|
| `make` | Build bullc, bullfc, bpkg |
| `make test` | Run basic compilation test |
| `make clean` | Remove build artifacts |
| `make install` | Install to `/usr/local/bin` |
| `make llvm-info` | Show LLVM configuration |

## Example Programs

### Hello World
```bull
using standard

int main() {
    print("Hello, World!\n");
    return 0;
}
```

### Variables and Arithmetic
```bull
using standard

int main() {
    int a = 10;
    int b = 32;
    print(a + b);   // 42
    return 0;
}
```

### Function Call
```bull
using standard

int add(int x, int y) {
    return x + y;
}

int main() {
    int r = add(5, 3);
    print(r);       // 8
    return 0;
}
```

### Arrays
```bull
using standard

int main() {
    int arr{{5}};
    arr{{0}} = 10;
    arr{{1}} = 20;
    print(arr{{0}} + arr{{1}});  // 30
    return 0;
}
```

### Classes
```bull
using standard

class Counter {
    int value;

    glass init() {
        this.value = 0;
    }

    glass inc() {
        this.value = this.value + 1;
    }

    int get() {
        return this.value;
    }
}

int main() {
    Counter c;
    c.init();
    c.inc();
    c.inc();
    print(c.get());   // 2
    return 0;
}
```

### Input
```bull
using standard

int main() {
    int x;
    input(x);
    print(x + 1);
    return 0;
}
```

### For Loop
```bull
using standard

int main() {
    for (int i = 0; i < 5; i = i + 1) {
        print(i);
    }
    return 0;
}
```

### While Loop
```bull
using standard

int main() {
    int i = 0;
    while (i < 5) {
        print(i);
        i = i + 1;
    }
    return 0;
}
```

### Syscall (exit)
```bull
using standard

int main() {
    syscall(60, 0);   // exit(0)
    return 0;
}
```

### Kernel If
```bull
using standard

int main() {
    kernif (1) {
        print("kernel path\n");
    } kernelse {
        print("regular path\n");
    }
    return 0;
}
```

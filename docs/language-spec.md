# Bull Language Specification

Bull is a systems programming language for kernel-level development with the Ark kernel. It compiles to x86_64 machine code via LLVM 22.

## Lexical Structure

### Keywords
`print` `input` `class` `int` `char` `glass` `bool` `matrix` `call` `rc`
`kernif` `kernelse` `else` `if` `return` `true` `false` `this` `var`
`using` `for` `while`

### Comments
```bull
// Single line
/* Multi-line */
```

### Literals
| Kind | Example |
|------|---------|
| Decimal integer | `42`, `0`, `-1` |
| Hex integer | `0xFF`, `0x22` |
| Boolean | `true`, `false` |
| Character | `'A'`, `'\n'` (ASCII value, supports escapes) |
| String | `"hello\n"` (escape sequences parsed literally) |

### Crate Directive
```bull
using standard
using sys
using mem
```
The `using` keyword followed by a crate name imports a crate at compile time. The `[crates]` section in `bullfc.toml` controls linking of pre-compiled archives.

## Types

| Type | Description | LLVM Mapping |
|------|-------------|-------------|
| `int` | Signed 64-bit integer | `i64` |
| `bool` | Boolean | `i1` |
| `char` | 8-bit character (ASCII) | `i8` |
| `char2` | 16-bit word character | `i16` |
| `char4` | 32-bit double-word character | `i32` |
| `glass` | Void (no value) | `void` |
| `int{{N}}` | Array of N integers | `[N x i64]` |
| User types | Class/struct instances | pointer to struct |

## Variables

```bull
int x = 42;
bool flag = true;
int y;          // zero-initialized
```

### Arrays
```bull
int arr{{5}};            // fixed-size array of 5 ints
arr{{0}} = 10;           // index assignment
arr{{i}} = arr{{i+1}};   // indexed read/write
int val = arr{{2}};      // indexed read
```

Array literals use `{{ }}`:
```bull
int sum = {{1, 2, 3, 4, 5}};  // array literal (5 elements)
```

## Functions

```bull
int main() {
    print("hello\n");
}

int add(int a, int b) {
    return a + b;
}
```

Parameters are passed by value. Return type defaults to `glass` (void). `return` without a value is valid in `glass` functions.

## Classes

```bull
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
    print(c.get());    // prints: 1
    return 0;
}
```

- Fields are declared as variables inside the class body
- Methods receive an implicit `this` pointer as the first parameter
- Member access uses dot notation: `this.field`, `object.method()`
- Methods can be called on instances: `c.method(args)`

## Control Flow

### If
```bull
if (x > 0) {
    print("positive\n");
} else {
    print("non-positive\n");
}
```

### While
```bull
int i = 0;
while (i < 10) {
    print(i);
    i = i + 1;
}
```

### For
```bull
for (int i = 0; i < 10; i = i + 1) {
    print(i);
}
```

### Kernel If/Else
```bull
kernif (condition) {
    // generates LLVM block "kern_then"
} kernelse {
    // generates LLVM block "kern_else"
}
// merge block is "kern_ifcont"
```
Used for kernel-level branching in the Ark kernel. IR block names are prefixed with `kern_`.

## Built-in Functions

### `print(value)`
Outputs to stdout. If the argument is a string pointer (`i8*`), prints it directly. If it's an integer (`i64`), auto-formats as `"%lld\n"`.

```bull
print("hello\n");   // prints: hello\n
print(42);           // prints: 42
```

### `input(var)`
Reads a 64-bit integer from stdin into a variable.

```bull
int x;
input(x);
```

### `syscall(number, arg1, ...)`
Inline syscall — generates `syscall` instruction with register constraints. Up to 6 arguments + syscall number.

```bull
syscall(60, 0);     // exit(0)
syscall(1, 1, addr, len);  // write(1, addr, len)
```

### `sys_exit`, `sys_write`, `sys_read`, `sys_open`, `sys_close`, `sys_brk`
Provided by the `standard` crate. Wrappers around raw syscalls.

## Crate System

Source-level crate directives (`using <name>`) are parsed by the lexer as `TOKEN_USING` + `TOKEN_IDENTIFIER`. At build time, `bullfc.toml` `[crates]` entries are resolved via `bpkg`:

```toml
[crates]
standard = "0.1"
```

Crates are cloned via git from a configurable mirror, compiled to `.a` archives, and linked into the final executable.

## Operators

### Arithmetic
`+` `-` `*` `/` `%`

### Comparison
`==` `!=` `<` `>` `<=` `>=`

### Logical
`!` `&&` `||`

### Assignment
`=` `+=` `-=` `*=` `/=` `%=` `&=` `|=` `^=`

### Increment/Decrement
`++` `--`

### Bitwise
`&` `|` `^` `~` `<<` `>>`

### Array Index
`{{` `}}` — used for array indexing and array literals

### Member Access
`.` — access class fields and methods

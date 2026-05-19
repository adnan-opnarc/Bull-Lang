# Bull Standard Library

The `standard` crate provides OS-level helpers via raw Linux syscalls and basic I/O.

## Module: `io`

Basic console input/output using libc `printf`/`scanf`.

### `glass print_int(int x)`
Print an integer to stdout followed by a newline.
```bull
using standard

int main() {
    print_int(42);   // prints: 42
    return 0;
}
```

### `int read_int()`
Read a 64-bit integer from stdin.
```bull
using standard

int main() {
    int x = read_int();
    return 0;
}
```

### `glass print_str(int s)`
Print a string (passed as address via `int`). Currently prints the address value due to type erasure.

### `glass print_ln()`
Print a newline.
```bull
using standard

int main() {
    print_ln();      // prints: \n
    return 0;
}
```

## Module: `sys`

Raw Linux syscall wrappers. Each function maps to a syscall via `syscall()` built-in with inline assembly.

### `int sys_exit(int code)`
Exit the process (syscall 60).
```bull
sys_exit(0);
```
Note: does not flush stdio buffers — use `return` from `main` for normal exit.

### `int sys_write(int fd, int buf, int count)`
Write to a file descriptor (syscall 1).
```bull
sys_write(1, str_addr, len);  // write to stdout
```

### `int sys_read(int fd, int buf, int count)`
Read from a file descriptor (syscall 0).
```bull
sys_read(0, buf_addr, len);  // read from stdin
```

### `int sys_open(int path, int flags, int mode)`
Open a file (syscall 2).
```bull
int fd = sys_open(str_addr, 0, 0);  // O_RDONLY
```

### `int sys_close(int fd)`
Close a file descriptor (syscall 3).
```bull
sys_close(fd);
```

### `int sys_brk(int addr)`
Set the program break (syscall 12). Used for memory allocation.
```bull
int heap = sys_brk(0);  // get current break
sys_brk(heap + size);   // extend heap
```

## Module: `mem`

Basic memory allocation via `sys_brk`.

### `int allocate(int size)`
Allocate memory by extending the heap. Wraps `mmap` syscall (9) with `MAP_ANONYMOUS` for zero-initialized pages.

### `int deallocate(int addr, int size)`
Deallocate memory via `munmap` syscall (11).

## Crate Configuration

```toml
[project]
name = "standard"
org = "bull.std"
ver = "0.1"

[srcs]
io = "src/io.bl"
sys = "src/sys.bl"
mem = "src/mem.bl"

[build]
target = "x86_64_lib"
name.out = "standard"
```

## Using the Standard Library

In your Bull source file, import with `using`:

```bull
using standard
```

## Building the Standard Library

```bash
cd standard
bullc -ar -- bullfc.toml -o build/standard.a
```

Install for use in other projects:
```bash
mkdir -p myproject/crates/standard/build
cp standard/build/standard.a myproject/crates/standard/build/
```

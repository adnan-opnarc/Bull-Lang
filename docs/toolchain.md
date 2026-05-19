# Bull Toolchain Reference

## Components

| Tool    |                           Description                               |
|-------------------------------------------------------------------------------|
| `bullc` | Compiler — `.bl` → bitcode/object/executable                        |
| `bullfc`| Project manager — scaffold and configure Bull projects              |
| `bpkg`  | Package manager — install and build crate dependencies              |
| `bullv` | Version/build artifact manager — track builds and generate metadata |

---

## `bullc` — Compiler

### Usage
```
bullc [options] <input.bl>
bullc [options] <bullfc.toml>
```

### Options

| Flag | Description |
|------|-------------|
| `-o <file>` | Output filename |
| `-obj` | Emit object file (`.o`) |
| `-exe` / `-bin` / `-elf` | Emit executable (links with gcc) |
| `-ar` | Emit static library (`.a`) via `ar` |
| `-r` | Run the compiled executable after build |
| `-i` | Install crates from `bullfc.toml` before compiling |

### Output Modes

**Bitcode** (default):
```bash
bullc src/main.bl
# produces: out.bc
```

**Object file**:
```bash
bullc -obj src/io.bl -o io.o
```

**Executable**:
```bash
bullc -exe src/main.bl -o program
# links with gcc -no-pie + crates/*/build/*.a
```

**Static library**:
```bash
bullc -ar -- bullfc.toml -o mylib.a
# compiles all [srcs] and archives into .a
```

**One-step install + build**:
```bash
bullc -i -exe -- bullfc.toml
# 1. bpkg install (clones crates from [crates])
# 2. compile sources
# 3. link into executable
```

### Config File Mode

When passed a `.toml` config, `bullc` reads `[srcs]` for source files and `[build]` for output configuration:

```toml
[srcs]
main = "src/main.bl"
other = "src/helper.bl"

[build]
target = "x86_64_exec"
name.out = "myprogram"
```

---

## `bullfc` — Project Manager

### Usage
```
bullfc <project-name>
bullfc .
bullfc --update .
```

### What it creates
```
myproject/
├── bullfc.toml       # project config
├── bpkg.toml         # mirror config
├── src/
│   └── main.bl       # default source
├── build/            # output directory
└── crates/           # installed dependencies
```

### Default `bullfc.toml`
```toml
[project]
name = "demo"
org = "com.demo.demo"
ver = "1.0"

[toolchain]
arch = "x86_64"
source = "open"

[srcs]
main = "src/main.bl"

[crates]
standard = "0.1"

[build]
target = "x86_64_exec"
name.out = "demo"
```

### Default `src/main.bl`
```bull
using standard

int main() {
    print("Hello, Bull!\n");
    return 0;
}
```

### Default `bpkg.toml`
```toml
[mirrors]
official = "https://github.com/adnan-opnarc"
```

---

## `bpkg` — Package Manager

### Usage
```
bpkg install            # install all crates from bullfc.toml [crates]
bpkg install <crate>    # install a specific crate
bpkg list               # list installed crates
bpkg update             # create/update package.lst registry
```

### Install Flow
1. Read mirror URL from `bpkg.toml` → `bullfc.toml [mirrors]` → fallback `https://github.com/adnan-opnarc`
2. `git clone <mirror>/<name>.git` into `crates/<name>/`
3. Run `bullc -- bullfc.toml -ar` inside the cloned crate to build `.a`
4. Result: `crates/<name>/build/<name>.a`

### Mirror Resolution
```
bpkg.toml [mirrors]              # checked first
→ bullfc.toml [mirrors]          # checked second
→ https://github.com/adnan-opnarc  # default fallback
```

---

## Directory Layout

```
project/
├── bullfc.toml        # project configuration
├── bpkg.toml          # package mirror config
├── src/
│   └── main.bl        # source files
├── build/
│   └── program        # compiled output
├── crates/
│   └── standard/
│       ├── bullfc.toml
│       ├── src/
│       │   ├── io.bl
│       │   ├── sys.bl
│       │   └── mem.bl
│       └── build/
│           └── standard.a   # pre-built archive
└── docs/
    └── ...
```

---

## `bullv` — Version Manager

### Usage
```
bullv init              # initialize .bullv/ tracking directory and inf.txt
bullv generate          # generate/update inf.txt from bullfc.toml
bullv info              # show current version and build information
bullv list              # list build artifacts in .bullv/
bullv log               # show snapshot history
bullv -a "<message>"    # snapshot current source state (like git commit)
bullv -s <id> [path]    # restore a snapshot to a directory (like git checkout)
bullv -d <id>           # delete a snapshot
bullv -m <id> "<msg>"   # amend a snapshot's message
```

### Snapshot System (git-like)

Snapshots are stored in `.bullv/snapshots/<random-8-char-id>/` with a `snapshot.txt` metadata file. Each snapshot contains a full copy of `src/`, `bullfc.toml`, and `bpkg.toml`.

### Example workflow
```bash
# Initialize version tracking in your project
bullv init

# Take snapshots as you develop
bullv -a "initial project setup"
# ... edit code ...
bullv -a "add input parsing"
# ... edit code ...
bullv -a "fix edge case"

# View history
bullv log
#   [a3f8k2m1] HEAD "fix edge case"
#   [x7p1q9r4]       "add input parsing"
#   [sw1v6p8e]       "initial project setup"

# Restore a previous snapshot
bullv -s x7p1q9r4

# Restore to a different directory
bullv -s sw1v6p8e ../old-version

# Delete a snapshot
bullv -d x7p1q9r4

# Amend a snapshot message
bullv -m sw1v6p8e "initial project scaffold"
```

### What it creates
```
.bullv/
├── inf.txt             # build metadata (pkgname, org, version, built, target, name.out)
├── HEAD                # points to current snapshot id
└── snapshots/
    └── <random-id>/
        ├── snapshot.txt   # metadata (id, message, timestamp, parent)
        ├── src/           # copied source files
        ├── bullfc.toml    # project config copy
        └── bpkg.toml      # package mirror config copy
```

### `inf.txt` format
```
pkgname = "myproject"
org = "com.user_bull.myproject"
version = "1.0"
built = "2026-05-18 12:00:00"
target = "x86_64"
name.out = "myproject"
```

---

## LLVM Integration

- Uses LLVM-C API (Core, ExecutionEngine, Target, Analysis, BitWriter)
- Requires LLVM 22+
- Generates x86_64 object files, links with `gcc -no-pie`
- Backend: `codegen.c` builds IR, then emits object or bitcode
- Opaque pointers require explicit function type storage for calls

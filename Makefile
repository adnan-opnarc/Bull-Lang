# Bull Compiler Build System
# OpenArc-1 BUll Compiler --- (c) 2025-2026

CC = gcc
CXX = g++
CFLAGS = -Wall -Wextra -std=c11 -O2 -g0 -s -g
LDFLAGS =
STATIC_LLVM_LIBS = $(shell $(LLVM_CONFIG) --link-static --libs core support native 2>/dev/null)
STATIC_LLVM_SYSLIBS = $(shell $(LLVM_CONFIG) --link-static --system-libs 2>/dev/null)

# LLVM configuration
LLVM_CONFIG ?= llvm-config
LLVM_VERSION ?= $(shell $(LLVM_CONFIG) --version 2>/dev/null || echo 22)
LLVM_CFLAGS = $(shell $(LLVM_CONFIG) --cflags 2>/dev/null)
LLVM_LDFLAGS = $(shell $(LLVM_CONFIG) --ldflags 2>/dev/null)
LLVM_LIBS = $(shell $(LLVM_CONFIG) --libs core support native 2>/dev/null)
LLVM_LIBS += $(shell $(LLVM_CONFIG) --system-libs 2>/dev/null)

# Source files
SRC_DIR = src
SRCS = $(SRC_DIR)/lexer.c $(SRC_DIR)/parser.c $(SRC_DIR)/codegen.c $(SRC_DIR)/toml.c $(SRC_DIR)/bullc.c $(SRC_DIR)/error.c
SRCS_FC = $(SRC_DIR)tools/bullfc.c $(SRC_DIR)/toml.c
SRCS_BPKG = $(SRC_DIR)tools/bpkg.c $(SRC_DIR)/toml.c
SRCS_BULLV = $(SRC_DIR)tools/bullv.c $(SRC_DIR)/toml.c
OBJS = $(SRCS:.c=.o)
OBJS_FC = $(SRCS_FC:.c=.o)
OBJS_BPKG = $(SRCS_BPKG:.c=.o)
OBJS_BULLV = $(SRCS_BULLV:.c=.o)
DEPS = $(SRCS:.c=.d) $(SRCS_FC:.c=.d) $(SRCS_BPKG:.c=.d) $(SRCS_BULLV:.c=.d)

# Targets
TARGET = bullc
TARGET_FC = bullfc
TARGET_BPKG = bpkg
TARGET_BULLV = bullv

# Default target
.PHONY: all
all: $(TARGET) $(TARGET_FC) $(TARGET_BPKG) $(TARGET_BULLV)

# Static build target (statically linked binaries)
.PHONY: static
static: STATIC_FLAGS = -static
static: CFLAGS += $(STATIC_FLAGS)
static: LDFLAGS += $(STATIC_FLAGS)
static: LLVM_LIBS = $(STATIC_LLVM_LIBS) $(STATIC_LLVM_SYSLIBS)
static: clean all

# Main compiler
$(TARGET): src/lexer.o src/parser.o src/codegen.o src/toml.o src/tools/bullc.o src/error.o
	@echo "  CC	  src/tools/bullc.o"
	@echo "  LD      $@"
	@$(CXX) $(CFLAGS) $(LDFLAGS) $(LLVM_LDFLAGS)  -o $@ $^ $(LLVM_LIBS)  2>&1 || (rm -f $@; exit 1)
#	@echo "  Built $@ "

# Project manager
$(TARGET_FC): src/toml.o src/tools/bullfc.o
	@echo "  CC	  src/tools/bullfc.o"
	@echo "  LD      $@"
	@$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ 2>&1 || (rm -f $@; exit 1)
#	@echo "  Built $@ "

# Package manager
$(TARGET_BPKG): src/toml.o src/tools/bpkg.o
	@echo "  CC	  src/tools/bpkg.o"
	@echo "  LD      $@"
	@$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ 2>&1 || (rm -f $@; exit 1)
#	@echo "  Built $@ "

# Version manager
$(TARGET_BULLV): src/toml.o src/tools/bullv.o
	@echo "  CC	  src/tools/bullv.o"
	@echo "  LD      $@"
	@$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ 2>&1 || (rm -f $@; exit 1)
#	@echo "  Built $@ "

# Compile source files
$(SRC_DIR)/%.o: $(SRC_DIR)/%.c
	@echo "  CC      $@"
	@$(CC) $(CFLAGS) $(LLVM_CFLAGS) -I$(SRC_DIR) -c $< -o $@ 2>&1 || (rm -f $@; exit 1)

# Generate dependencies
$(SRC_DIR)/%.d: $(SRC_DIR)/%.c
	@$(CC) $(CFLAGS) $(LLVM_CFLAGS) -I$(SRC_DIR) -MM -MT $(@:.d=.o) $< > $@ 2>/dev/null || true

# Include dependencies
-include $(DEPS)

# Test target
.PHONY: test
test: $(TARGET)
	@echo "Running tests..."
	@chmod +x tests/run_tests.sh
	@tests/run_tests.sh

# Run with test (legacy)
.PHONY: run-test
run-test: test

# Clean build artifacts
.PHONY: clean
clean:
	@echo "  CLEAN SRC/.o"
	@echo "  CLEAN $(TARGET)"
	@echo "  CLEAN $(TARGET_BPKG)"
	@echo "  CLEAN $(TARGET_FC)"
	@echo "  CLEAN $(TARGET_BULLV)"
	@rm -f $(TARGET) $(TARGET_BPKG) $(OBJS) $(DEPS) $(TARGET_FC) $(TARGET_BULLV) $(OBJS_BULLV)
	@rm -f /tmp/test.bl /tmp/test.bc
#	@echo "Clean complete"

# Install (to local bin)
.PHONY: install
install: $(TARGET)
	@install -m 755 $(TARGET) /usr/local/bin/$(TARGET)
	@echo "  INS /usr/local/bin/$(TARGET)"
	@install -m 755 $(TARGET_FC) /usr/local/bin/$(TARGET_FC)
	@echo "  INS /usr/local/bin/$(TARGET_FC)"
	@install -m 755 $(TARGET_BPKG) /usr/local/bin/$(TARGET_BPKG)
	@echo "  INS /usr/local/bin/$(TARGET_BPKG)"
	@install -m 755 $(TARGET_BULLV) /usr/local/bin/$(TARGET_BULLV)
	@echo "  INS /usr/local/bin/$(TARGET_BULLV)"

# Uninstall
.PHONY: uninstall
uninstall:
#	@echo "Uninstalling from /usr/local/bin/"
	@echo "  UINS $(TARGET)"
	@rm -f /usr/local/bin/$(TARGET)
	@echo "  UINS $(TARGET_FC)"
	@rm -f /usr/local/bin/$(TARGET_FC)
	@echo "  UINS $(TARGET_BPKG)"
	@rm -f /usr/local/bin/$(TARGET_BPKG)
	@echo "  UINS $(TARGET_BULLV)"
	@rm -f /usr/local/bin/$(TARGET_BULLV)
#	@echo "Uninstall complete"

# Install syntax highlighting
.PHONY: syn
syn:
	@echo "Installing syntax highlighting..."
	@mkdir -p ~/.vim/syntax && cp syntax/bull.vim ~/.vim/syntax/bull.vim && echo "  Vim:     ~/.vim/syntax/bull.vim"
	@mkdir -p ~/.vim/ftdetect && printf 'au BufRead,BufNewFile *.bl set filetype=bull\n' > ~/.vim/ftdetect/bull.vim && echo "  Vim:     ~/.vim/ftdetect/bull.vim"
	@mkdir -p ~/.nano/syntax && cp syntax/bull.nanorc ~/.nano/syntax/bull.nanorc && echo "  Nano:    ~/.nano/syntax/bull.nanorc"
	@if ! grep -q "include ~/.nano/syntax/bull.nanorc" ~/.nanorc 2>/dev/null; then echo 'include "~/.nano/syntax/bull.nanorc"' >> ~/.nanorc && echo "  Added include to ~/.nanorc"; fi
	@EXTDIR=~/.vscode/extensions/bull-lang.bull-1.0.0; \
	mkdir -p $$EXTDIR/syntaxes; \
	cp syntax/bull.tmLanguage.json $$EXTDIR/syntaxes/; \
	cp syntax/bull.package.json $$EXTDIR/package.json 2>/dev/null || printf '{"name":"bull","publisher":"bull-lang","version":"1.0.0","engines":{"vscode":"^1.0.0"},"contributes":{"languages":[{"id":"bull","aliases":["Bull","bull"],"extensions":[".bl"],"configuration":{}}],"grammars":[{"language":"bull","scopeName":"source.bull","path":"./syntaxes/bull.tmLanguage.json"}]}}\n' > $$EXTDIR/package.json; \
	python3 syntax/install-vscode.py && echo "  VS Code: $$EXTDIR"
	@echo "Syntax highlighting installed. Restart your editor to apply."

# Show LLVM config
.PHONY: llvm-info
llvm-info:
	@echo "LLVM Version: $(LLVM_VERSION)"
	@echo "LLVM CFLAGS: $(LLVM_CFLAGS)"
	@echo "LLVM LIBS: $(LLVM_LIBS)"

# Help
.PHONY: help
help:
	@echo "Bull Compiler Build System"
	@echo ""
	@echo "Targets:"
	@echo "  all         Build compiler, project manager, package manager, version manager"
	@echo "  static      Build statically linked binaries"
	@echo "  test        Run basic compilation test"
	@echo "  run-test    Run tests"
	@echo "  clean       Remove build artifacts"
	@echo "  install     Install to /usr/local/bin"
	@echo "  uninstall   Remove from /usr/local/bin"
	@echo "  syn         Install syntax highlighting (vim, nano)"
	@echo "  llvm-info   Show LLVM configuration"
	@echo "  help        Show this help message"

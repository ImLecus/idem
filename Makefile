# Idem -- build system
#
# The reference compiler is strict C89.  Sources are discovered with
# wildcard, so adding a .c file under src/ or runtime/ requires no edit
# here.
#
# Targets:
#   make            build the compiler when src/*.c exists
#   make runtime    build build/libidemrt.a when runtime/*.c exists
#   make test       run the test suite (runner not implemented yet)
#   make check      clean rebuild
#   make dirs       create the directory skeleton
#   make clean      remove build artifacts

CC       ?= cc
AR       ?= ar
CFLAGS   ?= -std=c89 -pedantic -Wall -Wextra -O2 -g
CPPFLAGS ?=
LDFLAGS  ?=
LDLIBS   ?= -lm

BIN         := idem
BUILD       := build
OBJDIR      := $(BUILD)/obj
RUNTIME_DIR := $(BUILD)/runtime
RUNTIME_LIB := $(BUILD)/libidemrt.a

SRC         := $(wildcard src/*.c)
OBJ         := $(patsubst src/%.c,$(OBJDIR)/%.o,$(SRC))
RUNTIME_SRC := $(wildcard runtime/*.c)
RUNTIME_OBJ := $(patsubst runtime/%.c,$(RUNTIME_DIR)/%.o,$(RUNTIME_SRC))

.PHONY: all compiler runtime test check dirs clean help

all: compiler

compiler: $(BIN)

$(BIN): $(OBJ)
	@if [ -z "$(SRC)" ]; then \
		echo "idem: no compiler sources in src/ yet (structure only)"; \
	else \
		$(CC) $(LDFLAGS) -o $@ $(OBJ) $(LDLIBS); \
	fi

$(OBJDIR)/%.o: src/%.c
	@mkdir -p $(OBJDIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

runtime: $(RUNTIME_LIB)

$(RUNTIME_LIB): $(RUNTIME_OBJ)
	@mkdir -p $(BUILD)
	@if [ -z "$(RUNTIME_SRC)" ]; then \
		echo "runtime: no runtime sources in runtime/ yet (structure only)"; \
	else \
		$(AR) rcs $@ $(RUNTIME_OBJ); \
	fi

$(RUNTIME_DIR)/%.o: runtime/%.c
	@mkdir -p $(RUNTIME_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

test: compiler
	@echo "test: no test runner yet"
	@echo "      add unit tests under tests/unit/ and golden tests under tests/golden/"

check: clean compiler

dirs:
	@mkdir -p src runtime tests/unit tests/golden tests/errors $(OBJDIR) $(RUNTIME_DIR)
	@touch src/.gitkeep runtime/.gitkeep tests/unit/.gitkeep tests/golden/.gitkeep tests/errors/.gitkeep

clean:
	rm -rf $(BUILD) $(BIN)

help:
	@echo "Idem build targets:"
	@echo "  all (default)  build the compiler when src/*.c exists"
	@echo "  compiler       same as all"
	@echo "  runtime        build build/libidemrt.a when runtime/*.c exists"
	@echo "  test           run the test suite (runner not implemented yet)"
	@echo "  check          clean rebuild of the compiler"
	@echo "  dirs           create the directory skeleton"
	@echo "  clean          remove build artifacts"

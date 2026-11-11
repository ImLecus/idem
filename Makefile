# Idem -- build system
#
# The reference compiler is strict C89.  Sources are discovered with
# wildcard, so adding a .c file under src/ or runtime/ requires no edit
# here.
#
# Targets:
#   make            build the compiler when src/*.c exists
#   make runtime    build build/libidemrt.a when runtime/*.c exists
#   make test       build and run the unit tests
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
CORE_OBJ    := $(filter-out $(OBJDIR)/main.o,$(OBJ))
RUNTIME_SRC := $(wildcard runtime/*.c)
RUNTIME_OBJ := $(patsubst runtime/%.c,$(RUNTIME_DIR)/%.o,$(RUNTIME_SRC))

UNIT_SRC    := $(wildcard tests/unit/*.c)
UNIT_OBJ    := $(patsubst tests/unit/%.c,$(BUILD)/unit/%.o,$(UNIT_SRC))
UNIT_BIN    := $(BUILD)/unit-tests

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
	$(CC) $(CPPFLAGS) -Isrc $(CFLAGS) -c $< -o $@

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
	$(CC) $(CPPFLAGS) -Isrc $(CFLAGS) -c $< -o $@

test: compiler $(UNIT_BIN)
	@if [ -x "$(UNIT_BIN)" ]; then \
		"$(UNIT_BIN)"; \
	else \
		echo "test: no unit tests yet"; \
	fi

$(UNIT_BIN): $(UNIT_OBJ) $(CORE_OBJ)
	@if [ -z "$(UNIT_SRC)" ]; then \
		echo "unit: no unit tests in tests/unit/ yet"; \
	else \
		$(CC) $(LDFLAGS) -o $@ $(UNIT_OBJ) $(CORE_OBJ) $(LDLIBS); \
	fi

$(BUILD)/unit/%.o: tests/unit/%.c
	@mkdir -p $(BUILD)/unit
	$(CC) $(CPPFLAGS) -Isrc $(CFLAGS) -c $< -o $@

check: clean compiler

dirs:
	@mkdir -p src runtime tests/unit tests/golden tests/errors $(OBJDIR) $(RUNTIME_DIR)
	@touch runtime/.gitkeep tests/golden/.gitkeep tests/errors/.gitkeep

clean:
	rm -rf $(BUILD) $(BIN)

help:
	@echo "Idem build targets:"
	@echo "  all (default)  build the compiler when src/*.c exists"
	@echo "  compiler       same as all"
	@echo "  runtime        build build/libidemrt.a when runtime/*.c exists"
	@echo "  test           build and run unit tests from tests/unit/"
	@echo "  check          clean rebuild of the compiler"
	@echo "  dirs           create the directory skeleton"
	@echo "  clean          remove build artifacts"

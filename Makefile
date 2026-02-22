# Heluna Toolchain
# Build: make
# Clean: make clean
# Test:  make test

CC       = cc
CFLAGS   = -std=c11 -Wall -Wextra -Wpedantic -Iinclude
LDFLAGS  =
AR       = ar

# Debug vs release
ifdef DEBUG
  CFLAGS += -g -O0 -DHELUNA_DEBUG
else
  CFLAGS += -O2
endif

# Gather sources
LIB_SRC    = $(wildcard src/*.c)
LIB_OBJ    = $(LIB_SRC:src/%.c=build/obj/%.o)
LIB        = build/libheluna.a

VENDOR_SRC = $(wildcard include/vendor/*.c)
VENDOR_OBJ = $(VENDOR_SRC:include/vendor/%.c=build/obj/vendor/%.o)

TOOLS_SRC  = $(wildcard tools/*.c)
TOOLS_BIN  = $(TOOLS_SRC:tools/%.c=bin/%)

TEST_SRC   = $(wildcard test/*.c)
TEST_BIN   = $(TEST_SRC:test/%.c=build/test/%)

# ── Targets ──────────────────────────────────────────────

.PHONY: all clean test

all: $(TOOLS_BIN)

# Static library from src/ and vendored code
$(LIB): $(LIB_OBJ) $(VENDOR_OBJ)
	@mkdir -p $(dir $@)
	$(AR) rcs $@ $^

# Compile src/*.c
build/obj/%.o: src/%.c | build/obj
	$(CC) $(CFLAGS) -c $< -o $@

# Compile vendored sources
build/obj/vendor/%.o: include/vendor/%.c | build/obj/vendor
	$(CC) $(CFLAGS) -c $< -o $@

# Link each tool against the library
bin/%: tools/%.c $(LIB) | bin
	$(CC) $(CFLAGS) $< $(LIB) $(LDFLAGS) -o $@

# ── Tests ────────────────────────────────────────────────

test: $(TEST_BIN)
	@echo "Running tests..."
	@for t in $(TEST_BIN); do \
		echo "  $$t"; \
		$$t || exit 1; \
	done
	@echo "All tests passed."

build/test/%: test/%.c $(LIB) | build/test
	$(CC) $(CFLAGS) $< $(LIB) $(LDFLAGS) -o $@

# ── Directories ──────────────────────────────────────────

build/obj:
	@mkdir -p $@

build/obj/vendor:
	@mkdir -p $@

build/test:
	@mkdir -p $@

bin:
	@mkdir -p $@

# ── Clean ────────────────────────────────────────────────

clean:
	rm -rf build bin

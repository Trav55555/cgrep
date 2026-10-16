CUTILS ?= ../c-utils
DCHECK ?= $(HOME)/projects/dependable-c-agent-toolchain/bin/dcheck

# make test needs only a C compiler; check needs dcheck; difftest needs
# ripgrep (rg) on PATH, the reference cgrep's output is compared against.
STRICT = -std=c89 -pedantic-errors -Wall -Wextra -Werror
CFLAGS ?= -g -O2
BUILD ?= build
CPPFLAGS ?= -D_GNU_SOURCE
INCS = $(CPPFLAGS) -Iinclude -I$(CUTILS)/include
SRCS = $(wildcard src/*.c) $(wildcard $(CUTILS)/src/*.c)
TESTS := $(patsubst tests/%.c,$(BUILD)/%,$(wildcard tests/test_*.c)) \
         $(patsubst semantics/%.c,$(BUILD)/%,$(wildcard semantics/sem_*.c))

.PHONY: all check test difftest iotest mutate verify FORCE
all: $(BUILD)/cgrep

$(BUILD)/cgrep: cmd/main.c $(SRCS) src/parallel.inc src/ignore.inc include/cgrep.h $(CUTILS)/include/cu.h $(BUILD)/revision | $(BUILD)
	$(CC) $(STRICT) $(CFLAGS) $(INCS) -DCGREP_REVISION="\"$$(cat $(BUILD)/revision)\"" -o $@ cmd/main.c $(SRCS)

# Revisions for --version. A tree without its own .git reports unknown
# rather than an enclosing repository's. Rewritten only when they change.
$(BUILD)/revision: FORCE | $(BUILD)
	@rev() { if [ -e "$$1/.git" ]; then git -C "$$1" describe --always --dirty 2>/dev/null || echo unknown; \
	  else echo unknown; fi; }; \
	r="$$(rev .), c-utils $$(rev $(CUTILS))"; \
	if [ "$$(cat $@ 2>/dev/null)" != "$$r" ]; then printf '%s\n' "$$r" > $@; fi

FORCE:

test: $(TESTS)
	@for t in $(TESTS); do echo "$$t"; "$$t" || exit 1; done

$(BUILD)/test_%: tests/test_%.c cmd/main.c $(SRCS) src/parallel.inc src/ignore.inc include/cgrep.h $(CUTILS)/include/cu.h | $(BUILD)
	$(CC) $(STRICT) $(CFLAGS) $(INCS) -o $@ $< $(SRCS)

# The vector variants include the portable suites.
$(BUILD)/test_find_simd: tests/test_find.c
$(BUILD)/test_count_simd: tests/test_count.c

$(BUILD)/sem_%: semantics/sem_%.c $(wildcard tests/test_*.c) $(SRCS) src/parallel.inc src/ignore.inc include/cgrep.h $(CUTILS)/include/cu.h | $(BUILD)
	$(CC) $(STRICT) $(CFLAGS) $(INCS) -o $@ $< $(SRCS)

check:
	$(DCHECK) gate
	$(DCHECK) fast cmd/main.c
	$(MAKE) iotest

iotest: $(BUILD)/cgrep
	python3 -I tests/io_test.py $(BUILD)/cgrep

difftest: $(BUILD)/cgrep
	python3 -I tests/difftest.py $(BUILD)/cgrep

verify:
	CUTILS="$(abspath $(CUTILS))" DCHECK="$(DCHECK)" BUILD="$(BUILD)" bash tests/verify.sh

mutate:
	CUTILS="$(CUTILS)" bash tests/mutate.sh

$(BUILD):
	mkdir -p $@

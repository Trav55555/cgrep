CUTILS ?= ../c-utils
DCHECK ?= $(HOME)/projects/dependable-c-agent-toolchain/bin/dcheck

# make test needs only a C compiler; check needs dcheck; difftest needs
# ripgrep (rg) on PATH, the reference cgrep's output is compared against.
STRICT = -std=c89 -pedantic-errors -Wall -Wextra -Werror
CFLAGS ?= -g -O2
BUILD ?= build
INCS = -Iinclude -I$(CUTILS)/include
SRCS = $(wildcard src/*.c) $(wildcard $(CUTILS)/src/*.c)
TESTS := $(patsubst tests/%.c,$(BUILD)/%,$(wildcard tests/test_*.c))

.PHONY: all check test difftest iotest mutate
all: $(BUILD)/cgrep

$(BUILD)/cgrep: cmd/main.c $(SRCS) include/cgrep.h $(CUTILS)/include/cu.h | $(BUILD)
	$(CC) $(STRICT) $(CFLAGS) $(INCS) -o $@ cmd/main.c $(SRCS)

test: $(TESTS)
	@for t in $(TESTS); do echo "$$t"; "$$t" || exit 1; done

$(BUILD)/test_%: tests/test_%.c $(SRCS) include/cgrep.h $(CUTILS)/include/cu.h | $(BUILD)
	$(CC) $(STRICT) $(CFLAGS) $(INCS) -o $@ $< $(SRCS)

check:
	$(DCHECK) gate
	$(DCHECK) fast cmd/main.c
	$(MAKE) iotest

iotest: $(BUILD)/cgrep
	python3 -I tests/io_test.py $(BUILD)/cgrep

difftest: $(BUILD)/cgrep
	python3 -I tests/difftest.py $(BUILD)/cgrep

mutate:
	CUTILS="$(CUTILS)" bash tests/mutate.sh

$(BUILD):
	mkdir -p $@

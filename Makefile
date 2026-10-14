# HeapForge Makefile
#   make            release build (-O2)            -> bin/heapforge (+ ./heapforge symlink)
#   make debug      -O0 -g3 build                  -> bin/heapforge-debug
#   make asan       ASan+UBSan build, runs tests   -> bin/heapforge-asan
#   make tsan       ThreadSanitizer build, runs tests
#   make test       build and run all unit tests
#   make benchmark  run the benchmark suite
#   make examples   build the example programs
#   make clean
CC      ?= gcc
MODE    ?= release
STD     := -std=gnu11 -D_GNU_SOURCE
WARN    := -Wall -Wextra -Wpedantic -Wshadow -Wstrict-prototypes -Wformat=2
INC     := -Iinclude

ifeq ($(MODE),release)
  OPT := -O2
  SUFFIX :=
else ifeq ($(MODE),debug)
  OPT := -O0 -g3 -DHF_DEBUG
  SUFFIX := -debug
else ifeq ($(MODE),asan)
  OPT := -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer
  SUFFIX := -asan
else ifeq ($(MODE),tsan)
  OPT := -O1 -g -fsanitize=thread -fno-omit-frame-pointer
  SUFFIX := -tsan
else
  $(error unknown MODE '$(MODE)')
endif

CFLAGS  := $(STD) $(WARN) $(OPT) $(INC) -MMD -MP
LDFLAGS := $(OPT)
LDLIBS  := -pthread -lm

OBJDIR  := build/$(MODE)
LIBSRC  := allocator block free_list heap debug stats visualizer benchmark
APPSRC  := main cli demo
LIBOBJ  := $(LIBSRC:%=$(OBJDIR)/%.o)
APPOBJ  := $(APPSRC:%=$(OBJDIR)/%.o)
LIB     := $(OBJDIR)/libheapforge.a
BIN     := bin/heapforge$(SUFFIX)

TESTS   := test_malloc test_free test_split test_coalesce test_realloc test_debug test_threads test_stress
TESTBIN := $(TESTS:%=bin/tests/$(MODE)/%)
EXAMPLES := basic fragmentation leak_demo corruption_demo concurrency
EXBIN   := $(EXAMPLES:%=bin/examples/$(MODE)/%)

.PHONY: all debug asan tsan test examples benchmark clean help
all: $(BIN)
	@if [ "$(MODE)" = "release" ]; then ln -sf bin/heapforge heapforge; fi

$(BIN): $(APPOBJ) $(LIB) | bin
	$(CC) $(LDFLAGS) -o $@ $(APPOBJ) $(LIB) $(LDLIBS)

$(LIB): $(LIBOBJ)
	ar rcs $@ $^

$(OBJDIR)/%.o: src/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

bin/tests/$(MODE)/%: tests/%.c $(LIB)
	@mkdir -p $(dir $@)
	$(CC) $(STD) $(WARN) $(OPT) $(INC) -Itests $< $(LIB) $(LDFLAGS) $(LDLIBS) -o $@

bin/examples/$(MODE)/%: examples/%.c $(LIB)
	@mkdir -p $(dir $@)
	$(CC) $(STD) $(WARN) $(OPT) $(INC) $< $(LIB) $(LDFLAGS) $(LDLIBS) -o $@

bin $(OBJDIR):
	@mkdir -p $@

test: $(TESTBIN)
	@echo "Running HeapForge test suite ($(MODE))"; fail=0; \
	for t in $(TESTBIN); do ./$$t || fail=1; done; \
	if [ $$fail -ne 0 ]; then echo "*** SOME TESTS FAILED"; exit 1; fi; \
	echo "*** ALL TEST PROGRAMS PASSED"

examples: $(EXBIN)

debug:
	$(MAKE) MODE=debug all

asan:
	$(MAKE) MODE=asan all test

tsan:
	$(MAKE) MODE=tsan all test

benchmark: all
	./bin/heapforge benchmark

clean:
	rm -rf build bin heapforge results

help:
	@sed -n '2,10p' Makefile

-include $(OBJDIR)/*.d

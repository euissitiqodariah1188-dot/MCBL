# ============================================================
# CompilerX64 Makefile
# cx64 — McBL# Native x86_64 Compiler
# ============================================================
# Build: make
# Build tanpa ASM (fallback): make NO_ASM=1
# Debug:  make DEBUG=1
# Fast:   make FAST=1

CXX      = g++
CXXFLAGS = -O2 -std=c++17 -Wall -Wextra -pthread
LDFLAGS  = -pthread -lm

# Detect platform
UNAME_S := $(shell uname -s 2>/dev/null || echo Windows)

# NASM check
NASM := $(shell command -v nasm 2>/dev/null)

# Output binary
TARGET = cx64

ifdef DEBUG
  CXXFLAGS += -g -fsanitize=address -DCMAKE_BUILD_TYPE=Debug
  LDFLAGS  += -fsanitize=address
endif

ifdef FAST
  CXXFLAGS += -O3 -march=native -funroll-loops -ffast-math
endif

# C++ source files
CXX_SRCS = cx64_core.cpp cx64_emit.cpp cx64_opt.cpp cx64_jit.cpp
CXX_OBJS = $(CXX_SRCS:.cpp=.o)

# ASM source
ASM_SRC  = cx64_asm.asm

ifdef NO_ASM
  # Build without ASM — provide C stubs
  CXXFLAGS += -DCX64_NO_ASM
  ALL_OBJS  = $(CXX_OBJS) cx64_asm_stub.o
else ifdef NASM
  # Full build with NASM
  ifneq (,$(findstring MINGW,$(UNAME_S)))
    ASM_FMT = win64
    LDFLAGS += -lws2_32
  else ifneq (,$(findstring Darwin,$(UNAME_S)))
    ASM_FMT = macho64
  else
    ASM_FMT = elf64
  endif
  ALL_OBJS = $(CXX_OBJS) cx64_asm.o
else
  # NASM not found — use C stubs
  CXXFLAGS += -DCX64_NO_ASM
  ALL_OBJS  = $(CXX_OBJS) cx64_asm_stub.o
  $(info NASM not found — building with C stubs (cx64_asm_stub.c))
endif

.PHONY: all clean test stubs

all: stubs $(TARGET)
	@echo ""
	@echo "  cx64 built → ./$(TARGET)"
	@echo "  Test: ./$(TARGET) run ../examples/hello.cbl"

$(TARGET): $(ALL_OBJS)
	$(CXX) -o $@ $^ $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

# ASM object (NASM)
cx64_asm.o: cx64_asm.asm
	nasm -f $(ASM_FMT) -o $@ $<

# C stubs object
cx64_asm_stub.o: cx64_asm_stub.c
	gcc -O2 -c -o $@ $<

# Generate C stubs if NASM not available
stubs: cx64_asm_stub.c

cx64_asm_stub.c:
	@echo "/* cx64_asm_stub.c — C fallback when NASM not available */" > $@
	@echo "#include <stdint.h>" >> $@
	@echo "#include <string.h>" >> $@
	@echo "#include <stdio.h>"  >> $@
	@echo "#include <time.h>"   >> $@
	@echo "" >> $@
	@echo "void    cx64_asm_call_native(void *fn) { typedef void(*F)(void); ((F)fn)(); }" >> $@
	@echo "void    cx64_asm_cache_flush(unsigned char *s, size_t l) { (void)s;(void)l; }" >> $@
	@echo "int64_t cx64_asm_rdtsc(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return t.tv_sec*1000000000LL+t.tv_nsec; }" >> $@
	@echo "int64_t cx64_asm_get_time_ns(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return t.tv_sec*1000000000LL+t.tv_nsec; }" >> $@
	@echo "void    cx64_asm_sfence(void) {}" >> $@
	@echo "void    cx64_asm_mfence(void) {}" >> $@
	@echo "void    cx64_asm_lfence(void) {}" >> $@
	@echo "void    cx64_asm_loop_burst(void *fn, int count) { typedef void(*F)(void); F f=(F)fn; for(int i=0;i<count;i++) f(); }" >> $@
	@echo "int64_t cx64_asm_int_div(int64_t a, int64_t b) { return b?a/b:0; }" >> $@
	@echo "int64_t cx64_asm_mulhi64(int64_t a, int64_t b) { __int128 r=(__int128)a*b; return (int64_t)(r>>64); }" >> $@
	@echo "int     cx64_asm_clz64(uint64_t x) { return x?__builtin_clzll(x):64; }" >> $@
	@echo "int     cx64_asm_popcnt64(uint64_t x) { return __builtin_popcountll(x); }" >> $@
	@echo "double  cx64_asm_sqrt_f64(double x) { return __builtin_sqrt(x); }" >> $@
	@echo "double  cx64_asm_fma_f64(double a,double b,double c) { return a*b+c; }" >> $@
	@echo "Generated cx64_asm_stub.c"

test: $(TARGET)
	@echo "=== cx64 Test Suite ==="
	./$(TARGET) run   ../examples/hello.cbl
	./$(TARGET) ir    ../examples/math_demo.cbl
	./$(TARGET) bench ../examples/loop.cbl
	@echo "=== Tests done ==="

clean:
	rm -f $(CXX_OBJS) cx64_asm.o cx64_asm_stub.o cx64_asm_stub.c $(TARGET)


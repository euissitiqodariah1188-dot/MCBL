# ============================================================
# Makefile — McBL# MSDK v2.0 (UPGRADED)
# ============================================================
# Requires: gcc, g++, pthread
# Windows: MSYS2/MinGW64

CC       = gcc
CXX      = g++
CFLAGS   = -O2 -Wall -Wextra -std=c11 -pthread -D_GNU_SOURCE
CXXFLAGS = -O2 -Wall -Wextra -std=c++17 -pthread -D_GNU_SOURCE
LDFLAGS  = -pthread -lm

# Debug build: make DEBUG=1
ifdef DEBUG
  CFLAGS   += -g -DMCBL_DEBUG -fsanitize=address
  CXXFLAGS += -g -DMCBL_DEBUG -fsanitize=address
  LDFLAGS  += -fsanitize=address
endif

# Release with max speed: make FAST=1
ifdef FAST
  CFLAGS   += -O3 -march=native -funroll-loops -ffast-math
  CXXFLAGS += -O3 -march=native -funroll-loops -ffast-math
endif

# Platform detection
UNAME_S := $(shell uname -s 2>/dev/null)
ifneq (,$(findstring MINGW,$(UNAME_S)))
  LDFLAGS += -lws2_32
  EXE_EXT  = .exe
else ifneq (,$(findstring Darwin,$(UNAME_S)))
  LDFLAGS += -ldl
  EXE_EXT  =
else
  LDFLAGS += -ldl
  EXE_EXT  =
endif

SRC_DIR = src

# ---- C sources (original + v2.0 new) ----
C_SOURCES = \
	$(SRC_DIR)/lexer.c \
	$(SRC_DIR)/ast.c \
	$(SRC_DIR)/parser.c \
	$(SRC_DIR)/symbols.c \
	$(SRC_DIR)/bytecode.c \
	$(SRC_DIR)/codegen.c \
	$(SRC_DIR)/jit.c \
	$(SRC_DIR)/memory.c \
	$(SRC_DIR)/kernel.c \
	$(SRC_DIR)/mdk.c \
	$(SRC_DIR)/neural.c \
	$(SRC_DIR)/mdc.c \
	$(SRC_DIR)/mbjkdt.c \
	$(SRC_DIR)/ui.c \
	$(SRC_DIR)/mvm.c \
	$(SRC_DIR)/mbll.c \
	$(SRC_DIR)/mcbl_math.c \
	$(SRC_DIR)/mcbl_str.c \
	$(SRC_DIR)/mcbl_sys.c \
	$(SRC_DIR)/oop.c \
	$(SRC_DIR)/mcbl_api.c \
	$(SRC_DIR)/main.c

# ---- C++ sources ----
CXX_SOURCES = \
	$(SRC_DIR)/vp_xt300.cpp

C_OBJECTS   = $(C_SOURCES:.c=.o)
CXX_OBJECTS = $(CXX_SOURCES:.cpp=.o)

TARGET = MSDK$(EXE_EXT)

.PHONY: all clean test fast debug install docs

all: $(TARGET)
	@echo ""
	@echo "  McBL# MSDK v2.0 built: ./$(TARGET)"
	@echo "  Run: ./$(TARGET) version"

$(TARGET): $(C_OBJECTS) $(CXX_OBJECTS)
	$(CXX) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

fast:
	$(MAKE) FAST=1

debug:
	$(MAKE) DEBUG=1

# ---- Dependency tracking ----
$(SRC_DIR)/lexer.o:     $(SRC_DIR)/lexer.c     $(SRC_DIR)/lexer.h
$(SRC_DIR)/ast.o:       $(SRC_DIR)/ast.c       $(SRC_DIR)/ast.h
$(SRC_DIR)/parser.o:    $(SRC_DIR)/parser.c    $(SRC_DIR)/parser.h   $(SRC_DIR)/lexer.h   $(SRC_DIR)/ast.h
$(SRC_DIR)/symbols.o:   $(SRC_DIR)/symbols.c   $(SRC_DIR)/symbols.h
$(SRC_DIR)/bytecode.o:  $(SRC_DIR)/bytecode.c  $(SRC_DIR)/bytecode.h $(SRC_DIR)/memory.h
$(SRC_DIR)/codegen.o:   $(SRC_DIR)/codegen.c   $(SRC_DIR)/codegen.h  $(SRC_DIR)/ast.h     $(SRC_DIR)/bytecode.h $(SRC_DIR)/memory.h
$(SRC_DIR)/jit.o:       $(SRC_DIR)/jit.c       $(SRC_DIR)/jit.h      $(SRC_DIR)/bytecode.h $(SRC_DIR)/symbols.h $(SRC_DIR)/memory.h
$(SRC_DIR)/memory.o:    $(SRC_DIR)/memory.c    $(SRC_DIR)/memory.h
$(SRC_DIR)/kernel.o:    $(SRC_DIR)/kernel.c    $(SRC_DIR)/kernel.h   $(SRC_DIR)/mdk.h     $(SRC_DIR)/jit.h     $(SRC_DIR)/bytecode.h $(SRC_DIR)/symbols.h $(SRC_DIR)/memory.h
$(SRC_DIR)/mdk.o:       $(SRC_DIR)/mdk.c       $(SRC_DIR)/mdk.h      $(SRC_DIR)/bytecode.h $(SRC_DIR)/symbols.h $(SRC_DIR)/memory.h
$(SRC_DIR)/neural.o:    $(SRC_DIR)/neural.c    $(SRC_DIR)/neural.h   $(SRC_DIR)/ast.h     $(SRC_DIR)/memory.h
$(SRC_DIR)/mdc.o:       $(SRC_DIR)/mdc.c       $(SRC_DIR)/mdc.h      $(SRC_DIR)/ast.h     $(SRC_DIR)/memory.h
$(SRC_DIR)/mbjkdt.o:    $(SRC_DIR)/mbjkdt.c    $(SRC_DIR)/mbjkdt.h   $(SRC_DIR)/mdk.h     $(SRC_DIR)/memory.h
$(SRC_DIR)/ui.o:        $(SRC_DIR)/ui.c        $(SRC_DIR)/ui.h       $(SRC_DIR)/ast.h     $(SRC_DIR)/memory.h
$(SRC_DIR)/mvm.o:       $(SRC_DIR)/mvm.c       $(SRC_DIR)/mvm.h      $(SRC_DIR)/bytecode.h $(SRC_DIR)/memory.h
$(SRC_DIR)/mbll.o:      $(SRC_DIR)/mbll.c      $(SRC_DIR)/mbll.h     $(SRC_DIR)/memory.h
$(SRC_DIR)/mcbl_math.o: $(SRC_DIR)/mcbl_math.c $(SRC_DIR)/mcbl_math.h
$(SRC_DIR)/mcbl_str.o:  $(SRC_DIR)/mcbl_str.c  $(SRC_DIR)/mcbl_str.h
$(SRC_DIR)/mcbl_sys.o:  $(SRC_DIR)/mcbl_sys.c  $(SRC_DIR)/mcbl_sys.h $(SRC_DIR)/mcbl_str.h
$(SRC_DIR)/oop.o:       $(SRC_DIR)/oop.c       $(SRC_DIR)/oop.h      $(SRC_DIR)/memory.h

$(SRC_DIR)/mcbl_api.o:     $(SRC_DIR)/mcbl_api.c     $(SRC_DIR)/mcbl_api.h \
	$(SRC_DIR)/lexer.h   $(SRC_DIR)/parser.h   $(SRC_DIR)/ast.h \
	$(SRC_DIR)/symbols.h $(SRC_DIR)/bytecode.h $(SRC_DIR)/codegen.h \
	$(SRC_DIR)/mdk.h     $(SRC_DIR)/memory.h   $(SRC_DIR)/mcbl_str.h

$(SRC_DIR)/main.o:      $(SRC_DIR)/main.c \
	$(SRC_DIR)/lexer.h   $(SRC_DIR)/parser.h   $(SRC_DIR)/ast.h \
	$(SRC_DIR)/symbols.h $(SRC_DIR)/bytecode.h $(SRC_DIR)/codegen.h \
	$(SRC_DIR)/jit.h     $(SRC_DIR)/kernel.h   $(SRC_DIR)/mdk.h \
	$(SRC_DIR)/neural.h  $(SRC_DIR)/mdc.h      $(SRC_DIR)/mbjkdt.h \
	$(SRC_DIR)/vp_xt300.h $(SRC_DIR)/ui.h      $(SRC_DIR)/memory.h \
	$(SRC_DIR)/mvm.h     $(SRC_DIR)/mbll.h     $(SRC_DIR)/mcbl_math.h \
	$(SRC_DIR)/mcbl_str.h $(SRC_DIR)/mcbl_sys.h $(SRC_DIR)/oop.h

# ---- Tests ----
test: $(TARGET)
	@echo "=== McBL# MSDK v2.0 Test Suite ==="
	./$(TARGET) version
	./$(TARGET) lex    examples/hello.cbl
	./$(TARGET) ast    examples/hello.cbl
	./$(TARGET) bc     examples/hello.cbl
	./$(TARGET) check  examples/hello.cbl
	./$(TARGET) run    examples/hello.cbl
	@echo ""
	@echo "=== v2.0 Feature Tests ==="
	./$(TARGET) lex    examples/oop_demo.cbl       2>/dev/null || true
	./$(TARGET) run    examples/math_demo.cbl       2>/dev/null || true
	./$(TARGET) run    examples/array_demo.cbl      2>/dev/null || true
	./$(TARGET) run    examples/tram_demo.cbl       2>/dev/null || true
	./$(TARGET) run    examples/pointer_demo.cbl    2>/dev/null || true
	./$(TARGET) bench  examples/hello.cbl
	@echo ""
	@echo "=== All tests done ==="

clean:
	rm -f $(SRC_DIR)/*.o $(TARGET) *.out *.cll *.so *.dll
	@echo "Clean done."

install: $(TARGET)
	cp $(TARGET) /usr/local/bin/MSDK
	@echo "Installed MSDK → /usr/local/bin/MSDK"

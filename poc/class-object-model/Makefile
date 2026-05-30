# PoC — multi-module annotated C class model + RPC.
#
# Each module under src/<module>/ owns its annotated sources. Generated
# public headers land in include/<module>/; generated internal artefacts
# (.gen.c, methods.gen.c, rpc.gen.c, model.yaml) stay under the
# module's src dir next to the annotated .c they belong to.

CC      ?= cc
UV      ?= uv

# -Werror=incompatible-pointer-types so the typedef-based override
# signature check (see emit_class_accessor in gen/codegen.py) is a
# hard error on every supported compiler — clang already errors by
# default, GCC only warns without this flag.
CFLAGS  ?= -std=c2x -Wall -Wextra -Wno-attributes -Iinclude -g \
           -Werror=incompatible-pointer-types

# Shared runtime — hand-written, no codegen.
RUNTIME_HEADERS := include/class.h include/rpc.h include/uthash.h \
                   include/result.h include/ytrace.h
RUNTIME_SRC := src/class.c src/rpc.c src/result.c src/ytrace.c

LDFLAGS ?= -lpthread

# ---- yvehicle module --------------------------------------------------
YV_SRCDIR := src/yvehicle
YV_INCDIR := include/yvehicle
YV_ANN := $(YV_SRCDIR)/vehicle.c $(YV_SRCDIR)/motorbike.c \
          $(YV_SRCDIR)/car.c $(YV_SRCDIR)/sportscar.c \
          $(YV_SRCDIR)/electric.c
YV_PUB_H := $(YV_INCDIR)/vehicle.h $(YV_INCDIR)/motorbike.h \
            $(YV_INCDIR)/car.h $(YV_INCDIR)/sportscar.h \
            $(YV_INCDIR)/electric.h \
            $(YV_INCDIR)/methods.gen.h $(YV_INCDIR)/rpc.gen.h
YV_INTERNAL := $(patsubst %.c,%.gen.c,$(YV_ANN)) \
               $(YV_SRCDIR)/methods.gen.c $(YV_SRCDIR)/rpc.gen.c \
               $(YV_SRCDIR)/model.yaml
YV_GEN := $(YV_PUB_H) $(YV_INTERNAL)
YV_OBJ := $(patsubst %.c,%.o,$(YV_ANN)) \
          $(YV_SRCDIR)/methods.gen.o $(YV_SRCDIR)/rpc.gen.o

# ---- yanimal module ---------------------------------------------------
YA_SRCDIR := src/yanimal
YA_INCDIR := include/yanimal
YA_ANN := $(YA_SRCDIR)/animal.c $(YA_SRCDIR)/cat.c \
          $(YA_SRCDIR)/dog.c $(YA_SRCDIR)/pet.c
YA_PUB_H := $(YA_INCDIR)/animal.h $(YA_INCDIR)/cat.h \
            $(YA_INCDIR)/dog.h $(YA_INCDIR)/pet.h \
            $(YA_INCDIR)/methods.gen.h $(YA_INCDIR)/rpc.gen.h
YA_INTERNAL := $(patsubst %.c,%.gen.c,$(YA_ANN)) \
               $(YA_SRCDIR)/methods.gen.c $(YA_SRCDIR)/rpc.gen.c \
               $(YA_SRCDIR)/model.yaml
YA_GEN := $(YA_PUB_H) $(YA_INTERNAL)
YA_OBJ := $(patsubst %.c,%.o,$(YA_ANN)) \
          $(YA_SRCDIR)/methods.gen.o $(YA_SRCDIR)/rpc.gen.o

# ---- ytuning module (cross-domain subclass of yvehicle:sportscar) -----
YT_SRCDIR := src/ytuning
YT_INCDIR := include/ytuning
YT_ANN := $(YT_SRCDIR)/tuned_sportscar.c
YT_PUB_H := $(YT_INCDIR)/tuned_sportscar.h \
            $(YT_INCDIR)/methods.gen.h $(YT_INCDIR)/rpc.gen.h
YT_INTERNAL := $(patsubst %.c,%.gen.c,$(YT_ANN)) \
               $(YT_SRCDIR)/methods.gen.c $(YT_SRCDIR)/rpc.gen.c \
               $(YT_SRCDIR)/model.yaml
YT_GEN := $(YT_PUB_H) $(YT_INTERNAL)
YT_OBJ := $(patsubst %.c,%.o,$(YT_ANN)) \
          $(YT_SRCDIR)/methods.gen.o $(YT_SRCDIR)/rpc.gen.o

# ---- aggregate --------------------------------------------------------
RUNTIME_OBJ := $(patsubst %.c,%.o,$(RUNTIME_SRC))
ALL_OBJ := $(RUNTIME_OBJ) $(YV_OBJ) $(YA_OBJ) $(YT_OBJ) src/main.o

ALL_HEADERS := $(RUNTIME_HEADERS) $(YV_PUB_H) $(YA_PUB_H) $(YT_PUB_H)
ALL_GEN := $(YV_GEN) $(YA_GEN) $(YT_GEN)

BIN := poc

.PHONY: help all build run clean

help:
	@awk 'BEGIN{FS=":"} /^## / {sub(/^## /,""); print "  " $$0}' $(MAKEFILE_LIST)

## all       generate everything, build the binary
all: build

## build     compile the PoC binary
build: $(BIN)

## run       build then execute the PoC binary
run: build
	./$(BIN)

## clean     remove build artefacts and every generated file
clean:
	rm -f $(ALL_OBJ) $(BIN) $(ALL_GEN)
	@rmdir $(YV_INCDIR) $(YA_INCDIR) $(YT_INCDIR) 2>/dev/null || true

$(BIN): $(ALL_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Compile rule. Every .c depends on every generated header so that a
# regen of any module's headers triggers a rebuild of everything that
# shares the -Iinclude search path.
%.o: %.c $(ALL_HEADERS) $(ALL_GEN)
	$(CC) $(CFLAGS) -c -o $@ $<

# Per-module codegen — grouped-target rule (GNU make >= 4.3). One
# codegen invocation produces ALL of $(YV_GEN); the `&:` syntax tells
# make so. If ANY file in the group is missing or older than any
# prerequisite (the annotated .c, the runtime headers, or the codegen
# script), make re-runs the recipe and refreshes all of them.
$(YV_GEN) &: gen/codegen.py $(YV_ANN) $(RUNTIME_HEADERS)
	$(UV) run gen/codegen.py yvehicle include $(YV_SRCDIR) $(YV_ANN)

$(YA_GEN) &: gen/codegen.py $(YA_ANN) $(RUNTIME_HEADERS)
	$(UV) run gen/codegen.py yanimal include $(YA_SRCDIR) $(YA_ANN)

# ytuning has a cross-domain override targeting a yvehicle-owned slot.
# Its codegen needs yvehicle's generated headers — specifically the
# function-pointer typedef in yvehicle/methods.gen.h the .gen.c uses
# for the compile-time signature check. Depend on that header (and
# on yvehicle/sportscar.h, the parent's accessor decl) directly.
$(YT_GEN) &: gen/codegen.py $(YT_ANN) $(RUNTIME_HEADERS) \
             $(YV_INCDIR)/methods.gen.h $(YV_INCDIR)/sportscar.h
	$(UV) run gen/codegen.py ytuning include $(YT_SRCDIR) $(YT_ANN)

# PoC — multi-module annotated C class model + RPC.
#
# Each module under src/<module>/ owns its annotated sources. Generated
# public headers land in include/<module>/; generated internal artefacts
# (.gen.c, methods.gen.c, rpc.gen.c, model.yaml) stay under the
# module's src dir next to the annotated .c they belong to.

CC      ?= cc
UV      ?= uv

CFLAGS  ?= -std=c2x -Wall -Wextra -Wno-attributes -Iinclude -g

# Shared runtime — hand-written, no codegen.
RUNTIME_HEADERS := include/class.h include/rpc.h include/uthash.h
RUNTIME_SRC := src/class.c src/rpc.c

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
YV_STAMP := $(YV_SRCDIR)/.stamp
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
YA_STAMP := $(YA_SRCDIR)/.stamp
YA_OBJ := $(patsubst %.c,%.o,$(YA_ANN)) \
          $(YA_SRCDIR)/methods.gen.o $(YA_SRCDIR)/rpc.gen.o

# ---- aggregate --------------------------------------------------------
RUNTIME_OBJ := $(patsubst %.c,%.o,$(RUNTIME_SRC))
ALL_OBJ := $(RUNTIME_OBJ) $(YV_OBJ) $(YA_OBJ) src/main.o

ALL_HEADERS := $(RUNTIME_HEADERS) $(YV_PUB_H) $(YA_PUB_H)
ALL_GEN := $(YV_PUB_H) $(YV_INTERNAL) $(YV_STAMP) \
           $(YA_PUB_H) $(YA_INTERNAL) $(YA_STAMP)

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
	@rmdir $(YV_INCDIR) $(YA_INCDIR) 2>/dev/null || true

$(BIN): $(ALL_OBJ)
	$(CC) $(CFLAGS) -o $@ $^

# Compile rule. Both module stamps are global deps — a header touched in
# either module triggers a recompile of everything that depends on the
# shared search path.
%.o: %.c $(YV_STAMP) $(YA_STAMP) $(ALL_HEADERS)
	$(CC) $(CFLAGS) -c -o $@ $<

# Per-module codegen invocation. include/<module>/ is created by the
# generator; module_src is the existing src/<module>/.
$(YV_STAMP): gen/codegen.py $(YV_ANN) $(RUNTIME_HEADERS)
	$(UV) run gen/codegen.py yvehicle include $(YV_SRCDIR) $(YV_ANN)
	@touch $@

$(YA_STAMP): gen/codegen.py $(YA_ANN) $(RUNTIME_HEADERS)
	$(UV) run gen/codegen.py yanimal include $(YA_SRCDIR) $(YA_ANN)
	@touch $@

# Existence of the stamp implies the generated artefacts exist.
$(YV_PUB_H) $(YV_INTERNAL): $(YV_STAMP)
	@test -f $@
$(YA_PUB_H) $(YA_INTERNAL): $(YA_STAMP)
	@test -f $@

DIR = $(shell find inc -type d)
BINDIR = $(DIR:inc%=bin%)
LIBS =
LIBCPPFLAGS = # $(shell pkg-config --cflags $(LIBS))
LIBLDFLAGS = # $(shell pkg-config --libs $(LIBS))
SAN =
CPPFLAGS = -MMD -MP -Ibin -Iinc $(LIBCPPFLAGS)
DEBUG = -ggdb3
OPT ?=
CFLAGS = $(SAN) $(DEBUG) $(OPT)
LDFLAGS = $(SAN) $(LIBLDFLAGS)

CC = gcc
LD = gcc
RM = rm

BIN = main
BIN_PATH = $(BIN:%=bin/%)

HDR = $(shell find inc -type f)
GCH = $(HDR:inc/%=bin/%.gch)

SRC = $(shell find src -type f -regex ".*\.\(c\|cpp\)")
SRC_NOMAIN = $(filter-out $(BIN:%=src/%.c),$(SRC))
OBJ = $(SRC:src/%=bin/%.o)
OBJ_NOMAIN = $(SRC_NOMAIN:src/%=bin/%.o)

DEP = $(SRC:src/%=bin/%.d) $(HDR:inc/%=bin/%.d)

all:: $(BINDIR) $(GCH) $(BIN_PATH)

$(BIN_PATH): bin/%: bin/%.c.o $(OBJ_NOMAIN)
	$(LD) -o $@ $^ $(LDFLAGS)

bin/%.c.o: src/%.c
	$(CC) $(CPPFLAGS) -c -o $@ $< $(CFLAGS)

bin/%.h.gch: inc/%.h
	$(CC) $(CPPFLAGS) -c -o $@ $< $(CFLAGS)

run:: run-main

run-%:: all
	$(@:run-%=bin/%)

clean::
	$(RM) -rf bin

$(BINDIR): %:
	mkdir -p $@

-include $(DEP)


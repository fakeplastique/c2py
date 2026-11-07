CC     ?= gcc
CFLAGS ?= -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -O2
B      := build
SRC    := src/ast.c src/sema.c src/interp.c src/pygen.c src/main.c
PORT   ?= 8000

all: $(B)/c2py

$(B):
	mkdir -p $(B)

$(B)/parser.c $(B)/parser.h: src/parser.y | $(B)
	bison -Wall --report=all -d -o $(B)/parser.c src/parser.y

$(B)/lexer.c: src/lexer.l $(B)/parser.h
	flex -o $@ src/lexer.l

$(B)/c2py: $(B)/parser.c $(B)/lexer.c $(SRC) src/ast.h
	$(CC) $(CFLAGS) -Isrc -I$(B) -o $@ $(B)/parser.c $(B)/lexer.c $(SRC)

conflicts: | $(B)
	sed -f src/conflicts.sed src/parser.y > $(B)/conflicts.y
	bison -Wcounterexamples --report=all -o $(B)/conflicts.c $(B)/conflicts.y > $(B)/conflicts.log 2>&1
	@grep -E 'Wconflicts' $(B)/conflicts.log
	@grep -A9 'conflict on token "else"' $(B)/conflicts.log
	@echo "full report: $(B)/conflicts.log, automaton: $(B)/conflicts.output"

test: all
	python3 tests/e2e.py

web: all
	PORT=$(PORT) python3 web/server.py

clean:
	rm -rf $(B)

.PHONY: all conflicts test web clean

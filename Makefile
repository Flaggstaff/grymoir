CC     ?= cc
CFLAGS ?= -std=c99 -Wall -Wextra -pedantic -O2

all: grym-lex test_lexeur

grym-lex: src/grym-lex.c src/lexeur.c src/lexeur.h
	$(CC) $(CFLAGS) -o $@ src/grym-lex.c src/lexeur.c

test_lexeur: tests/test_lexeur.c src/lexeur.c src/lexeur.h
	$(CC) $(CFLAGS) -Isrc -o $@ tests/test_lexeur.c src/lexeur.c

test: test_lexeur
	./test_lexeur

clean:
	rm -f grym-lex test_lexeur grym-lex.exe test_lexeur.exe

.PHONY: all test clean

CC     ?= cc
CFLAGS ?= -std=c99 -Wall -Wextra -pedantic -O2

LEXEUR    = src/lexeur.c
ANALYSEUR = src/analyseur.c src/arbre.c src/texte.c $(LEXEUR)
ENTETES   = src/lexeur.h src/analyseur.h src/arbre.h src/texte.h

all: grym-lex grym-arbre test_lexeur test_analyseur

grym-lex: src/grym-lex.c $(LEXEUR) $(ENTETES)
	$(CC) $(CFLAGS) -o $@ src/grym-lex.c $(LEXEUR)

grym-arbre: src/grym-arbre.c $(ANALYSEUR) $(ENTETES)
	$(CC) $(CFLAGS) -o $@ src/grym-arbre.c $(ANALYSEUR)

test_lexeur: tests/test_lexeur.c $(LEXEUR) $(ENTETES)
	$(CC) $(CFLAGS) -Isrc -o $@ tests/test_lexeur.c $(LEXEUR)

test_analyseur: tests/test_analyseur.c $(ANALYSEUR) $(ENTETES)
	$(CC) $(CFLAGS) -Isrc -o $@ tests/test_analyseur.c $(ANALYSEUR)

test: test_lexeur test_analyseur
	./test_lexeur
	./test_analyseur

clean:
	rm -f grym-lex grym-arbre test_lexeur test_analyseur *.exe

.PHONY: all test clean

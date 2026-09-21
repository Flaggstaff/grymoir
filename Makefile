CC     ?= cc
CFLAGS ?= -std=c99 -Wall -Wextra -pedantic -O2

LEXEUR    = src/lexeur.c
ANALYSEUR = src/analyseur.c src/arbre.c src/texte.c $(LEXEUR)
EXECUTION = src/evaluateur.c src/decimal.c $(ANALYSEUR)
ENTETES   = src/lexeur.h src/analyseur.h src/arbre.h src/texte.h src/evaluateur.h src/decimal.h

all: grym grym-lex grym-arbre grym-suites test_lexeur test_analyseur test_evaluateur

grym: src/grym.c $(EXECUTION) $(ENTETES)
	$(CC) $(CFLAGS) -o $@ src/grym.c $(EXECUTION)

grym-lex: src/grym-lex.c $(LEXEUR) $(ENTETES)
	$(CC) $(CFLAGS) -o $@ src/grym-lex.c $(LEXEUR)

grym-arbre: src/grym-arbre.c $(ANALYSEUR) $(ENTETES)
	$(CC) $(CFLAGS) -o $@ src/grym-arbre.c $(ANALYSEUR)

grym-suites: src/grym-suites.c $(ANALYSEUR) $(ENTETES)
	$(CC) $(CFLAGS) -o $@ src/grym-suites.c $(ANALYSEUR)

test_lexeur: tests/test_lexeur.c $(LEXEUR) $(ENTETES)
	$(CC) $(CFLAGS) -Isrc -o $@ tests/test_lexeur.c $(LEXEUR)

test_analyseur: tests/test_analyseur.c $(ANALYSEUR) $(ENTETES)
	$(CC) $(CFLAGS) -Isrc -o $@ tests/test_analyseur.c $(ANALYSEUR)

test_evaluateur: tests/test_evaluateur.c $(EXECUTION) $(ENTETES)
	$(CC) $(CFLAGS) -Isrc -o $@ tests/test_evaluateur.c $(EXECUTION)

test: test_lexeur test_analyseur test_evaluateur
	./test_lexeur
	./test_analyseur
	./test_evaluateur

clean:
	rm -f grym grym-lex grym-arbre grym-suites test_lexeur test_analyseur test_evaluateur *.exe

.PHONY: all test clean

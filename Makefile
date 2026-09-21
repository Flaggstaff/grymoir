CC     ?= cc
CFLAGS ?= -std=c99 -Wall -Wextra -pedantic -O2

LEXEUR    = src/lexeur.c
ANALYSEUR = src/analyseur.c src/arbre.c src/texte.c src/imprimeur.c src/decimal.c $(LEXEUR)
EXECUTION = src/compilateur.c src/vm.c src/bytecode.c $(ANALYSEUR)
ENTETES   = src/imprimeur.h src/lexeur.h src/analyseur.h src/arbre.h src/texte.h src/compilateur.h src/vm.h src/bytecode.h src/decimal.h

all: grym grym-lexeur grym-arbre grym-suites test_lexeur test_analyseur test_machine test_imprimeur

grym: src/grym.c $(EXECUTION) $(ENTETES)
	$(CC) $(CFLAGS) -o $@ src/grym.c $(EXECUTION)

grym-lexeur: src/grym-lexeur.c $(LEXEUR) $(ENTETES)
	$(CC) $(CFLAGS) -o $@ src/grym-lexeur.c $(LEXEUR)

grym-arbre: src/grym-arbre.c $(ANALYSEUR) $(ENTETES)
	$(CC) $(CFLAGS) -o $@ src/grym-arbre.c $(ANALYSEUR)

grym-suites: src/grym-suites.c $(ANALYSEUR) $(ENTETES)
	$(CC) $(CFLAGS) -o $@ src/grym-suites.c $(ANALYSEUR)

test_lexeur: tests/test_lexeur.c $(LEXEUR) $(ENTETES)
	$(CC) $(CFLAGS) -Isrc -o $@ tests/test_lexeur.c $(LEXEUR)

test_analyseur: tests/test_analyseur.c $(ANALYSEUR) $(ENTETES)
	$(CC) $(CFLAGS) -Isrc -o $@ tests/test_analyseur.c $(ANALYSEUR)

test_machine: tests/test_machine.c $(EXECUTION) $(ENTETES)
	$(CC) $(CFLAGS) -Isrc -o $@ tests/test_machine.c $(EXECUTION)

test_imprimeur: tests/test_imprimeur.c $(ANALYSEUR) $(ENTETES)
	$(CC) $(CFLAGS) -Isrc -o $@ tests/test_imprimeur.c $(ANALYSEUR)

test: test_lexeur test_analyseur test_machine test_imprimeur
	./test_lexeur
	./test_analyseur
	./test_machine
	./test_imprimeur

clean:
	rm -f grym grym-lexeur grym-arbre grym-suites test_lexeur test_analyseur test_machine test_imprimeur *.exe

.PHONY: all test clean

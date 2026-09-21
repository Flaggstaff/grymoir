CC     ?= cc
CFLAGS ?= -std=c99 -Wall -Wextra -pedantic -O2

LEXEUR    = src/lexeur.c
ANALYSEUR = src/analyseur.c src/compact.c src/arbre.c src/texte.c src/imprimeur.c src/decimal.c $(LEXEUR)
EXECUTION = src/compilateur.c src/vm.c src/bytecode.c $(ANALYSEUR)
# SQLite embarqué (vendor/sqlite, voir PROVENANCE.md) : compilé une fois, sans les avertissements
# de GrymoiR (ce code n'est pas le nôtre), avec les réglages de la charte.
SQLITE_FLAGS = -std=c99 -O2 -DSQLITE_THREADSAFE=0 -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_DQS=0 \
               -DSQLITE_DEFAULT_FOREIGN_KEYS=1 -DSQLITE_OMIT_DEPRECATED -DSQLITE_DEFAULT_MEMSTATUS=0
SQLITE_O     = vendor/sqlite/sqlite3.o

ENTETES   = src/compact.h src/imprimeur.h src/lexeur.h src/analyseur.h src/arbre.h src/texte.h src/compilateur.h src/vm.h src/bytecode.h src/decimal.h

all: grym grym-lexeur grym-arbre grym-suites test_lexeur test_analyseur test_machine test_imprimeur test_compact test_base

$(SQLITE_O): vendor/sqlite/sqlite3.c vendor/sqlite/sqlite3.h
	$(CC) $(SQLITE_FLAGS) -c -o $@ vendor/sqlite/sqlite3.c

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

test_compact: tests/test_compact.c $(EXECUTION) $(ENTETES)
	$(CC) $(CFLAGS) -Isrc -o $@ tests/test_compact.c $(EXECUTION)

test_base: tests/test_base.c $(SQLITE_O)
	$(CC) $(CFLAGS) -Ivendor/sqlite -o $@ tests/test_base.c $(SQLITE_O)

test: test_lexeur test_analyseur test_machine test_imprimeur test_compact test_base
	./test_lexeur
	./test_analyseur
	./test_machine
	./test_imprimeur
	./test_compact
	./test_base

clean:
	rm -f grym grym-lexeur grym-arbre grym-suites test_lexeur test_analyseur test_machine test_imprimeur test_compact test_base *.exe

# Recompiler SQLite prend une trentaine de secondes : « make clean » le garde, « make distclean » non.
distclean: clean
	rm -f $(SQLITE_O)

.PHONY: all test clean distclean

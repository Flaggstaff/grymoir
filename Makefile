CC     ?= cc
CFLAGS ?= -std=c99 -Wall -Wextra -pedantic -O2
CPPFLAGS += -isystem vendor/sqlite   # en-tête tiers : ses avertissements ne sont pas les nôtres

# Sous Windows, chaque exécutable porte le manifeste UTF-8 (outils/windows) : les essais ouvrent leurs fichiers
# dans les mêmes conditions que grym.exe, noms accentués compris (« données.grym »).
LEXEUR    = src/lexeur.c src/date.c src/texte.c src/teinte.c $(RESSOURCES)
ANALYSEUR = src/chemins.c src/analyseur.c src/compact.c src/arbre.c src/imprimeur.c src/decimal.c $(LEXEUR)
EXECUTION = src/natif.c src/json.c src/morse.c src/serie.c src/qr.c vendor/qrcodegen/qrcodegen.c src/coffre.c vendor/monocypher/monocypher.c src/compilateur.c src/vm.c src/console.c src/base.c src/bytecode.c src/empreinte.c $(ANALYSEUR) $(SQLITE_O)
# SQLite embarqué, avec SQLite3MC (vendor/sqlite, voir PROVENANCE.md) : compilé une fois, sans les avertissements
# de GrymoiR (ce code n'est pas le nôtre), avec les réglages de la charte.
SQLITE_FLAGS = -std=c99 -O2 -DSQLITE_THREADSAFE=0 -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_DQS=0 \
               -DSQLITE_DEFAULT_FOREIGN_KEYS=1 -DSQLITE_OMIT_DEPRECATED -DSQLITE_DEFAULT_MEMSTATUS=0
SQLITE_O     = vendor/sqlite/sqlite3.o

# Mbed TLS 3.6 LTS (vendor/mbedtls/PROVENANCE.md) : le chiffrement de l'aiguilleur sur le réseau (docs/v2.md, § 12)
MBEDTLS_OBJ = $(patsubst %.c,%.o,$(wildcard vendor/mbedtls/library/*.c))
MBEDTLS_A   = vendor/mbedtls/libmbedtls.a
TLS         = src/tls.c $(MBEDTLS_A)

# Windows : prises réseau et générateur aléatoire du système (grym servir, docs/v2.md)
# dlopen, pour les fonctions C des bibliothèques (3.14) : dans libdl sous Linux, déjà dans la bibliothèque du C
# ailleurs (macOS accepte -ldl) ; Windows charge par LoadLibrary
DL = -ldl
ifeq ($(OS),Windows_NT)
DL =
RESEAU = -lws2_32 -lbcrypt
# le manifeste des exécutables : les chemins accentués s'ouvrent (outils/windows/grymoir.manifest)
RESSOURCES = outils/windows/grym-ressources.o
WINDRES ?= windres
endif

ENTETES   = src/natif.h src/grym_natif.h src/qr.h src/coffre.h src/teinte.h src/chemins.h src/interface.h src/base.h src/vm_interne.h src/date.h src/compact.h src/imprimeur.h src/lexeur.h src/analyseur.h src/arbre.h src/texte.h src/compilateur.h src/vm.h src/bytecode.h src/decimal.h

all: grym grym-lexeur grym-arbre grym-suites test_lexeur test_analyseur test_machine test_imprimeur test_compact test_base test_lsp test_serveur test_guide test_serie

vendor/mbedtls/library/%.o: vendor/mbedtls/library/%.c
	$(CC) -std=c99 -O2 -w -Ivendor/mbedtls/include -Ivendor/mbedtls/library -c -o $@ $<

$(MBEDTLS_A): $(MBEDTLS_OBJ)
	$(AR) rcs $@ $(MBEDTLS_OBJ)

$(SQLITE_O): vendor/sqlite/sqlite3.c vendor/sqlite/sqlite3.h
	$(CC) $(SQLITE_FLAGS) -c -o $@ vendor/sqlite/sqlite3.c

grym: src/grym.c src/lsp.c src/serveur.c src/aiguilleur.c $(TLS) $(EXECUTION) $(ENTETES) src/lsp.h src/json.h src/serveur.h src/aiguilleur.h src/tls.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -isystem vendor/mbedtls/include -o $@ src/grym.c src/lsp.c src/serveur.c src/aiguilleur.c $(TLS) $(EXECUTION) $(RESEAU) -lm $(DL)

outils/windows/grym-ressources.o: outils/windows/grym.rc outils/windows/grymoir.manifest
	$(WINDRES) -I outils/windows -i outils/windows/grym.rc -o $@

grym-lexeur: src/grym-lexeur.c $(LEXEUR) $(ENTETES)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ src/grym-lexeur.c $(LEXEUR)

grym-arbre: src/grym-arbre.c $(ANALYSEUR) $(ENTETES)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ src/grym-arbre.c $(ANALYSEUR)

grym-suites: src/grym-suites.c $(ANALYSEUR) $(ENTETES)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ src/grym-suites.c $(ANALYSEUR)

test_serie: tests/test_serie.c src/serie.c src/texte.c src/serie.h $(RESSOURCES)
	$(CC) $(CFLAGS) $(CPPFLAGS) -Isrc -o $@ tests/test_serie.c src/serie.c src/texte.c $(RESSOURCES) $(RESEAU)

test_lexeur: tests/test_lexeur.c $(LEXEUR) $(ENTETES)
	$(CC) $(CFLAGS) $(CPPFLAGS) -Isrc -o $@ tests/test_lexeur.c $(LEXEUR)

test_analyseur: tests/test_analyseur.c $(ANALYSEUR) $(ENTETES)
	$(CC) $(CFLAGS) $(CPPFLAGS) -Isrc -o $@ tests/test_analyseur.c $(ANALYSEUR)

test_machine: tests/test_machine.c $(EXECUTION) $(ENTETES)
	$(CC) $(CFLAGS) $(CPPFLAGS) -Isrc -o $@ tests/test_machine.c $(EXECUTION) $(RESEAU) -lm $(DL)

test_imprimeur: tests/test_imprimeur.c $(ANALYSEUR) $(ENTETES)
	$(CC) $(CFLAGS) $(CPPFLAGS) -Isrc -o $@ tests/test_imprimeur.c $(ANALYSEUR)

test_compact: tests/test_compact.c $(EXECUTION) $(ENTETES)
	$(CC) $(CFLAGS) $(CPPFLAGS) -Isrc -o $@ tests/test_compact.c $(EXECUTION) $(RESEAU) -lm $(DL)

test_base: tests/test_base.c $(SQLITE_O)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ tests/test_base.c $(SQLITE_O)

test_lsp: tests/test_lsp.c src/lsp.c src/json.c $(ANALYSEUR) $(ENTETES) src/lsp.h src/json.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -Isrc -o $@ tests/test_lsp.c src/lsp.c src/json.c $(ANALYSEUR)

test_serveur: tests/test_serveur.c src/serveur.c $(EXECUTION) $(ENTETES) src/serveur.h src/json.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -Isrc -o $@ tests/test_serveur.c src/serveur.c $(EXECUTION) $(RESEAU) -lm $(DL)

test_guide: tests/test_guide.c docs/guide.md $(EXECUTION) $(ENTETES)
	$(CC) $(CFLAGS) $(CPPFLAGS) -Isrc -o $@ tests/test_guide.c $(EXECUTION) $(RESEAU) -lm $(DL)

test: grym grym-arbre test_lexeur test_analyseur test_machine test_imprimeur test_compact test_base test_lsp test_serveur test_guide test_serie
	./test_lexeur
	./test_analyseur
	./test_machine
	./test_imprimeur
	./test_compact
	./test_base
	./test_lsp
	./test_serveur
	./test_guide
	./test_serie
	sh tests/formes
	sh tests/praxis-versions
	sh tests/mise-a-jour
	sh tests/bibliotheques
	CC="$(CC)" sh tests/natif
	sh tests/listes-sur-place
	sh tests/recherche-par-cle
	sh tests/conflits
	sh tests/connexion
	sh tests/sessions-concurrentes
	sh tests/certificats
	sh tests/sessions-reseau
	sh tests/trousseau
	sh tests/demarrage
	sh tests/apprendre
	sh tests/praxis-justificatif
	sh tests/praxis-electronique
	sh tests/praxis-twint
	sh tests/praxis-scenarios
	sh tests/praxis-remise
	sh tests/praxis-autrement
	sh tests/praxis-agenda
	sh tests/praxis-reste-du
	sh tests/praxis-horaire
	sh tests/praxis-fusion
	sh tests/praxis-acces
	sh tests/praxis-salaire
	sh tests/praxis-virements
	sh tests/praxis-virements-recus
	sh tests/plantages
	sh tests/ecoutille-epellation
	sh tests/ecoutille-pilotage
	sh tests/ecoutille-carnet
	sh tests/ecoutille-scenarios
	sh tests/ecoutille
	sh tests/balayer

clean:
	rm -f grym grym-lexeur grym-arbre grym-suites test_lexeur test_analyseur test_machine test_imprimeur test_compact test_base test_lsp test_serveur test_guide *.exe outils/windows/grym-ressources.o

# Recompiler SQLite prend une trentaine de secondes : « make clean » le garde, « make distclean » non.
distclean: clean
	rm -f $(SQLITE_O) $(MBEDTLS_OBJ) $(MBEDTLS_A)

.PHONY: all test clean distclean

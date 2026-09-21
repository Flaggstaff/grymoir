# GrymoiR

Langage de programmation francophone. Référence : `docs/charte-grymoir.md`, `docs/grammaire.md` et `docs/vm.md`.

## État : v0.2 en cours (machine virtuelle, conditions, formules)

Compilation et tests (compilateur C99 requis : gcc, clang ou zig cc) :

    make
    make test
    ./grym                                # boucle interactive
    ./grym lancer exemples/facture.grym   # compiler puis exécuter
    ./grym compiler exemples/facture.grym # produire exemples/facture.grymb
    ./grym desassembler exemples/facture.grymb
    ./grym-lexeur exemples/facture.grym      # jetons
    ./grym-arbre exemples/facture.grym    # arbre syntaxique
    printf 'Le total vaut 1.\nLe to' | ./grym-suites   # aide à la saisie

Sous Windows sans `make` :

    gcc -std=c99 -O2 -o grym.exe src/grym.c src/compilateur.c src/vm.c src/bytecode.c src/decimal.c src/analyseur.c src/arbre.c src/texte.c src/lexeur.c
    gcc -std=c99 -O2 -o grym-lexeur.exe src/grym-lexeur.c src/lexeur.c
    gcc -std=c99 -O2 -Isrc -o test_lexeur.exe tests/test_lexeur.c src/lexeur.c
    gcc -std=c99 -O2 -o grym-arbre.exe src/grym-arbre.c src/analyseur.c src/arbre.c src/texte.c src/lexeur.c
    gcc -std=c99 -O2 -o grym-suites.exe src/grym-suites.c src/analyseur.c src/arbre.c src/texte.c src/lexeur.c
    gcc -std=c99 -O2 -Isrc -o test_analyseur.exe tests/test_analyseur.c src/analyseur.c src/arbre.c src/texte.c src/lexeur.c

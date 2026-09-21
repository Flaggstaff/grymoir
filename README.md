# GrymoiR

Langage de programmation francophone. Référence : `docs/charte-grymoir.md` et `docs/grammaire.md`.

## État : v0.1 (lexer, analyseur, suites attendues, calcul décimal exact, boucle interactive)

Compilation et tests (compilateur C99 requis : gcc, clang ou zig cc) :

    make
    make test
    ./grym                                # boucle interactive
    ./grym lancer exemples/facture.grym   # exécuter un fichier
    ./grym-lex exemples/facture.grym      # jetons
    ./grym-arbre exemples/facture.grym    # arbre syntaxique
    printf 'Le total vaut 1.\nLe to' | ./grym-suites   # aide à la saisie

Sous Windows sans `make` :

    gcc -std=c99 -O2 -o grym.exe src/grym.c src/evaluateur.c src/decimal.c src/analyseur.c src/arbre.c src/texte.c src/lexeur.c
    gcc -std=c99 -O2 -o grym-lex.exe src/grym-lex.c src/lexeur.c
    gcc -std=c99 -O2 -Isrc -o test_lexeur.exe tests/test_lexeur.c src/lexeur.c
    gcc -std=c99 -O2 -o grym-arbre.exe src/grym-arbre.c src/analyseur.c src/arbre.c src/texte.c src/lexeur.c
    gcc -std=c99 -O2 -o grym-suites.exe src/grym-suites.c src/analyseur.c src/arbre.c src/texte.c src/lexeur.c
    gcc -std=c99 -O2 -Isrc -o test_analyseur.exe tests/test_analyseur.c src/analyseur.c src/arbre.c src/texte.c src/lexeur.c

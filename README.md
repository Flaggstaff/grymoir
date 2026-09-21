# GrymoiR

Langage de programmation francophone. Voir `charte-grymoir-v1.2.md` et `grammaire-v0.1.md`.

## État : v0.1, étape 2 sur 3 (lexer, analyseur)

Compilation et tests (compilateur C99 requis : gcc, clang ou zig cc) :

    make
    make test
    ./grym-lex exemples/facture.grym      # jetons
    ./grym-arbre exemples/facture.grym    # arbre syntaxique

Sous Windows sans `make` :

    gcc -std=c99 -O2 -o grym-lex.exe src/grym-lex.c src/lexeur.c
    gcc -std=c99 -O2 -Isrc -o test_lexeur.exe tests/test_lexeur.c src/lexeur.c
    gcc -std=c99 -O2 -o grym-arbre.exe src/grym-arbre.c src/analyseur.c src/arbre.c src/texte.c src/lexeur.c
    gcc -std=c99 -O2 -Isrc -o test_analyseur.exe tests/test_analyseur.c src/analyseur.c src/arbre.c src/texte.c src/lexeur.c

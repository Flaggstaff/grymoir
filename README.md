# GrymoiR

Langage de programmation francophone. Voir `charte-grymoir-v1.2.md` et `grammaire-v0.1.md`.

## État : v0.1, étape 1 sur 3 (lexer)

Compilation et tests (compilateur C99 requis : gcc, clang ou zig cc) :

    make
    make test
    ./grym-lex exemples/facture.grym

Sous Windows sans `make` :

    gcc -std=c99 -O2 -o grym-lex.exe src/grym-lex.c src/lexeur.c
    gcc -std=c99 -O2 -Isrc -o test_lexeur.exe tests/test_lexeur.c src/lexeur.c

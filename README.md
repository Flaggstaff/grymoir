# GrymoiR

Langage de programmation francophone. Référence : `docs/charte-grymoir.md`, `docs/grammaire.md` et `docs/vm.md`.

## État : v1.0 en préparation (application console)

Chaque poussée compile et lance les tests sous Linux (gcc, clang, puis ASan et UBSan), macOS (clang) et Windows (MinGW-w64 gcc) : `.github/workflows/tests.yml`. Critères de sortie de la v1.0 : charte, art. 12.

## Aperçu

Une application console complète, base comprise : le formulaire se déduit de l'entité, une erreur annule le choix en cours et le menu revient.

    Un contact, conservé, a :
        un nom (texte), unique,
        une naissance (date), facultative.

    Pour ajouter :
        Le c vaut un nouveau contact saisi.
        Conserver c.
        Afficher « Ajouté : » puis nom du c.

    Pour lister :
        Pour chaque contact conservé, par nom :
            Afficher nom du contact sur 20 puis naissance du contact.

    Le choix vaut la réponse en nombre entier à « 1 ajouter, 2 lister, 0 quitter ? ».
    Tant que choix ≠ 0 :
        Essayer :
            Selon choix :
                Cas 1, ajouter.
                Cas 2, lister.
        En cas d'échec, afficher « Rien n'a changé : » puis le motif de l'échec.
        Le choix devient la réponse en nombre entier à « 1 ajouter, 2 lister, 0 quitter ? ».

    1 ajouter, 2 lister, 0 quitter ? 1
    Nom ? Élodie
    Naissance ? 21.09.1990
    Ajouté : Élodie
    1 ajouter, 2 lister, 0 quitter ? 1
    Nom ? Élodie
    « Élodie » est déjà pris. Tapez « . » seul pour annuler.

Pour aller plus loin : `exemples/partotheque.grym`, une bibliothèque de partitions (liens, genres, corbeille, modification par formulaire). La référence du langage est `docs/grammaire.md` ; ce que chaque version promet est l'art. 13 de la charte.

## Compiler

Compilation et tests (compilateur C99 requis : gcc, clang ou zig cc) :

    make
    make test
    ./grym                                # boucle interactive
    ./grym lancer exemples/facture.grym   # compiler puis exécuter
    ./grym compiler exemples/facture.grym # produire exemples/facture.grymb
    ./grym desassembler exemples/facture.grymb
    ./grym formater exemples/formules.grym   # forme littéraire canonique
    ./grym traduire exemples/formules.grym   # forme compacte : exemples/formules.grymc
    ./grym lancer exemples/formules.grymc    # la forme compacte s'exécute aussi
    ./grym lancer exemples/conserver.grym    # base conserver.grymd, créée à côté
    ./grym lancer exemples/registre.grym     # se relit d'une exécution à l'autre (registre.grymd)
    ./grym lancer exemples/saisie.grym       # carnet d'adresses : questions, colonnes, menu
    ./grym lancer exemples/partotheque.grym  # bibliothèque de partitions : formulaires, Essayer, corbeille
    ./grym --base essai.grymd                # boucle interactive sur une base conservée
    ./grym-lexeur exemples/facture.grym      # jetons
    ./grym-arbre exemples/facture.grym    # arbre syntaxique
    printf 'Le total vaut 1.\nLe to' | ./grym-suites   # aide à la saisie

Sous Windows sans `make` :

    gcc -std=c99 -O2 -DSQLITE_THREADSAFE=0 -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_DQS=0 -DSQLITE_DEFAULT_FOREIGN_KEYS=1 -DSQLITE_OMIT_DEPRECATED -DSQLITE_DEFAULT_MEMSTATUS=0 -c -o vendor/sqlite/sqlite3.o vendor/sqlite/sqlite3.c
    gcc -std=c99 -O2 -Ivendor/sqlite -o grym.exe src/grym.c src/lsp.c src/json.c src/compilateur.c src/vm.c src/base.c src/bytecode.c src/decimal.c src/analyseur.c src/arbre.c src/texte.c src/imprimeur.c src/compact.c src/lexeur.c src/date.c vendor/sqlite/sqlite3.o
    gcc -std=c99 -O2 -o grym-lexeur.exe src/grym-lexeur.c src/lexeur.c src/date.c src/texte.c
    gcc -std=c99 -O2 -Ivendor/sqlite -o test_base.exe tests/test_base.c vendor/sqlite/sqlite3.o
    gcc -std=c99 -O2 -Isrc -o test_lexeur.exe tests/test_lexeur.c src/lexeur.c src/date.c src/texte.c
    gcc -std=c99 -O2 -o grym-arbre.exe src/grym-arbre.c src/analyseur.c src/arbre.c src/texte.c src/imprimeur.c src/compact.c src/decimal.c src/lexeur.c src/date.c
    gcc -std=c99 -O2 -o grym-suites.exe src/grym-suites.c src/analyseur.c src/arbre.c src/texte.c src/imprimeur.c src/compact.c src/decimal.c src/lexeur.c src/date.c
    gcc -std=c99 -O2 -Isrc -o test_analyseur.exe tests/test_analyseur.c src/analyseur.c src/arbre.c src/texte.c src/imprimeur.c src/compact.c src/decimal.c src/lexeur.c src/date.c
    gcc -std=c99 -O2 -Isrc -Ivendor/sqlite -o test_machine.exe tests/test_machine.c src/compilateur.c src/vm.c src/base.c src/bytecode.c src/decimal.c src/analyseur.c src/arbre.c src/texte.c src/imprimeur.c src/compact.c src/lexeur.c src/date.c vendor/sqlite/sqlite3.o
    gcc -std=c99 -O2 -Isrc -Ivendor/sqlite -o test_compact.exe tests/test_compact.c src/compilateur.c src/vm.c src/base.c src/bytecode.c src/decimal.c src/analyseur.c src/arbre.c src/texte.c src/imprimeur.c src/compact.c src/lexeur.c src/date.c vendor/sqlite/sqlite3.o
    gcc -std=c99 -O2 -Isrc -o test_imprimeur.exe tests/test_imprimeur.c src/analyseur.c src/arbre.c src/texte.c src/imprimeur.c src/compact.c src/decimal.c src/lexeur.c src/date.c
    gcc -std=c99 -O2 -Isrc -o test_lsp.exe tests/test_lsp.c src/lsp.c src/json.c src/analyseur.c src/arbre.c src/texte.c src/imprimeur.c src/compact.c src/decimal.c src/lexeur.c src/date.c

## SQLite embarqué

`vendor/sqlite` contient SQLite 3.53.4, dans le domaine public. Sa provenance et sa vérification sont décrites dans `vendor/sqlite/PROVENANCE.md`. Sa compilation prend une demi-minute ; `make clean` garde `sqlite3.o`, `make distclean` le supprime.

## Éditeur : VS Code

`grym lsp` est un serveur d'aide à la saisie (protocole LSP) : erreurs en direct, autocomplétion,
mise en forme. L'extension de `editeurs/vscode/` le lance et colore les deux formes. Voir
`editeurs/vscode/README.md` pour l'installation, et `docs/lsp.md` pour la spécification.

# GrymoiR

Langage de programmation francophone. Référence : `docs/charte-grymoir.md`, `docs/grammaire.md` et `docs/vm.md`.

## État : v2.0 livrée : l'application en console (`grym lancer`) ou dans le navigateur (`grym servir`)

Chaque poussée compile et lance les tests sous Linux (gcc, clang, puis ASan et UBSan), macOS (clang) et Windows (MinGW-w64 gcc) : `.github/workflows/tests.yml`. Jalons et critères : charte, art. 12 ; promesses de compatibilité : art. 13.

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

## Faire évoluer le langage

`docs/mainteneur.md` : l'architecture du compilateur, les outils de mise au point, et la recette pour ajouter une phrase de bout en bout.

## Compiler

L'atelier, en une commande (construit tout, puis lance ; sous macOS, Qt 6 par Homebrew : `brew install qt`) :

    ./atelier
    ./appliquer ~/Downloads/grymoir-x.patch   # appliquer un patch, vérifier, publier, relancer

Compilation et tests (compilateur C99 requis : gcc, clang ou zig cc) :

    make
    make test
    ./grym                                # boucle interactive
    ./grym lancer exemples/facture.grym   # compiler puis exécuter
    ./grym compiler exemples/facture.grym # produire exemples/facture.grymb
./grym migration prog.grym     # ce que le prochain lancement fera à la base, sans rien changer
    ./grym desassembler exemples/facture.grymb
    ./grym formater exemples/formules.grym   # forme littéraire canonique
    ./grym traduire exemples/formules.grym   # forme compacte : exemples/formules.grymc
    ./grym lancer exemples/formules.grymc    # la forme compacte s'exécute aussi
    ./grym lancer exemples/conserver.grym    # base conserver.grymd, créée à côté
    ./grym lancer exemples/registre.grym     # se relit d'une exécution à l'autre (registre.grymd)
    ./grym lancer exemples/saisie.grym       # carnet d'adresses : questions, colonnes, menu
    ./grym lancer exemples/partotheque.grym  # bibliothèque de partitions : formulaires, Essayer, corbeille
    ./grym servir exemples/partotheque.grym  # la même, dans le navigateur (serveur local, 127.0.0.1)

`grym servir` ouvre le navigateur sur une adresse locale protégée par un jeton, et le programme y tourne sans une ligne à changer : chaque formulaire devient une page, les liens proposent les objets existants, les fichiers se téléversent, les images s'affichent. Le Terminal reste occupé tant que l'application tourne ; Ctrl+C l'arrête. Options : `--port N`, `--sans-navigateur`. Le protocole et ses règles de sécurité : `docs/v2.md`, § 5, § 9 et § 10.
    ./grym --base essai.grymd                # boucle interactive sur une base conservée
    ./grym-lexeur exemples/facture.grym      # jetons
    ./grym-arbre exemples/facture.grym    # arbre syntaxique
    printf 'Le total vaut 1.\nLe to' | ./grym-suites   # aide à la saisie

Sous Windows sans `make` :

    gcc -std=c99 -O2 -DSQLITE_THREADSAFE=0 -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_DQS=0 -DSQLITE_DEFAULT_FOREIGN_KEYS=1 -DSQLITE_OMIT_DEPRECATED -DSQLITE_DEFAULT_MEMSTATUS=0 -c -o vendor/sqlite/sqlite3.o vendor/sqlite/sqlite3.c
    gcc -std=c99 -O2 -Ivendor/sqlite -o grym.exe src/grym.c src/lsp.c src/json.c src/serveur.c src/compilateur.c src/vm.c src/console.c src/base.c src/bytecode.c src/decimal.c src/analyseur.c src/arbre.c src/texte.c src/imprimeur.c src/compact.c src/lexeur.c src/date.c vendor/sqlite/sqlite3.o -lws2_32 -lbcrypt
    gcc -std=c99 -O2 -o grym-lexeur.exe src/grym-lexeur.c src/lexeur.c src/date.c src/texte.c
    gcc -std=c99 -O2 -Ivendor/sqlite -o test_base.exe tests/test_base.c vendor/sqlite/sqlite3.o
    gcc -std=c99 -O2 -Isrc -o test_lexeur.exe tests/test_lexeur.c src/lexeur.c src/date.c src/texte.c
    gcc -std=c99 -O2 -o grym-arbre.exe src/grym-arbre.c src/analyseur.c src/arbre.c src/texte.c src/imprimeur.c src/compact.c src/decimal.c src/lexeur.c src/date.c
    gcc -std=c99 -O2 -o grym-suites.exe src/grym-suites.c src/analyseur.c src/arbre.c src/texte.c src/imprimeur.c src/compact.c src/decimal.c src/lexeur.c src/date.c
    gcc -std=c99 -O2 -Isrc -o test_analyseur.exe tests/test_analyseur.c src/analyseur.c src/arbre.c src/texte.c src/imprimeur.c src/compact.c src/decimal.c src/lexeur.c src/date.c
    gcc -std=c99 -O2 -Isrc -Ivendor/sqlite -o test_machine.exe tests/test_machine.c src/compilateur.c src/vm.c src/console.c src/base.c src/bytecode.c src/decimal.c src/analyseur.c src/arbre.c src/texte.c src/imprimeur.c src/compact.c src/lexeur.c src/date.c vendor/sqlite/sqlite3.o
    gcc -std=c99 -O2 -Isrc -Ivendor/sqlite -o test_compact.exe tests/test_compact.c src/compilateur.c src/vm.c src/console.c src/base.c src/bytecode.c src/decimal.c src/analyseur.c src/arbre.c src/texte.c src/imprimeur.c src/compact.c src/lexeur.c src/date.c vendor/sqlite/sqlite3.o
    gcc -std=c99 -O2 -Isrc -o test_imprimeur.exe tests/test_imprimeur.c src/analyseur.c src/arbre.c src/texte.c src/imprimeur.c src/compact.c src/decimal.c src/lexeur.c src/date.c
    gcc -std=c99 -O2 -Isrc -o test_lsp.exe tests/test_lsp.c src/lsp.c src/json.c src/analyseur.c src/arbre.c src/texte.c src/imprimeur.c src/compact.c src/decimal.c src/lexeur.c src/date.c
    gcc -std=c99 -O2 -Isrc -Ivendor/sqlite -o test_serveur.exe tests/test_serveur.c src/serveur.c src/compilateur.c src/vm.c src/console.c src/base.c src/bytecode.c src/decimal.c src/analyseur.c src/arbre.c src/texte.c src/imprimeur.c src/compact.c src/lexeur.c src/date.c vendor/sqlite/sqlite3.o -lws2_32 -lbcrypt

## SQLite embarqué

`vendor/sqlite` contient SQLite 3.53.4, dans le domaine public. Sa provenance et sa vérification sont décrites dans `vendor/sqlite/PROVENANCE.md`. Sa compilation prend une demi-minute ; `make clean` garde `sqlite3.o`, `make distclean` le supprime.

## Éditeur : VS Code

`grym lsp` est un serveur d'aide à la saisie (protocole LSP) : erreurs en direct, autocomplétion,
mise en forme. L'extension de `editeurs/vscode/` le lance et colore les deux formes. Voir
`editeurs/vscode/README.md` pour l'installation, et `docs/lsp.md` pour la spécification.

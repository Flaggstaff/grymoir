# GrymoiR : l'atelier

Proposition soumise à relecture, rédigée le 24 septembre 2026. Rien n'est implémenté.
Référence : Charte de GrymoiR v1.36, art. 1, 3, 4, 11, 12 et 13 ; grammaire 1.37, § 12, § 16 ; `docs/v2.md`.
Toute décision prise sur ce document passe dans la charte par une révision numérotée.

---

## 1. Thèse

GrymoiR devient un environnement de développement complet : on y définit ses données, on y dessine ses écrans, on y écrit ses traitements, on y lance son application. Le tout dans de vraies fenêtres, sous Windows, macOS et Linux.

**Bibliothèque graphique : Qt 6**, décidé le 24 septembre 2026. L'atelier et les applications qu'il produit utilisent la même bibliothèque : un écran dessiné dans l'atelier s'affiche à l'identique dans l'application.

## 2. Principe directeur : l'atelier n'écrit que du GrymoiR

Tout geste dans l'atelier produit des phrases GrymoiR dans les fichiers `.grym` du projet : créer une entité, ajouter un champ, tracer un lien, plus tard dessiner un écran.

- Aucune base de métadonnées cachée, aucun format propre à l'atelier pour ce qui fait le programme. Le fichier `.grym` est la seule vérité (charte, art. 4 : la forme littéraire fait référence).
- Une modification faite à la main dans le fichier apparaît dans l'atelier ; une modification faite dans l'atelier se lit dans le fichier.
- Ce que l'atelier génère (plus tard : une liste et une fiche par entité) est du code ordinaire, qui appartient au développeur et se modifie comme le reste.
- `grym lancer`, `grym formater`, `grym traduire`, `git` et l'extension VS Code continuent de fonctionner sur tout projet fait dans l'atelier.

Conséquence : l'atelier ne peut rien faire que le langage ne sache dire. Chaque nouvelle fonction de l'atelier commence par sa phrase GrymoiR.

## 3. Réécriture chirurgicale

L'atelier modifie **la seule phrase concernée**, à sa position dans la source, et laisse le reste du fichier octet pour octet.

- Ajouter un champ insère une ligne dans la déclaration de l'entité ; renommer un type réécrit la parenthèse ; rien d'autre ne bouge.
- Le texte inséré est produit par l'imprimeur (grammaire, § 12), dans la forme canonique.
- Réécrire tout le fichier par `grym formater` à chaque geste est exclu : un développeur qui a ses habitudes d'écriture les perdrait au premier clic.
- Après chaque réécriture, l'atelier réanalyse le fichier. Si le résultat ne s'analyse pas, la réécriture est annulée et l'erreur affichée : l'atelier ne laisse jamais un fichier cassé derrière lui.

## 4. Architecture

```
            ┌──────────────── cœur, C99, sans dépendance ─────────────────┐
            │ lexeur · analyseur · imprimeur · bytecode · VM · base SQLite │
            └───────┬──────────────────────┬──────────────────────┬───────┘
                    │                      │                      │
       grym (console, navigateur, LSP)   interface Qt          grym-atelier
       inchangé, aucune dépendance       (src/qt/, C++)        (C++, Qt 6)
```

- **Le cœur reste en C99 sans dépendance.** `grym` en console fonctionne partout, sans Qt, comme aujourd'hui.
- **Une interface Qt** implémente `src/interface.h`, comme la console et le navigateur l'implémentent déjà : les programmes actuels s'affichent en fenêtre sans modification.
- **`grym-atelier`** est un exécutable C++ qui lie le cœur directement : il appelle l'analyseur et l'imprimeur sans passer par un processus ni par le protocole LSP.
- **Qt Widgets**, pas QML : contrôles natifs de bureau, tout en C++, sans second langage de description.
- **Version : Qt 6.4 au moins**, décidé le 24 septembre 2026 : c'est la version des distributions Linux courantes (Ubuntu 24.04, Debian 12), et l'atelier n'emploie que Qt Widgets, stable depuis Qt 6.0. Construit et essayé avec Qt 6.4.2 (Linux) ; Homebrew fournit Qt 6.11.2 sous macOS.
- **Licence de Qt : LGPLv3**, liée dynamiquement. Le dépôt reste sous tous droits réservés ; la LGPL impose seulement de permettre le remplacement de la bibliothèque Qt, ce que la liaison dynamique assure.
- **Construction** : le `Makefile` actuel reste pour le cœur et `grym`. La partie Qt, qui a besoin des outils de Qt (`moc`), se construit avec CMake. L'intégration continue ajoute la construction de l'atelier sur les trois systèmes.

## 5. Jalons proposés

| Jalon | Contenu |
|---|---|
| A1 | Squelette : fenêtre, ouvrir un projet (un dossier), liste de ses fichiers, affichage du texte avec coloration et erreurs en direct, bouton « Lancer » qui exécute le programme dans un panneau de l'atelier (l'interface Qt, en mode séquentiel) |
| A2 | Inclusion d'un fichier dans un autre (grammaire, conçue d'abord) ; éditeur de données : schéma des entités (une boîte par entité, une flèche par lien), panneau de propriétés (champs, types, unique, facultatif, valeur de départ, cascade), réécriture chirurgicale (§ 3), aperçu de ce que la migration fera à la base avant de l'appliquer (§ 16.7), et refus expliqué quand elle détruirait des données |
| A3 | Les écrans dans le langage : fenêtres, listes, fiches, boutons, modèle par événements. Conception phrase par phrase dans la grammaire, avant tout code |
| A4 | Éditeur d'écrans : génération d'une liste et d'une fiche par entité, en GrymoiR modifiable ; agencement à la souris |

État de A1 (24 septembre 2026) : fait. La fenêtre, l'ouverture d'un projet (un dossier, ou le dernier ouvert), la liste de ses fichiers `.grym` et `.grymc`, l'éditeur (numéros de ligne, coloration par le lexeur, première erreur de l'analyseur soulignée en direct, sa ligne en rouge dans la marge, son message au survol et dans le panneau du bas, un clic y mène), l'enregistrement sûr (écrit à côté, puis remplace).

« Lancer » (Ctrl+R, Cmd+R sous macOS) enregistre le fichier, refuse de lancer s'il porte une erreur connue, puis exécute `grym-atelier --lancer fichier`, décidé le 24 septembre 2026 : un processus à part, pour que l'atelier survive à tout ce qui arrive au programme (charte, principe 1). Le programme a sa fenêtre : le fil de ce qu'il affiche (texte, images, fiches), les réponses acceptées recopiées dans le fil comme en console, et les champs de la question en cours, avec les menus du § 10 de `docs/v2.md` (lien, vrai ou faux), un bouton « Choisir… » pour un fichier ou une image, la case « vider », le refus sous le champ refusé. La machine tourne dans un fil à elle : la fenêtre reste vivante pendant un long calcul. « Arrêter », dans l'atelier ou dans la fenêtre du programme, lève l'interruption de la machine (docs/vm.md, § 6), qui annule l'exécution ; fermer la fenêtre en cours d'exécution fait de même. L'atelier demande l'arrêt par SIGTERM (sous Windows, par la fermeture de la fenêtre), puis, après cinq secondes sans réponse, met fin au processus : SQLite annule alors la transaction inachevée. L'erreur d'exécution, lue sur la sortie d'erreur au format de `grym lancer`, s'ajoute au panneau du bas, et un clic mène à sa ligne.

Intégration continue : l'atelier se construit et ses essais tournent sous Ubuntu, macOS et Windows (MSYS2), dans `.github/workflows/tests.yml`.

Construction : `cmake -B construction`, puis `cmake --build construction` ; essais sans fenêtre : `ctest --test-dir construction` (`tests/test_atelier.cpp`). Le cœur est compilé une seconde fois par CMake, depuis les mêmes sources que le `Makefile`.

Chaque jalon livre un outil utilisable. A1 est volontairement mince : il pose la fenêtre, le lien avec le cœur et la construction sur trois systèmes, sur quoi tout le reste repose.

## 6. Questions ouvertes

1. **Positions des boîtes du schéma.** Où ranger qu'une entité est dessinée en haut à gauche ? Ce n'est pas du programme. Proposition : un fichier `.grymatelier` à côté du programme, texte lisible, que l'atelier recrée s'il manque (disposition automatique). Le perdre ne perd jamais rien du programme.
2. **Un projet, un fichier ?** Aujourd'hui, un programme tient dans un fichier. Une vraie application voudra plusieurs fichiers (données, écrans, traitements). Décidé le 24 septembre 2026 : l'inclusion se conçoit en A2, dans la grammaire, avant l'éditeur de données, qui travaille d'emblée sur un projet à plusieurs fichiers.
3. **L'éditeur de code** de l'atelier. Décidé le 24 septembre 2026 : un éditeur maison sur `QPlainTextEdit`, dont la coloration passe par le lexeur de GrymoiR et l'autocomplétion par le calcul des suites (grammaire, § 8), les erreurs venant de l'analyseur, sans passer par le protocole LSP. Une bibliothèque existante aurait demandé de réécrire sa coloration, et QScintilla, sous GPLv3 ou licence commerciale, aurait imposé la GPL à l'atelier.
4. **Distribution** : l'atelier embarque ses bibliothèques Qt (outils de déploiement fournis par Qt sur macOS et Windows). La signature des exécutables pour macOS et Windows est reportée.

## 7. Ce que la charte devra dire

- Art. 3 : deux exécutables, `grym` (sans dépendance) et `grym-atelier` (Qt 6).
- Art. 11 : l'interface graphique native entre dans le périmètre ; l'éditeur visuel d'interfaces quitte l'horizon pour les jalons A3 et A4.
- Art. 12 : les jalons A1 à A4.
- `docs/v2.md` : `grym servir` reste un mode d'affichage parmi d'autres ; les « écrans nommés » et le modèle par événements prévus en v2.x passent en A3, pour Qt d'abord.

---

## Journal des révisions

| Version | Date | Changement |
|---------|------|------------|
| 0.1 | 2026-09-24 | Proposition initiale : Qt 6, l'atelier n'écrit que du GrymoiR, réécriture chirurgicale, architecture, jalons A1 à A4, questions ouvertes |
| 0.2 | 2026-09-24 | § 6.2 : l'inclusion se conçoit en A2, avant l'éditeur de données |
| 0.3 | 2026-09-24 | § 6.3 : éditeur de code maison, branché sur le lexeur, les suites et l'analyseur |
| 0.4 | 2026-09-24 | § 4 : Qt 6.4 au moins ; § 5 : A1 commencé (fenêtre, projet, éditeur, coloration, erreurs en direct) |
| 0.5 | 2026-09-24 | § 5 : A1 fait ; « Lancer » dans un processus à part, fenêtre d'exécution, arrêt, intégration continue |

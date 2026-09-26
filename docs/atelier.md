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

État de A2 (24 septembre 2026) : les fichiers utilisés sont faits (grammaire, § 21). Dans l'atelier, la liste du projet écrit les programmes en gras et les fichiers de déclarations normalement, le survol dit lequel ; une erreur venue d'un fichier utilisé se signale sur la phrase `Utiliser`. « Lancer », depuis un fichier de déclarations, exécute le programme principal : le seul du projet, ou celui qu'on choisit une fois (menu « Programme », « Choisir le programme principal… »), retenu par projet. Une erreur d'exécution dans un autre fichier l'ouvre au clic. L'éditeur de données se livre en trois tranches, décidé le 24 septembre 2026 : A2-a voir, A2-b modifier, A2-c migrer sans surprise.

A2-a, fait le 24 septembre 2026 : un onglet « Données » à côté du code dessine le schéma de tout le projet, relu à chaque ouverture de l'onglet. Une boîte par entité (nom, classe parente, aptitudes adoptées, champs avec leur type, « unique », « facultatif », « plusieurs », « disparaît avec »), une flèche par lien, une double pointe pour « plusieurs », un trait tireté à pointe creuse pour l'héritage, une boucle pour un lien vers soi. Les entités viennent de tous les fichiers du projet, fichiers utilisés compris, chacune une fois ; les classes ordinaires n'y figurent pas. Un fichier qui ne s'analyse pas est nommé sous le schéma. Une entité sans place reçoit la sienne par étages : en haut celles que rien ne désigne, chaque entité un étage sous celles qu'elle désigne, chaque étage centré ; les flèches montent. Les boîtes se déplacent à la souris ; leur place s'écrit dans `projet.grymatelier` quand on cesse de les déplacer. Double-clic : la déclaration dans le code. Le programme principal a quitté les réglages de la machine pour `projet.grymatelier` ; l'ancien réglage est repris à la première ouverture du projet. En lecture seule : rien ne s'écrit dans les `.grym`.

A2-b, commencé le 24 septembre 2026 : les relations se traduisent ainsi, décidé ce jour. Un lien se déclare d'un seul côté, l'autre sens se déduit (grammaire, § 16.10) : plusieurs vers un, obligatoire (`un compositeur (compositeur)`) ; le même, facultatif (`, facultatif`) ; un pour un (`, unique`) ; plusieurs vers plusieurs (`des genres (genre)`) ; cascade (`, et disparaît avec lui`). Fait : un panneau à droite du schéma montre l'entité choisie ; on la renomme (la déclaration, ses compléments, les liens qui la désignent et les entités qui en héritent suivent), on la supprime, on ajoute une entité (« Nouvelle entité… », dans le fichier qui déclare le plus d'entités, avec un champ `nom (texte), unique` pour qu'un formulaire puisse la désigner), et, dans un tableau, on ajoute, modifie ou supprime un champ : nom, type (de base, ou une entité : c'est alors un lien), féminin, unique, facultatif, plusieurs. Une relation se crée donc déjà en donnant à un champ le type d'une entité. La valeur de départ et la cascade d'un champ existant se gardent quand on le modifie. Chaque geste passe par la réécriture chirurgicale (`src/atelier/reecriture.cpp`) : la phrase de l'entité, réimprimée seule dans la forme canonique, remplace son ancienne étendue ; le reste des fichiers ne bouge pas d'un octet. Puis tout le projet est réanalysé : un fichier qui s'analysait et ne s'analyse plus fait annuler le geste, avec le message (« Geste annulé : il casserait « prog.grym », ligne 3 : … »). « Annuler le dernier geste » défait les gestes un à un. Le code en cours d'édition s'enregistre avant chaque geste, et se relit après. A2-b, fait le 24 septembre 2026. Un lien se crée en tirant une flèche d'une boîte à l'autre (Maj+glisser, ou clic droit, « Lier à ») : un dialogue demande le nom du champ, son genre, sa sorte (plusieurs pour un, facultatif ou non ; un pour un ; plusieurs pour plusieurs) et la cascade, accordés aux genres des deux entités, et montre la phrase GrymoiR qu'il écrira. Un double-clic sur une flèche rouvre ce dialogue ; un clic droit la supprime. Dans le panneau, deux colonnes de plus : « Disparaît avec » et « Au départ ». La valeur de départ se lit par l'analyseur lui-même (« Le v vaut … ») : une constante du type du champ, sinon le geste est refusé avant toute écriture. Une phrase réécrite s'imprime dans son contexte (`imprimer_phrase`, `src/imprimeur.c`) : les genres, aptitudes et pluriels déclarés dans le fichier servent aux accords (« et disparaît avec elle »).

A2-c, fait le 24 septembre 2026 : sous le schéma, l'atelier dit ce que le prochain lancement du programme principal fera à sa base, relu après chaque geste : « La base est à jour », « « compositeur » : champ « pays » ajouté ; 12 compositeurs reçoivent « Suisse » », ou, en rouge, que le lancement sera refusé et pourquoi (les règles du § 16.7 de la grammaire, avec leurs messages). « Lancer » fait le même essai d'abord, et ne lance pas un programme dont la migration serait refusée. L'essai passe par la machine elle-même (`machine_essai_migration`, `src/vm.c`) : elle prépare la base comme pour une exécution, dans sa transaction, note chaque changement (`base_rapport`, `src/base.c`), puis annule tout ; une base qui n'existe pas n'est pas créée. En console : `grym migration programme.grym`. A2 est fait.

Chaque jalon livre un outil utilisable. A1 est volontairement mince : il pose la fenêtre, le lien avec le cœur et la construction sur trois systèmes, sur quoi tout le reste repose.

## 5 bis. A4 : l'éditeur d'écrans, conception

### Écran généré pour une entité *(validé le 25 septembre 2026)*

« Écran pour une entité… » écrit, dans le fichier des écrans, un écran ordinaire, qui appartient au développeur :

```
L'écran des compositeurs montre :
    la liste des compositeurs conservés, par nom,
    un bouton « Nouveau »,
    un bouton « Supprimer »,
    un bouton « Fermer ».
Quand on choisit un compositeur dans l'écran des compositeurs :
    Ouvrir la fiche du compositeur.
Quand on clique sur « Nouveau » dans l'écran des compositeurs :
    Le nouveau vaut un nouveau compositeur saisi.
    Conserver nouveau.
Quand on clique sur « Supprimer » dans l'écran des compositeurs :
    Si le compositeur choisi de l'écran est présent, supprimer le compositeur choisi de l'écran.
Quand on clique sur « Fermer » dans l'écran des compositeurs :
    Fermer l'écran.
```

- Tri par le champ texte unique (la clé des menus et des fiches) ; sans clé, dans l'ordre de conservation.
- Pas de bouton « Modifier » : la fiche en a un, à un double-clic.
- « Supprimer » met dans la corbeille (grammaire, § 16.12).
- Un écran d'accueil, créé au premier écran généré, reçoit un bouton par écran généré ; l'atelier propose d'écrire `Ouvrir l'écran d'accueil.` dans le programme principal.

### L'onglet « Écrans » *(validé le 25 septembre 2026)*

- À gauche, les écrans du projet, lus dans l'arbre ; « Écran pour une entité… » et « Nouvel écran vide ».
- Au centre, l'aperçu de l'écran choisi, dessiné avec les composants de l'exécution ; redessiné après chaque geste et après chaque modification du code.
- À droite, les propriétés de l'élément cliqué : bouton (libellé, « Voir l'événement »), texte, zone (nom, type, départ, facultative), liste (entité, tri, colonnes à cocher, condition `dont` en GrymoiR), écran (titre).
- L'aperçu montre la structure, pas les données : titres des colonnes et deux lignes grisées d'exemple. Les conditions `dont` peuvent citer des zones encore vides ; pour voir les données, « Lancer ».

### Les gestes *(validé le 25 septembre 2026)*

Chaque geste réécrit la déclaration de l'écran, et elle seule ; il est réanalysé, annulé s'il casse quelque chose, et Cmd+Z le défait.

- Ajouter : une palette (bouton, texte, zone, liste) ; glisser à l'endroit voulu, ou un clic pour la fin. Un bouton ajouté reçoit son événement, dont le corps est une remarque (`Remarque : à écrire.`, accepté par le langage, vérifié le 25 septembre 2026).
- Déplacer : glisser dans l'aperçu ; au-dessus ou en dessous, l'ordre change ; sur le bord gauche ou droit, les deux éléments passent `côte à côte`, dans un bloc créé au besoin ; un bloc réduit à un élément disparaît.
- Supprimer (Suppr) : un bouton part avec son événement, après confirmation qui montre le code supprimé ; une zone citée ailleurs est refusée par la réanalyse, qui montre la ligne.
- Renommer un bouton : le libellé change dans la déclaration et dans son événement.
- Écartés : redimensionner à la souris, positions libres.

### Découpage

| Tranche | Contenu |
|---|---|
| A4-a *(fait)* | L'onglet « Écrans » : liste des écrans, aperçu (structure, deux lignes d'exemple), sélection d'un élément, propriétés en lecture, « Voir l'événement » |
| A4-b *(fait)* | « Écran pour une entité… », « Nouvel écran vide », l'écran d'accueil |
| A4-c *(fait)* | Propriétés modifiables ; ajouter, supprimer, renommer, avec les événements |
| A4-d *(fait)* | Déplacer à la souris, blocs créés et retirés |

État de A4-a (25 septembre 2026) : fait. Un onglet « Écrans » à côté de « Code » et « Données » ; les écrans du projet, fichiers utilisés compris, relus à chaque ouverture de l'onglet (le code en cours s'enregistre d'abord). L'aperçu est dessiné par `dessiner_ecran` (`src/atelier/vue_ecran.cpp`), le même dessin que l'exécution, qui l'emploie désormais aussi : blocs, boutons en ligne, listes avec leurs colonnes, par défaut ou choisies, et deux lignes « … » grisées. Dans l'aperçu, un clic choisit un élément, cadré en bleu, sans rien déclencher ; le panneau montre ses propriétés, son texte GrymoiR tel qu'écrit dans le fichier, et « Voir l'événement » ou « Voir la déclaration » ouvre le code à la bonne ligne.

État de A4-b (26 septembre 2026) : fait. « Écran pour une entité… » demande l'entité et écrit l'écran validé ci-dessus, accordé au genre et au pluriel de l'entité (« une nouvelle œuvre saisie », « l'œuvre choisie … est présente ») ; « Nouvel écran vide » demande le nom et écrit un écran avec un bouton « Fermer » et son événement. Le fichier : celui des écrans existants, sinon le programme principal, sinon le fichier ouvert ; en forme littéraire seulement. La place : après la dernière déclaration (classe, aptitude, écran, événement), avant la première phrase qui s'exécute, puisqu'un écran se déclare avant d'être ouvert. L'écran d'accueil naît au premier écran généré, reçoit un bouton par écran (avant « Fermer ») et l'événement qui l'ouvre ; l'atelier propose ensuite d'écrire `Ouvrir l'écran d'accueil.` à la fin du programme principal. Chaque geste passe par la réanalyse et s'annule. Conséquence à connaître : un programme qui ouvre un écran ne se lance plus en console ni dans le navigateur (grammaire, § 22.3). L'imprimeur accorde désormais « … choisie de l'écran » au genre de l'entité.

État de A4-c (26 septembre 2026) : fait. Sous les propriétés, un formulaire selon l'élément choisi : le titre de l'écran ; le libellé d'un bouton ou d'un texte ; le nom, le type, le genre, « facultative » et la valeur de départ d'une zone ; le tri, « décroissant » et les colonnes à cocher d'une liste (aucune cochée : les colonnes par défaut). « Appliquer » réécrit la seule déclaration de l'écran, et l'événement concerné : un bouton renommé l'est aussi dans son `Quand on clique`, une zone dans son `Quand on change`. La condition `dont` d'une liste se garde telle qu'écrite ; elle se modifie dans le code. Une palette ajoute un bouton (avec son événement, `Remarque : à écrire.`), un texte, une zone ou une liste à la fin de l'écran. « Supprimer » ou la touche Suppr retire l'élément, et son événement, après avoir montré ce code ; un bloc se retire sans ses éléments ; le dernier élément d'un écran ne se retire pas. Tout passe par la réanalyse (un libellé en double est refusé, rien n'est écrit) et s'annule.

État de A4-d (26 septembre 2026) : fait, et A4 avec lui. Dans l'aperçu, on glisse un élément ; un trait bleu montre où il atterrira : le quart gauche ou droit d'un élément met à côté, sa moitié haute au-dessus, sa moitié basse en dessous. Dans le sens du bloc qui contient la cible, l'élément prend simplement sa place ; à travers, les deux forment un bloc : `côte à côte` dans une colonne, `l'un sous l'autre` dans une rangée. Un bloc réduit à un seul élément disparaît. La palette ajoute à la fin (un clic) ; on place ensuite l'élément en le glissant : le glisser directement depuis la palette n'est pas fait. Un bloc ne se glisse pas lui-même : on déplace ses éléments.

## 6. Questions ouvertes

1. **Positions des boîtes du schéma.** Décidé le 24 septembre 2026 : un fichier texte `projet.grymatelier` à la racine du projet, lisible et versionné, que seul l'atelier lit (`grym` l'ignore). Une ligne par entité, triée par nom (`compositeur : 40, 120`), et le programme principal (`programme principal : partotheque.grym`), qui quitte les réglages de la machine : c'est une information du projet, pas du poste. S'il manque, ou s'il ignore une entité (nouvelle, ou renommée à la main), l'atelier la place lui-même et complète le fichier. Le perdre ne perd jamais rien du programme. Pas de syntaxe GrymoiR : ce n'est pas du programme.
2. **Un projet, un fichier ?** Aujourd'hui, un programme tient dans un fichier. Une vraie application voudra plusieurs fichiers (données, écrans, traitements). Décidé le 24 septembre 2026 : l'inclusion se conçoit en A2, dans la grammaire, avant l'éditeur de données, qui travaille d'emblée sur un projet à plusieurs fichiers.
3. **L'éditeur de code** de l'atelier. Décidé le 24 septembre 2026 : un éditeur maison sur `QPlainTextEdit`, dont la coloration passe par le lexeur de GrymoiR et l'autocomplétion par le calcul des suites (grammaire, § 8), les erreurs venant de l'analyseur, sans passer par le protocole LSP. Une bibliothèque existante aurait demandé de réécrire sa coloration, et QScintilla, sous GPLv3 ou licence commerciale, aurait imposé la GPL à l'atelier.
4. **Distribution** : l'atelier embarque ses bibliothèques Qt (outils de déploiement fournis par Qt sur macOS et Windows). La signature des exécutables pour macOS et Windows est reportée.
5. **Aide.** Décidé le 24 septembre 2026, fait : menu « Aide », « Le langage GrymoiR » (F1, Cmd+? sous macOS). La grammaire (`docs/grammaire.md`) est embarquée dans l'exécutable : l'aide est toujours celle de sa version, même hors ligne. Sommaire des sections à gauche, recherche en haut (Entrée : suivant, Maj+Entrée : précédent, Ctrl+F ou Cmd+F pour y aller). Plus tard : l'aide en contexte (F1 sur `Selon` ouvre sa section), et un guide pour débuter, à écrire à part, la grammaire étant une spécification plutôt qu'un manuel.
6. **Faire évoluer la grammaire.** Précisé le 24 septembre 2026 : pouvoir continuer à faire évoluer le langage seul, sans aide extérieure. Un éditeur de grammaire en données est écarté : il couvrirait la forme des phrases, jamais leur sens, qui demande du code. Décidé : le manuel du mainteneur (`docs/mainteneur.md`, charte art. 14), puis le vocabulaire en données là où c'est possible (messages, mots de construction, synonymes).

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
| 0.6 | 2026-09-24 | § 5 : A2 commencé, fichiers utilisés (grammaire, § 21), programme principal |
| 0.7 | 2026-09-24 | § 6.1 : `projet.grymatelier` ; § 6.5 : aide, la grammaire embarquée ; § 6.6 : faire évoluer la grammaire, à préciser |
| 0.8 | 2026-09-24 | § 6.6 : faire évoluer le langage seul ; manuel du mainteneur, puis vocabulaire en données |
| 0.9 | 2026-09-24 | § 5 : A2 découpé en A2-a, A2-b, A2-c ; A2-a fait (schéma des données, `projet.grymatelier`) |
| 0.10 | 2026-09-24 | § 5 : A2-b commencé : traduction des relations, panneau des propriétés, réécriture chirurgicale, annulation des gestes |
| 0.11 | 2026-09-24 | § 5 : A2-b fait : liens tirés à la souris, dialogue des sortes, cascade et valeur de départ, impression dans le contexte |
| 0.12 | 2026-09-25 | § 5 : A2-c fait (aperçu des migrations, refus avant de lancer, `grym migration`) ; A2 fait |
| 0.13 | 2026-09-25 | § 5 bis : A4, l'écran généré pour une entité |
| 0.14 | 2026-09-25 | § 5 bis : l'onglet « Écrans » |
| 0.15 | 2026-09-25 | § 5 bis : les gestes ; découpage A4-a à A4-d |
| 0.16 | 2026-09-26 | § 5 bis : A4-a fait (onglet « Écrans », aperçu partagé avec l'exécution) |
| 0.17 | 2026-09-26 | § 5 bis : A4-b fait (écran pour une entité, écran vide, écran d'accueil) |
| 0.18 | 2026-09-26 | § 5 bis : A4-c fait (propriétés modifiables, palette, suppression avec l'événement) |
| 0.19 | 2026-09-26 | § 5 bis : A4-d fait (glisser-déposer, blocs créés et retirés) ; A4 fait |

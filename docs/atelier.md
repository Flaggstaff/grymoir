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
| A5 | Les projets et leurs fichiers : nouveau projet, projets récents, renommer et mettre à la corbeille un fichier, fermer le projet (§ 5 quater) |

État de A1 (24 septembre 2026) : fait. La fenêtre, l'ouverture d'un projet (un dossier, ou le dernier ouvert), la liste de ses fichiers `.grym` et `.grymc`, l'éditeur (numéros de ligne, coloration par le lexeur, première erreur de l'analyseur soulignée en direct, sa ligne en rouge dans la marge, son message au survol et dans le panneau du bas, un clic y mène), l'enregistrement sûr (écrit à côté, puis remplace).

« Lancer » (Ctrl+R, Cmd+R sous macOS) enregistre le fichier, refuse de lancer s'il porte une erreur connue, puis exécute `grym-atelier --lancer fichier`, décidé le 24 septembre 2026 : un processus à part, pour que l'atelier survive à tout ce qui arrive au programme (charte, principe 1). Le programme a sa fenêtre : le fil de ce qu'il affiche (texte, images, fiches), les réponses acceptées recopiées dans le fil comme en console, et les champs de la question en cours, avec les menus du § 10 de `docs/v2.md` (lien, vrai ou faux), un bouton « Choisir… » pour un fichier ou une image, la case « vider », le refus sous le champ refusé. La machine tourne dans un fil à elle : la fenêtre reste vivante pendant un long calcul. « Arrêter », dans l'atelier ou dans la fenêtre du programme, lève l'interruption de la machine (docs/vm.md, § 6), qui annule l'exécution ; fermer la fenêtre en cours d'exécution fait de même. L'atelier demande l'arrêt par SIGTERM (sous Windows, par la fermeture de la fenêtre), puis, après cinq secondes sans réponse, met fin au processus : SQLite annule alors la transaction inachevée. L'erreur d'exécution, lue sur la sortie d'erreur au format de `grym lancer`, s'ajoute au panneau du bas, et un clic mène à sa ligne.

Intégration continue : l'atelier se construit et ses essais tournent sous Ubuntu, macOS et Windows (MSYS2), dans `.github/workflows/tests.yml`.

Construction : `cmake -B construction`, puis `cmake --build construction` ; essais sans fenêtre : `ctest --test-dir construction` (`tests/test_atelier.cpp`). Le cœur est compilé une seconde fois par CMake, depuis les mêmes sources que le `Makefile`.

État de A2 (24 septembre 2026) : les fichiers utilisés sont faits (grammaire, § 21). Dans l'atelier, la liste du projet écrit les programmes en gras et les fichiers de déclarations normalement, le survol dit lequel ; une erreur venue d'un fichier utilisé se signale sur la phrase `Utiliser`. « Lancer », depuis un fichier de déclarations, exécute le programme principal : le seul du projet, ou celui qu'on choisit une fois (menu « Programme », « Choisir le programme principal… »), retenu par projet. Il en va de même quand aucun fichier n'est ouvert, comme au démarrage, où l'atelier rouvre le dernier projet sans fichier (corrigé le 18 octobre 2026 : le bouton ne faisait alors rien, sans un mot) ; sans projet, la barre d'état dit d'en ouvrir un. Une erreur d'exécution dans un autre fichier l'ouvre au clic. L'éditeur de données se livre en trois tranches, décidé le 24 septembre 2026 : A2-a voir, A2-b modifier, A2-c migrer sans surprise.

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

## 5 ter. Le thème *(tranche T1 faite le 26 septembre 2026)*

Décisions : `docs/ecrans.md`, § 5. Maquettes et jetons : Claude Design, le 26 septembre 2026.

- `src/atelier/theme/grymoir-jetons.json` : les couleurs, en clair et en sombre, les cinq accents et leurs rapports de contraste, la coloration, la typographie, les espacements. `grymoir.qss.modele` : la feuille de style Qt, dont les `@jetons@` sont remplacés au démarrage.
- `src/atelier/theme.cpp` : polices embarquées, style Fusion, feuille et palette, icônes recolorées pour chaque mode, suivi du mode du système (Qt 6.5 et plus ; avant, clair). `GRYMOIR_MODE=clair` ou `sombre` impose un mode. L'atelier et les programmes qu'il lance ont le même thème ; leur accent est `bleue` jusqu'à la tranche T3.
- Polices : Atkinson Hyperlegible Next et Mono (`vendor/atkinson`, SIL Open Font License 1.1). Icônes : Lucide (`vendor/lucide`, ISC ; `check` et `chevron-down` viennent de Feather, MIT). Les licences se lisent dans l'atelier, menu Aide.
- Nouvelle dépendance : le module Qt SVG, pour rendre les icônes.
- Plus aucune couleur écrite en dur dans `src/atelier` : tout passe par `Theme::couleur`. Les dates ont leur propre couleur dans l'éditeur.
- Rôles de boutons : `principal` (Lancer, Appliquer, Envoyer), `danger` (Supprimer).
- Piège de Qt, noté dans le modèle : une règle `QHeaderView::up-arrow` ou `::down-arrow` fait réserver la place d'une flèche dans chaque titre de colonne, même sans tri, et rogne le texte. Le modèle n'en a pas ; la flèche de Fusion suit la palette.
- Limites connues : au changement de mode pendant que l'atelier est ouvert, le cadre de sélection de l'aperçu d'écran et le texte des panneaux de propriétés gardent l'ancienne couleur jusqu'au prochain choix.

Tranche T2, faite le 26 septembre 2026 : les écrans des applications, dans `vue_ecran.cpp`, donc aussi l'aperçu de l'onglet Écrans.
- Mesures des jetons, par `Theme::mesure` : marge de l'écran (24), écart des éléments et des blocs « l'un sous l'autre » (12), des blocs « côte à côte » (16), des boutons d'une rangée (8), hauteur des lignes de liste (32).
- Une zone montre son libellé au-dessus du contrôle, en étiquette ; une case à cocher porte le sien.
- Une liste n'a plus de grille : ses lignes se séparent par un trait, ses titres s'alignent à gauche.
- Le titre de l'écran est un texte simple, mis en forme par la feuille ; le message d'un événement raté s'affiche juste dessous.

Tranche T3, faite le 26 septembre 2026 : la phrase `Les écrans ont la couleur … et le logo « … ».` (grammaire, § 22.5).
- À l'exécution, la fenêtre du programme prend l'accent choisi (le processus du programme seul : l'atelier qui l'a lancé garde le sien), et le logo : 64 px sur un écran sans écran dessous, 24 px sur un écran ouvert par-dessus (jeton `logo-titre`), en icône de la fenêtre.
- Dans l'aperçu de l'onglet Écrans, chaque écran reçoit l'apparence du programme qui le déclare : son logo, à la taille qu'il aura (l'écran d'accueil est celui que ce programme ouvre lui-même au premier niveau), et la feuille de son accent, posée sur l'aperçu seul.
- Le thème est fini : T1 à T3.

## 5 quater. A5 : les projets et leurs fichiers *(fait le 10 octobre 2026)*

Jusqu'ici, un projet se créait dans le Finder, puis s'ouvrait ; un fichier renommé à la main cassait en silence les phrases `Utiliser` qui le désignaient, et la base d'un programme renommé semblait perdue.

- **Fichier > Nouveau projet…** (Maj+Cmd+N) : un nom, puis un emplacement (`Documents/GrymoiR` proposé). L'atelier crée le dossier, un programme `Nom.grym` réduit à une remarque, et `projet.grymatelier` qui le désigne comme programme principal ; puis il ouvre le programme. Jamais par-dessus un dossier existant ; un nom sans `/`, sans point en tête, sans guillemets (`src/atelier/projet.cpp`, `creer_projet`).
- **Projets du dépôt** *(18 octobre 2026)* : quand l'atelier tourne depuis le dépôt (`./atelier`), les projets de `projets/` (un dossier avec son `projet.grymatelier`), ouverts sur place. Le dépôt se cherche en remontant depuis l'exécutable, six niveaux au plus ; une application installée n'en a pas, et le menu n'y paraît pas.
- **Projets récents** : les huit derniers, du plus récent au plus ancien. La liste vit dans les réglages du poste, pas dans le projet : c'est le partage du § 6.1, ce qui décrit le projet va dans `projet.grymatelier`, ce qui décrit le poste reste sur le poste. Un projet disparu le dit au clic et quitte la liste.
- **Renommer un fichier** (menu Fichier, ou clic droit dans la liste du projet) : l'extension reste, car elle dit la forme (`grym traduire` change de forme). Chaque phrase `Utiliser` qui désigne le fichier suit, en résolvant le chemin depuis le dossier du fichier qui l'écrit (grammaire, § 21.3), forme compacte comprise. Un programme emporte sa base (`.grymd`, ses journaux `-journal`, `-wal`, `-shm`) et son bytecode (`.grymb`) : la base porte le nom du programme (grammaire, § 16.5), et un programme renommé sans elle repartirait d'une base vide. Le programme principal suit dans `projet.grymatelier`. Refus : un nom pris sous l'une ou l'autre forme (« données » désignerait les deux, § 21.1), une base déjà prise sous le nouveau nom (une base n'est jamais écrasée). Le projet est réanalysé : un fichier qui s'analysait et ne s'analyse plus fait tout défaire. C'est un geste : « Annuler le dernier geste » rend les noms, puis les contenus, octet pour octet.
- **Mettre le fichier à la corbeille** : la corbeille du système, jamais une suppression définitive. Refusé si un autre fichier l'utilise, en les nommant. La base d'un programme reste dans le dossier : les données ne se jettent pas avec le code. Le programme principal s'efface de `projet.grymatelier` s'il le désignait.
- **Fermer le projet** : la fenêtre reste vide, et l'atelier redémarre vide.
- Corrigé en passant : les gestes de l'éditeur de données se vident à l'ouverture d'un autre projet. Avant, « Annuler le dernier geste » réécrivait en silence les fichiers du projet précédent.
- Essais : `test_atelier`, création, récents, renommage (sous-dossier, forme compacte, base et journal, programme principal, refus, annulation) et corbeille. Les menus eux-mêmes n'ont pas été cliqués par un essai.

Premier projet créé ainsi : `projets/Gestion de projet`, une application de gestion de projet écrite en GrymoiR (personnes, projets, tâches et leurs états, deux règles, cinq écrans, un bilan). Elle a révélé :

- deux défauts du navigateur, corrigés (`docs/web.md`, § 3.1) : un menu changé n'agissait qu'au prochain bouton ; sélectionner une ligne revenait à la choisir ;
- une zone sans valeur de départ ni `facultatif` ne se lit pas, pas même pour demander `est absente` : « Le champ « projet » n'a pas de valeur. ». L'application déclare ses zones `facultatif`. Le message devrait le suggérer : à reprendre dans la grammaire (§ 22) ;
- l'absence d'arrondi : un avancement en pour cent afficherait 28 décimales. Le bilan compte donc les tâches terminées ;
- l'absence de dessin : pas de diagramme de Gantt sans un nouvel élément d'écran, à concevoir.

## 5 quinquies. L'éditeur de données rattrape le langage *(fait le 13 octobre 2026)*

Le langage avait gagné les valeurs permises (grammaire, § 16.15) et l'heure (§ 14.7) le 12 octobre 2026 ; l'éditeur de données ne les connaissait pas, et deux gestes perdaient des données sans le dire :

- un champ à valeurs permises se lisait comme un `(texte)` simple ; « Appliquer » le réécrivait sans sa liste ;
- un champ `(heure)` ne trouvait pas son type dans le menu, qui retombait sur `texte` ; « Appliquer » le retypait.

Corrigé, et ajouté :

- **Valeurs permises** : une colonne du panneau des champs, `à faire ; en cours ; fini`, lue dans l'arbre (le nœud du champ, `texte3`, valeurs séparées par U+001E) et écrite par l'imprimeur, en forme canonique : `un état (« à faire », « en cours » ou « fini »)`. Refusé avant d'écrire, avec le message du langage : une seule valeur, un doublon, une valeur vide ou entre guillemets, une liste sur autre chose qu'un champ `(texte)` simple, une valeur de départ hors de la liste.
- **`heure`** dans les types des champs et des zones d'écran. Un type que le menu ne connaît pas s'y ajoute : « Appliquer » ne change jamais un type en silence.
- **Contenu de la base** (onglet Données, « Contenu de la base… ») : les objets conservés de l'entité choisie, sous-entités comprises, comme `les … conservés`. Base du programme principal (§ 16.5), ouverte en lecture seule. Une colonne par champ réel de la table, aptitudes comprises ; valeurs écrites comme GrymoiR les écrit (`31.03.1685`, `−12,50`, `vrai`) ; un lien montre la clé de l'objet désigné (son premier champ texte unique, comme les menus), sinon `n° 12` ; un fichier, son nom et sa taille ; un champ « plusieurs », son nombre d'éléments. Filtre sur toutes les colonnes, tri par colonne, corbeille montrée à la demande (§ 16.12), relecture par un bouton (`src/atelier/contenu.cpp`).
- **Lecture seule, par principe.** Créer, modifier ou supprimer un objet passe par le programme : une écriture de l'atelier dans la base contournerait les règles (§ 16.14), les valeurs permises et la transaction.
- Essais : `test_atelier`, lecture, écriture et refus des valeurs permises ; panneau qui rend la liste et l'heure telles quelles ; contenu d'une base remplie par la machine (valeurs, lien, compte d'un « plusieurs », corbeille, base absente, entité inconnue).
- Trou relevé dans le langage, non corrigé ici : une heure n'est pas une valeur de départ (`un début (heure), 08:00 au départ` est refusé à l'analyse).

## 5 sexies. Sauvegarder, restaurer, importer *(fait le 13 octobre 2026)*

Trois gestes de l'onglet Données, sur la base du programme principal (grammaire, § 16.5). Aucun n'ajoute de phrase au langage.

- **Sauvegarder la base** : une copie datée, `Sauvegardes/programme 2026-10-13 14h30.grymd`, dans le dossier du projet, jamais par-dessus une autre (` (2)`, ` (3)`). La copie passe par l'API de sauvegarde de SQLite : elle est cohérente même si la base est ouverte. Refusée pendant que le programme tourne. Les sauvegardes restent hors de Git (`*.grymd`) et hors des applications fabriquées (§ 8).
- **Restaurer une sauvegarde…** : une base choisie remplace celle du programme, après confirmation. La base actuelle est d'abord sauvegardée : une restauration se défait en restaurant celle-ci. Refusé : un fichier qui n'est pas une base GrymoiR (sans table `grym_schema`), la base en service elle-même, un programme qui tourne. Après coup, l'atelier dit ce que le prochain lancement fera (§ A2-c) : une sauvegarde peut dater d'un autre schéma, et la migration se dit avant de lancer, pas au milieu.
- **Importer un tableau CSV…** : l'atelier n'écrit que du GrymoiR (§ 2), donc il n'importe rien lui-même. Il écrit la boucle du § 15.5 dans le programme principal, gardée pour ne s'exécuter que tant que la base n'a aucun objet de l'entité :

```
Remarque : importé de « o.csv » par l'atelier, tant que la base n'a aucune œuvre.
Si le nombre d'œuvres conservées = 0 :
    Pour chaque rangée du tableau « o.csv » :
        L'importé vaut une nouvelle œuvre :
            Le titre vaut la colonne « Titre » de la rangée.
            Le genre vaut la colonne « Genre » de la rangée.
        Si la colonne « Compositeur » de la rangée est présente :
            Le compositeur de l'importé devient le compositeur conservé dont le nom est la colonne « Compositeur » de la rangée.
        Si la colonne « Année » de la rangée est présente :
            L'année de l'importé devient la colonne « Année » de la rangée en année.
        Conserver l'importé.
```

  - Le dialogue lit la ligne des titres (séparateur deviné comme GrymoiR le devine, marque d'ordre, guillemets), propose pour chaque champ la colonne de même nom, accents et casse ignorés, et montre le code avant de l'écrire.
  - Champs proposés : ceux de la lignée, sans « plusieurs », fichiers ni images ; un lien seulement si l'entité désignée a une clé (son premier champ texte unique), retrouvée par `dont`. Un champ obligatoire sans colonne bloque l'écriture ; un champ facultatif s'écrit après le bloc, sous `est présente`, pour qu'une cellule vide le laisse absent (§ 16.9).
  - Le code s'écrit avant la première phrase de premier niveau qui montre quelque chose (`Ouvrir`, `Afficher`, une fiche, `Effacer l'écran`, `Exporter`), sinon à la fin : les données de départ écrites plus haut existent déjà, et ce qui est montré voit les objets importés. C'est un geste ordinaire : il s'annule, et le code se déplace ou se modifie à la main.
  - Un tableau hors du dossier du programme y est copié (jamais par-dessus un autre fichier) ; la copie repart si le geste est refusé.
  - Tout ou rien : une ligne refusée (cellule illisible, doublon d'une clé, valeur non permise, règle) annule toute l'importation au lancement, avec le message du langage.
- Deux défauts du langage trouvés en chemin, corrigés (grammaire 1.73) : `t en vrai ou faux` n'était pas reconnu après un nom (« « t en » inconnu ») ; l'imprimeur écrivait `la colonne … est présent`, au masculin.
- Essais : `test_atelier`, titres d'un CSV, code produit, place dans le programme, deux lancements sans doublon, sauvegarde sans écrasement, restauration et retour en arrière, refus ; `test_analyseur` et `test_imprimeur` pour les deux défauts.

## 5 septies. Les états dans l'éditeur d'écrans *(fait le 14 octobre 2026)*

Le panneau des propriétés suit la grammaire, § 22.6 : pour une zone, « sur plusieurs lignes », « en lecture seule », « Active si », « Visible si » ; pour un bouton, « Actif si », « Visible si » ; pour un texte fixe, « Visible si ». Une condition s'écrit en GrymoiR, telle qu'elle ira dans le fichier ; vide, l'élément est toujours actif ou visible. L'arbre porte une marque que l'impression remplace par ce texte, puis la réanalyse du projet le vérifie : une condition fausse annule le geste, avec le message du langage. L'aperçu montre la zone sur plusieurs lignes et la lecture seule, mais tous les éléments, quel que soit leur état, puisque l'état dépend de l'exécution.

Avec B2 (grammaire, § 22.7) : le panneau d'une zone a une ligne « Valeurs permises » (`actif ; suspendu ; retiré`), vérifiée comme dans l'éditeur de données ; l'aperçu montre les boutons radio ou le menu que l'utilisateur verra. « Mettre dans un groupe… » entoure l'élément choisi d'un groupe titré ; le titre d'un groupe se change dans son panneau ; un groupe d'un seul élément reste (un bloc `côte à côte` d'un seul élément disparaît), un groupe vide part.

Avec B2 bis (grammaire, § 22.8) : « Image » dans la palette d'ajout choisit un fichier du projet, écrit relatif à son dossier (une image hors du projet est refusée : elle ne partirait pas avec l'application) ; le panneau d'une image donne son fichier et « Visible si » ; l'aperçu la montre, ou son nom si le fichier manque.

Corrigé en chemin : le panneau d'une zone ne lisait pas son genre ; « Appliquer » réécrivait au masculin une zone féminine (`une remarque` devenait `un remarque`).

## 5 octies. La position libre dans l'éditeur d'écrans *(fait le 14 octobre 2026)*

Le langage place au pixel dans un bloc `en position libre :` (grammaire, § 22.9) ; l'éditeur d'écrans le rend direct.

- **À la création.** « Nouvel écran vide » demande la disposition : libre (le bouton « Fermer » déjà dans un bloc en position libre), proposée d'abord, ou décrite (les éléments s'empilent, l'écran s'adapte). « Mettre en position libre », dans le panneau de l'écran, convertit un écran existant : chaque unité du premier niveau garde la place et la largeur qu'elle a dans l'aperçu (une liste et un bloc gardent aussi leur hauteur), ramenées sur la grille ; un bloc reste un bloc, avec sa règle à l'intérieur.
- **Glisser.** Dans un bloc libre, l'élément suit la souris, sans le glisser-déposer des autres blocs. La grille d'alignement apparaît sur le bloc le temps du geste : un trait tous les 8 pixels, appuyé tous les 32, à la couleur d'accent. L'élément s'y aimante ; Alt (Option sous macOS) le place au pixel près. La barre d'état donne la place en cours. Rien n'est écrit avant de lâcher ; le lâcher est un geste ordinaire, qui s'annule.
- **Agrandir.** Saisi par son coin en bas à droite (10 pixels), l'élément change de taille, aimanté de même ; le lâcher écrit `large de` et `haut de`.
- **Le panneau** d'un élément placé donne Gauche, Haut, Largeur et Hauteur (0 : la taille naturelle) et « Placer ».
- **L'ordre.** Après chaque placement, l'atelier réécrit les éléments du bloc de haut en bas, puis de gauche à droite : l'ordre de lecture (Tab, lecteurs d'écran) suit ce qu'on voit.
- **Ajouter.** Dans un écran qui a un bloc libre, la palette ajoute au dernier, à 16 de la gauche et 48 pixels sous l'élément le plus bas (la hauteur réelle des éléments n'est connue qu'au dessin).
- **Tout se place** *(atelier 0.40)* : un groupe, un bloc `côte à côte` ou `l'un sous l'autre`, un bloc libre posé dans un bloc libre se glissent et s'agrandissent comme un élément, avec tout leur contenu. « Mettre dans un groupe… » marche aussi dans un bloc libre : le groupe prend la place et la taille de l'élément, qui s'y empile. Retirer un groupe placé remonte ses éléments à sa place, l'un sous l'autre (48 pixels d'écart) ; retirer un bloc libre placé garde les places de ses éléments, décalées de la sienne.
- **Écran mixte** *(0.50)* : un écran qui a déjà un bloc libre et des éléments restés décrits en dehors. Glisser (ou flécher) un de ces éléments le fait entrer dans le bloc libre, là où on le lâche (le bloc libre sous la souris s'il y en a plusieurs) ; la conversion de tout l'écran, refusée sur un tel écran, ne se tente plus, et l'élément ne revient plus à sa place.
- **La grille est transparente** *(0.50)* : le thème peint le fond de tout widget, et la grille comme le cadre pointillé devenaient des plaques qui cachaient l'écran. Ils n'ont plus de fond, la grille passe sous les éléments, et ses traits sont plus discrets.
- **D'un bloc libre à un autre** *(0.54)* : un élément placé, lâché dans un autre bloc libre visible (celui de l'écran, celui d'un groupe, la page d'un onglet), y passe, à l'endroit du lâcher ; lâché ailleurs dans l'aperçu, hors de tout bloc, il va dans le bloc libre de l'écran. Un bloc libre posé dans un conteneur (groupe, page, bloc rangé) remplit son conteneur. Seule compte la surface visible d'un bloc : une page d'onglet cachée, ou la part d'un bloc qui déborde de sa page pendant un geste, n'attrape rien.
- **Les guides** *(0.43)* : pendant un glisser ou un agrandissement, un bord ou le centre de l'élément qui passe à moins de 5 pixels d'un bord ou du centre d'un voisin s'y colle, plus fort que la grille, et un trait à la couleur d'accent le montre sur toute la hauteur ou la largeur du bloc. Les voisins sont les éléments du même bloc libre (sur un écran décrit, tous ceux de l'aperçu). Alt coupe la grille et les guides.
- **Le clavier.** L'élément placé choisi se déplace aux flèches, d'un pixel, ou de 8 avec Maj ; Alt + flèches changent sa taille. Chaque appui est un geste, qui s'annule.
- **La poignée.** Un carré à la couleur d'accent marque le coin de l'élément placé choisi : le saisir l'agrandit. La prise fait 12 pixels : le bord droit change la largeur, le bord bas la hauteur, le coin les deux ; au survol, le curseur l'annonce (↔, ↕, ⤡) *(0.41)*.
- **Plus de mode** *(0.42)* : tout élément de tout écran se glisse, s'agrandit par ses bords et se déplace aux flèches. Sur un écran décrit, un fantôme pointillé suit la souris sur la grille ; au lâcher, l'atelier passe l'écran en position libre en gardant ce qu'on voit, l'élément à sa nouvelle place, en un seul geste qui s'annule. Les blocs existants restent des blocs, placés d'un seul tenant. Un élément dans un bloc (groupe, onglet…) fait suivre son geste au bloc du premier niveau qui le contient : seules les unités du premier niveau ont une place *(0.45)*. Remettre dans l'ordre par glisser reste possible avec Cmd (Ctrl). « Nouvel écran vide » propose « Libre » d'abord.
- **Le tri** se fait par unités : un bloc et tout son contenu bougent ensemble dans l'ordre écrit.
- **Refus.** Un élément en position libre ne se glisse pas vers un autre bloc ; retirer le bloc libre de premier niveau garde ses éléments et efface leur place. Le contour pointillé montre le bloc dans l'aperçu.
- La grille est un outil de l'atelier : elle n'existe pas dans le programme, qui ne garde que les nombres.
- Pas encore : glisser un élément d'un autre bloc vers le bloc libre, aide à la saisie (grammaire, § 8) pour les nouvelles tournures.

## 5 nonies. L'image calculée dans l'éditeur d'écrans *(fait le 14 octobre 2026)*

Avec B2 ter (grammaire, § 22.8) : « Image » dans la palette demande ce que l'image montre, un fichier du projet ou une image calculée ; pour la seconde, on écrit la valeur sans « de » (`le portrait du compositeur choisi de l'écran`), et l'atelier contracte (`l'image du portrait …`, `de la photo …`, `d'une …`). Le panneau d'une image calculée donne « Image de » et « Visible si » ; la réanalyse du projet vérifie la valeur, comme une condition. L'aperçu ne connaît pas les données : il montre la source (« Image : le portrait du compositeur choisi de l'écran ») à la place de l'image. À l'exécution, l'image change à chaque tour de l'écran ; absente, la place reste vide.

## 5 decies. Les onglets dans l'éditeur d'écrans *(fait le 14 octobre 2026)*

« Onglets » dans la palette ajoute `les onglets :` avec deux onglets, « Premier » et « Second », un texte dans chacun ; dans un écran libre, ils se placent sous ce qui y est, comme tout ajout. L'aperçu les dessine en vrais onglets : un clic sur un onglet le montre (c'est le seul clic que l'aperçu laisse passer) ; un clic dans sa page choisit l'élément. Le panneau d'un onglet donne son titre. Les onglets se placent, s'agrandissent et se déplacent comme tout bloc (§ 5 octies).

**Comme dans un navigateur** *(0.46)* : un clic sur un onglet de l'aperçu ouvre sa page et choisit l'onglet ; la page ouverte le reste quand l'aperçu se redessine après un geste. « Ajouter » pose dans le bloc de ce qui est choisi : la page ouverte quand on a cliqué un onglet, le groupe d'un élément choisi dans un groupe, etc. ; sans choix, à la fin de l'écran comme avant. Chaque onglet créé par la palette contient un bloc libre : ce qu'on y pose prend une place sous l'élément le plus bas, puis se glisse et s'agrandit au pixel. Un clic dans le vide d'une page la choisit sans la déplacer.

**Prise, place, rangement, suppression** *(0.47)* : les onglets se saisissent par leur barre ou par le vide d'une page, comme une fenêtre par son titre, pour les déplacer, et s'agrandissent par leurs bords ; un simple clic sur un onglet change toujours de page. Créés dans un bloc libre, ils naissent à 480 × 320, et le bloc libre de chaque page remplit la page. En glissant un élément, survoler un onglet ouvre sa page ; lâcher sur l'en-tête de l'onglet y range l'élément, sous ce qui y est. Le panneau d'un élément d'un onglet propose « Mettre dans l'onglet ». Lâché **dans la page ouverte** d'un onglet, un élément de l'écran (ou d'un autre bloc) y passe, à l'endroit du lâcher, et n'est plus que sur cet onglet ; la barre d'état l'annonce pendant le geste *(0.48)*. Le seul élément d'un onglet ne le quitte pas (l'onglet resterait vide, ce que la grammaire refuse) : l'atelier le dit. Le dernier élément d'un bloc de rangement (côte à côte, l'un sous l'autre, groupe, bloc libre placé), lui, en sort, et le bloc vidé disparaît *(0.51)*. Supprimer un onglet emporte son contenu ; le dernier emporte les onglets ; le panneau d'un onglet propose aussi « Supprimer les onglets ».

## 5 undecies. Le graphique dans l'éditeur d'écrans *(fait le 15 octobre 2026)*

« Graphique » dans la palette demande l'entité, puis le champ qui regroupe ; il compte, en barres. L'aperçu, sans données, dessine un exemple grisé de trois barres ; à l'exécution, le graphique se remplit à chaque tour avec les groupes que la machine envoie. Le panneau d'un graphique donne « Regroupé par » (les champs de l'entité ; un chemin écrit dans le code, comme « catégorie du membre », y figure tel quel), « Période » pour un champ date (chaque date, par mois, par semaine, par année, grammaire § 22.11 ; grisée pour un autre champ) *(0.81)*, « Valeur » (le nombre d'objets, ou la somme d'un champ nombre) et « Forme » (barres ou secteurs) *(0.53)* ; « Appliquer » garde le `dont` et la place. Les couleurs des secteurs viennent d'une suite de huit teintes, la même que dans le navigateur.

## 5 duodecies. Les menus dans l'éditeur d'écrans *(fait le 15 octobre 2026)*

« Menu » dans la palette ajoute un menu en tête de l'écran, après les menus déjà là, avec un élément « Nouvel élément » et son événement à écrire. L'aperçu montre la barre de menus en tête de l'écran, dans la vue et non dans la barre système (l'écran vit dans la fenêtre de l'atelier, dont la barre système est celle de l'atelier). Un clic sur le titre d'un menu le choisit et l'ouvre ; un clic sur un élément du menu ouvert le choisit ; « Ajouter » pose un bouton dans le menu choisi, avec son événement. Le panneau d'un menu donne son titre. À l'exécution, chaque élément est une action reliée à un bouton invisible, qui porte son événement et ses états (actif si, visible si) ; l'action les reflète. Un menu ne se place ni ne s'agrandit ; la conversion en position libre le laisse en tête, hors du bloc libre.

Deux défauts corrigés en chemin. Un bouton posé par « Ajouter » dans un bloc choisi (une page d'onglet) arrivait sans son événement, et la réanalyse refusait le geste. Le graphique, numéroté après les blocs, passait pour un bloc ouvert dans les parcours de l'aperçu : sur un écran décrit, glisser un élément écrit après un graphique déplaçait le graphique ; une fonction `est_bloc` dit maintenant ce qu'est un bloc.

## 5 terdecies. Le clic droit : les champs disponibles *(fait le 16 octobre 2026)*

Un clic droit sur l'aperçu d'un écran ouvre un menu des entités du projet, et pour chacune ses champs, écrits comme on les déclare (« un titre (texte) », « des genres (genre) »). Choisir un champ ajoute la zone qui lui correspond : même nom, même type, même genre, mêmes valeurs permises ; un lien vers une entité devient le choix d'un objet, un champ multiple des choix multiples. Dans un écran en position libre, la zone se pose là où l'on a cliqué ; ailleurs, sous ce qui est déjà là. Sur un élément, le menu propose d'abord de le supprimer, comme le panneau : un événement qui partirait avec lui est montré avant, et la suppression attend un oui. Le même menu propose d'abord les éléments de la palette (bouton, texte, zone, interrupteur, choix multiples, fichier, liste, graphique, image, onglets, menu), posés de même là où l'on a cliqué *(0.60)*. La palette gagne « Choix multiples » (l'entité, puis le nom au pluriel) et « Fichier » (un fichier ou une image) ; palette et clic droit tirent leur liste d'une seule source.

## 5 quaterdecies. Écouter et enregistrer les sons *(fait le 18 octobre 2026)*

La fenêtre d'exécution, donc aussi une application fabriquée, écoute et enregistre les sons par Qt Multimedia (`src/atelier/son.cpp`), grammaire § 15.7. `LecteurSon` joue un son gardé en mémoire, sans fichier provisoire ; l'extension du nom, ou à défaut celle de la signature, guide le décodeur. `EnregistreurSon` enregistre le micro par défaut dans un fichier provisoire, rendu en octets puis effacé ; le format est le premier que `QMediaFormat` sait écrire ici parmi M4A (AAC), Ogg (Opus, Vorbis), FLAC et WAV, tous reconnus par la machine. Une zone `(son)` d'écran ou de formulaire reçoit « ▶ Écouter » et « ● Enregistrer » à côté du choix de fichier ; un enregistrement fait le changement de la zone. La machine décrit un fichier reçu par sa taille (« un fichier de 44 octets ») : la fenêtre retient la première valeur qui suit son envoi, et oublie le son quand la valeur change autrement. Dans le fil, un son affiché devient un lien « ▶ Écouter » ; dans une fiche déduite, un lecteur, et une image s'y affiche.

Sous macOS, l'autorisation du micro se demande à l'ouverture d'un programme qui a un champ ou une zone `(son)` (19 octobre 2026) ; si elle a déjà été refusée, une boîte dit où la rendre et ouvre la page Microphone des réglages. Elle passe par `QMicrophonePermission` (Qt 6.5 et suivants ; Qt 6.4 n'en a pas besoin), et le paquet déclare `NSMicrophoneUsageDescription` : `outils/Info.plist.in`, le modèle de CMake plus cette clé. Une application fabriquée l'hérite de l'atelier qui lui sert de lanceur. Rien n'est vérifié avec un vrai micro : le conteneur où se font les essais n'en a pas, et les essais appellent directement ce que rend la fin d'un enregistrement.

## 6. Questions ouvertes

1. **Positions des boîtes du schéma.** Décidé le 24 septembre 2026 : un fichier texte `projet.grymatelier` à la racine du projet, lisible et versionné, que seul l'atelier lit (`grym` l'ignore). Une ligne par entité, triée par nom (`compositeur : 40, 120`), et le programme principal (`programme principal : partotheque.grym`), qui quitte les réglages de la machine : c'est une information du projet, pas du poste. S'il manque, ou s'il ignore une entité (nouvelle, ou renommée à la main), l'atelier la place lui-même et complète le fichier. Le perdre ne perd jamais rien du programme. Pas de syntaxe GrymoiR : ce n'est pas du programme.
2. **Un projet, un fichier ?** Aujourd'hui, un programme tient dans un fichier. Une vraie application voudra plusieurs fichiers (données, écrans, traitements). Décidé le 24 septembre 2026 : l'inclusion se conçoit en A2, dans la grammaire, avant l'éditeur de données, qui travaille d'emblée sur un projet à plusieurs fichiers.
3. **L'éditeur de code** de l'atelier. Décidé le 24 septembre 2026 : un éditeur maison sur `QPlainTextEdit`, dont la coloration passe par le lexeur de GrymoiR et l'autocomplétion par le calcul des suites (grammaire, § 8), les erreurs venant de l'analyseur, sans passer par le protocole LSP. Une bibliothèque existante aurait demandé de réécrire sa coloration, et QScintilla, sous GPLv3 ou licence commerciale, aurait imposé la GPL à l'atelier.

   Autocomplétion faite le 26 septembre 2026 (elle manquait depuis A1) :
   - La liste s'ouvre à la frappe dès deux lettres d'un mot, et à tout moment par Ctrl+Espace (sous macOS, la touche Contrôle, et non Commande : Cmd+Espace appartient à Spotlight). Entrée ou Tab écrit la suite choisie, Échap ferme la liste.
   - Les suites viennent de `suites_valides_fichier`, sur le texte jusqu'au curseur, comme pour `grym lsp` : noms déclarés, tournures accordées au genre, mots de construction. Les catégories (« (nombre) ») et les gabarits (« « … » ») ne sont pas proposés.
   - La suite remplace le début de mot : lettres, chiffres, `_`, ou un nom entre crochets encore ouvert (`[fr` → `[frais et port]`). Après une élision, seul le mot qui suit est remplacé (`L'a` → `L'addition`).
   - Limite, héritée du cœur : rien en forme compacte (charte, art. 12, reports de l'aide à la saisie).
   - La frappe se lit sur les changements du document, pas sur les touches : sous macOS, un caractère peut arriver par la méthode de saisie du système, que Qt signale comme le remplacement d'un passage entier avec un caractère de plus. Une recoloration (autant retiré qu'ajouté) n'est pas une frappe.

   Guillemets à la frappe, faits le 26 septembre 2026 : les claviers suisses n'ont pas `«` ni `»`.
   - Un `"` tapé devient `«  »`, curseur au milieu ; tapé devant le `»` qui ferme le texte, il le franchit. Le fichier reste ainsi en forme canonique (grammaire, § 12).
   - Il reste `"` dans un texte déjà ouvert par `«`, ou quand il ferme un texte ouvert par `"` (grammaire, § 1.5 : les deux s'acceptent).
   - Ctrl+Z rend le `"` tapé. Un `"` collé avec d'autres caractères n'est jamais touché.
4. **Distribution** : l'atelier embarque ses bibliothèques Qt (outils de déploiement fournis par Qt sur macOS et Windows). La signature des exécutables pour macOS et Windows est reportée.
5. **Aide.** Décidé le 24 septembre 2026, fait : menu « Aide », « Le langage GrymoiR » (F1, Cmd+? sous macOS). La grammaire (`docs/grammaire.md`) est embarquée dans l'exécutable : l'aide est toujours celle de sa version, même hors ligne. Sommaire des sections à gauche, recherche en haut (Entrée : suivant, Maj+Entrée : précédent, Ctrl+F ou Cmd+F pour y aller). Aide en contexte, faite le 26 septembre 2026 : dans le code, F1 sur un mot du langage ouvre sa section (`Selon` ou `_selon` → § 10.5) ; les mots voisins départagent (`Pour chaque` → § 10.3, `Pour relancer` → § 9.2, `Tant que`, `Ouvrir l'écran`, `Les écrans ont`, `Les nombres s'affichent`) ; sur un nom du programme, ou hors de l'éditeur, l'aide s'ouvre là où elle était. La table des mots (`src/atelier/aide.cpp`) suit les numéros de la grammaire ; un essai vérifie que chaque section qu'elle désigne existe encore. Guide pour débuter, fini le 28 septembre 2026 : menu Aide > Premiers pas (`docs/guide.md`, embarqué comme la grammaire). Un tutoriel en douze chapitres, au tutoiement, qui construit l'application d'une association (membres, cotisations, comptabilité en partie double) ; chaque chapitre a son programme complet dans `docs/guide/`, et `test_guide` vérifie que chaque extrait et chaque sortie du texte correspondent à ce programme. Pour qu'un débutant puisse commencer dans l'atelier seul : Fichier > Nouveau fichier… (Cmd+N), un fichier vide dans le projet, `.grym` ajouté, jamais d'écrasement.
6. **Faire évoluer la grammaire.** Précisé le 24 septembre 2026 : pouvoir continuer à faire évoluer le langage seul, sans aide extérieure. Un éditeur de grammaire en données est écarté : il couvrirait la forme des phrases, jamais leur sens, qui demande du code. Décidé : le manuel du mainteneur (`docs/mainteneur.md`, charte art. 14), puis le vocabulaire en données là où c'est possible (messages, mots de construction, synonymes).

## 7. Ce que la charte devra dire

- Art. 3 : deux exécutables, `grym` (sans dépendance) et `grym-atelier` (Qt 6).
- Art. 11 : l'interface graphique native entre dans le périmètre ; l'éditeur visuel d'interfaces quitte l'horizon pour les jalons A3 et A4.
- Art. 12 : les jalons A1 à A4.
- `docs/v2.md` : `grym servir` reste un mode d'affichage parmi d'autres ; les « écrans nommés » et le modèle par événements prévus en v2.x passent en A3, pour Qt d'abord.

---

## 8. Fabriquer une application *(voie A, faite le 28 septembre 2026)*

Programme > Fabriquer l'application… fait du projet une application autonome, pour le système où tourne l'atelier : `Nom.dmg` sous macOS, `Nom-linux.tar.gz` sous Linux. Le nom est celui du dossier du projet. La même chose sans fenêtre, pour les scripts et l'intégration continue : `grym-atelier --fabriquer projet destination` (programme principal lu dans `projet.grymatelier`).

- Une application, c'est le lanceur (l'atelier lui-même) et un dossier `Programme` : les fichiers du projet, sans ses données (`.grymd`), son bytecode (`.grymb`), `projet.grymatelier` ni fichiers cachés, et `application.txt` (nom, programme principal). Au démarrage, l'exécutable qui trouve ce dossier (`Contents/Resources/Programme` sous macOS, `Programme` à côté de lui ailleurs) ouvre le programme directement, sans éditeur, sous le nom de l'application.
- Sa base vit dans le dossier de données que le système donne à l'application (`~/Library/Application Support/Nom` sous macOS, `~/.local/share/Nom` sous Linux) : l'application elle-même n'est pas modifiable, et les données survivent à une réinstallation. Les données de départ du programme s'y créent au premier lancement.
- macOS : copie de GrymoiR.app (`ditto`), sans ses exemples, avec le programme ; nom et identifiant réécrits dans `Info.plist` (`plutil`) ; icône : la première image PNG du projet, par ordre alphabétique, sinon celle de GrymoiR (`outils/icone-application.png`), écrite en `.icns` par l'atelier lui-même, dans la grille de macOS (824 pixels sur 1024) ; signature ad hoc (`codesign`) ; disque (`hdiutil`). Depuis un atelier qui n'a pas été installé par `GrymoiR.dmg`, l'application dépend du Qt du Mac, et l'atelier le dit.
- Linux : le lanceur, un script au nom de l'application, le programme ; l'archive suppose Qt 6 sur la machine qui la reçoit.
- Jamais d'écrasement : un paquet du même nom arrête la fabrication.
- Limites : pas de signature Apple ni de notarisation (avertissement de macOS sur un autre Mac) ; pas d'icône propre sous Windows ; `Enregistrer … dans` écrit à côté du programme, dans l'application, qui n'est pas modifiable : à éviter dans une application fabriquée.

**E1, l'installation chez le client (faite le 16 octobre 2026).**

- *L'identité* : « Fabriquer l'application… » demande d'abord le nom que verra le client, la version (chiffres et points : `1.0`, `2.3.1`, ce que le Mac exige) et l'éditeur (facultatif). Préremplis, ils se gardent dans `projet.grymatelier` (`application, nom`, `application, version`, `application, éditeur` : des clés à virgule, qu'aucun nom d'entité ne peut prendre) et servent aussi à la fabrication par GitHub. L'identifiant vient du nom, sans accents (« Cotisations Élodie » : `cotisations-elodie`).
- *Mac* : le `.dmg` porte le nom affiché, la version (`CFBundleShortVersionString`, `CFBundleVersion`) et l'éditeur (`NSHumanReadableCopyright`).
- *Windows* : en plus du `.zip`, `Nom-installation.exe` (Inno Setup, `outils/windows/application.iss`) : sans droits d'administrateur, dans `%LOCALAPPDATA%\Programs\Nom`, avec son raccourci du menu Démarrer (et du bureau sur demande), son icône tirée du logo du projet (`--icone-ico`, un `.ico` qui embarque un PNG de 256 pixels), sa désinstallation. Une nouvelle version remplace l'ancienne (même identifiant), et la désinstallation ne touche jamais aux données du client.
- Commandes sans fenêtre pour les scripts : `--identite projet`, `--icone-ico projet fichier.ico`.
- Non vérifié ici : l'installateur Windows et le `.dmg`, que seuls GitHub et un Mac peuvent fabriquer. La signature viendra avec E2 (comptes Apple et Windows, à décider).

**Voie C, les trois systèmes par GitHub (commencée le 28 septembre 2026).** Les projets vivent pour l'instant dans le dépôt de GrymoiR, dossier `projets/` (un dépôt par projet viendra plus tard). Le flux `.github/workflows/applications.yml` lance `outils/fabriquer-ci` sur macOS, Linux et Windows, pour chaque projet, et publie les paquets (Actions > applications > Artifacts) :

- macOS : `./paquet-mac`, puis `--fabriquer` depuis l'atelier ainsi installé : `Nom.dmg` ;
- Linux : `--preparer`, puis linuxdeploy et son greffon Qt : `Nom-x86_64.AppImage`, un seul fichier qui embarque Qt ; construit sur Ubuntu 24.04, il tourne sur les distributions de 2024 et après ; icône : la première image PNG du projet, sinon celle de GrymoiR (`outils/icone-application.png`) ;
- Windows : `--preparer`, windeployqt, puis les bibliothèques de MSYS2 suivies jusqu'à la dernière : `Nom-windows.zip`, un dossier avec `Nom.exe` ; sans signature, Windows affiche son avertissement au premier lancement.
- L'atelier lui-même a son installateur Windows depuis le 10 octobre 2026 : `./paquet-windows`, `GrymoiR-installation.exe` (`docs/mainteneur.md`, § 2). Ses exécutables portent désormais l'icône de GrymoiR et un manifeste UTF-8, qui vaut aussi pour les applications fabriquées : un chemin accentué s'y ouvre. « Programme > Fabriquer l'application… » ne fabrique pas encore depuis Windows ; pour Windows, la voie reste GitHub.
- `--fabriquer` et `--preparer` tournent sans écran (`QCoreApplication`).

Vérifié ici : l'AppImage des Amis du Tilleul, fabriquée sans écran, lancée dans un dossier personnel vierge, crée sa base avec les données du guide. La partie macOS de la voie C et toute la partie Windows n'ont tourné que sur GitHub, pas ici.

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
| 0.20 | 2026-09-26 | § 5 ter : le thème, tranche T1 faite (jetons, feuille, polices, icônes, mode clair ou sombre) |
| 0.21 | 2026-09-26 | § 6.3 : autocomplétion de l'éditeur faite (suites du cœur, à la frappe et par Ctrl+Espace) |
| 0.22 | 2026-09-26 | § 6.3 : guillemets à la frappe ; frappe lue sur le document (méthode de saisie de macOS) |
| 0.23 | 2026-09-26 | § 5 ter : tranche T2 faite (mesures des jetons dans le dessin des écrans) |
| 0.24 | 2026-09-26 | § 5 ter : tranche T3 faite (apparence des écrans, à l'exécution et dans l'aperçu) ; le thème est fini |
| 0.25 | 2026-09-26 | § 6.5 : aide en contexte faite (F1 sur un mot du langage ouvre sa section) |
| 0.26 | 2026-09-27 | § 6.5 : guide « Premiers pas » commencé (chapitres 1 à 3), vérifié par `test_guide` ; Fichier > Nouveau fichier… |
| 0.27 | 2026-09-28 | § 6.5 : guide « Premiers pas » fini (douze chapitres) ; les programmes à écrans du guide (chapitres 11 et 12) sont vérifiés par les essais de l'atelier |
| 0.28 | 2026-09-28 | § 8 : fabriquer une application (voie A : macOS, Linux ; `--fabriquer`) |
| 0.29 | 2026-09-29 | Icône de l'atelier (un grimoire ouvert : forme littéraire à gauche, compacte à droite ; source `outils/icone/grymoir-atelier.svg`) ; § 8 : une application fabriquée pour macOS prend l'icône de son projet |
| 0.30 | 2026-10-10 | A5 fait (§ 5 quater) : nouveau projet, projets récents, renommer (Utiliser, base, programme principal suivent), corbeille, fermer le projet ; gestes vidés au changement de projet ; ce que l'application « Gestion de projet » a révélé |
| 0.31 | 2026-10-10 | § 8 : l'installateur Windows de l'atelier ; icône et manifeste UTF-8 des exécutables Windows |
| 0.32 | 2026-10-13 | § 5 quinquies : valeurs permises et heure dans l'éditeur de données (deux pertes silencieuses corrigées) ; contenu de la base en lecture seule |
| 0.33 | 2026-10-13 | § 5 quinquies : le contenu de la base lit la taille d'un fichier dans `grym_fichier` (format 2 des bases, docs/vm.md, § 8) |
| 0.34 | 2026-10-14 | § 5 sexies : sauvegarder et restaurer la base, importer un tableau CSV (la boucle du § 15.5, écrite dans le programme) |
| 0.35 | 2026-10-14 | § 5 septies : états des éléments dans le panneau des écrans ; genre d'une zone gardé par « Appliquer » |
| 0.36 | 2026-10-14 | § 5 septies : valeurs permises des zones, boutons radio dans l'aperçu, « Mettre dans un groupe… », titre d'un groupe |
| 0.37 | 2026-10-14 | § 5 septies : l'image dans la palette, le panneau et l'aperçu |
| 0.38 | 2026-10-14 | § 5 octies : position libre (grammaire, § 22.9) : disposition choisie à la création, « Mettre en position libre », glisser avec grille de 8 pixels et aimantation (Alt : au pixel), coin pour la taille, panneau Gauche, Haut, Largeur, Hauteur |
| 0.39 | 2026-10-14 | § 5 nonies : l'image calculée dans la palette, le panneau, l'aperçu et l'exécution |
| 0.40 | 2026-10-14 | § 5 octies : les blocs se placent et s'agrandissent dans un bloc libre ; grouper dans un bloc libre ; flèches (Maj : 8 pixels ; Alt : taille) ; poignée visible ; tri par unités |
| 0.41 | 2026-10-14 | § 5 octies : prise de 12 pixels, bord droit (largeur), bord bas (hauteur), coin (les deux) ; curseur au survol ; message sur le coin d'un élément décrit |
| 0.42 | 2026-10-14 | § 5 octies : plus de mode ; glisser, agrandir ou flécher un élément d'un écran décrit le passe en position libre en gardant ce qu'on voit ; la conversion garde les blocs ; Cmd pour remettre dans l'ordre ; « Libre » proposé d'abord |
| 0.43 | 2026-10-14 | § 5 octies : guides d'alignement sur les bords et centres des voisins, plus forts que la grille ; Alt les coupe |
| 0.44 | 2026-10-14 | § 5 decies : les onglets dans la palette, l'aperçu (cliquables), le panneau (titre), placés comme un bloc |
| 0.45 | 2026-10-14 | § 5 octies : sur un écran décrit, le geste sur un élément dans un bloc déplace le bloc du premier niveau (il ne faisait rien de visible) |
| 0.46 | 2026-10-14 | § 5 decies : clic sur un onglet de l'aperçu, page ouverte gardée ; « Ajouter » dans le bloc choisi (la page ouverte) ; un bloc libre dans chaque onglet créé |
| 0.47 | 2026-10-15 | § 5 decies : onglets déplacés par leur barre, agrandis par leurs bords ; 480 × 320 à la création, page remplie par son bloc libre ; glisser d'un onglet à l'autre ; « Mettre dans l'onglet » ; supprimer un onglet ou tous |
| 0.48 | 2026-10-15 | § 5 decies : un élément lâché dans la page d'un onglet y passe, à l'endroit du lâcher ; l'atelier s'ouvre en fenêtre agrandie à l'écran disponible |
| 0.49 | 2026-10-15 | § 5 undecies : le graphique dans la palette, l'aperçu (exemple grisé) et l'exécution |
| 0.50 | 2026-10-15 | § 5 octies : écran mixte, l'élément décrit entre dans le bloc libre au lieu de revenir ; grille et cadre transparents, grille sous les éléments |
| 0.51 | 2026-10-15 | § 5 decies : le dernier élément d'un bloc de rangement en sort, le bloc vidé disparaît ; le refus reste pour un onglet |
| 0.52 | 2026-10-15 | § 5 duodecies : les menus dans la palette, l'aperçu, le panneau et l'exécution ; un bouton ajouté dans un bloc a son événement ; le graphique n'est plus pris pour un bloc |
| 0.53 | 2026-10-15 | § 5 undecies : le graphique dans le panneau, regroupé par, valeur (nombre ou somme), forme (barres ou secteurs) |
| 0.54 | 2026-10-15 | § 5 octies : un élément placé passe d'un bloc libre à un autre (page d'onglet, écran, groupe) ; un bloc libre dans un conteneur le remplit |
| 0.55 | 2026-10-15 | Les zones de fichier et d'image : un bouton « Choisir un fichier… », le sélecteur de fichiers à l'exécution (images seules pour une image), le fichier choisi sur le bouton |
| 0.56 | 2026-10-15 | L'interrupteur dans l'aperçu et l'exécution : une case à cocher dessinée en rail et bouton, à la couleur d'accent |
| 0.57 | 2026-10-16 | Des choix multiples dans l'aperçu et l'exécution : une case par objet, chaque case change la zone |
| 0.58 | 2026-10-16 | Palette : « Interrupteur » ; « Image » propose « une image de l'ordinateur (copiée dans le projet) » et ouvre le dossier des images ; une image glissée depuis le Finder s'ajoute à l'écran ; panneau d'une zone : « en interrupteur » ; une zone multiple garde sa marque |
| 0.59 | 2026-10-16 | § 5 terdecies : le clic droit sur l'aperçu, les champs des entités, chacun ajouté comme sa zone, là où l'on a cliqué |
| 0.60 | 2026-10-16 | § 5 terdecies : le clic droit propose aussi les éléments de la palette, posés au clic ; la palette gagne « Choix multiples » et « Fichier » |
| 0.61 | 2026-10-16 | Le titre de l'écran garde sa hauteur naturelle (il prenait toute la hauteur libre de l'aperçu) ; un clic droit sur le titre, ou le logo, le renomme |
| 0.62 | 2026-10-16 | Le clic droit sur le titre propose sa taille (en petit, normale, en grand) ; l'aperçu et l'exécution la montrent |
| 0.63 | 2026-10-16 | L'indication d'une zone : une ligne « Indication » dans son panneau ; sous le libellé dans l'aperçu et à l'exécution |
| 0.64 | 2026-10-16 | La recherche : un menu modifiable qui propose les objets contenant la saisie ; la case « avec recherche » dans le panneau d'une zone |
| 0.65 | 2026-10-16 | Les bornes d'une zone : « Au moins » et « Au plus » dans son panneau ; la frappe d'un texte s'arrête à sa longueur |
| 0.66 | 2026-10-16 | Le masque : la case « masqué » dans le panneau d'une zone ; la frappe cachée, la valeur jamais réécrite à l'exécution |
| 0.67 | 2026-10-16 | Courriel, téléphone, adresse web : le clavier qui convient dans une zone |
| 0.68 | 2026-10-16 | § 5 terdecies : le clic droit sur un élément propose d'abord de le supprimer |
| 0.69 | 2026-10-16 | L'unité : à côté du champ dans l'aperçu et à l'exécution ; « Unité » et « l'unité devant le champ » dans le panneau |
| 0.70 | 2026-10-16 | À l'exécution, chaque zone en faute porte son message sous son libellé, jusqu'au geste suivant |
| 0.71 | 2026-10-16 | Les étiquettes : la cellule d'une valeur colorée prend ses couleurs dans les listes à l'exécution |
| 0.72 | 2026-10-16 | § 8, E1 : l'identité de l'application (nom, version, éditeur) ; l'installateur Windows des applications ; `--identite`, `--icone-ico` |
| 0.73 | 2026-10-16 | Les rapports en PDF (`rapport.cpp`, A4, numéros de page) ; une application fabriquée écrit ses fichiers dans Documents/<Nom> |
| 0.74 | 2026-10-17 | Le son dans les menus : « Son » dans la palette et le clic droit, « Un son » parmi les zones de fichier ; le panneau d'une zone propose son, courriel, téléphone, adresse web et mot de passe |
| 0.75 | 2026-10-17 | La transcription (grammaire § 15.8, étape 2) : whisper.cpp v1.9.5 intégré (`transcription.cpp`), le décodage des sons par Qt Multimedia, le modèle large-v3-turbo |
| 0.76 | 2026-10-17 | Le modèle de transcription dans les paquets : toujours dans l'atelier, dans une application seulement si son programme transcrit (`--transcrit`, `outils/modele-transcription`, vérifié par son empreinte, en cache sur GitHub) |
| 0.77 | 2026-10-17 | La relecture d'un texte transcrit (`relecture.cpp`) : nombres surlignés, mots douteux soulignés, un doute qui disparaît à la correction |
| 0.78 | 2026-10-18 | « Son » dans la palette pose deux zones d'un même geste : le son, et dessous sa transcription (texte, sur plusieurs lignes, transcrite du son) |
| 0.79 | 2026-10-18 | « Lancer » sans fichier ouvert (le projet rouvert au démarrage) lance le programme principal au lieu de ne rien faire ; de même « Fabriquer l'application » et l'onglet Données |
| 0.80 | 2026-10-18 | Fenêtre d'exécution : dans les fiches, l'étiquette colorée, le courriel et l'adresse web en liens ouverts par le système (`QDesktopServices`) ; les couleurs d'étiquette en un seul endroit (`teinte_etiquette`) |
| 0.81 | 2026-10-18 | § 5 undecies : « Période » dans le panneau d'un graphique regroupé par une date ; auparavant, « Appliquer » perdait la période écrite dans le code |
| 0.82 | 2026-10-18 | § 5 quaterdecies : écouter et enregistrer les sons (`son.cpp`), `NSMicrophoneUsageDescription` dans `outils/Info.plist.in`, lecteur et image dans la fiche déduite |
| 0.83 | 2026-10-18 | Menu « Projets du dépôt » ; dans l'exécution, le tri d'une liste par la valeur (`CelluleTriable`) et la ligne de total épinglée en bas (`epingler_total`) |
| 0.84 | 2026-10-18 | Fenêtre d'exécution : un écran ouvert occupe toute la fenêtre, centré dans la largeur de lecture (« largeur-contenu », 1120 px), en haut s'il n'a rien qui s'étire ; le fil et la zone des questions n'apparaissent que pour une question, à la fin du programme ou par Ctrl+J. Une application fabriquée ne montre donc plus de panneau d'exécution. `grym-atelier --capture sortie.png [--clics "A;B"] [--taille 1280x860] programme.grym` photographie un programme sans écran physique, pour comparer à une maquette |
| 0.85 | 2026-10-18 | Fenêtre d'exécution : sans liste, graphique, texte long ni onglets, l'écran se resserre en haut (un ressort sous ses éléments) au lieu de répartir la hauteur entre eux ; le bouton principal (grammaire 2.14) prend `role=principal` dans la feuille du thème |
| 0.86 | 2026-10-18 | La fenêtre d'exécution et l'application fabriquée s'ouvrent en grand, comme l'atelier (l'écran disponible, sans plein écran exclusif) ; le bandeau de la signature (grammaire 2.16) ; un bouton à côté du titre (2.17) |
| 0.87 | 2026-10-18 | Une zone d'objet dont la valeur devient un objet créé pendant que l'écran est ouvert (« Nouveau patient » depuis une séance) l'ajoute à sa liste déroulante au lieu de rester vide |
| 0.88 | 2026-10-19 | Capture : `--clics "Patients;ligne:Anne Rochat"` choisit la ligne d'une liste qui porte ce texte, comme un double-clic. Fenêtre d'exécution : une question demandée depuis un écran se voit sous lui ; le bandeau de signature et un texte posé au pixel ne prennent plus l'espace libre. Mesure : un écran posé au pixel tient dans 1072 points de large (la largeur de lecture moins ses marges), à 1512 × 982 comme à 1800 × 1169 |
| 0.89 | 2026-10-19 | Une zone (date) a son calendrier (icône Lucide « calendar » au bout du champ, semaine du lundi, jours en initiales, chevrons du thème) ; aussi dans un formulaire déduit. Capture : `--clics "…;calendrier"` ouvre le calendrier de la première zone de date, peint par-dessus l'écran |
| 0.90 | 2026-10-19 | Le micro se demande à l'ouverture d'un programme qui a un son (champ ou zone), plus au premier clic ; refusé, une boîte mène aux réglages (une fois par lancement). Le bouton du micro s'appelle « ● Dicter » |
| 0.91 | 2026-10-20 | Fenêtre d'exécution : chaque écran défile dans sa zone (`defilement-ecran`) quand il dépasse la fenêtre ; l'application fabriquée et le lancement depuis l'atelier ne s'ouvrent plus en grand (0.86) : la fenêtre prend la taille de chaque écran qui n'en recouvre aucun (la connexion, puis l'accueil), sa hauteur naturelle et au moins la largeur de lecture, sans dépasser l'écran disponible, centrée |

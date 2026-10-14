# GrymoiR : les écrans dans le langage

Conception validée le 25 septembre 2026. A3 implémenté le 25 septembre 2026.
Référence : Charte de GrymoiR v1.39, art. 2, 4, 7, 11, 12 et 13 ; grammaire 1.38, § 9, § 16, § 19, § 20 ; `docs/atelier.md`, jalon A3.
Toute décision prise sur ce document passe dans la grammaire et la charte par une révision numérotée.

---

## 1. Thèse

Jusqu'ici, un programme GrymoiR est une suite de phrases : il pose ses questions dans l'ordre, et l'utilisateur répond. A3 donne au langage des écrans : des fenêtres qui montrent des listes et des fiches, des boutons, et des événements. Entre l'ouverture d'un écran et sa fermeture, c'est l'utilisateur qui décide de l'ordre.

Les écrans s'affichent avec Qt, dans l'atelier et dans les applications (`docs/atelier.md`, § 1). Comme tout le reste, ils s'écrivent en GrymoiR : l'éditeur d'écrans d'A4 n'écrira que des phrases de ce document.

## 2. Exemple de référence

```
L'écran des compositeurs montre :
    la liste des compositeurs conservés, par nom,
    un bouton « Nouveau »,
    un bouton « Fermer ».

Quand on choisit un compositeur dans l'écran des compositeurs :
    Ouvrir la fiche du compositeur.

Quand on clique sur « Nouveau » dans l'écran des compositeurs :
    Le c vaut un nouveau compositeur saisi.
    Conserver c.

Quand on clique sur « Fermer » dans l'écran des compositeurs :
    Fermer l'écran.

Ouvrir l'écran des compositeurs.
```

## 3. Décisions

### 3.1 Les trois piliers *(validés le 25 septembre 2026)*

1. **Un écran se déclare**, comme une entité : `L'écran des compositeurs montre :`, suivi de ce qu'il contient. Une liste reprend la recherche du § 16.4 (`des compositeurs conservés dont …, par …`) ; elle est vivante et se relit après chaque événement.
2. **Les événements s'écrivent `Quand on …`.** Ce sont des actions (grammaire, § 9) : elles voient les variables du programme, conservent, affichent, posent des questions. Chaque événement forme une transaction (charte, art. 7) : un événement qui échoue n'écrit rien, et l'écran reste ouvert avec le message.
3. **`Ouvrir l'écran des compositeurs.`** montre la fenêtre, puis le programme attend les événements jusqu'à `Fermer l'écran.` ; la phrase suivante s'exécute ensuite.

La fiche d'un objet est l'écran déduit de son entité, comme le formulaire de `saisi` (§ 19) : `Ouvrir la fiche du compositeur.` n'exige aucune déclaration. On n'écrit un écran que pour autre chose que le défaut.

Coût accepté : `quand`, `ouvrir` et `fermer` deviennent des mots de construction (grammaire, § 10.7), inscrits au journal de la grammaire (charte, art. 13.2).

### 3.2 Les colonnes d'une liste *(validé le 25 septembre 2026)*

- **Par défaut**, une colonne par champ simple de l'entité, dans l'ordre de la déclaration. Un lien montre la clé de l'objet désigné (son champ texte unique, comme les menus des formulaires). Fichiers, images et champs « plusieurs » restent hors de la liste. Le titre d'une colonne est le nom du champ avec une majuscule (« Naissance »).
- **Au choix**, les champs nommés après `avec` : `la liste des œuvres conservées, par titre, avec le titre, le compositeur et l'année`. Le champ d'un lien s'écrit par le complément du nom (§ 13.3) : `avec le titre et la naissance du compositeur` ; la colonne s'appelle « Naissance du compositeur ».
- Sans rien écrire : un clic sur le titre d'une colonne trie la liste à l'écran, sans changer le programme ; un champ absent s'affiche vide.
- Écartés pour l'instant : colonnes calculées, largeur et alignement réglés à la main.

### 3.3 La disposition *(validé le 25 septembre 2026)*

On décrit l'ordre et le voisinage, jamais des pixels : le texte reste lisible, et l'écran s'adapte à toute taille, police ou système. L'éditeur d'écrans d'A4 écrira les mêmes phrases.

- Sans rien écrire : les contrôles s'empilent de haut en bas, dans l'ordre écrit ; des boutons qui se suivent se rangent sur une ligne, en bas à droite ; une liste prend la hauteur libre ; la fenêtre se redimensionne.
- `côte à côte :` range son bloc à l'horizontale, `l'un sous l'autre :` à la verticale ; les blocs s'emboîtent.
- Le titre de la fenêtre vient du nom de l'écran (« l'écran des compositeurs » : « Compositeurs » ; « l'écran d'accueil » : « Accueil »), ou se donne entre guillemets : `L'écran des compositeurs, « Nos compositeurs », montre :`.
- Écartés : positions et tailles au pixel, marges réglées à la main, onglets (à concevoir plus tard).

**Révisé le 14 octobre 2026 : la position libre.** Le développeur a choisi de pouvoir placer les éléments où il veut, au pixel, plutôt qu'une grille en cases, en connaissant le prix : un bloc posé au pixel ne s'adapte plus à la largeur de la fenêtre. Le bloc `en position libre :` (grammaire, § 22.9) l'admet ; hors de lui, tout ce qui précède reste la règle. Ce qu'on garde de la description :

- La place s'écrit en phrase (`à 96 de la gauche et 16 du haut`), relative au bloc, jamais à la fenêtre : un bloc libre se met dans un écran décrit comme un autre bloc.
- L'ordre écrit reste l'ordre de lecture (Tab, lecteurs d'écran) ; l'atelier l'écrit de haut en bas, puis de gauche à droite, pour que les deux ordres coïncident.
- La taille reste facultative : sans elle, l'interface prend la taille naturelle de l'élément, celle du thème.
- Le navigateur empile les éléments du bloc dans l'ordre écrit quand la fenêtre est plus étroite que le bloc.
- La grille d'alignement est un outil de l'atelier, pas du langage : elle n'apparaît jamais dans le programme (`docs/atelier.md`, § 5 octies).

### 3.4 Les contrôles *(validé le 25 septembre 2026)*

Un écran est un objet : ses zones de saisie sont ses champs (charte, art. 6).

- Texte fixe : `le texte « … »`.
- Zone de saisie : `un pays (texte)`, avec les types des entités et les contrôles des formulaires (case à cocher pour `(vrai ou faux)`, menu déroulant pour un lien, champ de date pour `(date)`) ; `, « … » au départ` et `, facultatif` comme pour les entités.
- Bouton et liste : § 3.1 et § 3.2.
- Dans un événement : `le pays de l'écran` se lit, `Le pays de l'écran devient « France ».` se modifie, comme le champ d'un objet. Une liste dont le `dont` cite une zone de saisie se relit dès que la zone change.
- Une zone de saisie n'est jamais conservée : elle vit le temps de l'écran. `de l'écran` désigne l'écran de l'événement en cours ; ailleurs, c'est une erreur d'analyse.
- Reportés : graphiques (§ 6). Les images fixes sont venues avec B2 bis (grammaire, § 22.8). Les textes sur plusieurs lignes sont venus avec B1 (grammaire, § 22.6).

### 3.5 Les événements *(validé le 25 septembre 2026)*

| Événement | Quand il arrive |
|---|---|
| `Quand on ouvre l'écran des compositeurs :` | avant le premier affichage |
| `Quand on clique sur « Nouveau » dans l'écran des compositeurs :` | un clic sur ce bouton |
| `Quand on choisit un compositeur dans l'écran des compositeurs :` | double-clic, ou Entrée, sur une ligne |
| `Quand on change le pays dans l'écran de recherche :` | une zone validée : Entrée, ou quittée après modification |
| `Quand on ferme l'écran des compositeurs :` | à la fermeture, par `Fermer l'écran.` ou par la croix |

- `Quand on choisit un compositeur` nomme la ligne `compositeur` dans son corps, comme un paramètre d'action (§ 9.3). Dans tout événement, `le compositeur choisi de l'écran` désigne la ligne sélectionnée, ou `absent`.
- Un bouton sans événement est une erreur d'analyse ; « Fermer » aussi s'écrit, sans comportement implicite.
- Un événement qui cite un bouton ou une zone inexistants est une erreur d'analyse, avec la correction la plus proche.
- La fermeture ne se refuse pas.
- Deux listes de la même entité dans un écran rendent `Quand on choisit …` ambigu : erreur pour l'instant.

### 3.6 Plusieurs écrans, et la transaction *(validé le 25 septembre 2026)*

- Les écrans s'empilent : `Ouvrir` dans un événement ouvre le nouvel écran par-dessus ; seul l'écran du dessus reçoit les événements ; à sa fermeture, l'événement qui l'a ouvert reprend à la phrase suivante. La fiche ouverte depuis une liste fait de même. `Fermer l'écran.` ferme l'écran de l'événement en cours.
- **`Ouvrir` valide d'abord ce qui précède.** Ce que l'événement, ou le programme, a fait avant `Ouvrir` est conservé ; chaque événement de l'écran ouvert est ensuite sa propre transaction ; après la fermeture, la suite forme une nouvelle transaction. Un clic réussi n'est jamais perdu à cause d'un autre ; chaque clic reste tout ou rien ; un programme sans écran reste une seule transaction.
- Conséquence : la charte, art. 7, reçoit une exception écrite : « `Ouvrir` un écran découpe l'exécution en transactions, une par événement ».
- Écartée : une seule transaction pour toute la durée d'un écran, qui verrouillerait la base et perdrait tout au moindre échec.

### 3.7 La forme compacte *(validé le 25 septembre 2026)*

Mêmes règles que la grammaire, § 11 : mots-clés à souligné, noms à soulignés internes, blocs fermés par `_fin`, champs par le point. Le nom d'un écran garde sa préposition (« l'écran des compositeurs » : `des_compositeurs` ; « l'écran d'accueil » : `d'accueil`), pour que l'aller-retour ne perde rien.

| Littéraire | Compacte |
|---|---|
| `L'écran des compositeurs, « Nos compositeurs », montre :` | `_écran des_compositeurs « Nos compositeurs »` … `_fin` |
| `la liste des compositeurs conservés, par nom, avec le nom et la naissance` | `_liste compositeur _conservé _par nom _avec nom ; naissance` |
| `un bouton « Nouveau »` | `_bouton « Nouveau »` |
| `le texte « Bienvenue »` | `_texte « Bienvenue »` |
| `un pays (texte), « Suisse » au départ` | `_un pays (texte) _départ « Suisse »` |
| `côte à côte :`, `l'un sous l'autre :` | `_côte_à_côte` … `_fin`, `_l'un_sous_l'autre` … `_fin` |
| `le pays de l'écran` | `_écran.pays` |
| `le compositeur choisi de l'écran` | `_écran.compositeur_choisi` |
| `Quand on clique sur « Nouveau » dans l'écran des compositeurs :` | `_quand _clique « Nouveau » _dans des_compositeurs` … `_fin` |
| `Quand on choisit un compositeur dans l'écran des compositeurs :` | `_quand _choisit _un compositeur _dans des_compositeurs` … `_fin` |
| `Quand on change le pays dans l'écran de recherche :` | `_quand _change pays _dans de_recherche` … `_fin` |
| `Quand on ouvre l'écran …`, `Quand on ferme l'écran …` | `_quand _ouvre …`, `_quand _ferme …` |
| `Ouvrir l'écran des compositeurs.`, `Ouvrir la fiche de c.` | `_ouvrir des_compositeurs`, `_ouvrir _fiche c` |
| `Fermer l'écran.` | `_fermer` |

### 3.8 Hors de l'atelier *(validé le 25 septembre 2026)*

- Un programme qui ouvre des écrans est refusé par `grym lancer`, avant toute exécution : « Ce programme ouvre des écrans : lance-le dans une fenêtre, avec grym-atelier. » Depuis W1 (`docs/web.md`), `grym servir` les montre dans le navigateur. Un programme sans écran tourne partout, comme avant.
- L'interface de la machine (`src/interface.h`) reçoit les appels des écrans ; les essais les pilotent par script (cliquer, choisir, saisir), sans Qt, pour l'intégration continue.
- Pour distribuer une application : `grym-atelier --lancer` d'abord ; un exécutable dédié, sans l'atelier, plus tard.
- Un repli en texte reste possible si un vrai besoin se présente.

## 4. Découpage de l'implémentation

La conception est complète : elle passe dans la grammaire (§ 22) et la charte (art. 7). Chaque tranche livre ses constructions dans les deux formes, avec l'imprimeur, la forme compacte, les suites attendues et la coloration (charte, art. 4), et ses essais pilotés par script.

| Tranche | Contenu |
|---|---|
| A3-a *(fait)* | Déclaration d'écran avec liste, bouton et texte fixe ; `Quand on clique …`, `Quand on choisit …` ; `Ouvrir` et `Fermer` ; la boucle d'événements dans la machine, par l'interface ; une transaction par événement ; refus en console et dans le navigateur ; l'affichage Qt dans la fenêtre d'exécution |
| A3-b *(fait)* | Zones de saisie et `de l'écran` ; `le … choisi de l'écran` ; `Quand on change …`, `Quand on ouvre …`, `Quand on ferme …` ; listes qui suivent les zones |
| A3-c *(fait)* | La fiche déduite (`Ouvrir la fiche de c.`), les écrans empilés |
| A3-d *(fait)* | Colonnes choisies (`avec`), disposition (`côte à côte`, `l'un sous l'autre`), titre donné |

---

## 5. L'apparence *(validé le 26 septembre 2026)*

### 5.1 Un seul thème

GrymoiR fixe l'apparence des écrans : polices, tailles, espacements, formes et couleurs neutres. Le développeur ne règle que deux choses, une couleur d'accent et un logo. Le clair ou le sombre suit le réglage du système, sans rien écrire (charte, principe 3).

Raisons : le public de GrymoiR, le développeur seul, ne veut pas jouer au graphiste ; chaque réglage ajouté en appelle un autre, et la liste fermée protège le langage de la dérive vers une feuille de style en français. Elle pourra s'élargir plus tard sans casser de programme ; la réduire en casserait. Un fichier de thème à part est exclu : l'apparence fait partie du programme (`docs/atelier.md`, § 2).

### 5.2 La phrase *(implémentée le 26 septembre 2026 : grammaire, § 22.5)*

```
Les écrans ont la couleur verte et le logo « logo.png ».
```

- `Les` en début de phrase : aucune phrase ne commence aujourd'hui par un article pluriel, donc aucune collision avec la création d'un nom, et aucun mot réservé ajouté.
- Chacun des deux réglages est facultatif. Une seule phrase par programme, au premier niveau ; une seconde est une erreur.
- `la couleur verte` s'accorde, contrôlé comme les autres accords.
- Le logo est un chemin écrit tel quel, jamais une expression : l'aperçu de l'atelier, qui n'exécute pas le programme, peut ainsi l'afficher. Un fichier absent, ou qui n'est pas une image, est refusé à l'analyse (grammaire, § 15.2).
- Forme compacte, esquisse : `_écrans _couleur verte _logo « logo.png »`.
- Où s'affiche le logo relève du thème, pas du langage. Décidé le 26 septembre 2026 : grand (64 px) à gauche du titre d'un écran que le programme ouvre lui-même, sans écran dessous ; petit (24 px) sur un écran ouvert par-dessus ; en icône de la fenêtre. Le langage ne connaît ni le nom de l'application ni un écran d'accueil désigné : le bandeau de la maquette (logo et nom de l'application en haut de chaque écran) aurait demandé une phrase de plus.
- Correction : la version 0.13 affirmait qu'aucune phrase ne commençait par `Les`. C'était faux (`Les nombres s'affichent …`, § 4.1 ; `Les genres de o gagnent …`, § 16.13). La phrase reste sans ambiguïté : `Les écrans ont` se distingue des deux autres dès son deuxième ou troisième mot.

### 5.3 La palette

Cinq couleurs d'accent : `bleue` (par défaut, sans phrase écrite), `verte`, `turquoise`, `violette`, `grise`.

- Pas de couleur écrite en hexadécimal : `#` ouvre une remarque en forme compacte (grammaire, § 11.1), et un non-programmeur ne devine pas ce que vaut `« #1E6FD9 »` (principe 2).
- Rouge : réservé aux erreurs et aux gestes destructeurs, jamais un accent.
- Jaune : aucune teinte ne porte un texte lisible sans virer au brun.
- Rose et orange : écartés après calcul. Leur teinte s'écarte de 20 à 30 degrés seulement de celle du danger, à clarté presque égale ; dans la maquette, une zone active à l'accent orange se confondait avec une zone en erreur.
- Chaque accent tient, en clair et en sombre, 4,5:1 pour le texte et 3:1 pour les contours (WCAG 2.1, critères 1.4.3 et 1.4.11). Les teintes et leurs rapports sont dans `src/atelier/theme/grymoir-jetons.json` ; un essai de l'atelier les recalcule.

## 6. Deuxième vague de contrôles *(conçue le 14 octobre 2026)*

L'éditeur d'écrans ne proposait que cinq éléments. La deuxième vague comble l'écart avec les environnements de gestion courants, sans revenir sur le § 3.3 : toujours aucun pixel, sauf dans le bloc en position libre, ajouté après coup (§ 3.3, révision du 14 octobre 2026). Ordre retenu, du plus demandé au plus coûteux.

| Tranche | Contenu | Statut |
|---|---|---|
| B1 | `sur plusieurs lignes`, `en lecture seule`, `actif si …`, `visible si …` (grammaire, § 22.6) | fait |
| B2 | valeurs permises d'une zone et boutons radio (au plus quatre), groupe titré `le groupe « Adresse » :` (grammaire, § 22.7) | fait |
| B2 bis | image affichée, fixe : `l'image « plan.png »` (grammaire, § 22.8) | fait |
| B2 ter | image calculée, `l'image du portrait du compositeur choisi de l'écran` (grammaire, § 22.8) | fait |
| B2 quater | position libre au pixel, `en position libre :` (grammaire, § 22.9), grille d'alignement dans l'atelier | fait |
| B3 | onglets, barre de menus, graphique | à concevoir |

### 6.1 Les états se déclarent *(validé le 13 octobre 2026)*

L'état actif ou visible d'un élément se déclare dans l'écran, par une condition, au lieu de se piloter dans les événements (`Le bouton … devient inactif.`). Raisons : l'aperçu de l'atelier lit l'écran sans exécuter, un état déclaré ne se désynchronise jamais d'un événement oublié, et la machine peut refuser un geste sur un élément inactif même si l'interface est trafiquée (le navigateur).

- La condition se relit avant chaque attente, comme les listes (§ 3.4) ; l'interface reçoit l'état de chaque élément (`ecran_etats`).
- Elle se lit à la fin de la déclaration : elle cite n'importe quelle zone ou liste de l'écran, même plus bas.
- Un élément caché disparaît libellé compris ; dans le navigateur, il n'est pas dans la page.
- Une zone vide vaut absente pour une condition ou un `dont` ; l'obligation d'une zone se vérifie dans l'événement qui la lit, avec un message pour l'utilisateur (« Remplis « Nom » : cette zone est obligatoire. »), comme un formulaire HTML vérifie `required` à l'envoi *(validé le 14 octobre 2026)*. Une case à cocher obligatoire vaut faux à l'ouverture ; les autres zones obligatoires portent un astérisque.
- Écarté pour l'instant : `visible si` sur une liste (la place d'une liste qui disparaît change toute la disposition), un état modifié par phrase dans un événement.

## Journal des révisions

| Version | Date | Changement |
|---------|------|------------|
| 0.1 | 2026-09-25 | Proposition initiale : thèse, exemple de référence, trois piliers validés, questions à trancher |
| 0.2 | 2026-09-25 | § 3.2 : colonnes d'une liste |
| 0.3 | 2026-09-25 | § 3.3 : disposition |
| 0.4 | 2026-09-25 | § 3.4 : contrôles ; un écran est un objet, ses zones de saisie sont ses champs |
| 0.5 | 2026-09-25 | § 3.5 : événements |
| 0.6 | 2026-09-25 | § 3.6 : écrans empilés ; `Ouvrir` valide ce qui précède, une transaction par événement |
| 0.7 | 2026-09-25 | § 3.7 : forme compacte |
| 0.8 | 2026-09-25 | § 3.8 : hors de l'atelier, refus expliqué ; § 4 : découpage en A3-a à A3-d ; conception validée |
| 0.9 | 2026-09-25 | § 4 : A3-a fait |
| 0.10 | 2026-09-25 | § 4 : A3-b fait |
| 0.11 | 2026-09-25 | § 4 : A3-c fait ; § 3.4 : l'exemple de case à cocher corrigé, c'est le type `(vrai ou faux)` qui la donne, pas le mot « case » |
| 0.12 | 2026-09-25 | § 4 : A3-d fait ; A3 fait |
| 0.13 | 2026-09-26 | Nouveau § 5 : l'apparence (un seul thème, la phrase `Les écrans ont …`, la palette de cinq accents) |
| 0.14 | 2026-09-26 | § 5.2 : phrase implémentée (T3) ; place du logo ; correction sur les phrases en `Les` |
| 0.15 | 2026-09-29 | § 3.8 : `grym servir` montre les écrans dans le navigateur (`docs/web.md`, W1) ; seul `grym lancer` les refuse |
| 0.16 | 2026-10-14 | § 6 : deuxième vague de contrôles ; B1 fait (états des éléments, grammaire § 22.6) ; § 3.4 : les textes sur plusieurs lignes ne sont plus reportés |
| 0.17 | 2026-10-14 | § 6 : B2 fait (groupe titré, valeurs permises d'une zone, boutons radio) ; l'image affichée passe en B2 bis |
| 0.18 | 2026-10-14 | § 6.1 : zone vide absente pour une condition ; obligation vérifiée dans l'événement ; case à faux ; astérisque |
| 0.19 | 2026-10-14 | § 6 : B2 bis fait (image fixe) ; l'image d'une zone passe en B2 ter |
| 0.20 | 2026-10-14 | § 3.3 révisé : la position libre au pixel, dans un bloc `en position libre :` ; § 6 : B2 quater fait |
| 0.21 | 2026-10-14 | § 6 : B2 ter fait (image calculée) |

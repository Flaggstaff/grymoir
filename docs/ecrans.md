# GrymoiR : les écrans dans le langage

Proposition soumise à relecture, rédigée le 25 septembre 2026. Rien n'est implémenté.
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

### 3.4 Les contrôles *(validé le 25 septembre 2026)*

Un écran est un objet : ses zones de saisie sont ses champs (charte, art. 6).

- Texte fixe : `le texte « … »`.
- Zone de saisie : `un pays (texte)`, avec les types des entités et les contrôles des formulaires (case à cocher pour `(vrai ou faux)`, menu déroulant pour un lien, champ de date pour `(date)`) ; `, « … » au départ` et `, facultatif` comme pour les entités.
- Bouton et liste : § 3.1 et § 3.2.
- Dans un événement : `le pays de l'écran` se lit, `Le pays de l'écran devient « France ».` se modifie, comme le champ d'un objet. Une liste dont le `dont` cite une zone de saisie se relit dès que la zone change.
- Une zone de saisie n'est jamais conservée : elle vit le temps de l'écran. `de l'écran` désigne l'écran de l'événement en cours ; ailleurs, c'est une erreur d'analyse.
- Reportés : images, textes sur plusieurs lignes, graphiques.

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

## 4. Questions à trancher

1. Les écrans en console et dans le navigateur (`grym lancer`, `grym servir`) : repli, ou refus expliqué.

---

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

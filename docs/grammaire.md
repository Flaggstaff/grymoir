# Grammaire littéraire de GrymoiR, v0.1

Version 1.1 de la spécification, révisée le 21 septembre 2026.
Référence : Charte de GrymoiR v1.3, art. 4, 9 et 12.
Toute modification passe par une révision numérotée.

Périmètre : trois familles de phrases. Nommer, calculer, afficher. Plus le calcul des suites attendues (§ 7). Tout le reste attend les versions suivantes.

---

## 1. Règles de lecture (lexer)

### 1.1 Phrases

- Une instruction correspond à une phrase, terminée par un point.
- Pas de point-virgule, pas d'accolade.
- Casse ignorée (charte, art. 4) : `Le` et `le` sont équivalents.

### 1.2 Nombres

- Séparateur décimal : la virgule (`3,5`).
- Règle de désambiguïsation : chiffre, virgule, chiffre collé forment un nombre. `3,5` est un nombre ; `3, 5` en fait deux.
- Séparateur de milliers accepté en entrée : apostrophe suisse (`1'000`) et espace insécable (U+00A0, U+202F : `1 000`).
- L'apostrophe de milliers suit toujours un chiffre ; l'apostrophe d'élision suit toujours une lettre. Aucune ambiguïté.
- Les séparateurs de milliers ne sont admis que dans la partie entière, par groupes de trois chiffres (`1'000`, `12'345`) ; `1'00` est une erreur.
- Le point décimal à l'anglaise est une erreur avec correction : `3.5` produit « Écrivez « 3,5 » ».
- Un nombre collé à un mot (`3kg`) et une seconde virgule décimale (`1,2,3`) sont des erreurs.

### 1.3 Apostrophes

- `'` (U+0027) et `’` (U+2019) sont équivalentes.
- La normalisation NFC ne les unifie pas : le lexer s'en charge.

### 1.4 Opérateurs

| Opération | Forme de référence | Équivalent ASCII |
|-----------|-------------------|------------------|
| Addition | `+` | `+` |
| Soustraction, négation | `−` (U+2212) | `-`, et `–` (U+2013) |
| Multiplication | `×` | `*` |
| Division | `÷` | `/` |
| Puissance | `^` | `^` |
| Groupement | `( )` | `( )` |

- Le tiret demi-cadratin `–` est accepté comme moins : la correction automatique de Word le substitue à `-`.
- Le tiret cadratin `—` est refusé, avec un message qui propose `-`, `−` ou `–`.
- Le trait d'union `-` est toujours un opérateur. Les noms à trait d'union sont interdits : `prix-remise` reste une soustraction.

### 1.5 Texte

- Délimité par `« »`, par `“ ”` (guillemets anglais, insérés par Word) ou par `" "`.
- Les espaces placées à l'intérieur des guillemets français ne font pas partie du texte : `« Bonjour »` vaut `Bonjour`. Les autres guillemets conservent leurs espaces.
- Le guillemet `„` est refusé.
- Un texte tient sur une ligne.

### 1.6 Commentaire

- Une ligne qui commence par `Remarque :` est un commentaire jusqu'à la fin de la ligne.
- Une remarque commence toujours une ligne. Elle ne peut pas couper une phrase écrite sur plusieurs lignes.
- Le commentaire est conservé dans l'arbre (charte, art. 4 : traduction sans perte).

---

## 2. Nommer

### 2.1 Deux verbes distincts

```
Le prix unitaire vaut 12,50.
La quantité vaut 3.
Le total vaut prix unitaire × quantité.
Le total devient total + 5.
```

- **`vaut` crée** un nom. Erreur si le nom existe déjà.
- **`devient` modifie** un nom. Erreur si le nom n'existe pas.

Justification : une faute de frappe dans une affectation ne crée jamais de variable en silence (charte, art. 8).

Correspondance prévue en forme compacte (v0.2) : `_soit total << 5` pour créer, `total << 5` pour modifier.

### 2.2 Noms composés

- Un nom peut compter plusieurs mots (`prix unitaire`, `date de création`).
- Dans une expression, le parser retient la plus longue correspondance parmi les noms déjà déclarés. Si `prix` et `prix unitaire` coexistent, `prix unitaire × 2` désigne `prix unitaire`.
- Les mots réservés `vaut`, `devient` et `puis` ne peuvent pas faire partie d'un nom.
- Un nom ne commence ni par un article ni par une élision. Articles et élisions sont permis à l'intérieur : `la date de la vente`, `le prix de l'article`.
- Un nom n'existe qu'après la phrase qui le crée : `Le total vaut total + 1.` est une erreur.

### 2.3 Articles et genre

- Article obligatoire en début de phrase : `Le`, `La`, `L'`.
- Article facultatif dans une expression : `Le total vaut le prix × la quantité.` est valide.
- Le premier article employé fixe le genre du nom. Devant une voyelle, `L'` ne fixe pas le genre ; le genre reste alors libre jusqu'au premier `le` ou `la`.
- Un article qui contredit le genre fixé produit une erreur : « `total` est masculin (déclaré ligne 3) ».

---

## 3. Calculer

### 3.1 Priorités

De la plus forte à la plus faible :

1. Parenthèses
2. Puissance `^` (associative à droite : `2 ^ 3 ^ 2` = `2 ^ 9`)
3. Négation `−`
4. `×` et `÷` (associatives à gauche)
5. `+` et `−` (associatives à gauche)

La puissance passe avant la négation, comme en mathématiques : `−2 ^ 2` vaut `−4`, et `(−2) ^ 2` vaut `4`. L'exposant peut être négatif : `2 ^ −1` vaut `0,5`.

### 3.2 Arithmétique

- Tous les nombres sont décimaux exacts, dès la v0.1 (charte, principe 1). `1,1 + 2,2` donne `3,3`.
- Une division au résultat non fini (`1 ÷ 3`) est arrondie à 28 chiffres significatifs, au plus proche, avec arrondi au pair en cas d'égalité (arrondi bancaire).
- Référence : spécification General Decimal Arithmetic, Mike Cowlishaw (IBM), adoptée par le module `decimal` de Python. Citée de mémoire, à vérifier avant implémentation.
- Division par zéro : erreur, jamais de valeur spéciale (infini, NaN).

### 3.3 Boucle interactive

- Une expression seule, sans point final, s'évalue et s'affiche.

```
> 1,1 + 2,2
3,3
```

- Une phrase complète, avec point, s'exécute normalement. Le point final de la dernière phrase saisie est facultatif.
- Une saisie ratée n'a aucun effet : aucun nom créé, aucun genre fixé, même par une phrase réussie de la même saisie.

---

## 4. Afficher

```
Afficher le total.
Afficher « Total à payer : » puis le total.
```

- `Afficher` suivi d'un ou plusieurs éléments reliés par `puis`.
- Un élément est un texte ou une expression. En v0.1, un texte n'apparaît que dans `Afficher`.

### 4.1 Format des nombres affichés

- Réglage par application.
- Défaut : style suisse, apostrophe pour les milliers et virgule décimale (`1'234,50`).
- Alternative : style français, espace insécable pour les milliers (`1 234,50`).
- Le mécanisme du réglage sera défini avec les applications (v1.0). En v0.1, seul le défaut existe.

---

## 5. Grammaire formelle (EBNF)

```
programme    = { phrase } ;
phrase       = création | modification | affichage | remarque ;
création     = article nom "vaut" expression "." ;
modification = article nom "devient" expression "." ;
affichage    = "Afficher" élément { "puis" élément } "." ;
remarque     = "Remarque" ":" texte-libre fin-de-ligne ;
élément      = texte | expression ;
expression   = terme { ( "+" | "−" ) terme } ;
terme        = unaire { ( "×" | "÷" ) unaire } ;
unaire       = "−" unaire | puissance ;
puissance    = base [ "^" unaire ] ;
base         = nombre | [ article ] nom | "(" expression ")" ;
article      = "le" | "la" | "l'" ;
```

Limites de cette notation :

- `nom` se résout par plus longue correspondance parmi les noms déclarés (§ 2.2), ce que l'EBNF n'exprime pas.
- Les équivalents ASCII des opérateurs (§ 1.4) sont traités au lexer.

---

## 6. Messages d'erreur de référence

| Situation | Message |
|-----------|---------|
| Nom inconnu proche d'un nom connu | « `totl` inconnu, vouliez-vous `total` ? » |
| `vaut` sur un nom existant | « `total` existe déjà (ligne 3). Pour le modifier, écrivez : Le total devient … » |
| `devient` sur un nom inconnu | « `total` n'existe pas. Pour le créer, écrivez : Le total vaut … » |
| Genre contradictoire | « `total` est masculin (déclaré ligne 3) » |
| Division par zéro | « Division par zéro (ligne 7, colonne 18) » |
| Phrase sans point final hors boucle interactive | « Point final manquant (ligne 5) » |
| Jeton inattendu | « « 2 » inattendu, attendu : un opérateur ou un point final. » |
| Article sans nom | « Nom attendu après « le ». » |
| Remarque mal placée | « Une remarque doit commencer une ligne : passez à la ligne avant « Remarque : ». » |
| Parenthèse non refermée | « Parenthèse fermante manquante : la parenthèse ouverte ligne 2, colonne 11 n'est pas refermée. » |

Chaque message indique fichier, ligne et colonne (charte, art. 8).

---

## 7. Suites attendues

À chaque position, l'analyseur calcule l'ensemble exact des suites valides (charte, art. 9).

- Catégories : début de phrase (`Le`, `La`, `L'`, `Afficher`, `Remarque :`), nombre, nom déclaré, nouveau nom, parenthèse, négation, texte, opérateurs, parenthèse fermante, `vaut` et `devient`, `puis`, point final.
- S'y ajoutent les mots qui prolongent un nom composé déclaré : après `prix`, `unitaire` si `prix unitaire` existe.
- Premier usage : les messages d'erreur (« `« 2 » inattendu, attendu : un opérateur ou un point final.` »).
- Second usage : l'aide à la saisie. Les suites sont calculées à la position du curseur ; si un mot est en cours de frappe, seules les suites qui le prolongent sont proposées, sans tenir compte de la casse.

Limites de la v0.1 :

- Une erreur placée avant le curseur supprime les suggestions. La reprise sur erreur viendra avec le serveur d'aide à la saisie (charte, art. 12, v0.4).
- Après un article, les noms proposés ne sont pas filtrés par genre.

---

## Journal des révisions

| Version | Date | Changement |
|---------|------|------------|
| 1.0 | 2026-09-20 | Spécification initiale : nommer (`vaut` / `devient`), calculer (décimal exact, arrondi bancaire à 28 chiffres), afficher (style suisse par défaut) |
| 1.1 | 2026-09-21 | Puissance avant négation (`−2 ^ 2` = `−4`). Guillemets `“ ”` et tiret `–` acceptés ; trait d'union toujours opérateur. Règles des noms (mots réservés, article initial interdit). Remarques en début de ligne. Boucle interactive : point final facultatif, saisie atomique. Nouveau § 7 : suites attendues |

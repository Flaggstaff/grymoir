# Grammaire littéraire de GrymoiR

Version 3.13 de la spécification, révisée le 7 novembre 2026. Tout ce qui suit est implémenté.
Référence : Charte de GrymoiR v1.36, art. 4, 5, 7, 8, 9 et 12.
Toute modification passe par une révision numérotée.

Périmètre : nommer (§ 2), calculer (§ 3), afficher et mettre en forme (§ 4), décider (§ 5), le calcul des suites attendues (§ 8), les formules (§ 9), répéter (§ 10), la forme compacte (§ 11), la forme canonique (§ 12), les objets (§ 13), les dates et les années (§ 14), les fichiers et les images (§ 15), les entités conservées (§ 16) les questions à l'utilisateur (§ 17), la reprise après erreur (§ 18) et le formulaire (§ 19). Ce que le langage ne sait pas encore faire est listé dans la charte, art. 11 et 12.

---

## 1. Règles de lecture (lexer)

### 1.1 Phrases

- Une instruction correspond à une phrase, terminée par un point.
- Pas de point-virgule, pas d'accolade.
- Casse ignorée (charte, art. 4) : `Le` et `le` sont équivalents.

### 1.2 Nombres

- Séparateur décimal : la virgule (`3,5`).
- Règle de désambiguïsation : chiffre, virgule, chiffre collé forment un nombre. `3,5` est un nombre ; `3, 5` en fait deux.
- Séparateur de milliers accepté en entrée : apostrophe suisse (`1'000`) et espace insécable (U+00A0, U+202F : `1 000`). Grouper ou non est un choix d'écriture, conservé dans l'arbre (§ 12) : `1747` reste `1747`.
- L'apostrophe de milliers suit toujours un chiffre ; l'apostrophe d'élision suit toujours une lettre. Aucune ambiguïté.
- Les séparateurs de milliers ne sont admis que dans la partie entière, par groupes de trois chiffres (`1'000`, `12'345`) ; `1'00` est une erreur.
- Le point décimal à l'anglaise est une erreur avec correction : `3.5` produit « Écris « 3,5 » ».
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
- Le trait d'union `-` est toujours un opérateur. Les noms à trait d'union sont interdits : `prix-remise` reste une soustraction. Là où un trait d'union collé entre deux mots ne peut pas être une soustraction (`Un rendez-vous a :`), le message le dit et propose les deux mots : « « rendez-vous » : un nom ne prend pas de trait d'union, « - » est une soustraction (§ 1.4). Écris « rendez vous », en deux mots. » Un nom de plusieurs mots est permis (§ 2).

### 1.5 Texte

- Délimité par `« »`, par `“ ”` (guillemets anglais, insérés par Word) ou par `" "`.
- Les espaces placées à l'intérieur des guillemets français ne font pas partie du texte : `« Bonjour »` vaut `Bonjour`. Les autres guillemets conservent leurs espaces.
- Le guillemet `„` est refusé.
- Un texte tient sur une ligne. Plusieurs lignes : le texte suivant (§ 4.7).

### 1.6 Comparaisons, crochets, indentation

| Comparaison | Forme de référence | Équivalent ASCII |
|-------------|-------------------|------------------|
| égal | `=` | `=` |
| différent | `≠` | `<>` |
| inférieur | `<` | `<` |
| supérieur | `>` | `>` |
| inférieur ou égal | `≤` | `<=` |
| supérieur ou égal | `≥` | `>=` |

- `[` … `]` délimite un nom écrit entre crochets (§ 2.2). Le contenu tient sur une ligne et ne contient que des mots et des élisions ; la casse et les espaces multiples ne comptent pas.
- L'indentation délimite les blocs (§ 5.4). Une tabulation en début de ligne est une erreur : « Tabulation en début de ligne : indente avec des espaces. » Une ligne vide peut contenir des tabulations.

### 1.7 Commentaire

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
- Les mots réservés ne peuvent pas faire partie d'un nom écrit sans crochets : `vaut`, `devient`, `puis`, `est`, `et`, `ou`, `si`, `sinon`, `vrai`, `faux`, `rendre`, `dont`, `définitivement`, `sur`, `suivi`, ainsi que l'élision `n'` devant `est`.
- La suite `d'un` ou `d'une` annonce un paramètre (§ 9.3) : elle ne fait jamais partie d'un nom.
- Un nom qui contient un mot réservé s'écrit entre crochets, à sa création comme à chaque usage : `Le [frais de port et d'emballage] vaut 12.` Tout nom peut s'écrire entre crochets : `[total]` et `total` désignent le même nom. L'aide à la saisie propose ces noms avec leurs crochets.
- Un nom entre crochets s'écrit seul entre l'article et le verbe, et ne commence pas par un article.
- Un nom ne commence ni par un article ni par une élision. Articles et élisions sont permis à l'intérieur : `la date de la vente`, `le prix de l'article`.
- Un nom n'existe qu'après la phrase qui le crée : `Le total vaut total + 1.` est une erreur.

### 2.3 Articles et genre

- Article obligatoire en début de phrase : `Le`, `La`, `L'`.
- Article facultatif dans une expression : `Le total vaut le prix × la quantité.` est valide.
- Le premier article employé fixe le genre du nom. Devant une voyelle, `L'` ne fixe pas le genre ; le genre reste alors libre jusqu'au premier `le` ou `la`.
- Un article qui contredit le genre fixé produit une erreur : « `total` est masculin (déclaré ligne 3) ».

---


### 2.4 Sigles *(2.21)*

```
Un cabinet, conservé, a :
    un nom (texte), unique,
    un numéro RCC (texte), facultatif.
```

- Un **sigle** est un mot d'au moins deux capitales, sans minuscule, écrit ainsi **partout** dans le programme et ses fichiers utilisés : `RCC`, `GLN`, `ASCA`. Les noms restent insensibles à la casse (charte, art. 4) : `le numéro rcc du c` désigne le même champ que `le numéro RCC du c`.
- Le sigle garde ses capitales là où un nom s'affiche : le libellé d'une zone, d'une fiche, d'un formulaire, le titre d'une colonne (« Numéro RCC »), dans le navigateur, l'atelier et l'application fabriquée.
- La forme canonique les garde aussi (§ 12), dans les deux formes : `numéro RCC`, `_le numéro_RCC`. Écrire un sigle tout en capitales est un choix d'écriture, comme grouper les chiffres d'un nombre.
- Écrit une seule fois autrement (`numéro Rcc`, `numéro rcc`), le mot n'est plus un sigle, nulle part : la forme canonique l'écrit alors en minuscules partout. Une capitale isolée (`A`) n'en fait pas un. `Le TOTAL vaut 1. Afficher total.` se formate donc en `Le total vaut 1. Afficher total.`
- Le compilateur range les sigles dans le module (instruction `SIGLES`, docs/vm.md) : un bytecode relu sans sa source les retrouve.
- Le message d'une erreur nomme encore le champ en minuscules (« « numéro rcc » … ») : seuls les libellés et la forme canonique changent.
## 3. Calculer

### 3.1 Priorités

De la plus forte à la plus faible :

1. Parenthèses
2. Puissance `^` (associative à droite : `2 ^ 3 ^ 2` = `2 ^ 9`)
3. Négation `−`
4. `×` et `÷` (associatives à gauche)
5. `+` et `−` (associatives à gauche)

La puissance passe avant la négation, comme en mathématiques : `−2 ^ 2` vaut `−4`, et `(−2) ^ 2` vaut `4`. L'exposant peut être négatif : `2 ^ −1` vaut `0,5`.

**Limites** *(3.13)*. Une expression ne s'imbrique pas sans fin : au-delà de 500 parenthèses, négations ou puissances emboîtées, « Expression trop imbriquée. » ; au-delà de 1000 opérations enchaînées (`+`, `−`, `×`, `÷`, `et`, `ou`, `suivi de`), « Expression trop longue : plus de 1000 opérations enchaînées. Découpe-la en plusieurs phrases, avec des valeurs intermédiaires. », signalée à l'opérateur qui dépasse. Le compilateur, l'imprimeur et la forme compacte parcourent l'arbre d'une expression récursivement : sans la seconde limite, `Afficher x + x + … .` avec quelques milliers de termes épuisait la pile, qui ne fait qu'un mégaoctet sous Windows (mesuré le 7 novembre 2026 : 1000 opérations passent avec une pile de 1 Mo, 4000 plantaient les trois commandes `lancer`, `formater` et `traduire`). Aucun programme écrit à la main n'approche ces limites ; un programme fabriqué par un outil, si.

### 3.2 Arithmétique

- Tous les nombres sont décimaux exacts, dès la v0.1 (charte, principe 1). `1,1 + 2,2` donne `3,3`.
- Addition, soustraction et multiplication sont exactes.
- Une division dont le quotient est fini en décimal reste exacte, quelle que soit sa longueur : `1 ÷ 8` donne `0,125`.
- Une division au quotient non fini (`1 ÷ 3`) est arrondie à 28 chiffres significatifs, au plus proche, avec arrondi au pair en cas d'égalité (arrondi bancaire). Un quotient non fini ne tombe jamais exactement sur une moitié : la règle d'égalité n'y sert pas. Arrondir un montant au centime ou aux cinq centimes est une autre opération (§ 3.4).
- La multiplication reste exacte même après une division arrondie : `1 ÷ 7 × 7` donne `1,0000000000000000000000000003`.
- Division par zéro : erreur, jamais de valeur spéciale (infini, NaN).
- Référence : spécification General Decimal Arithmetic, Mike Cowlishaw (IBM), adoptée par le module `decimal` de Python, citée de mémoire. Le comportement de GrymoiR a été comparé à ce module sur 6'672 opérations tirées au hasard, sans différence (21 septembre 2026).

#### Zéros de fin

Les décimales écrites se conservent, comme en comptabilité.

- Addition et soustraction : autant de décimales que l'opérande qui en a le plus. `100 − 100,00` donne `0,00`.
- Multiplication : la somme des décimales. `12,50 × 3` donne `37,50`.
- Division exacte : la différence des décimales, sans zéro superflu au-delà. `12,50 ÷ 5` donne `2,50` ; `1,0 ÷ 2` donne `0,5`.

#### Puissance

- L'exposant doit avoir une valeur entière : `2 ^ 3`, `2 ^ −1`, `2 ^ 2,0`. `2 ^ 0,5` est une erreur.
- Un exposant négatif calcule `1 ÷ x ^ n`, selon les règles de la division.
- `x ^ 0` vaut `1`, y compris `0 ^ 0`. `0` élevé à un exposant négatif est une division par zéro.

#### Limite

- Un résultat compte au plus 1'000 chiffres dans sa partie entière comme dans sa partie décimale. Au-delà : erreur « Nombre trop grand ».

### 3.3 Boucle interactive

- Une expression seule, sans point final, s'évalue et s'affiche.

```
> 1,1 + 2,2
3,3
```

- Une phrase complète, avec point, s'exécute normalement. Le point final de la dernière phrase saisie est facultatif.
- Une saisie ratée n'a aucun effet : aucun nom créé, aucun genre fixé, aucune valeur modifiée, même par une phrase réussie de la même saisie. Cela vaut aussi pour les erreurs de calcul (division par zéro).
- Ce qu'une exécution ratée a affiché avant l'erreur reste affiché : afficher n'est pas écrire. Après le message d'erreur, une phrase dit ce qui a été annulé, pour qu'aucun affichage ne laisse croire le contraire. Dans la boucle interactive : « Saisie annulée : aucun nom n'a changé », complété de « rien n'a été conservé dans la base » ou « aucun fichier n'a été écrit » s'il y a lieu. Avec `grym lancer`, sur la sortie d'erreur, seulement si une base ou des fichiers étaient en jeu : « Exécution annulée : rien n'a été conservé, ni dans la base ni sur le disque. » Après une question (§ 17) : « Exécution annulée : rien n'a été conservé depuis la dernière question. »
- `quitter`, ou la fin de l'entrée (Ctrl+D, Ctrl+Z sous Windows), termine la boucle.

### 3.4 Arrondir

```
Le montant vaut l'arrondi de brut × 1,081 à 0,05.          →  13,35 (si brut vaut 12,35)
La part vaut l'arrondi de total ÷ 3 à 0,01 vers le bas.
Afficher l'arrondi de 2,5 à 1 puis l'arrondi de 2,5 à 1 au pair.   →  3 2
```

- `l'arrondi de x à p` rend le multiple de `p` le plus proche de `x`. Le pas `p` est un nombre positif : `0,05`, `0,01`, `1`, `100`. Sinon, erreur à l'exécution : « Un arrondi se fait à un pas positif (0,05, 1, 10…), pas à 0. »
- **Moitié.** Par défaut, une valeur à égale distance de deux multiples s'éloigne de zéro : `2,5` donne `3`, `−2,5` donne `−3`. C'est l'arrondi d'une caisse, et celui de la fonction `ARRONDI` des tableurs (citée de mémoire). Le module `decimal` de Python l'appelle `ROUND_HALF_UP` ; la fonction `round()` de Python arrondit au pair, ce qui surprend.
- **Sens.** Après le pas : `vers le bas` (le multiple inférieur ou égal : `−2,1` donne `−3`), `vers le haut` (le multiple supérieur ou égal : `−2,1` donne `−2`), `au pair` (une moitié va au multiple pair : `2,5` donne `2`, `3,5` donne `4` ; l'arrondi bancaire).
- **Exact.** Le calcul se fait sur les chiffres, sans division arrondie : `l'arrondi de 1 ÷ 3 à 0,01` donne `0,33`, `l'arrondi de 7,024 à 0,05` donne `7,00`.
- **Décimales.** Le résultat porte les décimales du pas : `l'arrondi de 0,001 à 0,01` donne `0,00`, `l'arrondi de 1234 à 100` donne `1200`.
- **Liaison.** La valeur s'étend jusqu'à `à` : `l'arrondi de brut × 1,081 à 0,05` arrondit le produit. Le pas se lie comme un argument de calcul (§ 9.1) : `l'arrondi de x à 0,05 + 1` ajoute 1 au résultat ; un pas calculé se parenthèse, `à (p × 2)`. `du`, `au` se contractent comme ailleurs : `l'arrondi du prix au pas`.
- `l'arrondi de x à` sans pas : « L'arrondi se fait à un pas : « l'arrondi de x à 0,05 », « l'arrondi de x à 1 ». » Une valeur qui n'est pas un nombre : « On n'arrondit qu'un nombre, pas un texte. »
- Un calcul l'emploie librement : arrondir ne dépend que de ses arguments (§ 9.4).
- **Aucun mot réservé.** Un nom, un calcul ou un champ `arrondi` déclaré l'emporte : `L'arrondi d'un nombre vaut …` reste un calcul ordinaire (charte, art. 13.2). `vers`, `bas`, `haut`, `pair` ne comptent qu'après le pas.
- Vérification (12 octobre 2026) : 3 000 arrondis tirés au hasard, dans les quatre sens et sur dix pas (de `0,001` à `100`, dont `0,07` et `3`), donnent les mêmes résultats, décimales comprises, que `quantize` du module `decimal` de Python (`ROUND_HALF_UP`, `ROUND_FLOOR`, `ROUND_CEILING`, `ROUND_HALF_EVEN`).
- En forme compacte : `_arrondi(x ; 0,05)`, `_arrondi_bas(x ; 0,05)`, `_arrondi_haut(x ; 1)`, `_arrondi_pair(x ; 1)`.

### 3.5 Reste, partie entière, valeur absolue

```
Si le reste de n par 2 est nul, afficher « pair ».
Afficher la partie entière de 7,8 puis la valeur absolue de −3,50.     →  7 3,50
```

- `le reste de a par b` est ce qui reste de la division entière : `le reste de 17 par 5` vaut 2. **Il prend le signe du dividende** : `le reste de −7 par 3` vaut −1, `le reste de 7 par −3` vaut 1, comme le module `decimal` de Python (§ 3.2) et l'opérateur `%` de Java ; Python, sur ses entiers, suit au contraire le diviseur (`-7 % 3` vaut 2). Avec des décimales, exact : `le reste de 7,5 par 2` vaut 1,5. Par zéro : « Division par zéro. »
- Le dividende s'étend jusqu'à `par` : `le reste de n × 2 + 1 par 4`. Le diviseur se lie comme un argument de calcul (§ 9.1) : `le reste de 17 par 5 + 1` vaut 3 ; un diviseur calculé se parenthèse.
- `la partie entière de x` garde les chiffres avant la virgule : `la partie entière de −2,7` vaut −2, pas −3. C'est le sens courant (charte, principe 2), celui de `int()` en Python et de la conversion en `int` de Java ; les mathématiques françaises, elles, notent E(−2,7) = −3, comme la fonction `ENT` des tableurs en français (citée de mémoire). Ainsi `a = b × la partie entière de (a ÷ b) + le reste de a par b`.
- `la valeur absolue de x` enlève le signe : `la valeur absolue de −3,50` vaut 3,50.
- Ces deux-là se lisent comme le champ d'un nombre (§ 13.3) et se lient comme lui : `la valeur absolue de n − 20` soustrait 20 après ; `la valeur absolue de (n − 20)`, avant. Un nombre négatif s'écrit directement : `de −2,7`.
- Aucun mot réservé : un nom, un calcul ou un champ `reste`, `valeur absolue`, `partie entière` déclaré l'emporte (`Le reste vaut total − payé.` reste un nom).
- Vérification (12 octobre 2026) : 3 000 cas tirés au hasard, comparés au module `decimal` de Python (`%`, `to_integral_value(ROUND_DOWN)`, `abs`) : aucune différence.
- En forme compacte : `_reste(17 ; 5)`, `x.valeur_absolue`, `x.partie_entière`.

### 3.6 Hasard

```
Le dé vaut un nombre au hasard de 1 à 6.
Le gagnant vaut un élément au hasard parmi les inscrits.
```

- `un nombre au hasard de a à b` : un nombre entier, bornes comprises, dans un ordre quelconque, chacun avec la même chance. Les bornes sont des nombres entiers (au plus un milliard en valeur absolue).
- `un élément au hasard parmi les inscrits` : l'un des éléments d'un ensemble (§ 13.9), chacun avec la même chance ; un ensemble vide est une erreur.
- Seulement dans une action ou le programme : « Un calcul ne dépend pas du hasard : tire au hasard dans une action. » Un calcul rend toujours la même valeur pour les mêmes arguments (§ 9.4), et un exemple (§ 9.6) ne peut donc pas tirer au hasard.
- Le tirage est semé par l'horloge. Pour rejouer exactement une exécution, la variable d'environnement `GRYM_HASARD` fixe la graine : `GRYM_HASARD=42 grym lancer loto.grym`. Le générateur (xorshift64*, Sebastiano Vigna, 2014, cité de mémoire) convient à un tirage au sort d'association, pas à la cryptographie.
- Aucun mot réservé : `un nombre au hasard` et `un élément au hasard` ne valent qu'ainsi écrits.
- En forme compacte : `_hasard(1 ; 6)`, `_hasard_parmi(inscrits)`.

---

### 3.7 Racine carrée, plus grand, plus petit, et les nombres d'une liste *(2.95)*

```
Afficher la racine carrée de 2,25 puis la racine carrée de 2.            →  1,5 1,414213562373095048801688724
Afficher le plus grand de 3 et de 7 puis le plus petit de 3, de 7 et de 2.     →  7 2
Les notes valent la liste 4,5, 5 et 6.
Afficher la somme des notes puis la moyenne des notes.                  →  15,5 5,166666666666666666666666667
Afficher le plus grand élément des notes puis le plus petit élément des notes.   →  6 4,5
```

- **`la racine carrée de x`** suit la règle de la division (§ 3.2) : une racine finie reste exacte (`2,25` donne `1,5`, `100` donne `10`) ; une racine non finie est arrondie à 28 chiffres significatifs, au plus proche. D'un nombre négatif : « La racine carrée de −4 n'existe pas : un nombre négatif n'a pas de racine carrée réelle. » (sorte `nombre`, § 18.2). Vérification (6 novembre 2026) : 5 000 racines tirées au hasard, un quart de carrés parfaits, des entiers jusqu'à 10²⁰ et des décimaux de 10⁻⁴⁰ à 10³⁰, comparées à `Decimal.sqrt` du module `decimal` de Python en précision 28 : mêmes valeurs et mêmes exposants, zéros de queue compris (`√1,00` donne `1,0`, comme Python).
- **`le plus grand de a et de b`**, **`le plus petit de a et de b`** : deux valeurs ou plus (`de a, de b et de c`) : des nombres, des dates ou des heures, toutes de la même sorte, comparées comme par `<` (§ 5.1), qui n'ordonne pas les textes. Chaque argument se lit comme celui d'un calcul (§ 9), au plus court : un calcul dans un argument se parenthèse, `le plus grand de (x + 1) et de y` ; sans parenthèses, `le plus grand de x + 1 et de y` est refusé (« « et de » attendu »), comme un calcul déclaré refuse `la moyenne de x + 1 et de y`. Des sortes différentes : « Comparaison impossible entre un nombre et une date. »
- **Les nombres d'une liste ou d'un ensemble** (§ 13.9, § 13.10) : `la somme des notes`, `la moyenne des notes`, `le plus grand élément des notes`, `le plus petit élément des notes`. La somme d'une liste vide vaut 0 ; sa moyenne, son plus grand et son plus petit élément sont des erreurs de sorte `absence` (« La moyenne d'une liste vide n'existe pas. »), jamais 0, comme les agrégats de la base (§ 16.4). La somme et la moyenne veulent des nombres (« La somme d'une liste : « abc » est un texte, pas un nombre. ») ; le plus grand et le plus petit élément, des nombres, des dates ou des heures d'une même sorte, comme ci-dessus. La moyenne divise selon la règle de la division.
- Ces tournures ne se confondent pas avec celles de la base : `la somme des montants des factures conservées` (§ 16.4) reste un agrégat de la base, puisqu'après le nom vient `des` ; `le plus grand solde des clients conservés` n'a pas le mot `élément`.
- Elles se comportent comme des calculs que le langage déclare d'avance (§ 9) : un calcul de l'utilisateur nommé `racine carrée`, `plus grand` ou `plus petit` l'emporte. Aucun mot réservé.
- Un calcul les emploie librement.
- En forme compacte : `_racine(x)`, `_plus_grand(a ; b ; c)`, `_plus_petit(a ; b)`, `_somme(notes)`, `_moyenne(notes)`, `_plus_grand_élément(notes)`, `_plus_petit_élément(notes)`.

## 4. Afficher

```
Afficher le total.
Afficher « Total à payer : » puis le total.
```

- `Afficher` suivi d'un ou plusieurs éléments reliés par `puis`.
- Un élément est un texte ou une expression. Un texte est une valeur comme une autre : il se range dans un nom ou un champ (`Le nom vaut « Dupont ».`), se compare par égalité, mais ne se calcule pas.
- Les éléments s'affichent séparés par une espace, suivis d'un saut de ligne : `Afficher « Total : » puis 3.` produit `Total : 3`.

### 4.1 Style des nombres affichés

- Défaut : style suisse, apostrophe pour les milliers et virgule décimale (`1'234,50`). Un nombre négatif porte le signe `−` (U+2212) : `−12'000`.
- Un nombre entier de quatre chiffres ou moins ne se groupe pas : `2026`, `1020`, `−1500`. En français, un nombre qui numérote (une année, un numéro de compte ou de page) ne se sépare jamais, et le séparateur reste facultatif pour une quantité de quatre chiffres (Académie française, « Nombres », Questions de langue ; OQLF, Vitrine linguistique, « Espacements dans les nombres ») ; le langage ne sachant pas distinguer un numéro d'une quantité, il ne groupe aucun des deux. Avec des décimales, ou à partir de cinq chiffres, les milliers se groupent : `1'234,50`, `12'000`.
- Une phrase change le style pour tout le programme : `Les nombres s'affichent à la française.` (espace insécable U+202F, `1 234,50`), `à la suisse.`, ou `sans séparateur.` (`1234,50`).
- Elle se déclare au premier niveau, hors de toute formule et de tout bloc, une seule fois, avant le premier affichage. Sinon, erreur d'analyse.
- Le style ne concerne que l'affichage : la source (§ 12), la base (§ 16.1) et les comparaisons gardent la forme canonique. Une année s'affiche sans séparateur quel que soit le style (§ 14.5), une date garde sa forme (§ 14.4).
- En forme compacte : `_style _suisse`, `_style _française`, `_style _sans_séparateur`.

### 4.2 Largeur et saut de ligne

```
Afficher « Nom » sur 20 puis « Téléphone » sur 15, sans passer à la ligne.
Afficher nom du client sur 20 puis solde du client sur 10 à droite.
```

- `sur <largeur>` colle une largeur à une valeur et rend un texte : il se range dans un nom, se compare, s'affiche. La largeur se calcule (`sur l + 1`) ; elle vaut un nombre entier de 1 à 1000, sinon erreur d'exécution.
- Trop long, le texte est coupé à la largeur : une colonne qui déborde ruine l'alignement.
- Sens par défaut : à gauche pour un texte, une date, un booléen ou un fichier, à droite pour un nombre et une année. `à gauche` et `à droite`, après la largeur, forcent l'un ou l'autre. Seuls, ils sont une erreur d'analyse.
- La largeur compte des caractères, pas des octets : `« été » sur 4` occupe quatre colonnes.
- `sur` devient un mot réservé (§ 2.2) : un nom qui le contient s'écrit entre crochets.
- `, sans passer à la ligne` termine une phrase `Afficher` : la sortie s'arrête où elle s'arrête, sans saut de ligne ni espace ajoutée. La phrase suivante reprend au même endroit.
- Une ligne vide ne demande rien : `Afficher « ».` écrit un texte vide et son saut de ligne.
- En forme compacte : `nom _sur 20`, `solde _sur 10 _droite`, `_afficher x _sans_ligne`.

### 4.3 Effacer l'écran

```
Effacer l'écran.
```

- Efface l'écran et ramène le curseur en haut à gauche.
- Hors d'un terminal, la phrase n'écrit rien : une sortie redirigée dans un fichier ou dans un tube reste propre.
- Effet de bord, donc réservé aux actions. `effacer` ne peut pas commencer le nom d'une action.
- En forme compacte : `_effacer`.

### 4.4 Assembler des textes

```
Afficher « ( » suivi de l'année de l'œuvre suivi de « ) ».        →  (1747)
Le libellé vaut catalogue de l'œuvre suivi de « : » suivi du titre.
Afficher n puis « œuvre(s) » puis de nom du compositeur.         →  3 œuvre(s) d'Anton Webern
Afficher « plus ancien » puis que nom du compositeur.            →  plus ancien qu'Anton Webern
```

- `a suivi de b` colle deux valeurs en un texte, sans espace. Un nombre s'y écrit comme `Afficher` l'écrit (§ 4.1) : `« P- » suivi de 49999` donne `P-49'999`, `« P- » suivi de 2026` donne `P-2026`. Le texte obtenu est celui qu'un lecteur voit, aussi dans une clé rangée en base ; le chercher demande la même écriture (`dont la pièce est « P-49'999 »`). Le résultat se range, se compare, s'affiche, comme celui de `sur` (§ 4.2). `suivi du`, `suivi de la`, `suivi d'` suivent les contractions et élisions ordinaires (§ 5.2).
- Chaque valeur s'écrit comme `Afficher` l'écrirait : un nombre au style du programme (§ 4.1), une date au format suisse, une année sans séparateur, `vrai` ou `faux`. Une valeur absente est une erreur (§ 16.9).
- `suivi de` lie moins fort que les opérations et que `sur` : `« x » suivi de a + 1`, `« x » suivi de nom sur 10`. Il ne rend jamais une condition : `Si « a » suivi de « b », …` est une erreur d'analyse.
- **Élision.** En tête d'un élément d'`Afficher`, ou juste après `suivi de`, `de x` et `que x` écrivent `de`/`d'` et `que`/`qu'` selon la première lettre du texte de `x` : élision devant une voyelle, accentuée ou non, `œ`, `æ` ou `y`, jamais devant un `h`. Le h muet (« d'Hélène ») et le h aspiré (« de Haendel ») ne se distinguent pas par l'orthographe : deviner romprait le déterminisme (charte, art. 4). `de 1` s'écrit `de 1`.
- `suivi` devient un mot réservé (§ 2.2), sauf après `de`, `du`, `des` ou `d'`, où il est un mot de nom *(2.12)* : `un tarif de suivi (nombre)`, `Afficher le tarif de suivi du soin suivi de « CHF ».` Aucune expression ne finit par `de` : l'opérateur n'y est jamais possible, et la lecture reste sans ambiguïté. Le formatage n'y met pas de crochets, la coloration de l'atelier ne le colore pas.
- Joindre les éléments d'un ensemble : `les couleurs jointes par « , »` (§ 13.9).
- En forme compacte : `a _suivi b`, `_de x`, `_que x`.

### 4.5 Travailler un texte

```
Afficher la longueur du nom.                                  →  13 (« Élodie Müller »)
Afficher le nom en majuscules.                                →  ÉLODIE MÜLLER
Pour chaque membre conservé dont le nom contient « mül », par nom :
Si le code commence par « CH », …
Le prix vaut le texte saisi en nombre.
```

- `la longueur de t` compte les caractères d'un texte, pas ses octets : `la longueur de « été »` vaut 3. C'est le seul champ d'un texte, comme `année` pour une date (§ 14.5) ; il ne réserve rien : un champ ou un calcul `longueur` déclaré l'emporte.
- `t en majuscules`, `t en minuscules` rendent un texte. Les lettres accentuées du français et des langues voisines changent avec les autres : `« Müller » en majuscules` donne `MÜLLER`, `« ŒUVRE » en minuscules` donne `œuvre`.
- `t en nombre`, `en nombre entier`, `en date`, `en année`, `en vrai ou faux` lisent un texte comme une réponse de l'utilisateur (§ 17) : virgule décimale et séparateurs de milliers (§ 1.2), `jour.mois.année` (§ 14.1), `oui`, `non`, `vrai`, `faux`. Les espaces de début et de fin tombent. Un texte qui ne convient pas est une erreur à l'exécution, avec le message de la relance : « « abc » n'est pas un nombre. » ; un texte vide : « Un texte vide n'est pas un nombre. »
- `en …` suit une valeur et se lie comme elle : `code en nombre + 1` ajoute 1 au nombre lu ; `« a » suivi de b en majuscules` ne met que `b` en majuscules ; `(« a » suivi de b) en majuscules`, les deux. Seul un texte se convertit : « « en majuscules » s'applique à un texte, pas à un nombre. »
- **Retirer** *(2.64)*. `t sans « . »` rend le texte `t` sans aucune occurrence de `« . »`, de gauche à droite : `« 756.1234.5678.97 » sans « . »` donne `7561234567897`. Il se lie comme `en …`, à la valeur qui le précède, et se répète : `le numéro sans « . » sans " "`. Il est suivi d'un texte, d'un nom entre crochets ou d'un nom avec son article ; un « sans » suivi d'autre chose reste un mot de nom (« un repas sans gluten »). Les refus : « « sans » s'applique à un texte, pas à un nombre. » ; « « sans » retire un texte vide : rien à retirer. » En forme compacte : `t _sans « . »`.
- **Contenir.** `t contient p`, `t commence par p`, `t finit par p`, et leurs négations `ne contient pas`, `ne commence pas par`, `ne finit pas par`, sont des conditions. Elles ne tiennent compte **ni des accents ni de la casse**, comme le tri des recherches (§ 16.4) : `« Élodie Müller » contient « MUL »` est vrai. Un texte contient le texte vide. L'égalité, elle, reste exacte (`=`). Deux textes sont attendus : « « contient » compare deux textes, pas un nombre. »
- Dans une condition `dont` (§ 16.4), elles s'appliquent à un champ `(texte)` : `dont le nom contient « mül »`, `dont la ville ne commence pas par « b »`. Un champ absent n'est jamais retenu (§ 16.9). Un autre type : « « âge » : contient, commence par et finit par s'appliquent à un texte. »
- Aucun mot réservé : `contient`, `commence`, `finit` et `en` ne comptent qu'après une valeur, et seulement suivis de ce qu'ils annoncent (`par`, `pas`, un texte, une cible de conversion). L'aide à la saisie (§ 8) propose `contient`, `commence par`, `finit par` après une valeur dont le type texte est connu à l'analyse.
- **Remplacer** *(2.81)*. `t en remplaçant « a » par « b »` rend le texte `t` où chaque occurrence de `« a »` devient `« b »`, de gauche à droite, sans revenir sur ce qui vient d'être remplacé : `« CQ CQ de HB9 » en remplaçant « CQ » par « appel »` donne `appel appel de HB9`. Il se lie comme `sans`, à la valeur qui le précède, et se répète : `t en remplaçant « - » par « » en remplaçant « a » par « x »`. Un remplaçant vide retire, comme `sans`. Les refus : « « en remplaçant » s'applique à un texte, pas à un nombre. » ; « « en remplaçant » cherche un texte vide : rien à remplacer. » ; « « par » attendu : « t en remplaçant « a » par « b » ». » En forme compacte : `t _remplaçant « a » _par « b »`.
- **Les mots d'un texte** *(2.81)*. `Pour chaque mot parmi les mots de t :` parcourt les mots de `t` dans l'ordre, **doublons compris** (« CQ CQ » fait deux tours, ce qu'un ensemble aurait fusionné, § 13.9) ; le texte est lu une fois, au début. Les mots sont séparés par les espaces, tabulations, retours à la ligne et espaces insécables (U+00A0, U+202F) ; la ponctuation reste collée au mot (« Neuf, ») et `sans « , »` l'ôte ; les marques invisibles de la relecture d'une transcription (U+2062 à U+2064, § 15.8) sont retirées des mots. `le nombre de mots de t` les compte. `de`, `d'` et `du` s'écrivent comme ailleurs : `les mots du texte`. Un texte vide n'a aucun mot. `les mots de t` est une liste (§ 13.10) : `Les morceaux valent les mots de t.` En forme compacte : `_pour_chaque m _parmi _mots t`, `_nombre_mots t`.
- Extraire un morceau d'un texte, le couper : § 4.6.
- En forme compacte : `t.longueur`, `t _en_majuscules`, `t _en_minuscules`, `t _en_nombre`, `t _en_nombre_entier`, `t _en_date`, `t _en_année`, `t _en_vrai_ou_faux` ; `t _inclut p`, `t _commence p`, `t _finit p`, et `_non (t _inclut p)`. `_contient` reste réservé au champ multiple (§ 16.13).

### 4.6 Découper un texte *(2.90)*

```
La réponse vaut « FA014074000; ».
Afficher (la réponse après « FA » avant « ; ») en nombre.                 →  14'074'000
Afficher les caractères 3 à 11 de la réponse.                             →  014074000
Afficher les 2 premiers caractères de la réponse puis le dernier caractère de la réponse.   →  FA ;
Afficher le 3e caractère de « été » puis la position de « 4 » dans la réponse.               →  é 5
Les cellules valent les morceaux de « Ana;Bob;;Zoé » séparés par « ; ».
Afficher le nombre de cellules puis le 4e élément des cellules.           →  4 Zoé
```

- Un **caractère** est une lettre, un chiffre, un signe, une espace : `« été »` en compte 3, comme `la longueur de` (§ 4.5). Le premier a le rang 1.
- **Un caractère.** `le 3e caractère de t`, `le 1er caractère de t`, `le premier caractère de t`, `le dernier caractère de t`, `le caractère de rang k de t`, comme les éléments d'une liste (§ 13.10). Le rang suit les règles du § 13.10 : un entier d'au moins 1. Au-delà : « Pas de 20e caractère : le texte n'en a que 12. » ; dans un texte vide : « Pas de premier caractère : le texte est vide. »
- **Un début, une fin.** `les 5 premiers caractères de t`, `les 3 derniers caractères de t`. Le nombre se calcule (`les n premiers caractères de t`, l'expression finit avant `premiers` ou `derniers`) ; c'est un entier positif ou nul, sinon : « Un nombre de caractères est un nombre entier positif ou nul, pas 2,5. » Un texte plus court se rend entier : `les 5 premiers caractères de « abc »` vaut `abc`, comme la fonction `GAUCHE` d'un tableur (citée de mémoire). C'est la règle de `sur` (§ 4.2), qui coupe sans se plaindre.
- **Un morceau.** `les caractères 3 à 7 de t` : du 3e au 7e, bornes comprises. Les bornes se calculent (`les caractères a à a + 3 de t` ; la première finit avant `à`, la seconde avant `de`, un calcul dans la seconde se parenthèse). Il faut 1 ≤ a ≤ b ≤ la longueur : « Les caractères 7 à 3 : la première borne dépasse la seconde. », « Pas de 20e caractère : le texte n'en a que 12. »
- **Avant, après.** `t après « FA »` rend ce qui suit la **première** occurrence de `« FA »` ; `t avant « ; »`, ce qui précède sa première occurrence. Ils se lient comme `sans` (§ 4.5), à la valeur qui précède, et s'enchaînent de gauche à droite : `r après « FA » avant « ; »` vaut `(r après « FA ») avant « ; »`. Ils sont suivis d'un texte, d'un nom entre crochets ou d'un nom avec son article. Absent du texte, le repère est une erreur, rattrapable par `Essayer` : « « ; » n'est pas dans « FA0140 » : vérifie-le d'abord avec « contient ». » Un programme qui découpe une réponse mal formée s'arrête au lieu de calculer sur un morceau faux (charte, art. 8). Un repère vide : « « après » cherche un texte vide : rien à trouver. »
- **Trouver.** `la position de « 4 » dans t` : le rang du premier caractère de la première occurrence, ou 0 si elle n'y est pas. 0 n'est pas une erreur : c'est la réponse à « où ? », et `Si la position de « x » dans t est nulle` se lit bien.
- **Couper.** `les morceaux de t séparés par « ; »` est une liste (§ 13.10) : les textes entre les séparateurs, dans l'ordre, **vides compris** (`« a;;b »` en donne trois, le deuxième vide), comme une ligne de tableur. Un texte vide donne un morceau vide ; un texte sans séparateur, un morceau, lui-même. Elle se range dans une liste : `Les cellules valent les morceaux de ligne séparés par « ; ».` Un séparateur vide : « « séparés par » attend un séparateur, pas un texte vide. »
- **Comparaison exacte.** `après`, `avant`, `la position de` et `séparés par` cherchent le texte tel qu'il est écrit, casse et accents compris, comme `=` et `en remplaçant`. Seuls `contient`, `commence par` et `finit par` les ignorent (§ 4.5).
- Sur autre chose qu'un texte : « « le 3e caractère de » s'applique à un texte, pas à un nombre. », de même pour chaque tournure ; une valeur absente est l'erreur habituelle (§ 16.9).
- Un calcul les emploie librement.
- Aucun mot réservé : `caractère`, `caractères`, `premiers`, `derniers`, `après`, `avant`, `position`, `morceaux`, `séparés` ne comptent qu'ainsi placés ; un nom, un calcul ou un champ déclaré l'emporte.
- En forme compacte : `_caractère(t ; 3)`, `_premier_caractère(t)`, `_dernier_caractère(t)`, `_premiers_caractères(t ; 5)`, `_derniers_caractères(t ; 3)`, `_caractères(t ; 3 ; 7)`, `t _après « FA »`, `t _avant « ; »`, `_position(« x » ; t)`, `_les c << _morceaux(t ; « ; »)`. Un rang constant revient en chiffres (`le 3e caractère`), un rang calculé en `le caractère de rang … de …`.

### 4.7 Le texte suivant *(2.93)*

```
Le rappel vaut le texte suivant :
    Bonjour {prénom du patient},

    Votre séance du {date de la séance en toutes lettres} à {heure de la séance}
    est confirmée. Le tarif reste de {tarif de la séance} CHF.

    Les {{accolades}} s'écrivent doublées.
Afficher le rappel.
```

- `le texte suivant :` en fin de ligne ouvre un **texte sur plusieurs lignes** : les lignes qui suivent, plus indentées, en sont le contenu, tel qu'écrit. Apostrophes, deux-points, guillemets, ponctuation : rien n'y est un mot du langage. Le texte finit à la première ligne non vide qui n'est plus indentée ; il remplace le point final de la phrase.
- Le retrait de la **première** ligne est celui du texte : il est retiré de chaque ligne, et un retrait plus grand reste (`      Merci.` sous des lignes à quatre espaces garde deux espaces). Les lignes vides du milieu restent ; celles de la fin tombent. Les lignes se séparent par un retour à la ligne, la dernière n'en a pas.
- **Les accolades.** `{expression}` insère une valeur, écrite comme `suivi de` l'écrit (§ 4.4) : un nombre au style du programme, une date au format suisse, une valeur absente est une erreur. L'expression tient sur une ligne : « Une expression entre accolades tient sur une ligne : « } » manque. ». Une seule expression : « « } » attendu : une seule expression entre accolades, « {prénom du patient} ». ». `{{` et `}}` écrivent une accolade ; une accolade fermante seule est une erreur.
- C'est une valeur comme une autre : elle se range (`vaut`, `devient`), s'affiche, se passe en argument. Elle finit toujours sa phrase : `Afficher le texte suivant :` puis le bloc.
- Sans lignes plus indentées : « « le texte suivant : » attend ses lignes à la suite, plus indentées. »
- `suivant`, après `le texte` et avant `:` en fin de ligne, ne compte qu'ainsi : une variable `texte` garde son sens (`Selon le texte :`). Aucun mot réservé.
- Le texte se compile comme une suite de `suivi de` : rien de nouveau dans la machine.
- Un texte entre guillemets tient toujours sur une ligne (§ 1.5) : le texte suivant est la seule manière d'écrire plusieurs lignes.
- En forme compacte : `_le rappel << _texte_suivant`, puis le même bloc, plus indenté, les accolades en forme compacte (`{patient.prénom}`) ; pas de `_fin`.

## 5. Décider

### 5.1 Comparaisons

Une comparaison s'écrit avec des mots ou avec un symbole (§ 1.6). L'arbre retient la forme écrite.

```
Si le total est supérieur à 100, …
Si le total > 100, …
```

| Tournure | Sens |
|----------|------|
| `est égal à` | = |
| `est différent de` | ≠ |
| `est inférieur à`, `est supérieur à` | <, > |
| `est inférieur ou égal à`, `est supérieur ou égal à` | ≤, ≥ |
| `est positif` | > 0 |
| `est négatif` | < 0 |
| `est nul` | = 0 |
| `est vrai`, `est faux` | valeur d'un booléen |

- **Sens courant, pas sens mathématique.** En mathématiques françaises, « positif » inclut zéro. GrymoiR suit la langue courante (charte, principe 2) : un solde nul n'est ni positif ni négatif.
- **Négation** : `n'est pas` devant toute tournure. `Si le solde n'est pas nul, …`
- **Accord** : l'adjectif s'accorde avec un nom de genre connu (`la quantité est positive`, `supérieure ou égale à`). Un désaccord produit une erreur qui donne la forme juste. Un adjectif accordé fixe le genre d'un nom encore libre, comme un article (§ 2.3). Avec un sujet qui n'est pas un nom seul (`3 est positif`, `x + 1 est supérieur à 2`), les deux formes sont admises.
- Une comparaison porte sur les valeurs : `1,0 = 1` est vrai. Seuls deux nombres se comparent par ordre (`<`, `≤`, `>`, `≥`) ; l'égalité compare aussi deux booléens.
- Une comparaison ne s'enchaîne pas : `1 < x < 3` est une erreur. Écrivez `1 < x et x < 3`.

### 5.2 Contractions

- `à le` s'écrit `au`, `de le` s'écrit `du` : `est inférieur au prix`, `est différent du total`. L'article contenu dans la contraction se vérifie comme un article écrit (§ 2.3).
- `à le` et `de le` sont des erreurs avec correction : « « à le » s'écrit « au ». »
- `à la`, `à l'`, `de la`, `de l'`, `d'` s'écrivent normalement.

### 5.3 Et, ou

- `et`, `ou` relient deux conditions. `ou` est inclusif.
- Évaluation en court-circuit : si le premier membre suffit, le second n'est pas calculé. `Si x ≠ 0 et 1 ÷ x > 1` ne divise jamais par zéro.
- **Mélanger `et` et `ou` sans parenthèses est une erreur.** Écrivez `(A et B) ou C` ou `A et (B ou C)`.
- Un calcul seul n'est ni vrai ni faux : `x et 3` est une erreur.

### 5.4 Si

Forme courte, une phrase simple après la virgule :

```
Si le solde est négatif, afficher « Relance ».
Si le total > 100, afficher « grand ». Sinon, afficher « petit ».
```

Forme en bloc, ouverte par deux-points :

```
Si le total est supérieur à 100 :
    Le rabais devient 10.
    Le total devient total − rabais.
Sinon si le total est supérieur à 50 :
    Le rabais devient 5.
Sinon :
    Le rabais devient 0.
```

- La condition doit pouvoir être vraie ou fausse : une comparaison, `et`, `ou`, `vrai`, `faux`, ou un nom qui contient un booléen. `Si 3 + 4, …` est une erreur d'analyse ; un nom qui contient un nombre produit une erreur à l'exécution.
- La forme courte n'accepte qu'une phrase simple (`Le`, `La`, `L'`, `Afficher`).
- Un bloc commence à la ligne suivant les deux-points, plus indenté que la ligne du `Si`. Ses phrases s'alignent sur la même colonne ; une ligne moins indentée termine le bloc. Une ligne plus indentée sans bloc ouvert est une erreur. Les remarques échappent à la règle.
- `Sinon` s'aligne sur son `Si`, ou suit une forme courte sur la même ligne. `Sinon si` enchaîne une nouvelle condition.
- La première phrase du programme fixe la colonne de référence.
- **Portée** : un nom créé dans un bloc disparaît à la fin du bloc. Pour l'utiliser ensuite, créez-le avant le `Si`, puis modifiez-le dans les branches.
- Dans la boucle interactive, une ligne terminée par `:` ouvre un bloc ; une ligne vide le termine.

### 5.5 Booléens

- `vrai` et `faux` sont des valeurs : `Le test vaut le total > 100.` Elles s'affichent `vrai` et `faux`.
- Un booléen ne se calcule pas : `vrai + 1` est une erreur d'exécution.

## 6. Grammaire formelle (EBNF)

```
programme    = bloc ;
bloc         = { phrase } ;                      (* alignées sur une même colonne, § 5.4 *)
phrase       = création | modification | affichage | si | remarque
             | calcul | action | rendre | appel-action
             | tant-que | répéter | pour-chaque | sortir | passer | selon
             | classe | modif-champ | essayer | utiliser ;
utiliser     = "Utiliser" texte "." ;          (* § 21 : en tête du fichier, après les remarques *)
essayer      = "Essayer" ":" bloc-indenté "En" "cas" "d'" "échec" branche ;   (* § 18 *)
classe       = un nom "a" ":" un nom { "," un nom } "."
             | un nom "est" un ( nom | "chose" ) [ adjectifs ] "."
               [ un nom "a" ":" un nom { "," un nom } "." ] ;                  (* même classe *)
aptitude     = "une" "chose" adjectif [ "(" adjectif ")" ] "a" ":" un nom { "," un nom } "." ;
adjectifs    = adjectif { ( "," | "et" ) adjectif } ;
modif-champ  = article nom de base "devient" valeur ( "." | ":" initialisation ) ;
initialisation = { article nom "vaut" valeur "." } ;      (* indentée, après « un nouveau … : » *)
tant-que     = "Tant" "que" valeur branche ;
répéter      = "Répéter" expression "fois" branche ;
pour-chaque  = "Pour" "chaque" nom ( de | "du" ) expression à expression
               [ "par" "pas" de expression ] branche ;
sortir       = "Sortir" "de" "la" "boucle" "." ;
passer       = "Passer" "au" "tour" "suivant" "." ;
selon        = "Selon" valeur ":" { cas } [ autrement ] ;   (* cas et autrement indentés, alignés *)
cas          = "Cas" condition-de-cas { "ou" condition-de-cas } branche ;
autrement    = "Autrement" branche ;
condition-de-cas = de expression à expression | relation | expression ;
calcul       = article nom paramètres-de ( "vaut" valeur "." | ":" bloc-indenté ) ;
paramètres-de = "d'" un nom { "et" "d'" un nom } ;
action       = "Pour" nom [ un nom { "et" un nom } ] ":" bloc-indenté ;
un           = "un" | "une" ;
rendre       = "Rendre" valeur "." ;
appel-action = nom-d-action [ comparaison { "et" comparaison } ] "." ;
création     = article nom "vaut" valeur "." ;
modification = article nom "devient" valeur "." ;
affichage    = "Afficher" élément { "puis" élément } "." ;
si           = "Si" valeur branche [ "Sinon" ( si | branche ) ] ;
branche      = "," phrase-simple | ":" bloc-indenté ;
remarque     = "Remarque" ":" texte-libre fin-de-ligne ;
élément      = texte | valeur ;
valeur       = logique { "et" logique } | logique { "ou" logique } ;
logique      = "vrai" | "faux" | "(" valeur ")" | comparaison ;
comparaison  = expression [ comparateur expression | [ "n'" ] "est" [ "pas" ] relation ] ;
comparateur  = "=" | "≠" | "<" | ">" | "≤" | "≥" ;
relation     = ( "égal" | "égale" ) à expression
             | ( "différent" | "différente" ) de expression
             | ( "inférieur" | "inférieure" | "supérieur" | "supérieure" )
               [ "ou" ( "égal" | "égale" ) ] à expression
             | "positif" | "positive" | "négatif" | "négative" | "nul" | "nulle"
             | "vrai" | "vraie" | "faux" | "fausse" ;
à            = "à" | "au" ;
de           = "de" | "d'" | "du" ;
expression   = morceau { "suivi" ( "de" | "d'" | "du" ) morceau } ;   (* § 4.4 *)
morceau      = [ "de" | "que" ] somme [ "sur" somme [ "à" ( "gauche" | "droite" ) ] ] ;
somme        = terme { ( "+" | "−" ) terme [ unité ] } ;             (* unité : § 14.2 *)
unité        = "jour" | "jours" | "semaine" | "semaines" | "mois" | "an" | "ans" | "année" | "années" ;
terme        = unaire { ( "×" | "÷" ) unaire } ;
unaire       = "−" unaire | puissance ;
puissance    = base [ "^" unaire ] ;
base         = nombre | texte | date | "aujourd'hui" | fichier | nouveau | champ
fichier      = "le" "fichier" ( texte | "(" expression ")" ) ;
enregistrer  = "Enregistrer" expression "dans" valeur "." ;
gagner       = "Les" nom de base ( "gagnent" | "perdent" ) expression "." ;   (* nom : un champ multiple, § 16.13 *)
multiple     = "des" nom [ "(" nom ")" ] "(" nom ")" ;                          (* dans une entité : singulier, type *)
             | [ article ] ( nom | "[" nom "]" ) [ arguments ] | "(" expression ")" ;
nouveau      = ( "un" ( "nouveau" | "nouvel" ) | "une" "nouvelle" ) nom [ "saisi" | "saisie" ] ;   (* § 19 *)
champ        = [ article ] nom de base ;          (* nom : un champ déclaré dans une classe *)
arguments    = de unaire { "et" de unaire } ;   (* seulement après le nom d'un calcul *)
article      = "le" | "la" | "l'" ;
```

Limites de cette notation :

- `nom` se résout par plus longue correspondance parmi les noms déclarés (§ 2.2), ce que l'EBNF n'exprime pas.
- `"n'" "est" "pas"` : l'élision et `pas` vont ensemble ; `n'est` sans `pas` est une erreur.
- Une parenthèse ouvre un groupe logique si elle contient, à son premier niveau, une comparaison, `et`, `ou`, `vrai` ou `faux` ; sinon elle groupe un calcul.
- L'alignement des blocs et la règle « pas de mélange de `et` et `ou` » ne s'expriment pas en EBNF (§ 5.3, § 5.4).
- Qu'un nom désigne une variable, un calcul ou une action dépend des déclarations qui précèdent (§ 9.5).
- Les équivalents ASCII des opérateurs (§ 1.4) sont traités au lexer.

---

## 7. Messages d'erreur de référence

| Situation | Message |
|-----------|---------|
| Nom inconnu proche d'un nom connu | « `totl` inconnu, voulais-tu `total` ? » |
| `vaut` sur un nom existant | « `total` existe déjà (ligne 3). Pour le modifier, écris : Le total devient … » |
| `devient` sur un nom inconnu | « `total` n'existe pas. Pour le créer, écris : Le total vaut … » |
| Genre contradictoire | « `total` est masculin (déclaré ligne 3) » |
| Division par zéro | « Division par zéro. » (position de l'opérateur `÷`) |
| Résultat démesuré | « Nombre trop grand : un résultat est limité à 1000 chiffres. » |
| Exposant non entier | « Exposant non entier : en v0.1, la puissance n'accepte qu'un exposant entier (2 ^ 3, 2 ^ −1). » |
| Phrase sans point final hors boucle interactive | « Point final manquant (ligne 5) » |
| Jeton inattendu | « « 2 » inattendu, attendu : un opérateur ou un point final. » |
| Article sans nom | « Nom attendu après « le ». » |
| Remarque mal placée | « Une remarque doit commencer une ligne : passe à la ligne avant « Remarque : ». » |
| Parenthèse non refermée | « Parenthèse fermante manquante : la parenthèse ouverte ligne 2, colonne 11 n'est pas refermée. » |
| Mot réservé dans un nom | « « et » est un mot réservé : pour l'utiliser dans un nom, écris [frais de port et emballage]. » |
| Accord | « « quantité » est féminin (déclaré ligne 1) : écris « positive ». » |
| Contraction | « « à le » s'écrit « au ». » |
| Mélange de `et` et `ou` | « « et » et « ou » mélangés sans parenthèses : écris « (A et B) ou C » ou « A et (B ou C) » selon le sens voulu. » |
| Condition arithmétique | « Condition attendue après « Si » : une comparaison, par exemple « Si le total est supérieur à 100 ». Un calcul seul n'est ni vrai ni faux. » |
| Condition non booléenne (exécution) | « Condition ni vraie ni fausse : la valeur est un nombre. » |
| Indentation | « Indentation inattendue : seul un bloc ouvert par « : » s'indente. » |
| `Sinon` mal placé | « « Sinon » doit être aligné sur son « Si ». » |
| Variable lue dans un calcul | « « taux » n'est pas visible dans un calcul : un calcul ne voit que ses paramètres. Passe la valeur en paramètre. » |
| Nombre d'arguments | « « carré » attend 1 paramètre, 2 donnés. » |
| Calcul sans `Rendre` final | « Le calcul « valeur » doit se terminer par « Rendre … ». » |
| Affichage dans un calcul | « Un calcul n'affiche rien : il rend une valeur. Pour afficher, écris une action. » |
| Compteur modifié | « « mois » est le compteur de la boucle : il avance tout seul et ne se modifie pas. » |
| Nombre de tours (exécution) | « Nombre de tours invalide : un entier positif ou nul est attendu, pas 2,5. » |
| Pas nul (exécution) | « Pas nul : la boucle ne finirait jamais. » |
| Hors d'une boucle | « « Sortir de la boucle » hors d'une boucle. » |
| Cas après Autrement | « « Autrement » vient après tous les cas. » |
| Interruption (exécution) | « Interrompu (Ctrl+C). » |
| Année hors du champ (analyse ou exécution) | « Le champ « composition » attend une année (de 1 à 9999), pas 2,5. » |
| Calcul sur une année (exécution) | « On n'additionne pas deux années. » |
| Entité nommée comme un type | « « année » est un type : une entité ne peut pas porter ce nom. » |
| Date impossible | « Le 31 février 2026 n'existe pas. » |
| Date mal écrite | « Date mal formée « 21.9.26 » : écris jour.mois.année, l'année sur quatre chiffres (21.09.2026). » |
| Deux dates additionnées (exécution) | « On n'additionne pas deux dates. » |
| Décalage non entier (exécution) | « Une date se décale d'un nombre entier de jours. » (de semaines, de mois, d'années) |
| Durée seule | « Une durée ne se range pas seule : elle s'ajoute à une date ou s'en retire, « d + 3 mois », « d − 1 an ». » |
| Accord d'une unité | « Accord : « 2 ans ». » |
| Durée sur autre chose qu'une date (exécution) | « Des mois s'ajoutent à une date, pas à un nombre. » ; « Une année se décale d'années, pas de mois. » |
| Hors du calendrier (exécution) | « Date hors du calendrier : du 01.01.0001 au 31.12.9999. » |
| `aujourd'hui` dans un calcul | « Un calcul ne dépend pas du jour : passe la date en paramètre. » |
| Fichier absent (exécution) | « Fichier « photos/ana.jpg » introuvable ou illisible. » |
| Écrasement (exécution) | « « copie.jpg » existe déjà : il n'est jamais écrasé. » |
| Fichier lu dans un calcul | « Un calcul ne lit pas le disque : lis le fichier dans une action. » |
| Champ d'entité sans type | « Type attendu entre parenthèses : « un nom (texte) ». » |
| Type d'une valeur (analyse ou exécution) | « Le champ « solde » attend un nombre, pas un texte. » |
| Lien vers une classe ordinaire | « « personne » n'est pas une entité : un lien pointe vers une entité conservée. » |
| Objet incomplet (exécution) | « Le champ « nom » n'a pas de valeur : un client incomplet ne se conserve pas. » |
| Lien vers un objet non conservé | « Le champ « parrain » désigne un client qui n'est pas conservé : conserve-le d'abord. » |
| Unicité | « « licence » est unique : un autre client conservé a déjà « A-1 ». » |
| Suppression refusée | « Ce client est encore désigné par le champ « parrain » de 2 clients. » |
| Champ nouveau sans valeur de départ | « « pays » est nouveau, et 12 clients sont déjà conservés : donne-lui une valeur de départ, après son type : « (texte), … au départ ». » |
| Champ retiré | « « pays » a disparu de « client » : 12 valeurs conservées seraient perdues. … » |
| Recherche sans résultat unique | « 3 clients conservés répondent à cette condition : « le client conservé dont … » en attend un seul. » |
| Condition « dont » mal formée | « Une condition « dont » compare un champ du client à une valeur : « dont le solde est négatif ». » |
| Classe inconnue | « Classe « fournisseur » inconnue. » |
| Champ hérité redéclaré | « « nom » est déjà un champ hérité de « personne ». » |
| Version en double | « « saluer » existe déjà pour « personne ». » |
| Aucune version (exécution) | « Aucune version de « saluer » pour une ville. » |
| Accord d'une aptitude | « Accord : « personne horodatée ». » |
| Conflit d'aptitudes | « « décrire » est défini par les aptitudes « horodatée » et « numérotée » de « membre » : définis sa version pour « membre » afin de trancher. » |
| Accord de « nouveau » | « « client » est masculin : écris « un nouveau client ». » |
| Champ modifié avec `vaut` | « Un champ se modifie avec « devient » : « Le solde du client devient … ». » |
| Champ absent (exécution) | « Un client n'a pas de champ « montant ». » |
| Champ vide (exécution) | « Le champ « solde » n'a pas de valeur. » |
| Pas un objet (exécution) | « « nom » : la valeur n'est pas un objet, c'est un nombre. » |
| Champ multiple lu comme une valeur | « « genres » est un champ multiple, pas une valeur : il se lit avec « Pour chaque genre de … » ou « le nombre de genres de … », et change avec « Les genres de … gagnent … ». » |
| Champ multiple facultatif, unique, avec valeur de départ | « « genres » est multiple : il n'est pas facultatif, un ensemble vide lui suffit. » |
| Objet non conservé qui gagne (exécution) | « Une œuvre qui n'est pas conservée ne gagne rien : ses « genres » vivent dans la base. Conserve-la d'abord. » |
| Élément de la corbeille (exécution) | « Le champ « genres » gagnerait un genre supprimé : rétablis-le d'abord. » |
| Ambiguïté d'une relation (exécution) | « Plusieurs champs relient une œuvre à une personne : « compositeur » et « interprètes ». Précise avec « dont … est le compositeur » ou « dont … est parmi les interprètes ». » |
| `Essayer` sans son bloc d'échec | « « Essayer » attend son « En cas d'échec », aligné sur lui : une erreur ne passe jamais sous silence. » |
| Motif hors du bloc d'échec | « « le motif de l'échec » ne s'emploie que dans un bloc « En cas d'échec ». » |
| Récursion sans fin (exécution) | « Trop d'appels imbriqués : plus de 1000. Une formule s'appelle-t-elle sans fin ? » |

Chaque message est précédé du fichier, de la ligne et de la colonne (charte, art. 8) : `facture.grym:7:18 : erreur : Division par zéro.`

---

## 8. Suites attendues

À chaque position, l'analyseur calcule l'ensemble exact des suites valides (charte, art. 9).

- Catégories : début de phrase (`Le`, `La`, `L'`, `Afficher`, `Si`, `Pour`, `Remarque :`, et les actions déclarées, avec une majuscule), nombre, nom déclaré, nouveau nom, parenthèse, négation, texte, `vrai` et `faux`, opérateurs, comparaisons (`est`, `n'est pas`, symboles), `et` et `ou`, parenthèse fermante, `vaut` et `devient`, `,` et `:` après une condition, `puis`, point final.
- Après `est`, les tournures accordées au genre du sujet (`supérieure à`, `positive`…) ; après `supérieur`, `à`, `au` ou `ou`.
- `Tant que`, `Répéter`, `Pour chaque`, `Selon` et `Essayer` en début de phrase ; dans une boucle, `Sortir de la boucle` et `Passer au tour suivant`.
- Après le nom d'un calcul, `de` ou `du`. Dans un calcul, seuls ses paramètres, ses noms locaux et les formules sont proposés.
- S'y ajoutent les mots qui prolongent un nom composé déclaré : après `prix`, `unitaire` si `prix unitaire` existe.
- Dans un écran (§ 22) *(1.85)* : au début d'un élément, les tournures qui le commencent (`un bouton «`, `le texte «`, `la liste des`, `le graphique des`, `l'image «`, `l'image de`, les blocs), selon le bloc ouvert : dans un menu, `un bouton «` seulement ; dans `les onglets`, `l'onglet «` seulement ; `le menu «` au premier niveau seulement. Un premier mot déjà écrit (`le`, `l'`, `un`…) fait proposer la suite de chaque tournure. Après la virgule d'un élément, ses états, accordés à son genre (`facultative`, `active si`, `visible si`…), et, dans un bloc libre, ce qui reste de sa place et de sa taille (`à`, `large de`, `haut de`). Après la virgule d'une liste, `par` et `avec` ; d'un graphique, `par`, `avec la somme de`, `en secteurs` ; après `par`, les champs de l'entité *(1.90)*.
- Premier usage : les messages d'erreur (« `« 2 » inattendu, attendu : un opérateur ou un point final.` »).
- Second usage : l'aide à la saisie. Les suites sont calculées à la position du curseur ; si un mot est en cours de frappe, seules les suites qui le prolongent sont proposées, sans tenir compte de la casse.

Limites de la v0.1 :

- Une erreur placée avant le curseur supprime les suggestions. La reprise sur erreur viendra avec le serveur d'aide à la saisie (charte, art. 12, v0.4).
- Un nom entre crochets en cours de frappe n'est complété que sur son premier mot.
- Après un article, les noms proposés ne sont pas filtrés par genre.

---

## 9. Formules

Une formule est un calcul, qui vaut quelque chose, ou une action, qui fait quelque chose.

### 9.1 Calculs

Un calcul se définit comme il s'utilise :

```
Le carré d'un nombre vaut nombre × nombre.
Afficher le carré de 7.                        →  49
```

En bloc, quand il faut plusieurs étapes, chaque chemin se termine par `Rendre` :

```
La valeur absolue d'un nombre :
    Si nombre est négatif, rendre −nombre.
    Rendre nombre.
```

- Appel : `le carré de 7`, `la moyenne de 4 et de 6`, `le carré du prix` (contraction, § 5.2). L'article est facultatif ; s'il est écrit, il s'accorde avec le genre du calcul.
- Un argument se lie plus fort que les opérateurs : `le carré de 3 + 1` vaut 10. Pour passer une somme, parenthésez : `le carré de (3 + 1)`.
- `et` suivi de `de`, `d'` ou `du` continue la liste des arguments ; sinon c'est le `et` logique.
- Un calcul en bloc se termine par une phrase `Rendre` au premier niveau de son bloc.

### 9.2 Actions

```
Pour relancer un client :
    Si le client est négatif, afficher « Relance ».

Relancer le client.
```

- Une action s'écrit en bloc, après `Pour` et son nom (un ou plusieurs mots, à l'infinitif).
- Appel : le nom de l'action commence la phrase, suivi des arguments séparés par `et`. Chaque argument est une comparaison ou une expression ; un `et` logique dans un argument demande des parenthèses. *(2.38)* Entre parenthèses, l'argument est un groupe logique, comme dans une condition : `Encaisser la dette et le jour et (le reste > 1 et le dé ≤ 15).` La 2.37 le refusait encore (« Parenthèse fermante manquante ») ; l'essai de charge de Praxis l'a révélé.
- Une action ne rend rien : `Rendre` y est une erreur.
- *(2.31)* Un nom qui finit par `de` devant son premier paramètre s'écrit avec l'élision : `Pour acquitter le solde d'un relevé :`. L'appel contracte comme partout : `Acquitter le solde du relevé.`, `… de 4.`, `… d'Anne.`, `… des notes.`

### 9.3 Paramètres

- L'article indéfini déclare un paramètre et fixe son genre : `d'un nombre` (calcul), `un client` (action), `une remise` (féminin).
- Plusieurs paramètres : `d'un premier nombre et d'un second nombre`, `un montant et une remise`.
- Dans le corps, un paramètre se nomme comme tout autre nom : `nombre`, `le nombre`. Deux paramètres d'une même formule portent des noms distincts.

### 9.3 bis Paramètres facultatifs *(2.98)*

```
Pour relancer un client, avec un délai de 30 et un ton de « poli » :
    Afficher nom du client puis délai puis ton.

Relancer le client.                                           →  Ana 30 poli
Relancer le client, avec le ton de « ferme ».                 →  Ana 30 ferme
Relancer le client, avec le délai de 10 et le ton de « ferme ».
```

- Une **action** peut avoir des paramètres facultatifs : après ses paramètres, `, avec` puis `un délai de 30`, `une remise de 0`, séparés par `et` ; et avant `, dans l'écran …` (§ 22.2) s'il y en a un. Dans le corps, ils se nomment comme les autres paramètres.
- **La valeur de départ est une constante** : un texte, un nombre (négatif compris), une date, une heure, vrai ou faux, comme celle d'un champ d'entité (« « Suisse » au départ », § 16.7). Une durée n'en est pas une, puisqu'elle ne vit pas seule (§ 14) : un délai en jours s'écrit `un délai de 30`. Le nom va jusqu'au `de` qui précède la constante : `un délai de paiement de 30` nomme « délai de paiement ».
- **À l'appel**, après les arguments : `, avec le délai de 10`, plusieurs séparés par `et`, dans l'ordre qu'on veut, chacun avec son article (`le`, `la`, `l'`) ; `de` se contracte (`avec le ton du message`). Un facultatif omis prend sa valeur de départ. Quand deux noms commencent pareil (`délai`, `délai de paiement`), le plus long l'emporte.
- **Les refus** : un nom inconnu (« « relancer » a pour facultatifs « délai » ou « ton », suivis de « de » et de leur valeur : « , avec le délai de … ». »), un facultatif donné deux fois, sans article, une valeur de départ qui n'est pas une constante, un facultatif qui porte le nom d'un paramètre. Le compte des arguments ne compte que les paramètres obligatoires : « « relancer » attend 1 paramètre (et des facultatifs, après « , avec »), 0 donné. »
- **La machine ne change pas** : l'appel lui passe tous les paramètres, la valeur de départ d'un facultatif omis étant recopiée à la compilation. Une action à facultatifs se compile et s'exécute comme une action ordinaire.
- La forme canonique n'écrit que les facultatifs écrits, dans l'ordre de la déclaration.
- Les **calculs** n'en ont pas : leur appel (`le carré de 7`) n'a pas de virgule où loger `, avec`.
- En forme compacte : `_action relancer(_un client ; _un délai << 30 ; _un ton << « poli »)`, et à l'appel `relancer(c ; ton << « ferme »)`. La forme compacte n'écrit pas le genre du facultatif à l'appel : la forme littéraire reprend celui de la déclaration.

### 9.4 Pureté et visibilité

- **Un calcul ne voit que ses paramètres, ses noms locaux, les formules et la base, en lecture.** Les variables du programme lui sont invisibles, il n'affiche rien, n'appelle pas d'action et n'écrit jamais dans la base. Même entrée, même résultat, l'état de la base au moment de l'appel faisant partie de l'entrée : deux appels sans écriture entre eux rendent la même valeur. `Le solde d'un compte vaut la somme des débits des lignes du compte − …` est un calcul.
- **Une action voit et modifie les variables du programme.** Elle peut afficher et appeler d'autres formules.
- Un nom créé dans une formule lui est local et disparaît à la fin de l'appel.

### 9.5 Règles de définition

- Une formule se définit au premier niveau du programme, hors de tout bloc, et avant son premier usage.
- Une formule peut s'appeler elle-même (récursion). Au-delà de 1000 appels imbriqués, l'exécution s'arrête avec une erreur.
- Un calcul ne se modifie pas (`devient` est une erreur) ; un nom de formule ne se réutilise pas.
- Une action qui échoue n'écrit rien (charte, art. 7) : comme toute exécution, elle est annulée par le journal (docs/vm.md, § 6).
- Dans la boucle interactive, une formule définie par une saisie ratée n'existe pas.

---

### 9.6 Exemples

```
Le carré d'un nombre vaut nombre × nombre.
Exemple : le carré de 7 vaut 49.
Exemple : le carré de −1,5 vaut 2,25.
```

- `Exemple : expression vaut résultat.` dit ce qu'une formule doit rendre. C'est de la documentation qui se vérifie : elle montre au lecteur comment s'emploie la formule, et `grym exemples` la contrôle.
- `grym lancer` ignore les exemples. `grym exemples fichier.grym` ne fait que les vérifier : il compile les déclarations (classes, formules, règles, fichiers utilisés) et les exemples, et n'exécute rien d'autre ; la base est vide, en mémoire, jamais celle du programme.
- Chaque exemple s'affiche, juste ou faux, avec sa ligne : `✓ ligne 2 : le carré de 7 vaut 49`, `✗ ligne 3 : le carré de 2 vaut 5 ; obtenu : 4`, ou `✗ … ; erreur : …` si le calcul échoue. Puis le bilan : « 3 exemples, 2 justes. » La commande réussit si tous sont justes ; un script ou l'intégration continue peuvent s'en servir.
- L'égalité est celle des ensembles (§ 13.9) : `2` vaut `2,0`, un texte se compare exactement, un objet par identité.
- Un exemple se place au premier niveau du fichier. Son expression se lit comme dans un calcul (§ 9.4) : formules, constantes et base en lecture, jamais les variables du programme, ni `aujourd'hui`. Un exemple qui dépend du jour ne serait plus vrai demain.
- Aucun mot réservé : `Exemple` ne commence une phrase que suivi de « : ».
- En forme compacte : `_exemple carré(7) << 49`.

### 9.7 Scénarios *(3.05)*

```
Scénario « une relance se crée » :
    Le c vaut un nouveau client :
        Le nom vaut « Ana ».
    Conserver le c.
    Relancer le c.
    Vérifier que le nombre de relances conservées = 1.
```

- **Un scénario** éprouve ce que les exemples (§ 9.6) ne peuvent pas : des actions, la base, les règles. `Scénario « titre » :` et son bloc, au premier niveau ; un titre par scénario (« Le scénario « a » existe déjà : chacun a son titre. »).
- **`Vérifier que condition.`** : fausse, le scénario échoue, à sa ligne : « Vérification fausse : le nombre de relances conservées = 3. » Elle ne s'écrit que dans un scénario.
- **`grym scenarios fichier.grym`** joue chaque scénario **seul, sur une base neuve en mémoire** : ce qu'un scénario conserve, le suivant ne le voit pas, et la base du programme n'est jamais touchée. Le programme lui-même ne s'exécute pas : seules ses déclarations (classes, entités, aptitudes, types, contrats, calculs, actions, règles, fichiers utilisés) sont compilées. Chaque scénario s'affiche, `✓ « une relance se crée »` ou `✗ « un échec se voit » : fichier:25:5 : Vérification fausse : …`, puis le bilan : « 3 scénarios, 2 réussis. » La commande réussit si tous réussissent : un script ou l'intégration continue s'y fie. Les règles (§ 16.14) se vérifient à la fin du scénario comme à la fin d'un programme.
- **Ce qu'un scénario voit** : les déclarations du programme, et ses propres noms, qui disparaissent avec lui. Les variables du programme lui sont invisibles, puisque le programme ne s'exécute pas (« « total » est une variable du programme : un scénario ne la voit pas … Crée-la dans le scénario. »).
- **Les gestes** *(3.08)* : un scénario ouvre des écrans, joués par des gestes préparés **avant** `Ouvrir`, puisque l'ouverture attend jusqu'à la fermeture :
  ```
  On écrit « Ana Roth » dans le nom de l'écran de saisie.
  On choisit « Suisse » dans le pays de l'écran de saisie.
  On choisit le p dans l'écran liste.
  On clique sur « Enregistrer » dans l'écran de saisie.
  On ferme l'écran de saisie.
  Ouvrir l'écran de saisie.
  ```
  `On écrit` va dans une zone ; `On choisit` une valeur d'une zone à choix, ou un objet conservé d'une liste de l'écran ; `On clique sur « B »` ; `On ferme l'écran`. Chaque geste nomme son écran : il est joué quand cet écran attend, dans l'ordre écrit ; un écran qui ouvre un autre écran reçoit ses gestes à son tour. Quand il n'y a plus de geste, l'écran se ferme seul.
  - **Un geste impossible arrête le scénario**, à sa ligne : un bouton inactif ou caché (« Geste de la ligne 28 : « Enregistrer » est inactif dans l'écran de saisie : le geste ne peut pas se faire. »), une zone en lecture seule, un choix hors de la liste, un objet qu'aucune liste ne montre, un geste destiné à un autre écran que celui qui attend. Un geste préparé qu'aucun écran ne reçoit est une erreur aussi. Une erreur qu'un événement montre dans l'écran (`Refuser`) arrête le scénario, avec son message.
  - Un bouton inconnu se signale dès l'analyse. Un geste hors d'un scénario est refusé.
  - En forme compacte : `_on_écrit X _dans nom _écran de_saisie`, `_on_choisit X [_dans pays] _écran …`, `_on_clique « B » _écran …`, `_on_ferme _écran …`.
- **L'agenda** *(3.12)* : `On choisit le créneau du 05.11.2026 à 10:30 dans l'écran du calendrier.` est un clic dans la grille, hors des objets (`Quand on choisit un créneau`) ; `On déplace la séance au 06.11.2026 à 14:15 dans l'écran du calendrier.` glisse un objet de l'agenda et le dépose (`Quand on déplace une séance`). Le jour et l'heure sont des expressions (`à aujourd'hui à 15:30`). Un écran sans agenda, ou un objet que l'agenda ne montre pas, arrête le scénario. En forme compacte : `_on_créneau 05.11.2026 ; 10:30 _écran …`, `_on_déplace s ; 06.11.2026 ; 14:15 _écran …`.
- **L'état de départ** *(3.12)* : les créations de premier niveau du programme (`La grille de l'agenda vaut …`, `La pièce en cours vaut absent.`) s'exécutent avant chaque scénario, dans l'ordre écrit : un écran ou une action qui les lit les trouve. Les autres phrases du programme (afficher, ouvrir, appeler) ne s'exécutent pas, et ses variables restent invisibles au scénario.
- **Les confirmations** *(3.09)* : un bouton `à confirmer par « … »` (§ 22.23) pose sa question ; le geste qui suit le clic y répond, `On confirme.` ou `On renonce.` (rien ne se passe alors). Sans réponse : « Geste de la ligne 32 : « Supprimer » demande une confirmation (« Supprimer ce patient ? ») : écris « On confirme. » ou « On renonce. » juste après le clic. » Une réponse sans question est une erreur aussi.
- **Les documents** *(3.09)* : un scénario les fabrique sans PDF réel (la page du document en tient lieu) et ne les montre nulle part. **`les documents sortis`** est la liste des titres des documents ouverts (`Ouvrir le document de quittance.`), dans l'ordre ; elle se range, puis se lit par rang : `Les docs valent les documents sortis.`, `Vérifier que le premier élément des docs = « Quittance ».` (la 3.09 donnait ici `est parmi`, qui ne s'emploie encore que dans une condition `dont` : l'exemple était faux, corrigé à la 3.10). Un document fabriqué pour être rangé (`L'exemplaire de la q devient le document de quittance.`) n'est pas sorti ; un exemplaire gardé qu'on rouvre (`Ouvrir l'exemplaire de la f.`, un fichier) n'en fait pas sortir un nouveau : la liste dit ce qui a été fabriqué et montré. Hors d'un scénario, `les documents sortis` est refusé.
- **Ce qu'il ne fait pas** : ouvrir une fiche (§ 20.1), dont les gestes et la modification au clavier ne se simulent pas (refusé dès l'analyse ; une action qui en ouvre une échoue à sa ligne) ; une saisie y échoue au lieu d'attendre le clavier (« Aucune entrée : la question ne peut pas être posée ici. »).
- En forme compacte : `_on_confirme`, `_on_renonce`, `_documents_sortis`.
- **Un fichier de scénarios** *(3.10)* : `Ces scénarios éprouvent « Praxis ».`, en tête d'un fichier à part (`scenarios.grym`, à côté du programme). Il lit le programme comme `Utiliser`, mais en accepte les phrases qui s'exécutent (§ 21.2 les refuse dans un fichier utilisé), sans jamais les exécuter : seuls ses déclarations, ses écrans et ses formules servent. Le programme reste intact ; ses scénarios vivent à côté. Lancé seul, un tel fichier refuse : « Un fichier de scénarios se joue avec « grym scenarios », jamais seul : il ne lance pas le programme qu'il éprouve. » Il déclare ses propres actions d'aide (préparer un cabinet, poser une séance). En forme compacte : `_éprouver « Praxis »`.
- `grym lancer` ignore les scénarios, comme les exemples. Aucun mot réservé : `Scénario` ne commence une phrase que suivi d'un titre et de « : », `Vérifier` que suivi de « que ».
- En forme compacte : `_scénario « titre »` … `_fin` ; `_vérifier condition`.

## 10. Répéter

### 10.1 Tant que

```
Tant que le solde est négatif :
    Le solde devient solde + 100.
```

Même forme que `Si` (§ 5.4) : une condition, puis une phrase après une virgule ou un bloc après deux-points. La condition se teste avant chaque tour.

### 10.2 Répéter … fois

```
Répéter 3 fois :
    Afficher « Bip ».
```

- Le nombre de tours se calcule une fois, avant le premier tour.
- Il doit être un entier positif ou nul (`3`, `3,00`, `0`). `2,5` ou `−1` produisent une erreur à l'exécution, jamais un arrondi.

### 10.3 Pour chaque

```
Pour chaque mois de 1 à 12 :
    Afficher mois.
Pour chaque i du début à la fin, afficher i.
Pour chaque taux de 0 à 1 par pas de 0,25, afficher taux.
```

- Les deux bornes sont incluses. Début, fin et pas se calculent une fois, avant le premier tour.
- `du` et `au` s'écrivent devant une borne sans article (`du 05.10.2026 au 02.11.2026`) ; la forme canonique les conserve, comme toute contraction (§ 12).
- Sans pas écrit, le sens est automatique : `+1` si le début est inférieur ou égal à la fin, `−1` sinon. `de 10 à 1` compte à rebours.
- Avec un pas écrit, la boucle avance du pas tant que le compteur ne dépasse pas la fin, dans le sens du pas. Un pas qui s'éloigne de la fin donne zéro tour.
- Les pas décimaux sont exacts : `de 0 à 1 par pas de 0,1` fait exactement 11 tours. Les valeurs gardent les décimales du pas (§ 3.2) : `0`, `0,25`, `0,50`.
- Un pas nul est une erreur à l'exécution.
- Le compteur est un nom créé par la boucle, en lecture seule, sans article (`Pour chaque mois`). Il disparaît à la fin de la boucle et ne doit pas déjà exister.
- Le nom du compteur s'arrête au premier `de`, `d'` ou `du` : il ne peut pas en contenir.

### 10.4 Sortir, passer

- `Sortir de la boucle.` quitte la boucle la plus proche.
- `Passer au tour suivant.` saute à la fin du tour en cours : test suivant (`Tant que`, `Répéter`) ou incrémentation du compteur (`Pour chaque`).
- Hors d'une boucle, ces phrases sont des erreurs. Une formule ne voit pas les boucles de son appelant.

### 10.5 Selon

```
Selon le mois :
    Cas 2 :
        Le nombre de jours devient 28.
    Cas 4 ou 6 ou 9 ou 11 :
        Le nombre de jours devient 30.
    Autrement :
        Le nombre de jours devient 31.
```

- Le sujet se calcule une seule fois.
- Les cas s'alignent sous le `Selon`, plus indentés, chacun en forme courte ou en bloc (comme `Si`).
- Une condition de cas est une valeur (égalité par valeur, `1,0` = `1`), un intervalle `de a à b` (bornes incluses, dans un ordre quelconque) ou une tournure de `est` sans le `est` (`négatif`, `supérieur ou égal à 100`, `vrai`).
- Plusieurs conditions se séparent par `ou`, jamais par une virgule : `Cas 1,3` désigne le nombre 1,3.
- Seul le premier cas vrai s'exécute : aucun cas ne « tombe » dans le suivant.
- `Autrement`, facultatif, vient en dernier. Sans lui, si aucun cas ne convient, rien ne se passe.
- `Cas` et `Autrement` hors d'un `Selon` sont des erreurs.

### 10.6 Boucles infinies et interruption

- Aucune limite de tours : `Tant que vrai` est un programme légitime.
- Ctrl+C interrompt l'exécution proprement : « Interrompu (Ctrl+C). » Dans la boucle interactive, la saisie interrompue est annulée comme toute saisie ratée (§ 3.3).

### 10.7 Mots de construction

`tant`, `répéter`, `chaque`, `sortir`, `passer`, `selon`, `cas`, `autrement`, `essayer`, `saisir`, `utiliser` (§ 21), `quand`, `ouvrir`, `fermer` (§ 22) (avec `afficher`, `si`, `sinon`, `pour`, `rendre`) commencent des constructions : ils ne peuvent pas commencer le nom d'une action.

---

## 11. Forme compacte

La forme compacte (`.grymc`) écrit le même programme avec des mots-clés préfixés d'un souligné. Elle produit le même arbre que la forme littéraire (charte, art. 4).

### 11.1 Règles de lecture

- Les mots-clés commencent par un souligné : `_si`, `_fin`. Tout mot sans souligné initial est un nom.
- Un nom composé s'écrit avec des soulignés internes : `prix_unitaire` désigne `prix unitaire`. L'apostrophe d'une élision reste : `prix_de_l'article`. Aucun crochet n'est nécessaire : `frais_de_port_et_d'emballage`.
- Nombres, textes et opérateurs s'écrivent comme en forme littéraire (§ 1). Les comparaisons s'écrivent en symboles.
- Une instruction par ligne ; un bloc se ferme par `_fin`. L'indentation est libre ; l'imprimeur indente de quatre espaces.
- `#` commence une remarque jusqu'à la fin de la ligne.
- Les éléments d'une liste (`_afficher`, arguments, paramètres) se séparent par `;`, jamais par une virgule (virgule décimale).

### 11.2 Correspondances

| Forme littéraire | Forme compacte |
|------------------|----------------|
| `Remarque : note` | `# note` |
| `Le total vaut 0.` / `La quantité vaut 3.` / `L'addition vaut 0.` | `_le total << 0` / `_la quantité << 3` / `_l'addition << 0` |
| `Le total devient total + 1.` | `total << total + 1` |
| `Afficher « a » puis x.` | `_afficher « a » ; x` |
| `le total est supérieur à 100` | `total > 100` |
| `x est positif`, `négatif`, `nul`, `vrai`, `faux` | `x _positif`, `_négatif`, `_nul`, `_vrai`, `_faux` |
| `x n'est pas nul` | `_non (x _nul)` |
| `et`, `ou`, `vrai`, `faux` | `_et`, `_ou`, `_vrai`, `_faux` |
| `Si c :` … `Sinon si d :` … `Sinon :` … | `_si c _alors` … `_sinon_si d _alors` … `_sinon` … `_fin` |
| `Le carré d'un nombre vaut nombre × nombre.` | `_calcul _le carré(_un nombre) << nombre × nombre` |
| `La valeur absolue d'un nombre :` + bloc | `_calcul _la valeur_absolue(_un nombre)` + bloc + `_fin` |
| `le carré de 7`, `la moyenne de 4 et de 6` | `carré(7)`, `moyenne(4 ; 6)` |
| `Pour relancer un client :` + bloc | `_action relancer(_un client)` + bloc + `_fin` |
| `Relancer le client.`, `Saluer.` | `relancer(client)`, `saluer()` |
| `l'arrondi de x à 0,05`, `… vers le bas`, `… vers le haut`, `… au pair` | `_arrondi(x ; 0,05)`, `_arrondi_bas(…)`, `_arrondi_haut(…)`, `_arrondi_pair(…)` |
| `la longueur de t`, `t en majuscules`, `t en nombre` | `t.longueur`, `t _en_majuscules`, `t _en_nombre` |
| `t contient p`, `t commence par p`, `t ne finit pas par p` | `t _inclut p`, `t _commence p`, `_non (t _finit p)` |
| `la moyenne des soldes des clients conservés`, `le plus grand solde des …` | `_moyenne_de solde _de client _conservé`, `_plus_grand_de …`, `_plus_petit_de …` |
| `le client conservé au plus grand solde` | `_le client _conservé _au_plus_grand solde` |
| `un état (« à faire », « en cours » ou « fini »)` | `_un état (« à faire » ; « en cours » ; « fini »)` |
| `le reste de 17 par 5`, `la valeur absolue de x`, `la partie entière de x` | `_reste(17 ; 5)`, `x.valeur_absolue`, `x.partie_entière` |
| `Exporter les membres conservés, par nom, avec le nom et la cotisation, dans « m.csv ».` | `_exporter membre _conservé _par nom _avec nom ; cotisation _dans « m.csv »` |
| `Pour chaque ligne du tableau « f.csv » :`, `la colonne « Nom » de la ligne` | `_pour_chaque ligne _tableau « f.csv »`, `_colonne(« Nom » ; ligne)` |
| `Saluer m comme une personne.` | `saluer(m) _comme personne` |
| `Les couleurs valent « a » et « b ».`, `Les invités gagnent m.` | `_les couleurs << « a » ; « b »`, `_les invités _gagnent m` |
| `Exemple : le carré de 7 vaut 49.` | `_exemple carré(7) << 49` |
| `le nombre de mois entre d1 et d2`, `un nombre au hasard de 1 à 6` | `_écart_mois(d1 ; d2)`, `_hasard(1 ; 6)` |
| `19:30 + 15 minutes`, `maintenant`, `le nombre de minutes entre h1 et h2` | `19:30 + 15 _minutes`, `_maintenant`, `_écart_minutes(h1 ; h2)` |
| `Rendre x.` | `_rendre x` |
| `Tant que c :` | `_tant_que c` … `_fin` |
| `Répéter 3 fois :` | `_répéter 3 _fois` … `_fin` |
| `Pour chaque i de 1 à 9 par pas de 2 :` | `_pour_chaque i _de 1 _à 9 _pas 2` … `_fin` |
| `Sortir de la boucle.`, `Passer au tour suivant.` | `_sortir`, `_passer` |
| `Selon x :` / `Cas 1 ou de 2 à 3` / `Cas supérieur à 10` / `Autrement` | `_selon x` / `_cas 1 _ou _de 2 _à 3` / `_cas > 10` / `_autrement` … `_fin` |
| `Un client a : un nom, une date.` | `_classe _un client` / `_un nom` / `_une date` / `_fin` |
| `Un membre est une personne.` + `Un membre a : une licence.` | `_classe _un membre _est _une personne` / `_une licence` / `_fin` |
| `Une chose horodatée a : une date.` | `_aptitude horodatée` / `_une date` / `_fin` |
| `Un membre est une personne horodatée et active.` | `_classe _un membre _est _une personne _adopte horodatée ; active` |
| `Pour dater une chose horodatée :` | `_action dater(_une chose_horodatée)` |
| `un nouveau client`, `une nouvelle facture` | `_nouveau client`, `_nouveau facture` |
| `Le c vaut un nouveau client :` + `Le nom vaut « a ».` | `_le c << _nouveau client _avec` / `nom << « a »` / `_fin` |
| `le nom du client de la facture` | `facture.client.nom` |
| `Le solde du client devient 0.` | `client.solde << 0` |

- La forme compacte ne connaît pas les formes courtes : chaque construction ouvre un bloc fermé par `_fin` (sauf `_sinon`, `_sinon_si`, `_cas` et `_autrement`, qui continuent la construction en cours).
- Un argument de calcul est délimité par les parenthèses et les `;` : `carré(3 + 1)` vaut 16.
- Les articles n'apparaissent qu'à la création et dans l'en-tête d'un calcul : ils portent le genre, qui sert aux accords quand on revient à la forme littéraire.

- Un suffixe `_positif`, `_négatif`, `_nul`, `_vrai`, `_faux` après une valeur est une comparaison ; ailleurs, `_vrai` et `_faux` sont des valeurs. Une tournure écrite en forme compacte ne porte pas de genre : aucun accord n'y est vérifié.
- Une instruction occupe une ligne ; elle continue sur la ligne suivante tant qu'une parenthèse reste ouverte.

### 11.3 Outils

- `grym lancer`, `grym compiler`, `grym desassembler` et `grym formater` acceptent les fichiers `.grymc`.
- `grym traduire fichier.grym` produit `fichier.grymc` ; `grym traduire fichier.grymc` produit `fichier.grym`. Le fichier produit ne doit pas exister : `grym traduire` n'écrase jamais un fichier.

### 11.4 Lecture

La lecture réécrit chaque instruction compacte en la phrase littéraire équivalente, puis l'analyse comme telle. Les deux formes passent donc par les mêmes vérifications et produisent le même arbre. Les erreurs sont localisées dans le fichier compact (ligne et colonne).

**Articles** *(14 octobre 2026)* : la forme compacte n'écrit pas d'article (`_écran.compositeur_choisi.portrait`, `_avec titre`). Traduite en littéraire, elle les retrouve par le genre déclaré du champ, de la variable ou de l'entité, avec contraction et élision : `l'image du portrait du compositeur choisi de l'écran`, `avec le titre et la date de naissance du compositeur`, `le montant de la f`. L'objet ne prend son article que si son champ a pris le sien : un champ au genre inconnu (`longueur`, propriété d'un texte) laisse `longueur de n` tel quel, plutôt qu'un mélange. Seul ce qui vient de la forme compacte est touché : une phrase littéraire écrite sans article (`nom du c`) le reste, même formatée. Les variables aussi *(1.87)* : une variable créée avec un article (`L'effectif vaut 12.`) le retrouve partout où la forme compacte l'a perdu (`l'effectif + 3`, `la photo`, `du r`) ; un paramètre, ou une variable créée sans article, reste nu, même s'il porte le nom d'un champ. Deux exceptions gardent le nom nu : l'objet d'un champ resté sans article (`longueur de n`), et `puis de …`, `suivi de …`, qui ne contractent pas (`puis de nom`). Dans une condition `dont`, `=` devant une constante revient en `est` *(1.88)* (`dont le numéro est 2`, `dont le nom est « Actif »`), comme le guide l'écrit ; devant une variable, et pour `<`, `>`, `≤`, `≥`, `≠`, le symbole reste. Hors d'un `dont`, `est 2` ne se relirait pas. La forme compacte n'écrit le `Si` et le `Cas` qu'en bloc ; à la traduction, une branche d'une seule phrase `afficher …` ou `rendre …` revient en forme courte *(1.89)* (`Si âge < 18, rendre 40,00.`, `Sinon, afficher « réduite ».`, `Cas de 1000 à 1999, rendre « actif ».`), comme le guide l'écrit ; une affectation ou un appel restent en bloc. Mesuré sur le guide, les exemples et les projets : l'aller-retour passe de 544 lignes différentes (avant les articles) à 283, et chaque programme revenu se relit. Ce qui ne revient pas encore à l'identique tient surtout à l'usage du guide, qui écrit la même chose tantôt avec, tantôt sans article : le sens est le même, la forme diffère.

Limite de la v0.2 : les erreurs de structure propres à la forme compacte (`_fin` manquant, `_alors` attendu…) s'expriment en forme compacte ; les autres (nom inconnu, accord, pureté) empruntent encore le vocabulaire de la forme littéraire (charte, art. 8).

## 12. Forme canonique

`grym formater fichier.grym` réécrit un programme dans la forme littéraire canonique.

- Le programme est conservé : même arbre, mêmes noms, mêmes valeurs, mêmes remarques, mêmes choix d'écriture (articles, crochets, tournures en mots ou en symboles, contractions, formes courtes ou en bloc).
- La présentation est normalisée : une phrase par ligne, indentation de quatre espaces, majuscule en début de phrase, espaces simples, nombres au style suisse (`12,50` ; un nombre groupé l'est par l'apostrophe, `1 000` devient `1'000`, et un nombre écrit sans séparateur le reste, `1747`), textes entre `« »` (ou `" "` si le texte commence ou finit par une espace, ce que `« »` rognerait ; un texte vide s'écrit `« »`, avec une seule espace), une seule ligne vide là où la source en avait une ou plusieurs.
- La forme canonique est un point fixe : la formater ne change plus rien.

Garanties de la traduction (charte, art. 4) :

1. Compacte → littéraire → compacte : le texte compact revient à l'identique, une fois écrit par l'imprimeur.
2. Littéraire → compacte → littéraire : le même programme, en forme canonique. Les choix d'écriture que la forme compacte ne porte pas (articles dans les expressions, tournures en mots, contractions, formes courtes) prennent leur forme par défaut.
3. Un texte littéraire canonique écrit sans ces choix fait l'aller-retour à l'identique.

Vérification (21 septembre 2026) : sur les 147 programmes valides des suites de tests, la forme littéraire et sa traduction compacte donnent la même sortie à l'exécution, et les garanties 1 et 3 tiennent sans exception.

## 13. Objets

### 13.1 Classes

```
Un client a :
    un nom,
    un solde.
```

- Une classe est un nom : l'article indéfini fixe son genre (`Une facture a :`), comme pour chaque champ.
- Les champs se séparent par des virgules ; le dernier se termine par un point. La présentation est libre : `Un point a : un x, un y.`
- Les champs ne sont pas typés en v0.2 (charte, art. 5).
- Une classe se déclare au premier niveau du programme, avant son premier usage. Un nom de classe ne se réutilise pas.
- Un champ ne porte pas le nom d'un calcul ou d'une action. Un même nom de champ peut servir dans plusieurs classes, avec le même genre.

### 13.2 Créer un objet

```
Le client vaut un nouveau client.
Le client vaut un nouveau client :
    Le nom vaut « Dupont ».
    Le solde vaut 0.
```

- `un nouveau`, `un nouvel` (devant une voyelle), `une nouvelle` : l'article et l'adjectif s'accordent avec le genre de la classe.
- Le bloc facultatif initialise des champs de la classe, chacun au plus une fois, avec `vaut`.
- Un champ non initialisé n'a pas de valeur : le lire est une erreur à l'exécution, jamais une valeur par défaut.

### 13.3 Champs

```
Afficher le nom du client.
Le solde du client devient solde du client − 10.
Afficher le nom du client de la facture.            →  nom de (client de (facture))
```

- Un champ se lit par le complément du nom : `le solde du client`, `la date de la facture`, `le nom de l'employé`. Les compléments s'enchaînent de droite à gauche.
- Un champ se modifie avec `devient`, jamais avec `vaut`.
- Quand un nom de champ suivi de `de` peut aussi se lire comme un nom déclaré, la plus longue correspondance l'emporte ; à longueur égale, le champ l'emporte. `Le prix de vente vaut 3.` crée un nom, même si `prix` est un champ, tant que `vente` n'est pas un nom existant.
- Un calcul lit les champs de ses paramètres, mais ne les modifie pas (§ 9.4).
- Qu'un objet ait bien le champ demandé se vérifie à l'exécution.

### 13.4 Identité, affichage, ramasse-miettes

- `a = b` compare l'identité : vrai si les deux noms désignent le même objet, pas si leurs champs sont égaux.
- `Afficher le client.` écrit `un client`.
- Un objet vit tant qu'un nom, un champ ou une case locale le désigne ; le ramasse-miettes libère les autres, même quand ils forment des cycles.
- Une modification de champ passe par le journal : une saisie ratée rend à chaque champ sa valeur d'avant (§ 3.3).

### 13.5 Héritage

```
Une personne a :
    un nom.
Un membre est une personne.
Un membre a :
    une licence.
```

- Une classe hérite d'une seule classe, déjà déclarée (charte, art. 6). L'article s'accorde avec la classe parente : `Un membre est une personne.`
- Ses champs propres se déclarent dans la phrase qui suit immédiatement, avec `a :`. Sans cette phrase, la classe n'a que les champs hérités. Plus loin, redéclarer la classe est une erreur.
- Une classe a les champs de toute sa lignée. Un champ propre ne reprend pas le nom d'un champ hérité.
- Un objet d'une classe héritière s'initialise et se lit avec tous ses champs, hérités compris.
- En forme compacte : `_classe _un membre _est _une personne`, puis les champs propres et `_fin`.

### 13.6 Méthodes

```
Pour saluer une personne :
    Afficher « Bonjour » puis nom de la personne.
Pour saluer un membre :
    Afficher « Salut » puis nom du membre.

Saluer m.
```

- Quand le premier paramètre d'une formule porte le nom d'une classe, la formule est une méthode de cette classe. Rien d'autre à écrire.
- Une formule peut avoir plusieurs versions : une par classe de son premier paramètre. Toutes ont le même nombre de paramètres et sont de la même sorte (calcul ou action).
- L'appel choisit la version à l'exécution, selon la classe réelle du premier argument : celle de sa classe, sinon celle de la classe parente la plus proche. Un membre est salué par `Pour saluer un membre`, un invité par `Pour saluer une personne`.
- Seul le premier paramètre choisit la version ; les autres ne sont pas vérifiés.
- Erreurs à l'exécution : un premier argument qui n'est pas un objet, ou une classe sans version dans sa lignée (« Aucune version de « saluer » pour une ville. »).
- Une formule sans classe et une méthode ne partagent pas un nom.
- Dans la boucle interactive, une saisie peut ajouter une version à une formule existante ; les appels suivants la trouvent.

### 13.7 Aptitudes

```
Une chose horodatée a :
    une date de création.
Une chose active (actif) a :
    un état.

Un membre est une personne horodatée et active.
Un document est une chose horodatée.

Pour dater une chose horodatée :
    La date de création de la chose devient …
```

- Une aptitude est un adjectif. Elle se déclare avec `Une chose` et sa forme féminine ; sa forme masculine se déduit en retirant le « e » final, ou se déclare entre parenthèses quand elle est irrégulière.
- Une classe adopte des aptitudes en les ajoutant après sa classe parente. L'adjectif s'accorde avec le nom qu'il suit : `une personne horodatée`, `un employé horodaté`. Avec `une chose`, la classe n'a pas de parent, seulement ses aptitudes.
- `chose` est réservé aux aptitudes : ce n'est pas un nom de classe. Une aptitude et une classe ne partagent pas un nom.
- Une classe reçoit les champs de ses aptitudes. Deux sources (classe parente, aptitudes, champs propres) ne fournissent pas le même champ.
- `Pour dater une chose horodatée` : le premier paramètre s'appelle `chose` dans le corps, et la formule devient une version pour l'aptitude.
- Choix de la version, pour chaque classe de la lignée, en partant de la classe réelle : la version de la classe, sinon celle de l'une de ses aptitudes, sinon on passe à la classe parente.
- Conflit (charte, art. 6) : si deux aptitudes d'une classe définissent la même formule, la classe doit définir sa propre version, sinon l'analyse échoue. Aucune résolution implicite.
- En forme compacte : `_aptitude horodatée`, `_aptitude active (actif)` ; `_classe _un membre _est _une personne _adopte horodatée ; active` ; `_une chose_horodatée` pour le paramètre.

### 13.7 bis Contrats d'aptitude *(3.01)*

```
Une chose payable a :
    un montant.
Une chose payable doit savoir : le montant dû d'une chose payable.
Une chose payable doit savoir : encaisser une chose payable et un mode.

Une facture est une chose payable.
Un abonnement est une chose payable.
Le montant dû d'une facture vaut montant de la facture.
Le montant dû d'un abonnement vaut montant de l'abonnement × 12.
Pour encaisser une chose payable et un mode :
    Afficher « encaissé » puis le montant dû de la chose puis mode.
```

- **`Une chose payable doit savoir : …`** déclare une formule que chaque classe de l'aptitude doit avoir, écrite comme l'en-tête de sa définition : un **calcul** avec son article (`le montant dû d'une chose payable`), une **action** à l'infinitif (`encaisser une chose payable et un mode`). Le premier paramètre est l'aptitude elle-même (« Le premier paramètre d'un contrat est l'aptitude elle-même : « une chose payable ». »). L'aptitude est déclarée plus haut ; un contrat par phrase, au premier niveau.
- **Ce qui le remplit** : pour chaque classe qui adopte l'aptitude, elle-même ou par sa lignée, une version concrète de la formule, la sienne, celle d'un ancêtre, ou celle d'une aptitude de sa lignée. Une version écrite pour l'aptitude (`Pour encaisser une chose payable et un mode :`) sert de version par défaut à toutes ses classes : le contrat est rempli d'un coup.
- **La vérification** se fait à la fin de l'analyse, quand toutes les versions sont connues, et signale la classe, à sa ligne, avec ce qu'il faut écrire : « « abonnement » est une chose payable : il lui faut sa version de « montant dû », que l'aptitude exige. Écris : Le montant dû d'un abonnement … » Une sous-classe hérite de la version de sa classe parente ; une version sur la seule sous-classe laisse la classe parente en faute.
- Les versions suivent les règles du § 13.6 : même sorte, même nombre de paramètres que le contrat.
- **Dans la boucle interactive**, la vérification n'a pas lieu : une classe et sa version arrivent souvent sur deux saisies.
- Le contrat ne produit aucun code : c'est une promesse vérifiée à l'analyse. L'appel choisit la version comme d'habitude (§ 13.7).
- `doit savoir` après `Une chose …` est une tournure nouvelle ; aucune phrase ne commençait ainsi.
- En forme compacte : `_contrat payable _calcul _le montant_dû(_une chose_payable)`, `_contrat payable _action encaisser(_une chose_payable ; _un mode)`.

### 13.8 Appeler une autre version

```
Pour saluer un membre :
    Saluer le membre comme une personne.
    Afficher « Licence : » puis licence du membre.
Le titre d'un membre vaut « membre » suivi du titre du membre comme une personne.
```

- `comme une personne`, après les arguments d'une méthode (§ 13.6), choisit la version de cette classe au lieu de celle de la classe réelle du premier argument : c'est le `super` de Java, en nommant la classe. Si `personne` n'a pas sa propre version, celle de sa parente la plus proche, comme d'habitude, en partant d'elle.
- Le premier argument doit être de cette classe ou d'une classe qui en hérite : « « saluer » comme une personne : une ville n'en est pas une. »
- Une classe seulement, pas une aptitude ; l'article s'accorde (`comme un membre`, `comme une personne`). Sur une formule qui n'est pas une méthode : « « saluer » n'est pas une méthode : « comme une personne » ne choisit aucune version (§ 13.6). »
- La classe nommée peut être n'importe quel ancêtre, pas seulement la parente directe : `Saluer c comme un membre.` pour un cadet.
- Pour un calcul, `comme` suit ses arguments : `le titre du membre comme une personne`.
- Aucun mot réservé : `comme` ne compte que suivi de `un` ou `une`, après un appel.
- En forme compacte : `saluer(m) _comme personne`, `titre(m) _comme personne`.

### 13.9 Ensembles

```
Les couleurs valent « rouge », « vert » et « bleu ».
Les invités valent un ensemble vide.
Les invités gagnent m.
Les invités perdent m.
Si m est parmi les invités, …
Pour chaque invité parmi les invités :
Afficher le nombre d'invités puis les couleurs jointes par « , ».
```

- Un ensemble regroupe des valeurs, sans doublon, dans l'ordre où elles sont arrivées : textes, nombres, dates, objets, mélangés au besoin. Il se nomme au pluriel, avec `les`, et se crée avec `valent` : une liste d'éléments séparés par des virgules et un `et`, ou `un ensemble vide`.
- Il reprend le vocabulaire du champ multiple (§ 16.13) : `gagnent`, `perdent`, `parmi`, `Pour chaque … parmi les …`, `le nombre de …`. Gagner une valeur déjà présente, ou perdre une valeur absente, ne change rien. Deux valeurs sont la même si elles sont égales comme pour `=` : `2` et `2,0` ne font qu'un ; deux objets, s'ils sont le même objet.
- `Les autres valent les couleurs.` copie : changer l'un ne change pas l'autre. `Les invités deviennent …` remplace tout le contenu.
- `Pour chaque invité parmi les invités :` parcourt l'ensemble tel qu'il était au début de la boucle, comme une recherche (§ 16.4) ; le nom de l'élément est en lecture seule.
- `les couleurs jointes par « , »` (ou `joints`) est un texte : les éléments tels qu'`Afficher` les montre, un objet par sa clé (§ 22.1). `Afficher les couleurs.` les sépare par « , ».
- Une valeur absente n'entre pas dans un ensemble : « … est absente ». Un ensemble ne se range pas dans un champ d'entité : le champ multiple (§ 16.13) existe pour cela.
- Aucun mot réservé : `les` suivi d'un ensemble connu, `valent`, `deviennent`, `gagnent`, `perdent` ne valent qu'ainsi placés.
- Les listes ordonnées, avec doublons, sont faites depuis la 2.89 (§ 13.10) : Écoutille les demandait pour les mots d'une transcription, les décodages WSJT et les réponses des services. Les rangs (`le 3e élément de …`) valent aussi pour un ensemble.
- En forme compacte : `_les couleurs << « a » ; « b »`, `_les invités << _ensemble_vide`, `_les invités _gagnent m`, `_perdent`, `_deviennent`, `invités _contient m`, `_pour_chaque invité _parmi invités`, `_nombre_de invités`, `couleurs _jointes « , »`.

- *(3.06)* `vrai` et `faux` s'écrivent comme éléments : `Les réponses valent vrai, faux et vrai.` Avant la 3.06, ils étaient refusés là (« « vrai » inattendu »), faute d'y être attendus.

### 13.10 Listes *(2.89)*

```
Les étapes valent la liste « tri », « lavage », « tri » et « séchage ».
Les relevés valent une liste vide.
Les relevés gagnent « FA014074000; ».
Afficher le 3e élément des étapes.                          →  tri
Afficher le premier élément des étapes puis le dernier élément des étapes.   →  tri séchage
Afficher l'élément de rang n + 1 des étapes.
Les morceaux valent les mots de la transcription.
Les décodages valent les décodages WSJT.
Les réservations valent les éléments du champ « collection » de la réponse.
```

- Une **liste** est un ensemble (§ 13.9) qui garde ses doublons. Elle se nomme au pluriel, avec `les`, et se crée avec `valent la liste` suivi des éléments, séparés par des virgules et un `et`, ou avec `valent une liste vide`. Les éléments restent dans l'ordre où ils sont arrivés : textes, nombres, dates, objets, mélangés au besoin.
- Elle reprend tout le vocabulaire des ensembles : `parmi`, `Pour chaque … parmi les …`, `le nombre de …`, `jointes par`, `un élément au hasard parmi`, `deviennent`, la copie par `Les autres valent les étapes.` (une copie : changer l'une ne change pas l'autre).
- **Gagner, perdre.** `Les relevés gagnent x.` ajoute `x` à la fin, même s'il y est déjà. `Les relevés perdent x.` retire **la première** occurrence de `x`, et rien s'il n'y est pas : perdre défait exactement un gain. Deux valeurs sont la même comme pour `=` (`2` et `2,0`, un objet par identité).
- **Une liste reste une liste.** `deviennent` remplace le contenu sans changer la nature : `Les étapes deviennent « a » et « a ».` garde les deux « a », et la forme canonique écrit `la liste`. Un ensemble ne devient pas une liste : « « couleurs » est un ensemble : il ne devient pas une liste. » Une liste ne devient pas un ensemble non plus : `un ensemble vide` y est refusé (« « étapes » est une liste : « une liste vide ». »).
- **Les rangs** valent pour une liste **et pour un ensemble**, puisque tous deux gardent l'ordre d'arrivée. Le premier élément a le rang 1.
  - `le 3e élément des étapes`, `le 1er élément des étapes` : un rang écrit en chiffres, abrégé comme en typographie française : `1er`, puis `2e`, `3e`, `10e`. `élément` étant masculin, `1re` est refusé : « « élément » est masculin : écris « 1er ». » Les autres graphies sont refusées dès la lecture, avec la bonne : « « 3ème » s'écrit « 3e ». », de même pour `3eme`, `3è`, `3ième`, `2nd`, `2nde`, et `1e` (« « 1e » s'écrit « 1er ». »). Ces formes étaient déjà des erreurs (« un nombre ne peut pas être collé à un mot ») : aucun programme accepté hier n'est refusé.
  - `le premier élément des étapes`, `le dernier élément des étapes`.
  - `l'élément de rang k des étapes` : un rang calculé ; l'expression s'étend jusqu'à `des` (`de rang n + 1 des étapes`).
  - Le rang est un nombre entier d'au moins 1 (`3,0` convient). Les refus, à l'exécution : « Un rang est un nombre entier à partir de 1, pas 2,5. » ; « Pas de 5e élément : il n'y en a que 3. » ; « Pas de premier élément : la liste est vide. » (de même pour le dernier) ; et sur autre chose qu'une liste ou un ensemble : « « le 3e élément de » se lit dans une liste ou un ensemble, pas dans un nombre. ». `le 0e élément` est refusé à l'analyse : « Le premier élément a le rang 1 : « le 1er élément ». »
  - Après `des`, le nom d'une liste ou d'un ensemble déclaré : « « le 3e élément des … » : une liste ou un ensemble attendu (§ 13.9, § 13.10). »
- **Les listes du langage se rangent.** `les mots de t` (§ 4.5), `les éléments du champ « … » de r` (§ 22.18) et `les décodages WSJT` (§ 22.26) sont des listes : elles se rangent dans une liste par `valent` ou `deviennent`, et ne se rangent que là. `Les morceaux valent les mots de t.` crée une liste, même sans `la liste`.
- **Égalité.** Deux listes sont égales si elles ont les mêmes éléments, dans le même ordre, doublons compris. Deux ensembles le sont s'ils ont les mêmes éléments, dans un ordre quelconque. Une liste n'est jamais égale à un ensemble. Cela vaut aussi pour les exemples (§ 9.6).
- Un calcul emploie librement les listes et les rangs : rien ne dépend de l'extérieur.
- Une liste ne se range pas dans un champ d'entité, comme un ensemble.
- Aucun mot réservé : `liste`, `élément`, `rang`, `premier`, `dernier` ne comptent qu'ainsi placés ; un nom, un calcul ou un champ déclaré l'emporte.
- Trier : § 13.12. Reporté : insérer ou retirer à un rang, une tranche (`les 3 premiers éléments`) ; ils suivront le découpage des textes, qui rend des listes.
- En forme compacte : `_les étapes << _liste « tri » ; « lavage »`, `_les relevés << _liste_vide`, `_élément(étapes ; 3)`, `_premier(étapes)`, `_dernier(étapes)` ; `_les m << _mots t`, `_les d << _décodages_wsjt`, `_les r << _éléments « collection » réponse`. Un rang constant revient en forme littéraire en chiffres (`le 3e élément`), un rang calculé en `l'élément de rang … des …`.

### 13.11 Tables *(2.91)*

```
Les tarifs valent la table où « 7601 » vaut 24 et « 7602 » vaut 12.
Les compteurs valent une table vide.
Dans les compteurs, « CQ » vaut 0.
Dans les compteurs, « CQ » devient la valeur de « CQ » dans les compteurs + 1.
Afficher la valeur de « 7601 » dans les tarifs.                  →  24
Pour chaque code parmi les clés des tarifs :
    Afficher code puis la valeur de code dans les tarifs.
Si « 7603 » n'est pas parmi les clés des tarifs, afficher « inconnu ».
Les tarifs perdent « 7602 ».
Afficher le nombre de tarifs puis les tarifs.                    →  1 7601 : 24
```

- Une **table** range des valeurs sous des clés : à chaque clé, une valeur. Elle se nomme au pluriel, avec `les`, comme un ensemble (§ 13.9), et se crée avec `valent une table vide` ou `valent la table où` suivi de paires `clé vaut valeur`, séparées par des virgules et un `et`. Une clé est un texte, un nombre, une date, une heure, une année, vrai ou faux, ou un objet ; une valeur, n'importe quelle valeur sauf absente. Deux clés sont la même si elles sont égales comme pour `=` : `2` et `2,0`, un objet par identité.
- **Créer une clé, la modifier : deux verbes**, comme pour un nom (§ 2.1). `Dans les compteurs, « CQ » vaut 0.` ajoute la clé ; si elle y est déjà : « « CQ » est déjà une clé des compteurs. Pour changer sa valeur, écris : Dans les compteurs, « CQ » devient … » `Dans les compteurs, « CQ » devient 5.` change sa valeur ; si elle n'y est pas : « « CQ » n'est pas une clé des compteurs. Pour l'ajouter, écris : Dans les compteurs, « CQ » vaut … » Une faute de frappe dans une clé ne crée jamais d'entrée en silence. Dans `la table où`, une clé écrite deux fois est refusée de même.
- **Lire.** `la valeur de « CQ » dans les compteurs`. Une clé absente est une erreur, rattrapable par `Essayer` : « « CQ » n'est pas une clé des compteurs. » ; la vérifier d'abord : `Si « CQ » est parmi les clés des compteurs`. La clé s'étend jusqu'à `dans`. La tournure ne vaut que si `dans les …` désigne une table déclarée, plus loin dans la phrase : un champ `valeur` garde son sens partout ailleurs.
- **Retirer.** `Les compteurs perdent « CQ ».` retire la clé et sa valeur ; une clé absente ne change rien, comme pour un ensemble. `gagnent` n'existe pas pour une table : « Une table ne gagne pas : écris « Dans les compteurs, … vaut … ». »
- **Les clés, les valeurs.** `les clés des tarifs`, `les valeurs des tarifs` sont des listes (§ 13.10), dans l'ordre où les clés sont arrivées ; une clé modifiée garde sa place. Elles se parcourent (`Pour chaque code parmi les clés des tarifs :`, la table figée au début de la boucle), se testent (`parmi`), se rangent (`Les codes valent les clés des tarifs.`).
- `le nombre de tarifs` compte les clés. `Afficher les tarifs.` écrit `7601 : 24, 7602 : 12`. `Les autres valent les tarifs.` copie : changer l'une ne change pas l'autre. `Les tarifs deviennent …` remplace tout le contenu, par une table seulement.
- **Égalité.** Deux tables sont égales si elles ont les mêmes clés, avec les mêmes valeurs, dans un ordre quelconque. Une table n'est égale qu'à une table.
- Une table ne se mélange pas aux ensembles : `parmi les tarifs`, `le 2e élément des tarifs`, `les tarifs jointes par` sont refusés à l'analyse, avec la tournure de la table (« « tarifs » est une table : « parmi les clés des tarifs ». »).
- Un calcul emploie une table librement, y compris une table locale qu'il remplit : elle n'est visible que de lui.
- Une table ne se range pas dans un champ d'entité : une entité, avec un champ unique pour la clé, est la table conservée.
- `dans`, en tête de phrase, devient un mot de construction (§ 10.7) : il ne commence plus le nom d'une action. Aucun programme du dépôt n'en avait.
- Aucun autre mot réservé : `table`, `où`, `valeur`, `clés`, `valeurs` ne comptent qu'ainsi placés.
- En forme compacte : `_les tarifs << _table « 7601 » _vaut 24 ; « 7602 » _vaut 12`, `_les c << _table_vide`, `_dans compteurs « CQ » _vaut 0`, `_dans compteurs « CQ » _devient 5`, `_valeur(« CQ » ; compteurs)`, `_clés(tarifs)`, `_valeurs(tarifs)`, `_les tarifs _perdent « 7602 »`.

### 13.12 Trier *(2.96)*

```
Les notes valent la liste 6, 4,5 et 5.
Afficher les notes triées.                                 →  4,5, 5, 6
Afficher les notes triées par ordre décroissant.           →  6, 5, 4,5
Les noms valent la liste « élodie », « Zoé » et « Émile ».
Afficher les noms triés.                                   →  élodie, Émile, Zoé
Afficher les inscrits triés par nom puis les inscrits triés par cotisation décroissant.
```

- **`les notes triées`** rend une **nouvelle liste**, triée du plus petit au plus grand ; la liste d'origine ne change pas. Elle se range (`Les classées valent les notes triées.`), s'affiche, se parcourt. Un ensemble se trie aussi, et rend une liste. `triés` ou `triées`, selon le genre du nom : les deux s'acceptent, et la forme canonique garde celui qui est écrit.
- **`par ordre décroissant`**, du plus grand au plus petit ; `par ordre croissant`, l'ordre par défaut, s'écrit au besoin.
- **`par nom`** trie une liste d'objets par un champ, comme `, par nom` trie une recherche (§ 16.4) ; `par cotisation décroissant` à l'envers (`décroissant` invariable, comme dans la base). Un élément qui n'est pas un objet, ou dont l'entité n'a pas ce champ : « « nom » : l'élément 3 n'a pas ce champ. » ; un champ absent : « Le champ « nom » est absent : vérifie-le d'abord avec « est présent ». » (sorte `absence`).
- **Ce qui se trie** : des nombres, des dates, des heures, et **des textes, dans l'ordre du dictionnaire, sans tenir compte des accents ni de la casse**, exactement comme la base trie un champ texte (collation `GRYM_TEXTE`, la même fonction) : `élodie`, `Émile`, `Zoé`. Tous de la même sorte : « Tri impossible : un nombre et un texte ne se comparent pas. » Vrai ou faux, un objet, un fichier ne se trient pas.
- **Stable** : deux éléments égaux gardent leur ordre d'arrivée, comme dans la base. Deux tris successifs font donc un tri sur deux clés : trier par prénom, puis le résultat par nom, range par nom puis par prénom.
- Un calcul trie librement.
- `triés` et `triées` ne comptent qu'ainsi placés, après une liste ou un ensemble ; aucun mot réservé.
- **Par un calcul** *(2.97)* : `les mots triés par le calcul longueur`, `… par le calcul poids décroissant`. Le calcul est déclaré, avec **un** paramètre ; il est appelé une fois par élément, dans l'ordre de la liste, et ce qu'il rend est la clé du tri, qui suit les règles ci-dessus (nombres, dates, heures ou textes, d'une seule sorte). Un nom qui n'est pas un calcul : « « poids » n'est pas un calcul déclaré. » ; un calcul à deux paramètres : « « poids » attend 2 paramètres : pour trier, un calcul n'en a qu'un, l'élément. » ; une erreur dans le calcul arrête le tri, avec sa position dans le calcul. Les clés se calculent une fois chacune, jamais à chaque comparaison : un calcul coûteux reste raisonnable.

```
Le poids d'un mot vaut la longueur de mot.
Les mots valent les mots de « le chat dort profondément ».
Afficher les mots triés par le calcul poids.               →  le, chat, dort, profondément
```

- En forme compacte : `_trier(notes)`, `_trier(notes ; _décroissant)`, `_trier(inscrits ; nom)`, `_trier(inscrits ; cotisation ; _décroissant)`, `_trier(mots ; _calcul poids ; _décroissant)`. La forme compacte ne garde pas le genre : `triés` revient par défaut.

Un vrai affichage des objets reste à concevoir. L'absence de valeur est faite (§ 16.9).

## 14. Dates *(v0.3)*

### 14.1 Écriture

```
La date d'inscription du membre devient 21.09.2026.
```

- Une date s'écrit `jour.mois.année`, l'année sur quatre chiffres : trois groupes de chiffres séparés par des points, sans espace. `21.09` ou `21.9.26` sont des erreurs.
- Le lexeur la distingue d'un nombre sans ambiguïté : le point décimal est déjà refusé (`3.5`), et le point final d'une phrase suit la date sans la toucher (`… 21.09.2026.`).
- Forme canonique : jour et mois sur deux chiffres. `1.3.2026` devient `01.03.2026`.
- Calendrier grégorien, années 1 à 9999. Une date impossible est refusée à l'analyse : « Le 31 février 2026 n'existe pas. » Le 29 février n'existe que les années bissextiles.

### 14.2 Calcul

| Opération | Résultat |
|---|---|
| date + nombre entier | date, tant de jours plus tard |
| date − nombre entier | date, tant de jours plus tôt |
| date − date | nombre de jours, négatif si la seconde est plus tardive |
| date ± n `jours`, `semaines`, `mois`, `ans` | date, tant de jours, de semaines, de mois ou d'années plus tard ou plus tôt |
| année ± n `ans` | année (§ 14.5) |
| `=`, `≠`, `<`, `≤`, `>`, `≥` entre deux dates | ordre chronologique |

#### Mois, années, semaines

```
L'échéance vaut la date d'adhésion + 1 an.
Le rappel vaut échéance − 2 semaines.
La fin d'essai vaut aujourd'hui + 3 mois.
Pour chaque échéance du 31.01.2026 au 31.12.2026 par pas de 1 mois :
    Afficher échéance.                             →  31.01.2026, 28.02.2026, 31.03.2026, 30.04.2026…
```

- **Dernier jour du mois.** Un jour qui n'existe pas dans le mois d'arrivée est ramené au dernier jour de ce mois : le 31.01.2026 + 1 mois donne le 28.02.2026 ; le 29.02.2028 + 1 an donne le 28.02.2029. C'est la règle des échéances et des contrats. Elle ne se renverse pas : 31.01.2026 + 1 mois − 1 mois donne le 28.01.2026.
- Unités : `jour`, `semaine`, `mois`, `an` ou `année`. Une semaine vaut 7 jours, un an 12 mois. `d + 30` et `d + 30 jours` disent la même chose.
- L'unité suit l'opérande de droite d'un `+` ou d'un `−` et porte sur tout ce terme : `d + 2 × n mois` ajoute 2 × n mois ; une somme se met entre parenthèses, `d + (n + 1) mois`. Les opérations s'appliquent de gauche à droite : `d + 1 mois + 10 jours` ajoute d'abord le mois.
- `3 mois` n'est pas une valeur : « Une durée ne se range pas seule : elle s'ajoute à une date ou s'en retire, « d + 3 mois », « d − 1 an ». » Une durée variable se range comme un nombre : `d + (périodicité de la cotisation) mois`.
- Accord avec un nombre écrit : `0 jour`, `1 an`, `2 ans`, « Accord : « 2 ans ». » `mois` ne change pas. Avec une valeur calculée, les deux formes sont admises.
- Le nombre doit être entier : `d + 1,5 mois` est une erreur, « Une date se décale d'un nombre entier de mois. », jamais un arrondi. `3,00 mois` est admis.
- Une unité termine le nom qui la précède, comme `à` ou `fois` : `d + n mois` lit le nom `n`. Un nom déclaré qui contient l'unité l'emporte (plus longue correspondance, § 2.2).
- Une année (§ 14.5) se décale d'années : `l'année de d + 10 ans`. « Une année se décale d'années, pas de mois. »
- Boucles : `par pas de 1 mois`, `de 2 semaines`, `de −3 mois` (à rebours). Le compteur vaut le départ plus k pas, recalculé depuis le départ à chaque tour : 31.01, 28.02, puis 31.03, et non 28.03. Un tableur qui décale la cellule précédente dérive, lui, au 28 de chaque mois.
- Reporté : l'écart entre deux dates en mois ou en années (une ancienneté), qui pose son propre problème d'arrondi.
- Vérification (29 septembre 2026) : 5'251 opérations (tirées au hasard, plus toutes les fins de mois et tous les 29 février de 1900, 2000, 2024, 2026 et 2100) donnent les mêmes résultats que `relativedelta` de la bibliothèque Python `dateutil`, qui applique la même règle ; 26 résultats hors du calendrier sont refusés.
- Toute autre opération est une erreur à l'exécution : « On n'additionne pas deux dates. »
- `jours + date` vaut `date + jours`. Le décalage accepte `3,00`, pas `2,5`.
- Les boucles et `Selon` acceptent les dates : `Pour chaque jour du 01.01.2026 au 31.01.2026` avance d'un jour, `par pas de 7` d'une semaine ; `Cas de 01.07.2026 à 31.08.2026` teste un intervalle.
- Vérification (21 septembre 2026) : les 3 652 059 jours du calendrier et 2 951 opérations tirées au hasard donnent les mêmes résultats que le module `datetime` de Python.

### 14.3 `aujourd'hui`

- `aujourd'hui` vaut la date du jour, selon l'horloge locale, lue une fois au début de l'exécution : une exécution qui franchit minuit garde la même date du début à la fin.
- Un calcul ne peut pas l'utiliser (§ 9.4, pureté) : « Un calcul ne dépend pas du jour : passe la date en paramètre. »
- En forme compacte : `_aujourd'hui`.

### 14.4 Affichage et stockage

- En base : texte ISO 8601, `2026-09-21`, qui se trie dans l'ordre chronologique.
- `Afficher` écrit la forme de la source : `21.09.2026`.
- Reporté : l'heure, un type `(moment)`, et les fuseaux horaires.

### 14.5 Années

```
Une œuvre, conservée, a :
    un titre (texte),
    une composition (année).

La composition de o devient 1747.
Afficher composition de o.                         →  1747
Afficher composition de o + 3.                     →  1750
Afficher l'année de 21.09.2026.                    →  2026
Si l'année de la date de sortie > 1700, …
```

- Une année est une valeur à part entière, de 1 à 9999, comme les dates. Elle s'affiche sans séparateur de milliers : `1747`, jamais `1'747`.
- Aucun littéral : une année naît d'un champ `(année)`, où un nombre entier de 1 à 9999 devient une année (comme `3,0` devient un nombre entier dans un champ `(nombre entier)`, § 16.2) ; de `l'année de d`, champ intégré de toute date, qui ne réserve rien (§ 15.3) ; ou d'un calcul sur une année. `Le millésime vaut 1747.` crée un nombre : il s'affiche `1'747` (§ 4.1), mais la source garde `1747` (§ 12).
- Ranger `2,5`, `0` ou `12000` dans un champ `(année)` est une erreur : « Le champ « composition » attend une année (de 1 à 9999), pas 2,5. »

| Opération | Résultat |
|---|---|
| année + nombre entier, nombre entier + année, année − nombre entier | année |
| année − année | nombre d'années |
| année + année | erreur : « On n'additionne pas deux années. » |
| ×, ÷, ^, opposé | erreur : « On ne multiplie pas une année. »… |

- Une année se compare à une année ou à un nombre, par valeur : `année de o > 1700`, `1747 = année de o`. Elle ne se compare pas à une date : « Une année ne se compare pas à une date : compare l'année de la date, « l'année de d ». »
- `l'année de d` est permis dans un calcul : il ne dépend que de son paramètre. `l'année d'aujourd'hui` reste réservé aux actions (§ 14.3).
- Boucles : le compteur est une année dès que le début en est une (`Pour chaque an de l'année de d à 2026`). `Selon` : `Cas de 1700 à 1750` compare par valeur.
- En base : `INTEGER`. Une condition `dont` compare le champ à une année ou à un nombre ; le tri est numérique.
- Migrations (§ 16.7) : `(nombre entier)` devient `(année)` si toutes les valeurs conservées sont entre 1 et 9999, sinon refus avec leur nombre ; `(année)` redevient `(nombre entier)` sans perte.
- `année` est un nom de type : une entité ne peut pas le porter, pas plus que `texte` ou `date`.
- En forme compacte : `(année)`, `d.année`.

### 14.6 Écart entre deux dates

```
L'âge vaut le nombre d'années entre la naissance du membre et aujourd'hui.
Afficher le nombre de mois entre 31.01.2026 et 28.02.2026.     →  1
```

- `le nombre de jours`, `de semaines`, `de mois`, `d'années` (ou `d'ans`) `entre d1 et d2` compte les unités **entières** de d1 à d2 : l'âge, l'ancienneté, la durée d'un abonnement.
- Mois et années suivent la règle de `d + 3 mois` (§ 14.2) : c'est le plus grand n tel que `d1 + n mois` ne dépasse pas d2. Du 31.01 au 28.02, 1 mois ; au 27.02, 0. Du 29.02.2024 au 28.02.2025, 1 an : l'anniversaire d'un 29 février tombe le 28 les années ordinaires.
- Semaines : les jours divisés par 7, sans les jours restants.
- Si d2 précède d1, l'écart est négatif : `le nombre d'années entre 21.09.2026 et 21.09.1990` vaut −36.
- La seconde date se lie comme un argument (§ 9.1) : `entre d et (d + 3)`.
- Deux dates seulement : « Un écart se mesure entre deux dates, pas avec un nombre. » L'écart en jours reste aussi `d2 − d1` (§ 14.2).
- Aucun mot réservé. En forme compacte : `_écart_jours(d1 ; d2)`, `_écart_semaines`, `_écart_mois`, `_écart_années`.
- `le nombre de jours ouvrables entre d1 et d2` *(2.4)* : les jours du lundi au vendredi, d1 exclu et d2 compris, comme `d2 − d1` ; négatif à rebours. Du lundi 12 au vendredi 16 octobre 2026 : 4. Les jours fériés ne sont **pas** retirés : ils changent d'un canton à l'autre. Forme compacte : `_écart_ouvrables(d1 ; d2)`.

### 14.7 Heures

```
Une réunion, conservée, a : un titre (texte), un début (heure).
Le début vaut 19:30.
Afficher le début + 15 minutes.                         →  19:45
Afficher le nombre de minutes entre 8:05 et 19:30.      →  685
Si maintenant > 12:00, afficher « Bon après-midi ».
```

- Une heure s'écrit `heures:minutes`, sans espace : `14:30`, `8:05`, de `0:00` à `23:59`. Sinon : « L'heure « 24:00 » n'existe pas : de 0:00 à 23:59. » C'est l'heure locale, sans fuseau ni secondes : un horaire, un rendez-vous, un planning. Une date et une heure restent deux valeurs distinctes : décision du 12 octobre 2026.
- `Afficher` l'écrit sur deux chiffres, comme un horaire : `08:05`. La forme canonique du programme garde `8:05`.
- `maintenant` est l'heure, lue une fois au début de l'exécution, comme `aujourd'hui` (§ 14.3) ; interdit dans un calcul et un exemple (§ 9.4, § 9.6). Un nom `maintenant` déclaré l'emporte.
- `h + 15 minutes`, `h − 2 heures` : l'heure tourne comme une horloge, `23:50 + 15 minutes` donne `00:05`. Une heure ne se décale que de minutes ou d'heures ; un nombre seul est refusé (« ADDITION impossible avec une heure »), comme les jours ne s'ajoutent qu'à une date.
- `h2 − h1` donne des minutes, négatives si h2 précède : `14:30 − 8:05` vaut 385. `le nombre de minutes entre h1 et h2`, `le nombre d'heures entre h1 et h2` comptent les unités entières (§ 14.6).
- Les heures se comparent (`<`, `≥`…) et se trient. Un champ `(heure)` d'entité se range en texte `14:30`, triable ; dans `dont`, `le début est supérieur à 12:00`.
- *(2.54)* Une heure saisie (une zone, `t en heure`, une réponse) se tape aussi `13h30`, `13.30`, `13 30`, `1330`, `930` ou `13h`.
- `t en heure` lit un texte (§ 4.5) ; `la réponse en heure à « Début ? »` aussi (§ 17) : « « 25:00 » n'est pas une heure : écris heures:minutes (14:30). »
- Les unités `minute`, `minutes`, `heure`, `heures` après un nombre, comme `jours` ou `mois`, ne réservent rien ailleurs.
- En forme compacte : `8:05 + 15 _minutes`, `_maintenant`, `_écart_minutes(h1 ; h2)`, `_écart_heures(h1 ; h2)`, `t _en_heure`.

## 15. Fichiers et images *(v0.3)*


### 14.8 Le mois, le jour, en toutes lettres *(2.4)*

```
Afficher le mois de l'échéance.                  →  10
Afficher le jour de l'échéance.                  →  16
Afficher le jour de la semaine de l'échéance.    →  vendredi
Afficher l'échéance en toutes lettres.           →  vendredi 16 octobre 2026
```

- Comme `l'année de d` (§ 14.5) : `le mois de d` et `le jour de d` sont des nombres entiers ; `le jour de la semaine de d` est un texte, de « lundi » à « dimanche ».
- `d en toutes lettres` est un texte : le jour de la semaine, le jour (« 1er » le premier du mois), le mois, l'année. Seulement d'une date : « « en toutes lettres » s'applique à une date ».
- Forme compacte : `d.mois`, `d.jour`, `d.jour_de_la_semaine`, `d _en_toutes_lettres`.

### 15.1 Valeurs

- Deux sortes de valeurs : un **fichier**, contenu quelconque, et une **image**, dont les premiers octets désignent un format PNG, JPEG, GIF ou WebP.
- Leur contenu voyage avec la valeur : dans une entité, il est rangé dans la base, pas sous forme de chemin.
- Taille maximale : celle de SQLite, 1 000 000 000 octets (`SQLITE_MAX_LENGTH`).

### 15.2 Lire et écrire

```
La photo du membre devient le fichier « photos/ana.jpg ».
Enregistrer la photo du membre dans « copie.jpg ».
```

- `le fichier « chemin »` lit un fichier du disque. Le chemin est un texte écrit tel quel, ou une expression entre parenthèses : `le fichier (chemin)`. Sans texte ni parenthèse après lui, `le fichier` reste un nom ordinaire.
- Un chemin relatif part du dossier du programme ; dans la boucle interactive, du dossier courant. Fichier absent ou illisible : erreur à l'exécution.
- Une image est un fichier dont la signature est reconnue. Ranger dans un champ `(image)` un fichier qui n'en est pas une est une erreur : « « rapport.pdf » n'est pas une image (PNG, JPEG, GIF ou WebP). »
- *(2.83)* `t en fichier` fait d'un texte un fichier, en UTF-8, tel quel, pour l'enregistrer : `Le f vaut le carnet en fichier.` puis `Enregistrer le f dans « carnet.adi ».` Un carnet de trafic, un fichier de réglages, un texte pour un autre logiciel. Autre chose qu'un texte : « « en fichier » s'applique à un texte, pas à un nombre. » Forme compacte : `t _en_fichier`.
- `Enregistrer … dans « chemin ».` écrit le contenu. Un fichier existant n'est jamais écrasé : erreur, vérifiée à la phrase puis au moment d'écrire. Enregistrer deux fois au même chemin dans une exécution est aussi une erreur. *(Écraser explicitement demandera sa propre tournure.)*
- `enregistrer` ne peut pas commencer le nom d'une action.
- Lire ou écrire sur le disque est un effet de bord : réservé aux actions, jamais aux calculs.
- Écrire sur le disque ne s'annule pas avec la transaction : les écritures sur le disque sont donc différées à la fin de l'exécution, et n'ont lieu que si elle réussit. Elles se font toutes ou aucune : si l'une échoue, celles déjà faites sont retirées, et l'exécution échoue.

### 15.3 Ce qu'on en connaît

| Tournure | Valeur |
|---|---|
| `la taille de la photo` | nombre d'octets |
| `le format de la photo` | texte : `« PNG »`, `« JPEG »`, `« GIF »`, `« WebP »`, ou `« inconnu »` pour un fichier |
| `le nom de fichier de la photo` | texte : le nom du fichier d'origine, sans son dossier |

- Ces trois noms (`taille`, `format`, `nom de fichier`) sont des champs de toute valeur fichier. Ils ne réservent rien : une classe peut déclarer des champs du même nom, un calcul peut s'appeler `taille`, et un nom déclaré l'emporte toujours.
- Un fichier ne se modifie pas. Deux fichiers sont égaux s'ils ont le même contenu ; ils ne se comparent pas par ordre.
- `Afficher la photo.` écrit `une image JPEG de 2,3 Mo`, ou `un fichier de 7 octets`. Unités : octets, Ko, Mo, Go, en puissances de 1000, une décimale au plus, arrondie au plus proche (1 050 octets : `1,1 Ko`).
- Afficher l'image elle-même attend les interfaces graphiques (charte, art. 11).

### 15.4 Exporter un tableau

```
Exporter les membres conservés, par nom, avec le nom, le prénom et la cotisation, dans « membres.csv ».
Exporter les œuvres conservées dont la cotisation est positive dans « œuvres.csv ».
```

- Les objets d'une recherche (§ 16.4), avec `dont` et `, par …` comme une boucle, deviennent les lignes d'un tableau CSV, qu'un tableur ouvre directement.
- `, avec …` choisit les colonnes, comme une liste d'écran (§ 22.1) : des champs de l'entité, ou le champ d'un objet qu'elle désigne (`le nom du compositeur`). Le titre de chaque colonne reprend ce qui est écrit, sans l'article et avec une majuscule : `Nom du compositeur`. Sans `avec`, tous les champs, sauf les fichiers, les images et les champs multiples.
- **Le fichier** suit la règle d'`Enregistrer` (§ 15.2) : écrit à la fin d'une exécution réussie, jamais si elle échoue, jamais par-dessus un fichier existant (« « membres.csv » existe déjà : il n'est jamais écrasé. »), jamais deux fois le même chemin dans une exécution.
- **Le format.** UTF-8 avec marque d'ordre (les tableurs reconnaissent alors les accents), séparateur `;`, lignes terminées par CR LF, cellules entre guillemets quand elles contiennent un `;`, un guillemet (doublé), un retour à la ligne ou une espace au bord (RFC 4180, citée de mémoire). Nombres en forme exacte, virgule décimale, sans séparateur de milliers (`1234,5`, `-3`) ; dates `jour.mois.année` ; `vrai`, `faux` ; un lien par sa clé (§ 22.1) ; un champ absent : une cellule vide.
- Le point-virgule et la virgule décimale sont ce qu'attend un tableur réglé en français ou en suisse romand. Ce réglage dépend de l'ordinateur : non vérifié sur toutes les versions d'Excel, de Numbers et de LibreOffice.
- Aucun mot réservé : `Exporter` n'est une phrase que suivi de `les` et d'une entité conservée. `Pour exporter les clients :` reste une action, appelée par `Exporter les clients.`
- Un calcul n'exporte pas (§ 9.4) : « Un calcul n'écrit pas sur le disque : exporte dans une action. »
- Lire un tableau : § 15.5.
- En forme compacte : `_exporter membre _conservé [_dont …] [_par nom [_décroissant]] [_avec nom ; compositeur.nom] _dans « m.csv »`.

### 15.6 Un rapport *(implémenté le 16 octobre 2026)*

```
Exporter les membres conservés, par nom, avec le nom, la catégorie et la cotisation,
    avec le titre « Cotisations 2026 », avec le total de la cotisation, dans « cotisations.pdf ».
```

- **L'extension décide du format.** `.csv` : le tableau (§ 15.4). `.html` : une page prête à imprimer (A4, l'en-tête du tableau répété à chaque page, la date d'édition en pied), que la machine écrit elle-même, partout. `.pdf` : la même page mise en page par l'atelier ou par une application fabriquée, numérotée « page 1 / 3 » ; en console et dans le navigateur, « Un PDF se fabrique dans l'atelier ou dans une application fabriquée ; ici, exporte en « .html » et imprime la page depuis le navigateur. »
- `, avec le titre « … »` : en tête du rapport ; un CSV n'en a pas.
- `, avec le total de la cotisation [et du montant]` : une dernière ligne « Total », la somme des nombres de chaque colonne nommée ; aussi dans un CSV. Une colonne exportée seulement : « Le total porte sur une colonne exportée : ajoute « … » aux colonnes. »
- Les nombres s'alignent à droite ; un mot de passe s'écrit « •••••• » (§ 22.14).
- **Où s'écrit le fichier** : à côté du programme, comme `Enregistrer` (§ 15.2) ; dans une application fabriquée, qui n'est pas modifiable, dans le dossier `Documents/<Nom de l'application>` de l'utilisateur.
- Forme compacte : `_exporter membre _conservé _avec nom ; cotisation _titre « C » _total cotisation _dans « m.pdf »`.

### 15.6 bis Sauvegarder la base *(2.29, implémenté le 20 octobre 2026)*

```
Sauvegarder la base dans « Sauvegarde Cabinet.grymd ».
Sauvegarder la base dans (« Cabinet du » suivi d'aujourd'hui suivi de « .grymd »).
```

- Une copie complète de la base du programme, telle que l'exécution la voit à cette phrase (ce qu'elle a conservé avant compris), dans un fichier qu'un atelier restaure (docs/atelier.md, § 5 sexies) ou qu'un programme ouvre comme base.
- Écrite comme `Enregistrer` (§ 15.2) : à la fin de l'exécution réussie, jamais si elle échoue ; à côté du programme, ou dans `Documents/<Nom de l'application>` d'une application fabriquée (§ 15.6). Jamais par-dessus un fichier : si le nom est pris, sur le disque ou par cette exécution, la copie s'appelle « Sauvegarde Cabinet (2).grymd », puis « (3) »…
- Un programme sans entité conservée n'a pas de base : « Ce programme n'a pas de base : il ne conserve aucune entité. ». Un calcul ne sauvegarde pas (§ 9.4).
- Aucun mot réservé : `Sauvegarder` n'est cette phrase que suivi de `la base dans`.
- **Une par jour passé** *(2.53)* : `Sauvegarder la base dans « Sauvegardes/Praxis », une par jour passé.` Le chemin devient un préfixe : la copie s'appelle `Praxis-2026-10-24-1017.coffre` (`.grymd` pour une base non chiffrée), à la date et à l'heure de la phrase ; puis, dans ce dossier, pour chaque jour avant aujourd'hui, seule la copie la plus récente de ce préfixe reste, les autres s'effacent. Aucun autre fichier n'est touché, ni un autre préfixe. Sans mot de passe : « une par jour passé » ne se combine pas avec `avec le mot de passe`. Forme compacte : `_sauvegarder _dans « … » _une_par_jour_passé`.
- Forme compacte : `_sauvegarder _dans « c.grymd »`.

#### Le coffre *(2.30)*

```
Sauvegarder la base dans « ~/Sauvegardes/Cabinet.coffre » avec le mot de passe le code de l'écran.
Restaurer la base depuis la sauvegarde de l'écran avec le mot de passe le code de l'écran.
Restaurer la base depuis « Cabinet 20.10.2026.grymd ».
```

- **`avec le mot de passe m`** chiffre la copie : rien ne l'ouvre sans `m`. La clé vient du mot de passe par Argon2id (RFC 9106, 64 Mio, 3 passes, un sel aléatoire de 16 octets), le contenu est chiffré et authentifié par XChaCha20-Poly1305 (un nonce aléatoire de 24 octets), par Monocypher 4.0.2 (`vendor/monocypher`). Le fichier commence par `GRYMCOFFRE1` ; un octet changé, ou un en-tête retouché, et il ne s'ouvre plus. Le mot de passe n'est rangé nulle part : perdu, la copie est perdue.
- **`Restaurer la base depuis f`** : `f` est un chemin ou un fichier (une zone `(fichier)` d'un écran). Une copie chiffrée demande son mot de passe ; elle est déchiffrée, puis vérifiée : une base GrymoiR (table `grym_schema`) intacte (`PRAGMA integrity_check`). Sinon, rien ne change : « Mot de passe faux, ou sauvegarde abîmée : elle ne s'ouvre pas. », « Cette sauvegarde est chiffrée : il faut son mot de passe. », « Ce fichier n'est pas une base GrymoiR. ».
- **Quand elle a lieu.** La copie vérifiée s'écrit à côté de la base, `base.grymd.restaurer`, à la fin de l'exécution réussie, comme `Enregistrer` ; elle prend la place de la base **à la prochaine ouverture**, avant qu'aucun objet ne soit lu : un programme en cours garde en mémoire des objets de l'ancienne base, qui ne doivent pas se mêler à la nouvelle. La base d'avant n'est jamais perdue : elle devient `base.grymd avant restauration 2026-10-20 14h26.grymd`. Une seconde restauration demandée avant la réouverture remplace la première.
- **Chemins** *(2.30, pour toutes les écritures)* : `~/` en tête désigne le dossier de l'utilisateur (`~/Library/Mobile Documents/com~apple~CloudDocs/Cabinet`, le dossier iCloud Drive sur un Mac) ; un dossier qui manque se crée, pour `Enregistrer`, `Exporter` et `Sauvegarder`. `Sauvegarder` crée et essaie son dossier dès la phrase : une sauvegarde impossible (un dossier synchronisé absent, un disque plein) se dit là, où `Essayer` la rattrape (« Sauvegarde impossible dans « … » : le dossier ne s'écrit pas. »).
- Forme compacte : `_sauvegarder _dans « c.coffre » _mot_de_passe m`, `_restaurer _depuis f [_mot_de_passe m]`.
- *(2.85)* **`Vérifier la sauvegarde f [avec le mot de passe m].`** fait tout ce que `Restaurer` fait avant de remplacer : la copie se lit, se déchiffre (un coffre de format 2 avec son mot de passe, sinon celui de la séance ; un coffre de format 1 avec le sien), puis se vérifie : une base GrymoiR (`grym_schema`), intacte (`PRAGMA integrity_check`). Rien ne change, ni la base ni le disque. Sinon, l'échec se rattrape et dit pourquoi : « Ce fichier n'est pas une base GrymoiR. », « Ce fichier n'est pas une base GrymoiR intacte : la sauvegarde est abîmée ou incomplète. », « Mot de passe faux, ou sauvegarde abîmée : elle ne s'ouvre pas. », « Aucune sauvegarde à vérifier. » (f absent). Le programme n'a pas besoin d'une base pour vérifier une copie en clair. Après « Vérifier la sauvegarde », un mot qui n'est ni « de », « du », « des » ni « d' » : `Vérifier la sauvegarde du cabinet.` reste une action du programme. Pas dans un calcul : « Un calcul ne lit pas le disque : vérifie la sauvegarde dans une action. » Forme compacte : `_vérifier_sauvegarde f [_mot_de_passe m]`.
- *(2.85)* **`la dernière sauvegarde de « Sauvegardes/Praxis »`** : le chemin de la copie la plus récente qu'a écrite `Sauvegarder la base dans « Sauvegardes/Praxis », une par jour passé.` (`Praxis-AAAA-MM-JJ-HHMM[ (n)].coffre` ou `.grymd`, la date puis l'heure puis le rang), ou absent s'il n'y en a aucune. Les copies écrites par l'exécution en cours, qui n'arrivent sur le disque qu'à sa fin, n'y sont pas encore. Après « de », un texte écrit ou une parenthèse : `la dernière sauvegarde du cabinet` reste le champ d'un objet. Pas dans un calcul. Forme compacte : `_dernière_sauvegarde « p »`. Praxis vérifie ainsi, une fois par jour, la copie de la veille avant d'écrire celle du jour : `Vérifier la sauvegarde (la dernière sauvegarde de (dossier suivi de « /Praxis »)).`

### 15.7 Un son *(implémenté le 16 octobre 2026)*

```
Une visite, conservée, a :
    un lieu (texte),
    un mémo (son), facultatif.
```

- `(son)` est un fichier dont la signature est reconnue, comme `(image)` (§ 15.3) : WAV, MP3, AAC, M4A, Ogg, WebM, FLAC. Un autre fichier est refusé : « « contrat.pdf » n'est pas un son (WAV, MP3, M4A, Ogg, WebM ou FLAC). » `le format du mémo` donne le format reconnu.
- M4A se reconnaît à son conteneur MP4, qui peut aussi porter une vidéo : on n'en écouterait que le son.
- Dans le navigateur, `Afficher le mémo de v.` et les fiches, affichée ou déduite *(2.9)*, montrent un lecteur, avec ses commandes. Une zone `(son)` accepte un fichier sonore ; sur un téléphone, elle propose aussi d'enregistrer au micro (attribut `capture`, selon le navigateur du téléphone).
- Dans l'atelier et les applications fabriquées *(2.9)*, une zone `(son)` accepte un fichier sonore, l'écoute (« ▶ Écouter ») et l'enregistre au micro (« ● Enregistrer », un second clic arrête) ; l'enregistrement remplit la zone comme un fichier choisi, et la transcription suit (§ 15.8). Le format enregistré est le premier que le système sait écrire parmi M4A, Ogg, FLAC et WAV. `Afficher le mémo de v.` pose un lien « ▶ Écouter » dans le fil ; les fiches, un lecteur. Sous macOS, le système demande une fois l'accord pour le micro.
- La transcription en texte (`transcrite du mémo`) est à l'étude : reconnaissance locale (Whisper), rien ne quitte l'ordinateur.

### 15.8 Une transcription *(étape 1, le 17 octobre 2026)*

```
Une visite, conservée, a :
    un mémo (son), facultatif,
    une note (texte), facultative, transcrite du mémo.
```

- `, transcrite du mémo` (accordé : `transcrit` pour un champ masculin ; `du`, `de la`, `de l'`) : un champ de texte d'une entité reçoit le texte d'un champ `(son)` de la même entité, déclaré avant lui. Chaque fois que le son change, la machine le fait transcrire et range le texte dans le champ, conservé en base avec lui.
- **Dans un écran aussi** : une zone (texte) peut être `transcrite du mémo`, le mémo étant une zone (son) de l'écran déclarée avant elle. Choisir un son la remplit aussitôt, dans l'atelier et les applications :
  ```
  L'écran de visite montre :
      un mémo (son),
      une transcription (texte), facultative, sur plusieurs lignes, transcrite du mémo.
  ```
  Dans l'atelier, l'entrée « Son » de la palette pose ces deux zones d'un coup, la transcription juste dessous ; ce sont deux éléments, chacun avec sa taille.
- Le texte transcrit reste un texte ordinaire, **que l'utilisateur peut corriger**. Il est remplacé si le son change de nouveau.
- La transcription est faite par l'interface. Sans transcripteur (la console, le navigateur aujourd'hui), le champ reste tel quel.
- **Le transcripteur** (étape 2, le 17 octobre 2026) : Whisper, par whisper.cpp (v1.9.5), dans l'atelier et les applications fabriquées, sur l'ordinateur, sans rien envoyer ; le modèle large-v3-turbo, sans contexte entre les passages (`-mc 0`) et sans découpe du son, configuration retenue après essai sur une assemblée de 71 minutes (complète, sans boucle, environ 12 fois le temps réel sur un Mac à puce Apple). Les phrases que Whisper invente dans les silences sont retirées. Un WAV se lit directement ; MP3, M4A, Ogg, WebM et FLAC passent par le décodeur de Qt. Le modèle (`ggml-large-v3-turbo-q5_0.bin`) est cherché dans l'application, puis dans le dossier de données de GrymoiR ; `GRYMOIR_MODELE_TRANSCRIPTION` en désigne un autre. Le modèle est inclus dans l'atelier (il transcrit hors ligne dès l'installation) et, *(2.39)*, dans toute application fabriquée, qu'elle transcrive ou non : 547 Mo de plus pour chacune ; jusqu'au 22 octobre 2026, seulement si son programme déclarait un champ transcrit. - **La relecture** (étape 3b) : dans la zone de saisie où on lit le texte transcrit, **tous les nombres** sont surlignés, et les **mots dont Whisper doute** (probabilité sous 0,5) sont en plus soulignés ; corriger un mot douteux retire son doute. Les marques sont des caractères Unicode invisibles, rangés dans le texte même (U+2064 en tête : « transcrit » ; U+2062 et U+2063 autour d'un mot douteux), pour qu'elles suivent le texte partout, base, écrans et copies d'un champ à l'autre. Elles ne se voient pas dans une fiche, un rapport ou un PDF, et sont retirées d'un export CSV. Elles comptent dans `la longueur de` un texte, et une recherche `contient` qui chevauche le bord d'un mot douteux ne le trouve pas. En console et dans le navigateur, pas encore de transcription.
- Forme compacte : `_une note (texte) _transcrit mémo`.
- **Sur demande** *(2.78)* : `la transcription de x`, x un son, rend son texte par le même transcripteur, ou absent (sans transcripteur, son muet, échec). Le programme décide alors quand transcrire : `Si le morse du p est absent et la parole transcrite du poste, la transcription du p devient la transcription du son capté.`
- **L'amorce** *(2.80)* : `Amorcer la transcription avec t.`, t un texte, donne au transcripteur le vocabulaire attendu (indicatifs, codes, noms propres, termes d'un métier), pour toutes les transcriptions suivantes de l'exécution : champs transcrits, zones transcrites et `la transcription de x`. Whisper le reçoit comme le texte qui précède, à chaque fenêtre de 30 secondes, et en tient compte pour l'orthographe ; le texte déjà transcrit n'y entre toujours pas (`-mc 0` tenu). `Amorcer la transcription avec absent.` retire l'amorce. Whisper n'en garde que les 223 derniers morceaux de mots (whisper.cpp v1.9.5 : la moitié de son contexte de texte, moins une marque) : une amorce courte, l'essentiel à la fin. Une phrase d'action : « Un calcul n'amorce rien : amorce dans une action. » Sans transcripteur (console, navigateur), elle ne fait rien. Forme compacte : `_amorcer t`.
- **En toute langue** *(2.86)* : `Amorcer la transcription avec t, en toute langue.` fait de même, et laisse Whisper reconnaître la langue parlée : la parole s'écrit dans sa langue, telle qu'elle est dite. Sans cette mention, le français est imposé, comme avant : une parole dans une autre langue risque alors d'être mal transcrite, ou rendue à peu près en français (non mesuré ici). La mention vaut jusqu'à la prochaine phrase `Amorcer` ; sans transcripteur, la phrase ne fait rien. Forme compacte : `_amorcer t _toute_langue`.
- **La langue d'un texte** *(2.86)* : `la langue reconnue de t` rend le code ISO 639-1 de sa langue, `« fr »`, `« en »`, `« de »`, `« es »`, `« it »`, `« nl »` ou `« pt »`, d'après ses mots outils (articles, pronoms, prépositions, salutations courantes), sans accents ni casse, les élisions coupées (« c'est » : « c » et « est ») ; la langue qui en compte le plus l'emporte si elle en a deux au moins et un de plus que la suivante. Sinon, absent : un texte trop court, des indicatifs, des chiffres (« CQ CQ HB9 »). C'est une estimation, sans dictionnaire ni service extérieur ; elle se trompe sur une phrase très courte ou mêlée de deux langues. t absent : absent ; autre chose qu'un texte : « « la langue reconnue de » attend un texte. » Permise dans un calcul. Forme compacte : `_langue t`. Écoutille s'en sert pour n'envoyer à DeepL que la parole qui n'est pas française.
- **Le morse** *(2.78)* : `le morse de x`, x un son (un WAV PCM 16 bits, mono ou non, à toute fréquence), rend le texte lu en capitales (« CQ DE HB9ABC K »), ou absent s'il n'y a pas de morse (de la voix, du bruit). Le décodage se fait sur l'ordinateur (`src/morse.c`) : la note dominante de 300 à 1500 Hz, qui doit porter l'essentiel de l'énergie ; son enveloppe toutes les 4 ms, avec deux seuils ; la durée du point tirée des porteuses et des silences (de 5 à 50 mots par minute) ; la table de l'UIT (lettres, chiffres, ponctuation, « `<SK>` »). Une suite inconnue s'écrit « ? » ; au-delà de trois sur dix, ou moins de deux signes reconnus : absent. Éprouvé sur du morse fabriqué, de 8 à 40 mots par minute, jusqu'à un bruit plus fort que la note.

### 15.5 Lire un tableau

```
Pour chaque ligne du tableau « import.csv » :
    Le m vaut un nouveau membre :
        Le nom vaut la colonne « Nom » de la ligne.
        La cotisation vaut la colonne « Cotisation » de la ligne en nombre.
    Conserver m.
```

- `Pour chaque ligne du tableau « f.csv » :` lit le fichier une fois, au début, et parcourt ses lignes dans l'ordre. Le nom de la ligne est libre (`Pour chaque l du tableau …`), sans article, en lecture seule, comme un compteur ; `Sortir de la boucle.` et `Passer au tour suivant.` s'y emploient.
- La première ligne du fichier porte les titres. Les lignes vides sont ignorées. Une ligne qui n'a pas autant de cellules que de titres arrête la lecture : « « import.csv », ligne 3 : 2 cellules pour 3 colonnes. » ; de même un guillemet jamais refermé.
- `la colonne « Nom » de la ligne` est un texte : la cellule telle qu'écrite. Le titre se reconnaît sans tenir compte des accents ni de la casse, comme `contient` (§ 4.5). Une cellule vide est **absente** (§ 16.9) : un champ facultatif la reçoit, un champ obligatoire la refuse. Une colonne inconnue : « Aucune colonne « Pays » dans ce tableau : Nom, Cotisation, Ville. »
- Les conversions du § 4.5 lisent la cellule : `… en nombre`, `… en date`, `… en vrai ou faux`.
- Le format est celui d'un tableur ou d'un export (§ 15.4) : UTF-8, marque d'ordre ignorée ; séparateur deviné sur la ligne des titres, le plus fréquent de `;`, `,` et la tabulation, hors guillemets ; guillemets selon la RFC 4180 (citée de mémoire) ; lignes CR LF ou LF.
- `Afficher ligne.` montre ses cellules, séparées par « ; ».
- Un calcul ne lit pas de tableau (§ 9.4). Aucun mot réservé : `colonne` et `tableau` ne comptent qu'ainsi placés ; un nom, un calcul ou un champ `colonne` déclaré l'emporte.
- En forme compacte : `_pour_chaque ligne _tableau « f.csv »`, `_colonne(« Nom » ; ligne)`.

### 15.9 Les lignes, le JSON *(3.02)*

```
Pour chaque ligne parmi les lignes du fichier « journal.log » :
    Si ligne contient « ERREUR », afficher ligne.
Les relevés valent les lignes de la réponse.

Les articles valent la liste « pain » et « lait ».
Les données valent la table où « client » vaut « Ana », « total » vaut 12,50 et « articles » vaut les articles.
Afficher le JSON des données.         →  {"client": "Ana", "total": 12.50, "articles": ["pain", "lait"]}
```

- **`les lignes de x`** est une liste (§ 13.10) : les lignes d'un texte, ou d'un fichier lu comme un texte en UTF-8. Le séparateur est le retour à la ligne ; un `\r` qui le précède (fichier Windows) tombe ; un dernier retour à la ligne ne fait pas de ligne vide de plus. Un texte vide n'a aucune ligne. Elle se parcourt (`Pour chaque ligne parmi …`), se range, se trie, comme toute liste. Un fichier qui n'est pas du texte : « Le fichier « photo.png » n'est pas un texte en UTF-8. »
- **`le JSON de x`** est un texte : la valeur écrite en JSON, récursivement. Une **table** (§ 13.11) devient un objet, ses clés dans leur ordre d'arrivée ; une **liste** ou un **ensemble**, un tableau ; un texte, une chaîne, échappée (`\"`, `\\`, `\n`, les caractères de contrôle en `\u00XX`, les autres en UTF-8) ; un nombre, sa forme canonique (`12.50`) ; vrai ou faux, `true`, `false` ; une date, `"AAAA-MM-JJ"` ; une heure, `"HH:MM"` ; une valeur absente, `null`. Une clé de table est un texte, un nombre ou une date, écrits comme ci-dessus.
- **Les refus** : un objet (« Un objet ne s'écrit pas en JSON : range ses champs dans une table. »), un fichier, une clé qui n'est ni texte ni nombre ni date, une imbrication de plus de 64 niveaux.
- `le JSON de x` est un texte : il s'écrit dans un fichier (`… en fichier`), s'affiche, se range. Dans un envoi (§ 22.18), il resterait une chaîne JSON comme tout texte : l'envoi n'a pas encore de corps brut, ce qui permettrait d'y poser un document imbriqué ; reporté. La lecture d'un JSON reste celle du § 22.18 (`le champ « a.b » de r`, `les éléments du champ … de r`).
- Un calcul les emploie librement. `lignes` et `JSON` ne comptent qu'ainsi placés ; un nom déclaré l'emporte.
- En forme compacte : `_lignes x`, `_json(x)`.

### 15.10 Une commande *(3.04)*

```
Les arguments valent la liste « -m », « 2 » et « f ».
La fréquence vaut la sortie de la commande « rigctl » avec les arguments.
Essayer :
    Pour chaque ligne parmi les lignes de la sortie de la commande « wsjtx-decodes », afficher ligne.
En cas d'échec de commande :
    Afficher « Le décodeur ne répond pas : » puis le motif de l'échec.
```

- **`la sortie de la commande « p »`**, et **`… avec les arguments`** : lance le programme `p`, attend qu'il finisse, et rend ce qu'il a écrit, en texte, tel quel, retour à la ligne final compris (`les lignes de` s'en charge, § 15.9). Les arguments sont une liste (§ 13.10) de textes ou de nombres.
- **Sans shell.** Le programme et chaque argument passent tels quels au système : rien n'y est interprété, ni `;`, ni `|`, ni `*`, ni `$`. Un texte saisi par un utilisateur, mis dans les arguments, reste un texte : `« deux ; rm -rf / »` arrive au programme comme ces mots-là. Le programme est cherché dans le chemin du système (`PATH`), ou donné par son chemin complet. Son entrée est vide.
- **Les échecs**, rattrapables, de sorte `commande` (§ 18.2) : « La commande « x » est introuvable. » ; un code de sortie non nul, avec la première ligne de ce qu'il a écrit en erreur, « La commande « sh » a échoué (code 3) : oups » ; « … n'a pas fini en 30 secondes : arrêtée. » (le programme est arrêté) ; plus de 16 Mo écrits ; une sortie qui n'est pas du texte en UTF-8.
- **Où.** Dans la console (`grym lancer`), sous Mac et Linux ; dans l'atelier et l'application fabriquée, sous Mac, Windows et Linux. La console sous Windows refuse pour l'instant ; le navigateur refuse (« Une commande se lance dans la console, l'atelier ou une application fabriquée, pas dans le navigateur. »).
- Un calcul ne lance pas de commande (« Un calcul ne lance pas de commande : lance-la dans une action. ») : elle a des effets hors du programme, comme un envoi. Pour la même raison, une transaction annulée n'annule pas ce qu'une commande a fait.
- En forme compacte : `_commande(« rigctl » ; arguments)`.

## 16. Entités *(v0.3)*

### 16.1 Déclaration

```
Un client, conservé, a :
    un nom (texte),
    un solde (nombre),
    un nombre de commandes (nombre entier),
    un statut actif (vrai ou faux),
    une date d'inscription (date),
    une photo (image),
    un contrat (fichier),
    un parrain (client),
    une licence (texte), unique.
```

- `, conservé,` (ou `, conservée,` pour une entité féminine) fait d'une classe une entité : ses objets peuvent survivre au programme. Héritage, méthodes et aptitudes s'y appliquent comme aux classes.
- Chaque champ déclare son type entre parenthèses, obligatoirement.

| Type | En base |
|---|---|
| `(texte)` | `TEXT` |
| `(nombre)` | `TEXT`, décimal exact sous forme canonique |
| `(nombre entier)` | `INTEGER` |
| `(vrai ou faux)` | `INTEGER`, 0 ou 1 |
| `(date)` | `TEXT`, ISO 8601 |
| `(année)` | `INTEGER`, de 1 à 9999 (§ 14.5) |
| `(fichier)`, `(image)` | `BLOB`, plus le nom de fichier d'origine |
| `(client)`, le nom d'une entité | clé étrangère |

- Un lien pointe vers une entité, jamais vers une classe ordinaire.
- `, unique` : deux objets conservés n'ont pas la même valeur pour ce champ.
- Un champ est obligatoire, sauf s'il est déclaré `, facultatif` (§ 16.9) : conserver un objet dont un champ obligatoire n'a pas de valeur est une erreur.
- Un champ qui ne sert plus se déclare `, abandonné` *(2.49)* : `un code d'accès (mot de passe), abandonné`. Le programme ne le connaît plus (« « code d'accès » inconnu ») ; la migration efface ses valeurs, même conservées, et le dit (« champ « code d'accès » abandonné ; 1 valeur effacée »). On garde la ligne : une base qui n'a pas été lancée depuis l'abandon s'y retrouve aussi, alors que retirer le champ plein serait refusé (2.36). Forme compacte : `_abandonné`.
- `conservé` s'accorde avec le genre de l'entité (`Une facture, conservée, a :`). Seule une entité déclare un type, `, unique` ou un pluriel ; une aptitude peut typer ses champs, sans `, unique`.
- Une entité peut se désigner elle-même dans un lien (`un parrain (client)` dans `client`) ; un lien vers une autre entité suppose qu'elle soit déclarée avant.
- Une entité qui hérite se déclare `Un membre, conservé, est un client.` ; ses champs propres suivent dans la phrase suivante, `Un membre a :`, sans répéter `conservé`. `Un document, conservé, est une chose datée.` déclare une entité qui n'a que des aptitudes.
- Une entité hérite d'une entité, jamais d'une classe ordinaire, et une classe ordinaire n'hérite pas d'une entité. Une aptitude adoptée par une entité doit typer ses champs.
- Pluriel : `s` ajouté au nom ; un pluriel irrégulier se déclare entre parenthèses, comme le masculin d'une aptitude : `Un cheval (chevaux), conservé, a :` (charte, art. 4).
- `, hors des fiches,` *(2.10)*, après `, conservé,` : l'entité ne paraît pas en tableau dans la fiche des objets qu'elle désigne (§ 22.3). `Une note, conservée, hors des fiches, a :`, `Une cliente, conservée, hors des fiches, est une personne.` Sur une classe ordinaire : « Seule une entité se tient hors des fiches : … ». Rien ne change en base : l'ajouter ou le retirer ne demande aucune migration. Forme compacte : `_classe _une note _conservé _hors_des_fiches`.

### 16.2 Typage strict

Le typage des entités est vérifié (charte, art. 5) :

- **à l'analyse**, quand le type de la valeur y est connu (une constante, une date, `aujourd'hui`, un fichier lu, un nouvel objet, une comparaison) et le type du champ aussi : dans le bloc d'un nouvel objet, dont la classe est connue ; ailleurs, quand toutes les classes qui déclarent ce nom de champ lui donnent le même type. `Le solde du client devient « abc ».` est alors refusé avant toute exécution ;
- **sinon, avant toute écriture** : la valeur est vérifiée au moment de la ranger dans le champ, et jamais une valeur du mauvais type n'atteint la base.

Un `(nombre entier)` refuse `2,5` et accepte `3,0` ; un `(nombre)` accepte `2`. Un lien accepte un objet de l'entité ou d'une entité qui en hérite. Une `(image)` refuse un fichier dont le format n'est pas reconnu : « « rapport.pdf » n'est pas une image (PNG, JPEG, GIF ou WebP). »

### 16.3 Conserver, modifier, supprimer

```
Le client vaut un nouveau client :
    Le nom vaut « Ana ».
    …
Conserver le client.
Le solde du client devient 100.
Supprimer le client.
```

- Un nouvel objet vit en mémoire. `Conserver le client.` le range dans la base ; conserver deux fois le même objet est une erreur.
- Modifier un champ d'un objet conservé modifie la base aussitôt, dans la transaction de l'exécution (§ 16.6). Pas de second `Conserver`.
- `Supprimer le client.` le met dans la corbeille ; `Supprimer le client définitivement.` l'efface de la base (§ 16.12).
- Conserver, modifier et supprimer sont des effets de bord : réservés aux actions. `conserver` et `supprimer` ne commencent pas le nom d'une action.
- Un lien d'un objet conservé désigne un objet conservé : « Le champ « parrain » désigne un client qui n'est pas conservé : conserve-le d'abord. » Un objet peut se désigner lui-même (`Le parrain du a devient a.`, puis `Conserver a.`). Deux objets neufs qui se désignent l'un l'autre ne se conservent pas encore : il faudrait un champ facultatif (§ 13.8).
- `, unique` est vérifié à la conservation et à chaque modification : « « licence » est unique : un autre client conservé a déjà « A-1 ». »
- Un objet supprimé puis conservé à nouveau reçoit un nouvel identifiant : un identifiant n'est jamais réattribué.
- Une saisie ratée rend aussi à un objet son état : conservé ou non.

### 16.4 Retrouver

```
Pour chaque client conservé :
    …
Pour chaque client conservé dont le solde est négatif, par nom :
    …
Le client vaut le client conservé dont la licence est « A-12 ».
Afficher le nombre de clients conservés dont le statut actif est vrai.
```

- `Pour chaque client conservé` parcourt les objets conservés ; dans le corps, `le client` désigne l'objet du tour.
- `dont` introduit une condition sur les champs de l'entité : comparaisons et tournures du § 5, reliées par `et` / `ou`. Le programme ne voit jamais de SQL.
- `, par nom` trie ; `, par solde décroissant` trie à l'envers. Sans tri : dans l'ordre de conservation.
- `le client conservé dont …` exige exactement un objet. Aucun, ou plusieurs : erreur, avec leur nombre. Sans `dont` *(documenté en 2.21)*, `le cabinet conservé` désigne l'unique objet conservé de l'entité, avec la même exigence : c'est la façon d'atteindre une entité qui n'a qu'un objet (un cabinet, une association).
- `le nombre de clients conservés [dont …]` compte, au pluriel (§ 16.1).
- Dans une condition `dont`, un champ se lit aussi à travers un lien, de droite à gauche, sur autant d'étapes qu'il faut : `dont la comptabilisation de l'écriture est présente`, `dont le nom de la catégorie du membre est « Junior »`. Le dernier nom est un lien de l'entité examinée, chacun des autres un champ de l'entité que désigne le suivant. Un lien absent rend la condition fausse, comme un champ absent (§ 16.9), sauf `est absent`, qui la rend vraie : `dont la catégorie du membre est absente`. Une étape qui n'est pas un lien, ou un champ qui manque au bout, est une erreur d'analyse qui le dit : « « montant » n'est pas un lien vers une entité : « nom » ne se lit pas à travers lui. », « « couleur » n'est pas un champ du membre. » La base fait le travail, sans charger les objets. `parmi` (§ 16.13) ne s'emploie pas à travers un lien. En forme compacte : `_dont membre.catégorie.nom = « Junior »`.
- `la somme des soldes des clients conservés [dont …]` additionne un champ `(nombre)` ou `(nombre entier)` ; `la somme des débits des lignes du compte` passe par une relation inverse (§ 16.10). Le champ se met au pluriel par un s au premier mot (`débit` → `débits`, `nombre de parties` → `nombres de parties`) ; un premier mot en s, x ou z ne change pas. L'addition est exacte, en décimal, jamais en virgule flottante ; les champs absents ne comptent pas, les objets de la corbeille non plus ; une somme sur rien vaut 0. Un autre type : « « instrument » est un champ texte : seul un nombre s'additionne. » Comme toute recherche, elle n'entre pas dans un calcul. En forme compacte : `_somme_de solde _de client _conservé`, `_somme_de débit _de ligne _de compte`. Aucun mot n'est réservé : `La somme des prix vaut 3.` crée toujours un nom, tant que `prix` n'est le champ d'aucune entité suivie de « des ».
- `la moyenne des soldes des clients conservés [dont …]`, `le plus grand solde des clients conservés`, `la plus petite cotisation des membres conservés` : même construction que la somme, relations inverses comprises (`la moyenne des débits des lignes du compte`). Le champ est au pluriel après `la moyenne des`, au singulier après `le plus grand`, `le plus petit`, qui s'accordent avec lui : « Accord : « la plus grande cotisation ». » Un champ `(nombre)` ou `(nombre entier)` seulement. La moyenne divise la somme exacte par le nombre de valeurs présentes, selon les règles de la division (§ 3.2) : `l'arrondi de la moyenne des … à 0,05` la ramène aux centimes (§ 3.4). Les absents ne comptent pas. Sur rien, **pas de valeur, pas de 0** : « Aucun membre conservé n'a de « cotisation » : pas de moyenne. » Une moyenne nulle d'une liste vide serait un mensonge comptable ; la somme sur rien, elle, vaut 0. En forme compacte : `_moyenne_de solde _de client _conservé`, `_plus_grand_de …`, `_plus_petit_de …`.
- `le client conservé au plus grand solde [dont …]`, `le membre conservé à la plus petite cotisation` : **l'objet**, pas la valeur. Le champ, au singulier, est un nombre, une date ou une année ; l'accord suit le champ (`au plus grand`, `à la plus grande`). Seuls comptent les objets où le champ est présent ; à égalité, le plus ancien (le premier conservé). Aucun objet : « Aucun membre conservé n'a de « âge ». » Le tour se place avant `dont`. En forme compacte : `_le client _conservé _au_plus_grand solde`, `_au_plus_petit`.
- Un même objet conservé, retrouvé deux fois dans une exécution, est le même objet en mémoire : `=` compare toujours l'identité (§ 13.4).
- Un calcul lit la base, sans jamais y écrire (§ 9.4) : recherche, `le … conservé dont …`, nombre, somme, boucle `Pour chaque … conservé`. La règle d'origine l'interdisait, « prudente, qu'on pourra assouplir » ; elle a été assouplie le 28 septembre 2026, faute de quoi le solde d'un compte ne pouvait pas être un calcul. `aujourd'hui` reste interdit dans un calcul.
- Dans une condition `dont`, un champ de l'entité désigne celui de chaque objet examiné. Chaque comparaison met un champ face à une valeur calculée par le programme (une variable, une constante, un calcul) ; deux champs ne se comparent pas entre eux. `0 < solde` vaut `solde > 0`.
- `est` suivi d'une valeur vaut l'égalité : `dont la licence est « A-12 »`, `dont le parrain est a`, et `n'est pas` sa négation. Cette tournure n'existe que dans une condition `dont`.
- Ce qui se compare : texte, nombre, nombre entier et date par égalité et par ordre ; vrai ou faux et lien par égalité ; `positif`, `négatif`, `nul` pour un nombre, `vrai`, `faux` pour un vrai ou faux. Un fichier ne se compare pas. Un nombre entier se compare à un nombre à virgule (`dont le rang ≥ 2,5`).
- Les nombres se comparent et se trient en décimal exact : `100,000000000000000001 > 9`, `0,10 = 0,1`. Les textes se trient comme dans un dictionnaire, sans tenir compte des accents ni de la casse (Ana, Bob, Élodie, émile, Zoé) ; leur égalité reste exacte.
- Une entité retrouvée inclut les objets des entités qui en héritent : `Pour chaque client conservé` parcourt aussi les membres, qui restent des membres.
- La liste d'une boucle est figée à son début : un objet conservé pendant la boucle n'y entre pas. `Sortir de la boucle` et `Passer au tour suivant` s'y emploient.
- Le nom de l'objet du tour est celui de l'entité ; s'il existe déjà, erreur : « « client » existe déjà : renomme-le, car « Pour chaque client conservé » donne ce nom à l'objet de chaque tour. »
- `par nom` trie selon un champ de texte, de nombre, de date ou vrai ou faux ; à valeurs égales, dans l'ordre de conservation.
- Un objet retrouvé n'est lu qu'au premier accès à ses champs ; ses liens, à leur tour, au premier accès aux leurs.

### 16.5 La base

- Un programme `factures.grym` utilise la base `factures.grymd`, à côté de lui, créée au premier besoin.
- La boucle interactive utilise une base en mémoire, perdue à la sortie, sauf si on lui donne un fichier : `grym --base factures.grymd`.
- `factures.grymc` et `factures.grymb` utilisent aussi `factures.grymd`.
- Tant que les migrations (§ 16.7) ne sont pas là, une entité dont la définition a changé depuis la dernière exécution est refusée : « La base « factures.grymd » connaît « client » avec une autre définition… » La base n'est pas touchée.
- Aucune base n'est ouverte si le programme ne déclare aucune entité.
- Chaque base porte un numéro de format (charte, art. 13). Une base d'un format plus récent que celui de `grym` est refusée avant toute écriture : « La base « factures.grymd » a le format 2, plus récent que celui de ce grym (1) : ouvre-la avec une version plus récente de grym. Elle n'a pas été modifiée. »

### 16.6 Transaction

- Une exécution, une transaction : chaque programme lancé, et chaque saisie de la boucle interactive, s'exécute dans une seule transaction. Une question à l'utilisateur la referme et en ouvre une autre après la réponse (§ 17) : une application interactive ne garde donc pas la base verrouillée pendant qu'elle attend.
- Si l'exécution réussit, la transaction est validée. Si elle échoue, ou si on l'interrompt par Ctrl+C, elle est annulée : le journal de la machine restaure la mémoire, SQLite restaure la base.
- Pendant l'exécution, la base est verrouillée en écriture pour les autres programmes, sauf pendant qu'une question attend sa réponse. Après deux secondes d'attente : « La base « … » est utilisée par un autre programme. »
- Les fichiers enregistrés (§ 15.2) sont écrits juste avant la validation de la base ; si la validation échoue, ils sont retirés.
- La création des tables fait partie de la transaction : une première exécution ratée ne laisse aucune table.

### 16.7 Migrations

La charte (art. 7) promet des migrations de schéma automatiques ; le principe 1 interdit de détruire des données en silence.

| Changement dans le programme | Effet sur la base |
|---|---|
| nouvelle entité | table créée |
| nouveau champ, table vide | colonne ajoutée |
| nouveau champ, table non vide | refusé, sauf valeur de départ : `un pays (texte), « Suisse » au départ` |
| champ retiré ou retypé | refusé, avec un message qui dit combien de valeurs seraient perdues |
| champ ou entité renommé, avec `anciennement « … »` *(2.23)* | renommé : valeurs, liaisons, liens et objets gardés |
| `(nombre entier)` devenu `(nombre)` | accepté : aucune perte |
| `(nombre entier)` devenu `(année)`, et l'inverse | accepté si toutes les valeurs sont entre 1 et 9999 (§ 14.5) |

- Le schéma connu est rangé dans la base elle-même. La migration a lieu au début de l'exécution, dans sa transaction : si l'exécution échoue ensuite, la base garde son ancien schéma.
- La valeur de départ suit le type, et `, unique` s'il y a lieu : `un code (texte), unique, « C-1 » au départ`. C'est une constante (texte, nombre, date, vrai ou faux), vérifiée contre le type ; elle ne sert qu'aux objets déjà conservés au moment où le champ apparaît.
- Un champ nouveau et unique ne reçoit une valeur de départ que si un seul objet est déjà conservé.
- Un lien ou un fichier nouveau ne s'ajoute qu'à une entité sans objet conservé : il n'a pas de valeur de départ.
- Un champ retiré d'une entité sans objet conservé disparaît, sauf s'il est unique ou s'il est un lien.
- `, unique` s'ajoute à un champ existant si ses valeurs sont déjà toutes différentes. Il se retire seulement s'il a été ajouté par une migration.
- `(nombre entier)` devient `(nombre)` si le champ n'est pas unique.
- Changer la classe parente d'une entité est refusé.
- *(2.36)* Un champ retiré, ni unique ni lien, dont aucune valeur n'est présente (toutes absentes, corbeille comprise) part sans rien emporter, même si des objets sont conservés. Pour retirer un champ facultatif qui a encore des valeurs : un programme les efface d'abord (`Le rappel de la séance devient absent.`), puis la déclaration suivante ne le porte plus.
- Retirer un champ pour de bon, ou lever les limites ci-dessus, demandera une commande explicite de l'outil `grym`, à concevoir.

**Renommer** *(2.23)* :

```
Un patient, conservé, anciennement « client », a :
    un nom (texte), unique,
    un portrait (image), facultatif, anciennement « photo »,
    des humeurs (genre), anciennement « genres ».
Une séance, conservée, a :
    un patient (patient), anciennement « client »,
    un prix (nombre).
```

- `, anciennement « … »` dit le nom d'avant d'un champ d'entité, simple ou multiple, et `anciennement « … »,` après `conservé,` (et `hors des fiches,`) celui d'une entité. Sans la mention, un champ renommé est un champ retiré suivi d'un champ ajouté, et la migration le refuse dès qu'une valeur serait perdue ; le message propose alors la mention.
- Au lancement, avant toute autre migration, une entité renommée change de table, avec ses tables de liaison et ses index ; ses objets gardent leur numéro, et les entités qui la désignent (liens, champs multiples, classe parente) la retrouvent sous son nouveau nom. Puis chaque champ renommé change de colonne (et, pour un fichier ou une image, la colonne de son nom), ou de table de liaison pour un champ multiple. Rien n'est copié : SQLite renomme (`ALTER TABLE … RENAME`).
- L'aperçu des migrations l'annonce : « « client » devient « patient » : 6 objets gardés. », « « séance » : « client » devient « patient », 15 valeurs gardées. »
- La mention reste dans le programme, sans effet une fois la base à jour : une base restée à l'ancien nom (une sauvegarde, la base d'un client qui n'a pas encore mis l'application à jour) se renomme à son tour au premier lancement. On peut la retirer quand plus aucune base ne porte l'ancien nom.
- Le nom d'avant s'écrit entre guillemets, ramené à la forme d'un nom : `« Client »` s'écrit `« client »` en forme canonique, où la mention vient en dernier.
- Refus de l'analyse : le nom d'avant est le nom actuel (« « nom » est déjà le nom de ce champ. ») ; il est encore un champ de l'entité (« … un renommage suppose que l'ancien nom a disparu. ») ; deux champs revendiquent le même ; l'ancienne entité est encore déclarée ; la mention sur une classe ordinaire ou une aptitude (« Seule une entité se renomme »).
- Un champ hérité se renomme dans l'entité qui le déclare. Changer à la fois le nom et le type d'un champ reste refusé, comme un changement de type seul.
- Forme compacte : `_classe _un patient _conservé _anciennement « client »`, `_un patient (patient) _anciennement « client »`, `_des humeurs (genre) _anciennement « genres »`.
- Ces limites viennent de ce que SQLite modifie une table existante : il n'ajoute ni contrainte `UNIQUE` ni lien obligatoire à une colonne nouvelle, et ne retire pas une colonne unique ou liée.

### 16.8 Forme compacte

| Littéraire | Compacte |
|---|---|
| `Un client, conservé, a :` | `_classe _un client _conservé` (le genre n'est pas écrit) |
| `Un cheval (chevaux), conservé, a :` | `_classe _un cheval (chevaux) _conservé` |
| `Un membre, conservé, est un client daté.` | `_classe _un membre _conservé _est _un client _adopte datée` |
| `un âge (nombre entier)`, `un actif (vrai ou faux)` | `_un âge (nombre_entier)`, `_un actif (vrai_ou_faux)` |
| `un nom (texte)` | `_un nom (texte)` |
| `une licence (texte), unique` | `_une licence (texte) _unique` |
| `un pays (texte), « Suisse » au départ` | `_un pays (texte) _départ « Suisse »` |
| `un pays (texte), « Suisse » au départ` | `_un pays (texte) _départ « Suisse »` |
| `Conserver le client.` / `Supprimer le client.` | `_conserver client` / `_supprimer client` |
| `Supprimer le client définitivement.` / `Rétablir le client.` | `_supprimer client _définitivement` / `_rétablir client` |
| `Pour chaque client supprimé :` | `_pour_chaque client _supprimé` |
| `une partition (partition), et disparaît avec elle` | `_une partition (partition) _disparaît_avec` |
| `Pour chaque client conservé dont le solde est négatif, par nom décroissant :` | `_pour_chaque client _conservé _dont solde _négatif _par nom _décroissant` |
| `le client conservé dont la licence est « A-12 »` | `_le client _conservé _dont licence = « A-12 »` |
| `le nombre de clients conservés dont …` | `_nombre_de client _conservé _dont …` |
| `le fichier « photos/ana.jpg »` | `_fichier « photos/ana.jpg »` |
| `la taille de la photo` | `photo.taille` ; `(_fichier « a.jpg »).taille` |
| `Enregistrer la photo dans « copie.jpg ».` | `_enregistrer photo _dans « copie.jpg »` |
| `21.09.2026`, `aujourd'hui` | `21.09.2026`, `_aujourd'hui` |
| `d + 3 mois`, `d − 1 an`, `par pas de 2 semaines` | `d + 3 _mois`, `d − 1 _an`, `_pas 2 _semaines` |

### 16.9 Champs facultatifs et valeur absente

```
Une partition, conservée, a :
    un titre (texte),
    un arrangeur (compositeur), facultatif,
    une édition (date), facultative.

L'arrangeur de p devient absent.
Si l'arrangeur de p est présent, afficher nom de l'arrangeur de p.
Pour chaque partition conservée dont l'édition est absente :
    …
```

- `, facultatif` (accordé au champ : `une édition (date), facultative`) permet au champ de rester sans valeur. Il suit le type, avec `, unique` et la valeur de départ ; il vaut pour les champs d'entité, d'aptitude et de classe ordinaire.
- `absent` (`absente`) est la valeur d'un champ facultatif qui n'en a pas. Un champ facultatif d'un objet neuf est absent ; un champ facultatif se vide par `devient absent`. Le mot s'accorde avec le champ qu'il remplit : « Accord : « absente ». »
- `est absent`, `est présent` (et `n'est pas absent`…) testent un champ, dans une condition ordinaire comme dans une condition `dont`, pour tout type de champ, fichiers compris.
- Une valeur absente ne se laisse pas utiliser par mégarde : un calcul, une comparaison, le champ d'un lien absent ou l'appel d'une méthode sur elle donnent « Le champ « édition » est absent : vérifie-le d'abord avec « est présent ». » Elle s'affiche `absent`, se range dans un nom, et se copie dans un autre champ facultatif.
- Un champ obligatoire refuse `absent` : « Le champ « nom » n'est pas facultatif : il ne devient pas absent. »
- En base, un champ absent est `NULL`. Une condition `dont` autre que `est absent` ne retient jamais un objet dont le champ est absent (`dont l'édition < 01.01.2020` écarte les éditions absentes). Un tri place les absents en dernier, dans les deux sens.
- Deux objets neufs se désignent l'un l'autre en passant par un champ facultatif : conserver le premier avec le lien absent, puis le second, puis remplir le lien du premier.
- Migrations (§ 16.7) : un champ facultatif nouveau s'ajoute à une entité qui a déjà des objets, sans valeur de départ ; ils le reçoivent absent. Un champ existant ne devient pas facultatif, ni ne cesse de l'être, sur une table existante.
- En forme compacte : `_une édition (date) _facultatif`, `_absent`, `x.édition _absent`, `x.édition _présent`.

### 16.10 Relations inverses

```
Une chanson, conservée, a :
    un titre (texte),
    un compositeur (personne),
    un auteur (personne), facultatif.

Pour chaque œuvre de bach, par titre :
    Afficher titre de l'œuvre.
Afficher le nombre d'œuvres de bach.
Pour chaque chanson conservée dont brel est l'auteur :
    …
```

- `les œuvres de bach` désigne les œuvres conservées dont un lien désigne `bach`. L'inverse se déduit du lien : rien n'est à déclarer. Deux tournures : `Pour chaque œuvre de bach [dont …] [, par …] :` et `le nombre d'œuvres de bach [dont …]`. `conservée` n'y figure pas : un lien ne relie que des objets conservés.
- L'objet suit `de` : un nom, `du compositeur`, `de l'arrangeur de p`… Il doit être un objet ; absent, c'est l'erreur habituelle (§ 16.9).
- Le lien est choisi à l'exécution, selon la classe réelle de l'objet (un lien `(personne)` désigne aussi un membre, qui est une personne). S'il n'y en a aucun : « Aucun champ d'une œuvre ne peut désigner une œuvre : « les œuvres de … » ne désigne rien. » S'il y en a plusieurs : « Plusieurs champs d'une chanson peuvent désigner une personne : « compositeur » et « auteur ». Précise avec « dont … est le … », par exemple « dont … est le compositeur ». »
- Une entité sans aucun lien est refusée dès l'analyse : « Un « instrument » n'a aucun lien vers un autre objet : « les instruments de … » ne désigne rien. »
- La relation renversée, dans une condition `dont`, dit quel lien : `dont brel est l'auteur` vaut `dont l'auteur est brel`, écrit dans l'ordre naturel. Elle a sa négation : `dont rauber n'est pas l'auteur`, qui, comme toute condition, écarte les auteurs absents (§ 16.9).
- `Pour chaque i de 1 à 9` garde son sens de compteur : si un `à` suit `de` avant `dont`, `,` ou `:`, c'est un compteur, même si `i` est le nom d'une entité.
- `dont` devient un mot réservé (§ 2) : il termine le nom qui le précède.
- En forme compacte : `_pour_chaque œuvre _de bach _dont … _par titre`, `_nombre_de œuvre _de bach`. La relation renversée s'y écrit dans l'ordre ordinaire : `_dont auteur = brel`.

### 16.11 Accord avec la charte

La charte 1.9 reprend ce paragraphe : transaction par exécution (art. 7), typage vérifié à l'analyse ou avant toute écriture (art. 5), type `montant` retiré pour la v0.3, puisque tout `(nombre)` est déjà un décimal exact.

### 16.12 Corbeille et cascade

```
Un pupitre, conservé, a :
    une partition (partition), et disparaît avec elle,
    un instrument (instrument).

Supprimer p.                    ← dans la corbeille : invisible, rétablissable
Supprimer p définitivement.     ← effacée de la base, sans retour
Rétablir p.
Pour chaque partition supprimée :
    …
Afficher le nombre de partitions supprimées.
```

- `Supprimer x.` met l'objet dans la corbeille. Il garde ses valeurs, en base comme en mémoire, et la base note la date. La suppression simple ne casse aucun lien : elle est permise même si des liens désignent l'objet.
- Un objet de la corbeille disparaît de toutes les recherches (`conservés`, `le nombre de…`, `les œuvres de bach`). `supprimé` à la place de `conservé` cherche dans la corbeille : `Pour chaque client supprimé`, `le client supprimé dont …`, `le nombre de clients supprimés`.
- Les liens existants restent lisibles : la facture d'un client supprimé affiche toujours son client. Un nouveau lien vers un objet de la corbeille est refusé : « Le champ « client » désignerait un client supprimé : rétablis-le d'abord. »
- Un objet de la corbeille garde ses valeurs uniques : « « cote » est unique : « A-2 » appartient à une partition supprimée. Rétablis-la, ou supprime-la définitivement. »
- `Rétablir x.` le fait revenir. Supprimer deux fois est une erreur ; rétablir un objet qui n'est pas dans la corbeille aussi.
- `, et disparaît avec elle` (`avec lui`, accordé avec l'entité désignée) sur un lien : l'objet suit celui qu'il désigne. Il va dans la corbeille avec lui, revient avec lui, est effacé avec lui ; de proche en proche. Il ne se rétablit pas seul : « Ce pupitre a disparu avec un autre objet : rétablis celui-là, et il reviendra avec lui. » Un objet mis dans la corbeille pour lui-même, puis dont le lien désigne un objet supprimé, ne revient pas avant lui.
- `Supprimer x définitivement.` efface l'objet et ceux qui disparaissent avec lui, qu'ils soient dans la corbeille ou non. Tout autre lien qui les désigne l'empêche : « Cette partition est encore désignée par le champ « partition » d'une note. » ; si ces objets sont eux-mêmes dans la corbeille, le message le dit et propose de les supprimer définitivement d'abord. En mémoire, les objets effacés restent, mais ne sont plus conservés.
- `définitivement` est un mot réservé (§ 2).
- En base : `grym_objet` reçoit `supprime` (la date) et `supprime_avec` (l'objet dont la suppression a entraîné celle-ci). Une base plus ancienne reçoit ces colonnes à l'ouverture ; ses objets restent conservés. La mention `, et disparaît avec elle` ne vit que dans la définition sauvegardée : l'ajouter ou la retirer ne demande aucune migration.
- Reporté : vider la corbeille des objets supprimés depuis longtemps ; un lien facultatif qui deviendrait absent quand son objet est effacé ; l'autocomplétion des saisies de l'utilisateur, qui pourra puiser dans la corbeille.

### 16.13 Plusieurs vers plusieurs

```
Une œuvre, conservée, a :
    un titre (texte),
    un compositeur (personne),
    des interprètes (personne),
    des genres (genre).

Les genres de o gagnent baroque.
Les genres de o perdent fugue.
Pour chaque interprète de o, par nom :
    …
Afficher le nombre de genres de o.
Pour chaque œuvre de baroque :
    …
Pour chaque œuvre conservée dont callas est parmi les interprètes :
    …
```

- `des` suivi d'un nom au pluriel déclare un champ multiple ; son type, entre parenthèses, reste au singulier et désigne une entité. Seule une entité en déclare. Il passe par héritage aux entités filles.
- Le singulier se déduit : chaque mot perd son `s` ou son `x` final, jusqu'au premier complément (`genres` → `genre`, `pièces jointes` → `pièce jointe`, `numéros de téléphone` → `numéro de téléphone`). Un singulier irrégulier se déclare entre parenthèses, avant le type : `des travaux (travail) (tâche)`. Un nom sans `s` ni `x` final exige ce singulier.
- C'est un ensemble : gagner deux fois un même objet ne le compte qu'une fois ; perdre un objet que l'ensemble ne contient pas n'est pas une erreur.
- Un champ multiple n'est ni `, facultatif` (un ensemble vide suffit), ni `, unique`, n'a pas de valeur de départ et ne « disparaît » avec rien : chacune de ces mentions est une erreur, avec son message.
- `les genres de o` n'est pas une valeur : ni `Afficher`, ni un nom, ni un champ ne le reçoivent. Le champ vit dans cinq tournures : `gagnent`, `perdent`, `Pour chaque genre de o`, `le nombre de genres de o`, `dont … est parmi les genres`.
- Gagner et perdre sont des effets de bord, réservés aux actions. L'objet qui gagne doit être conservé ; l'objet gagné aussi, et hors de la corbeille. Un objet à la fois : `gagnent baroque et fugue` entrerait en conflit avec le `et` logique.
- `gagnent` et `perdent` ne sont pas réservés : `Les` en tête de phrase suffit à annoncer la tournure.
- Lecture par le champ : `Pour chaque interprète de o` (au singulier ; l'objet du tour s'appelle `interprète`, son genre se fixe au premier article), `le nombre d'interprètes de o` (au pluriel). `dont` et `, par` s'y ajoutent comme ailleurs (§ 16.4). Sans tri, l'ordre de conservation des objets.
- Lecture par l'entité : `Pour chaque genre de o`, `Pour chaque œuvre de baroque` étendent la relation inverse du § 16.10 aux champs multiples, dans les deux sens. Le champ est choisi à l'exécution selon la classe réelle de l'objet ; aucun ou plusieurs : erreur, qui dit comment préciser (« dont … est le compositeur », « dont … est parmi les interprètes », « les travaux de … »).
- Quand un nom est à la fois celui d'une entité et le singulier d'un champ multiple qui en contient les objets, le champ l'emporte : `le nombre de genres de o` lit le champ `genres` de `o`, sans ambiguïté. Si l'objet n'a pas ce champ, la lecture revient à la relation inverse.
- `parmi` : `dont baroque est parmi les genres`, `dont sacré n'est pas parmi les genres`. Il compare une valeur calculée par le programme à un champ multiple de l'entité examinée. Hors d'une condition `dont`, `parmi` est une erreur : le test d'appartenance en mémoire (`Si baroque est parmi les genres de o`) est reporté.
- Une boucle parcourt une liste figée à son début : gagner ou perdre pendant le parcours ne le perturbe pas.
- Corbeille : un objet supprimé disparaît des lectures, et ne se gagne pas ; rétabli, il retrouve ses ensembles.
- Effacer l'objet qui porte le champ efface ses liaisons. Effacer un objet encore gagné est refusé : « Ce genre est encore désigné par le champ « genres » d'une œuvre. »
- En base : une table de liaison `"m <entité>.<champ>"` par champ multiple, invisible pour le programmeur.
- Migrations : ajouter un champ multiple est toujours permis, les objets déjà conservés partent d'un ensemble vide ; le retirer n'est permis que s'il ne contient rien ; passer d'un lien simple à un champ multiple, ou l'inverse, est refusé.
- En forme compacte :

| Littéraire | Compacte |
|---|---|
| `des interprètes (personne)` | `_des interprètes (personne)` |
| `des travaux (travail) (tâche)` | `_des travaux (travail) (tâche)` |
| `Les genres de o gagnent baroque.` | `o.genres _gagne baroque` |
| `Les genres de o perdent fugue.` | `o.genres _perd fugue` |
| `Pour chaque interprète de o :` | `_pour_chaque interprète _de o` |
| `le nombre d'interprètes de o` | `_nombre_de interprète _de o` |
| `dont rauber est parmi les interprètes` | `_dont interprètes _contient rauber` |
| `dont rauber n'est pas parmi les interprètes` | `_dont _non (interprètes _contient rauber)` |


### 16.14 Règles *(implémenté le 28 septembre 2026)*

```
Chaque écriture conservée dont la comptabilisation est présente vérifie :
    la somme des débits des lignes de l'écriture = la somme des crédits des lignes de l'écriture,
    sinon « L'écriture » puis la pièce de l'écriture puis « n'est pas équilibrée. ».

Chaque ligne conservée vérifie :
    le débit de la ligne = 0 ou le crédit de la ligne = 0,
    sinon « Une ligne est au débit ou au crédit, pas aux deux. ».
```

- Une règle dit ce que chaque objet conservé d'une entité doit vérifier ; `dont` restreint les objets concernés (§ 16.4). Elle se déclare au premier niveau, dans le programme ou dans un fichier utilisé (§ 21), où elle vaut pour tout le projet.
- La condition parle de l'objet par le nom de l'entité : `le débit de la ligne`, `la somme des débits des lignes de l'écriture`. Un champ seul, sans l'objet, n'est pas reconnu. La condition peut employer sommes, nombres, chemins et calculs.
- Une règle est un calcul (§ 9.4) : elle lit la base, n'écrit rien, n'affiche rien, ne voit pas les variables du programme.
- Le message suit `sinon`, écrit comme celui de `Refuser` (§ 18.1) : des éléments reliés par `puis`.
- **Quand** : à la fin de chaque transaction, juste avant qu'elle soit validée ; pendant la transaction, les objets peuvent passer par des états intermédiaires faux (une écriture dont la seconde ligne manque encore). Concrètement : à la fin du programme, avant une question (§ 17), un formulaire (§ 19), une fiche ou un `Ouvrir` (§ 22.3), et à la fin de chaque événement d'écran. Un `Essayer` ne la déclenche pas : la règle attend la transaction, pas l'essai.
- **Si elle est violée** : la transaction échoue avec le message, comme par `Refuser`, et rien n'est écrit. Dans un événement, seul l'événement est annulé, et l'écran reste ouvert avec le message (§ 22.3).
- Une transaction qui n'a rien écrit dans la base ne revérifie aucune règle. Une règle dont aucune des entités qu'elle a lues à sa dernière vérification n'a été écrite depuis ne se revérifie pas non plus : son résultat ne peut pas avoir changé (docs/vm.md, § 3.3). Ajouter un compte ne revérifie donc pas l'équilibre des écritures ; ajouter une ligne, si. Sinon, chaque règle parcourt tous les objets qu'elle concerne : toujours juste. La première validation d'une exécution vérifie toutes les règles ; une écriture faite par un autre programme aussi. Mesure du 13 octobre 2026, 50'000 écritures, dans une même exécution : ajouter un compte coûte 0,001 s après la première validation, ajouter une écriture 0,5 s. Mesure du 28 septembre 2026, la règle d'équilibre ci-dessus, écritures à deux lignes : une transaction coûte 0,02 s sur 1'000 écritures, 0,09 s sur 5'000, 0,34 s sur 20'000, grâce à l'index de chaque colonne de lien (sans lui : 2,4 s sur 5'000). Ne revérifier que les objets touchés est reporté (charte, art. 12).
- Une règle ajoutée à une base qui la viole déjà ne dit rien avant la première transaction qui écrit : elle échoue alors, et le message désigne l'objet fautif.
- En forme compacte, une ligne : `_chaque écriture _conservé _dont comptabilisation _présent _vérifie _somme_de débit _de ligne _de écriture = … _sinon « L'écriture » ; écriture.pièce ; « n'est pas équilibrée. »`.

### 16.15 Valeurs permises

```
Une tâche, conservée, a :
    un titre (texte),
    un état (« à faire », « en cours » ou « fini »), « à faire » au départ.
```

- À la place du type, une liste de textes entre parenthèses : le champ est un `(texte)` qui n'accepte que ces valeurs. Au moins deux, séparées par des virgules, la dernière par `ou` (ou une virgule) ; ni vide, ni en double : « « a » est déjà dans la liste. »
- **Typage strict** (§ 16.2). Une écriture hors de la liste est refusée, avec la liste : « « fermé » n'est pas une valeur permise de « état » : « à faire », « en cours » ou « fini ». » Un texte écrit en clair se vérifie dès l'analyse (`L'état vaut « fermé ».`, `dont l'état est « fermée »`) ; une valeur calculée, à l'exécution. La comparaison est exacte : casse et accents comptent.
- **Saisie.** En console, la question montre la liste : `État (« à faire », « en cours » ou « fini ») ?`, et une réponse hors liste se redemande. Dans le navigateur et l'atelier, un menu (docs/v2.md, § 10) ; dans un écran (§ 22), la zone d'un tel champ devient un menu.
- **Migrations** (§ 16.7). Ajouter une valeur, en changer l'ordre : permis. Retirer une valeur que des objets portent encore, corbeille comprise : refusé, rien ne change : « « état » ne permet plus « fini », que 1 tâche conservée porte encore : remets « fini » dans la liste, ou change d'abord ces objets. » Passer de `(texte)` à une liste suit la même règle ; d'une liste à `(texte)`, toujours permis.
- Une liste ne se déclare que sur un champ d'entité ou d'aptitude, comme tout type ; elle n'existe que pour le texte.
- En forme compacte : `_un état (« à faire » ; « en cours » ; « fini »)`.

### 16.16 Types nommés et annotations *(2.99, 3.00)*

```
Un état est « à faire », « en cours » ou « fini ».
Une tâche, conservée, a : un titre (texte), un statut (état).
Le prochain (état) vaut « à faire ».
Le prochain devient « fini ».
Le prochain devient « fermé ».       →  « « fermé » n'est pas une valeur permise de « état » : « à faire », « en cours » ou « fini ». »
```

- **`Un état est « a », « b » ou « c ».`** déclare un type : un nom, et la liste de ses valeurs, des textes, au moins deux, séparés par des virgules et un `ou`. Au premier niveau du programme, avant son premier usage, une seule fois (« Le type « état » existe déjà. »). Les refus sont ceux des valeurs permises (§ 16.15) : une valeur vide, en double, une seule valeur.
- **Un champ** d'entité ou d'aptitude s'écrit `(état)` à la place de sa liste : c'est un champ aux valeurs permises du type, avec tout ce que dit le § 16.15 (vérification, menu de saisie, migrations). La forme canonique écrit `(état)` pour tout champ dont la liste est celle d'un type déclaré plus haut, sans couleurs.
- **Une variable** se type à sa création : `Le prochain (état) vaut « à faire ».` Chaque valeur qu'elle reçoit ensuite, par `devient` ou par `, qui donne`, doit être l'une des valeurs du type : un texte écrit en clair se vérifie dès l'analyse, une valeur calculée à l'exécution, avec le même message qu'un champ. Le type d'une variable ne change pas.
- La comparaison est exacte, casse et accents compris, comme au § 16.15. Une valeur qui n'est pas un texte est refusée.
- Un type n'est pas une valeur : son nom ne s'emploie qu'entre parenthèses, après un champ ou une variable. Il ne prend pas de place parmi les noms : une variable, un champ ou un calcul peuvent s'appeler `état` aussi.
- `Un … est « …` est une tournure nouvelle : avant la 2.99, aucune phrase ne commençait ainsi.
- **Les annotations** *(3.00, point 8 de l'audit)*. Entre parenthèses après le nom, un type nommé ou un type simple : `texte`, `nombre`, `date`, `heure`, `vrai ou faux`.
  - **Une variable** : `Le total (nombre) vaut 0.` Chaque écriture est vérifiée comme pour un type nommé : à l'analyse pour une constante, à l'exécution sinon. « « total » est déclaré (nombre) : il ne reçoit pas un texte. »
  - **Un paramètre de formule** : `Le carré d'un x (nombre) vaut x × x.`, `Pour marquer une tâche et un statut (état) :`. Un argument écrit en clair se vérifie dès l'analyse, à la ligne de l'appel ; une valeur calculée, à l'exécution, en tête de la formule, à la position du paramètre. Dans le corps, le paramètre garde son type : `Le n devient « x ».` y est refusé de même.
  - Un type inconnu : « « couleur » n'est pas un type : texte, nombre, date, heure, vrai ou faux, ou un type déclaré plus haut (§ 16.16). »
  - Sans annotation, rien ne change : une variable ou un paramètre reçoit toujours toute valeur. Les annotations sont facultatives, partout.
  - Les paramètres facultatifs (§ 9.3 bis) n'en prennent pas : leur valeur de départ dit déjà leur sorte.
  - En forme compacte : `_le total (nombre) << 0`, `_calcul _le carré(_un x (nombre))`, `(vrai_ou_faux)`.
- En forme compacte : `_type _un état « à faire » ; « en cours » ; « fini »`, `_le prochain (état) << « à faire »`, `_un statut (état)`.

### 16.18 La base chiffrée *(2.37, implémenté le 21 octobre 2026)*

```
La base est chiffrée.
Changer le mot de passe de la base pour le nouveau mot de passe de l'écran, l'ancien étant le mot de passe actuel de l'écran.
```

- **La déclaration.** `La base est chiffrée.`, une fois, au premier niveau du programme principal (« … se déclare au premier niveau du programme. », « … se déclare une seule fois. »). Elle ne s'exécute pas : le module la note, et la base s'ouvre chiffrée, où que la phrase soit écrite. Une base en mémoire n'est pas concernée.
- **Le moteur.** SQLite3 Multiple Ciphers (vendor/sqlite, PROVENANCE.md), schéma ChaCha20-Poly1305 : toute la base est chiffrée, pages, index et journal compris. Rien de lisible dans le fichier.
- **La clé et ses enveloppes.** La base est chiffrée par une clé de 32 octets tirée au hasard, que personne ne tape. Elle est rangée, enveloppée, dans `base.grymd.clés`, à côté de la base : sous le mot de passe (Argon2id, 64 Mio, 3 passes ; XChaCha20-Poly1305 ; `coffre.h`). Changer le mot de passe refait l'enveloppe seulement : instantané, la base ne bouge pas. Une enveloppe de secours, ouverte par la clé du développeur, viendra (étape 3).
- **Le mot de passe, demandé par l'exécution**, avant la première lecture de la base, jamais par le programme : la phrase du thème lit déjà la base, avant tout `Ouvrir`. L'atelier et l'application fabriquée ouvrent une fenêtre, `grym servir` une page à champs masqués, `grym lancer` une saisie sans écho. La première fois (pas encore de fichier des clés), il se choisit et se confirme ; huit caractères au moins dans l'atelier et le navigateur. Il reste en mémoire, jamais écrit. Pour les essais automatiques, la variable d'environnement `GRYM_MOT_DE_PASSE_BASE` le donne.
- **Refus.** « Mot de passe faux. » ; sans mot de passe : « Cette base est chiffrée : sans son mot de passe, rien ne s'ouvre. » ; une base chiffrée lue par un programme qui ne le déclare pas : « La base « … » est chiffrée : écris « La base est chiffrée. » en tête du programme. ».
- **Une base en clair** (d'avant la déclaration) se chiffre sur place à la première ouverture, une fois, sans retour.
- **Sauvegarder** (§ 15.6 bis) une base chiffrée écrit d'office un coffre de format 2 : `GRYMCOFFRE2`, le fichier des clés, la copie chiffrée sous la clé de la base. Il s'ouvre avec le mot de passe de la base au moment de la sauvegarde. `avec le mot de passe` y est refusé : « La base est chiffrée : sa sauvegarde l'est d'office, avec ses clés. … ». Une sauvegarde emporte donc tout ce qu'il faut pour rouvrir la base sur un autre ordinateur.
- **Restaurer** accepte un coffre de format 2 (avec son mot de passe, ou celui de la séance), de format 1, ou une copie en clair. Rien n'attend en clair sur le disque : l'attente est un coffre de format 2. À la prochaine ouverture, elle prend la place de la base avec ses clés, et s'ouvre avec le mot de passe **de sa copie** ; l'ancienne base et ses clés restent à côté (« … avant restauration … »).
- **`Changer le mot de passe de la base pour n, l'ancien étant a.`** : « Donne d'abord le mot de passe actuel. », « Le mot de passe actuel est faux. », « Cette base n'est pas chiffrée : … ». Pas de contraction à écrire : `pour` et `étant` précèdent les deux valeurs.
- **Le secours** *(étape 3, 21 octobre 2026)*. Le développeur crée une fois sa clé de secours dans l'atelier (Programme, Clé de secours) : un fichier `.grymsecours` chez lui, sa clé secrète X25519 chiffrée sous un mot de passe de secours (douze caractères au moins ; Argon2id, XChaCha20-Poly1305). Il en pose la partie publique dans un projet, le fichier `clé-de-secours.pub` (64 chiffres hexadécimaux), que l'application fabriquée emporte ; la fabrication écarte toujours un `.grymsecours`, des `.clés` et des `.coffre`. À chaque ouverture d'une base chiffrée, si le dossier du programme a cette clé publique et le fichier des clés pas encore d'enveloppe de secours, elle s'ajoute : la clé de la base, enfermée pour la clé publique (une clé éphémère, BLAKE2b du secret partagé, XChaCha20-Poly1305). Les coffres de format 2 l'emportent avec les clés. La partie publique n'ouvre rien, et ne permet pas d'essayer des mots de passe.
- **Mot de passe oublié.** Dans l'atelier, « Rouvrir une base avec la clé de secours » : la base (avec son `.clés` à côté) ou un coffre de format 2, la clé de secours et son mot de passe, puis un nouveau mot de passe pour le client. Sur une base, le fichier des clés est refait, l'ancien gardé (« .clés avant secours ») ; d'un coffre, une base neuve s'écrit à côté, jamais par-dessus un fichier, avec ses clés. Refus : « Cette clé de secours n'est pas celle de cette base. », « Cette base n'a pas d'enveloppe de secours : elle a été créée sans clé de secours. », « Mot de passe de secours faux, ou clé de secours abîmée. ». Sans clé de secours, un mot de passe oublié reste perdu.
- Le client doit savoir, par écrit, que le développeur peut rouvrir sa base s'il en a le fichier en main.
- Forme compacte : `_base_chiffrée`, `_changer_mot_de_passe n _ancien a`.

### 16.19 Le journal d'une entité *(2.85, implémenté le 5 novembre 2026)*

```
Un patient, conservé, journalisé, a :
    un nom (texte), unique,
    un téléphone (texte), facultatif.
Afficher l'historique du p.
Afficher l'historique de la base.
```

- **`, journalisé,`** (`, journalisée,` pour une entité féminine) suit `conservé` (et `hors des fiches`, s'il y est), avant `anciennement`. Seule une entité tient un journal : « Seule une entité tient un journal : « Un patient, conservé, journalisé, a : ». » L'accord est vérifié.
- **Ce qui s'inscrit**, dans la table `grym_journal`, au moment UTC du changement : la création (`Conserver`), chaque champ changé avec sa valeur d'avant et d'après (par une phrase ou par un formulaire), un élément gagné ou perdu (§ 16.13), la mise à la corbeille, l'effacement définitif, le rétablissement. Une réécriture de la même valeur ne s'inscrit pas. Un mot de passe ou un secret (§ 22.17) s'inscrit « (caché) », jamais en clair ; un fichier par son nom ; un objet désigné par son premier champ unique, sinon son premier texte, sinon son numéro.
- **Dans la transaction.** Une ligne du journal part avec ce qu'elle décrit : une exécution qui échoue n'en laisse aucune, un `Essayer` rattrapé efface celles de son bloc. La table naît au premier changement ; une base sans entité journalisée ne la reçoit jamais.
- **`l'historique de x`** : le journal de l'objet x, en texte, une ligne par changement, le plus récent d'abord, 200 au plus : « 05.11.2026 09:14 · téléphone : 079 123 45 67 → 078 000 00 00 », « … · genres : + Jazz », « … · créé ». L'heure est locale ; une valeur absente s'écrit « (vide) », une valeur longue est coupée après 80 caractères (le journal la garde entière). x absent : absent. Une entité qui n'est pas journalisée : « Un genre n'a pas d'historique : écris « , journalisé, » dans la déclaration de son entité. »
- **`l'historique de la base`** : les 500 derniers changements de toutes les entités journalisées, chacun avec son entité et son objet : « 05.11.2026 09:14 · patient « Anne Rochat » · téléphone : … ».
- *(2.87)* **`Signer le journal avec t.`** : les lignes suivantes du journal portent l'auteur t (un texte), jusqu'à la prochaine signature ou la fin de l'exécution ; `Signer le journal avec absent.` les laisse sans auteur. L'historique le montre après l'heure : « 05.11.2026 09:14 (Direction) · téléphone : … ». GrymoiR ne connaît pas l'utilisateur d'un programme : c'est le programme qui dit qui travaille (un mot de passe de rôle, un choix d'utilisateur). Un journal né en 2.85 reçoit la colonne de l'auteur à sa première ligne signée ou non ; ses lignes d'avant restent sans auteur. Pas dans un calcul : « Un calcul ne signe rien : signe le journal dans une action. » Forme compacte : `_signer_journal t`.
- Pas dans un calcul : « Un calcul ne lit pas l'historique : lis-le dans une action ou un écran. » Forme compacte : `_classe _un patient _conservé _journalisé`, `_historique x`, `_historique_base`.

## 17. Questions à l'utilisateur *(v1.0)*

```
Le nom vaut la réponse à « Votre nom ? ».
L'âge vaut la réponse en nombre entier à « Âge ? ».
La naissance vaut la réponse en date à (question).
Si la réponse en vrai ou faux à « Encore ? », …
```

- `la réponse à « … »` lit une ligne tapée par l'utilisateur. C'est une valeur comme une autre : elle se range avec `vaut` ou `devient`, s'initialise dans le bloc d'un nouvel objet, se passe en argument. Aucune phrase nouvelle, donc aucune entorse au § 2.1.
- La question est un texte écrit tel quel, ou une expression entre parenthèses : `la réponse à (question)`. Sans texte ni parenthèse derrière, `la réponse` reste un nom ordinaire.
- Le type s'intercale : `en nombre`, `en nombre entier`, `en date`, `en année`, `en vrai ou faux`. Sans mention, la réponse est un texte.
- La question s'affiche telle quelle, suivie d'une espace, sans saut de ligne : le curseur attend sur la même ligne.
- Lecture : les nombres suivent le § 1.2 (virgule décimale, séparateurs de milliers), les dates le § 14.1, les années le § 14.5 ; `vrai ou faux` accepte `oui`, `non`, `vrai`, `faux`, sans tenir compte de la casse. Les espaces de début et de fin tombent.
- Relance : une réponse qui ne convient pas est annoncée, puis la question se repose. « « x » n'est pas un nombre. », « « 21.9.26 » n'est pas une date : écris jour.mois.année (21.09.2026). », « Réponds par oui ou non. » Une ligne vide convient à un texte, et relance pour les autres types. Le programme ne s'arrête pas parce que l'utilisateur a tapé de travers.
- **Annuler** : une ligne faite d'un point seul (espaces permises autour) fait échouer la question : « Saisie annulée. ». Un `Essayer` englobant la rattrape comme toute erreur (§ 18) ; sinon, le programme s'arrête. La première relance d'un programme l'annonce : « … Tape « . » seul pour annuler. ». Un texte fait d'un point seul ne peut donc pas être répondu ; `..` reste une réponse ordinaire.
- Fin de l'entrée (Ctrl+D) : erreur d'exécution, « Plus rien à lire : la réponse à « Âge ? » manque. » Ctrl+C interrompt comme partout ailleurs.
- **Une question valide ce qui la précède** : les fichiers en attente (§ 15.2) sont écrits, puis la base est validée, puis le journal se vide. Une erreur survenue plus tard n'annule que depuis la dernière question, et le dit (§ 3.3). Le verrou de la base est rendu pendant l'attente, et repris avec la réponse : un autre programme peut donc écrire entre deux questions, et les objets déjà lus gardent en mémoire les valeurs de leur lecture.
- Effet de bord, donc réservé aux actions : « Un calcul ne pose pas de question : demande dans une action. » Et interdit dans la boucle interactive, qui lit déjà sur la même entrée.
- En forme compacte : `_réponse « Nom ? »`, `_réponse (nombre_entier) « Âge ? »`, `_réponse (date) (q)`.


## 18. Reprise après erreur *(v1.0)*

```
Essayer :
    Ajouter un contact.
En cas d'échec :
    Afficher « Rien n'a été ajouté : » puis le motif de l'échec.
```

- `Essayer :` ouvre un bloc. `En cas d'échec` le suit obligatoirement, aligné sur `Essayer`, en forme courte (`En cas d'échec, afficher …`) ou en bloc. Un `Essayer` sans son bloc d'échec est une erreur d'analyse : une erreur ne passe jamais sous silence (charte, art. 8).
- Si une erreur survient dans le bloc essayé, y compris au fond d'une formule qu'il appelle, **tout ce que le bloc a fait est annulé** : noms, champs, cases locales, objets conservés, modifiés ou supprimés, fichiers prévus. Le bloc `En cas d'échec` s'exécute alors, et le programme continue après lui. Sans erreur, le bloc d'échec est sauté.
- Ce qui a été affiché reste affiché, comme pour toute exécution ratée (§ 3.3).
- `le motif de l'échec` vaut le message de l'erreur, en texte, sans sa position : « Division par zéro. ». Il n'existe que dans le bloc `En cas d'échec` ; ailleurs, les mêmes mots gardent leur sens ordinaire (un champ `motif` d'un objet `échec`), et sans un tel sens, c'est une erreur d'analyse.
- Une question (§ 17) posée dans le bloc essayé valide ce qui la précède : l'échec n'annule que depuis la dernière question.
- Une erreur dans le bloc d'échec n'est pas rattrapée par son propre `Essayer` ; un `Essayer` englobant la rattrape, sinon le programme s'arrête. Les essais s'imbriquent : un échec extérieur annule aussi ce qu'un essai intérieur réussi a gardé.
- Jamais rattrapées : l'interruption (Ctrl+C), la fin de l'entrée pendant une question, l'absence d'entrée, et une base devenue inutilisable. Rattraper la fin de l'entrée ferait tourner un menu sans fin.
- `Rendre`, `Sortir de la boucle` et `Passer au tour suivant` quittent le bloc essayé sans échec : ce qu'il a fait est gardé.
- `Essayer` s'emploie partout, calculs compris : rattraper une erreur ne rend pas un calcul impur. `essayer` ne commence pas le nom d'une action.
- Limite connue : un `Essayer` répété dans une longue boucle sans question garde au journal une entrée par tour et par nom modifié, jusqu'à la fin de l'exécution.
- En forme compacte : `_essayer` … `_échec` … `_fin`, et `_motif`.
- Une couleur d'étiquette par valeur *(2.2)* : `un état (« payée » en vert, « en retard » en rouge ou « annulée »)`. Couleurs : bleu, vert, turquoise, violet, gris, rouge, orange ; une valeur peut n'en avoir aucune. Dans une liste d'écran et dans une fiche *(2.8)*, affichée (`Afficher la fiche de c.`) ou déduite (`Ouvrir la fiche de c.`), la valeur colorée s'affiche en étiquette, un fond pâle et un texte foncé ; chaque paire dépasse 6,5:1 de contraste (le WCAG 2.1 demande 4,5:1 pour du texte normal). La couleur n'est qu'un affichage : la valeur reste le texte. Forme compacte : `« payée » _vert`.

### 18.1 Refuser *(implémenté le 27 septembre 2026)*

```
Pour comptabiliser une écriture :
    …
    Si le total des débits ≠ le total des crédits,
        refuser « Écriture déséquilibrée : débits » puis le total des débits puis « et crédits » puis le total des crédits.
    …
```

- `Refuser` suivi d'un ou plusieurs éléments reliés par `puis` arrête l'exécution par une erreur dont le message est ces éléments, écrits comme `Afficher` les écrirait (§ 4 ; nombres au style du programme, § 4.1) et séparés par une espace. Une valeur absente est l'erreur habituelle (§ 16.9).
- C'est une erreur comme une autre : elle s'affiche à la position de la phrase, annule tout ce que l'exécution, l'événement (§ 22.3) ou le bloc essayé a fait, objets conservés compris, et `Essayer` la rattrape : son message devient `le motif de l'échec`.
- Usage : protéger une règle que la base doit respecter (une écriture équilibrée, un stock positif) dans l'action qui crée ou modifie les objets concernés. La transaction garantit que rien n'est écrit si la règle est violée ; la garantie ne vaut que si les objets ne se créent que par cette action (une contrainte vérifiée par le langage lui-même, à chaque conservation, est reportée : charte, art. 12).
- Permis partout, calculs compris : échouer ne rend pas un calcul impur.
- `refuser` est un mot de construction (§ 10.7) : il ne commence pas le nom d'une action.
- En forme compacte : `_refuser « Écriture déséquilibrée : » ; d`.

### 18.2 Les sortes d'échec *(2.92)*

```
Essayer :
    La réponse vaut l'envoi au service de :
        « numéro » : le numéro du patient.
    Conserver le patient.
En cas d'échec d'unicité :
    Afficher « Ce patient existe déjà. »
En cas d'échec de réseau ou de fichier :
    Afficher « Réessaie plus tard : » puis le motif de l'échec.
En cas d'échec :
    Afficher la sorte de l'échec puis le motif de l'échec.
```

- Chaque échec a une **sorte**, un mot de la liste ci-dessous. La sorte est **promise** : elle ne change pas d'une version à l'autre (charte, art. 13.4). Le texte du motif, lui, ne l'est pas (art. 13.5) : un programme qui choisit sa conduite d'après le motif se casserait à la première reformulation, d'où les sortes.
- **Les sortes.**

| Sorte | Échecs |
|---|---|
| `division` | une division par zéro (§ 3.2) |
| `nombre` | un nombre trop grand, un exposant non entier, un rang ou un nombre de caractères qui n'est pas un entier convenable |
| `lecture` | un texte qui ne se lit pas comme un nombre, une date, une heure, une année, vrai ou faux (§ 4.5, § 15.5) |
| `absence` | une valeur absente employée (§ 16.9) |
| `clé` | une clé absente d'une table, ou déjà présente (§ 13.11) |
| `rang` | un élément ou un caractère au-delà de la fin (§ 4.6, § 13.10) |
| `texte` | un repère absent d'un texte (`après`, `avant`, § 4.6) |
| `unicité` | une valeur déjà prise par un champ `, unique` (§ 16.3) |
| `lien` | un lien vers un objet non conservé ou supprimé, une suppression définitive empêchée par un lien (§ 16.3, § 16.12) |
| `fichier` | un fichier introuvable, illisible, ou qui existe déjà (§ 15.2) |
| `réseau` | tout échec d'un envoi ou d'une lecture à un service web (§ 22.18) |
| `port` | tout échec d'un port série ou de l'écoute de WSJT-X (§ 22.25, § 22.26) |
| `saisie` | une question annulée par « . » (§ 17) |
| `refus` | un `Refuser « … ».` (§ 18.1) |
| `commande` | une commande (§ 15.10) : programme introuvable, échoué (code non nul), trop lente, sortie qui n'est pas du texte *(3.04)* |
| `autre` | tout le reste : en général une faute du programme plutôt qu'un aléa |

- **Les blocs.** Après le bloc `Essayer`, un ou plusieurs blocs `En cas d'échec de … :`, alignés sur lui, chacun en forme courte ou en bloc ; la sorte suit `de` ou `d'` (`d'unicité`, `d'absence`), plusieurs se séparent par `ou` (`de réseau ou de fichier`). Le premier bloc dont une sorte convient s'exécute ; les autres sont sautés. Un bloc `En cas d'échec` sans sorte prend tout ce qui reste ; il vient en dernier : « « En cas d'échec » sans sorte vient après les blocs qui en ont une. »
- **Rien n'est avalé.** Sans bloc qui convienne, l'échec **remonte** : à l'essai englobant, sinon il arrête le programme, avec son motif et sa position d'origine. Ce qu'a fait le bloc essayé reste annulé.
- `la sorte de l'échec` vaut la sorte, en texte (`« unicité »`), dans tout bloc `En cas d'échec`, comme `le motif de l'échec`. Elle se compare : `Si la sorte de l'échec est « réseau », …`.
- Les refus à l'analyse : une sorte inconnue (« « unicite » n'est pas une sorte d'échec : division, nombre, lecture, absence, clé, rang, texte, unicité, lien, fichier, réseau, port, saisie, refus, commande ou autre. »), une sorte nommée deux fois dans le même essai (« La sorte « réseau » a déjà son bloc, ligne 4. »).
- **Comment la machine trouve la sorte.** Les instructions dont le contexte est clair la fixent (un envoi : `réseau` ; un port : `port` ; `Refuser` : `refus`) ; pour les autres, une table relie des fragments de motif à leur sorte (`src/vm.c`, `SORTES`). Chaque sorte a son essai dans `test_machine` : reformuler un motif sans mettre la table à jour fait échouer les essais, la sorte ne change jamais en silence.
- Un `Essayer` de la 2.91, avec son seul `En cas d'échec`, se lit et se comporte comme avant.
- En forme compacte : `_échec_de unicité`, `_échec_de réseau _ou fichier`, après le bloc essayé ; `_sorte` pour la sorte de l'échec.

## 19. Formulaire *(v1.0)*

```
Le c vaut un nouveau contact saisi.
Conserver c.
Le p vaut une nouvelle partition saisie :
    Le compositeur vaut bach.
```

- `saisi` (`saisie`, accordé avec l'entité) après `un nouveau …` demande chaque champ par une question (§ 17), puis rend l'objet. C'est une valeur, comme `la réponse à` : elle se range, s'initialise, se passe en argument. Le formulaire ne conserve rien : `Conserver` reste explicite.
- Seule une entité se saisit : ses champs ont un type. Comme toute question, le formulaire est réservé aux actions et au programme, jamais à un calcul ni à la boucle interactive.
- Ordre : les champs hérités, puis ceux des aptitudes, puis les champs propres. Un champ initialisé dans le bloc n'est pas demandé. Un champ multiple (§ 16.13) n'est jamais demandé : il reste vide.
- Libellé : le nom du champ, première lettre en capitale : « Date d'inscription ? ». Une valeur de départ (§ 16.7) s'affiche entre crochets et répond à une ligne vide : « Pays [Suisse] ? ».
- Lecture selon le type, avec la relance du § 17. Une ligne vide laisse `absent` un champ facultatif ; pour un champ obligatoire sans valeur de départ, elle relance : « Une réponse est attendue. » (un nom vide n'a jamais voulu dire quelque chose).
- Fichier ou image : la réponse est un chemin (§ 15.2) ; un fichier illisible ou qui n'est pas une image relance.
- Courriel, téléphone, adresse web *(2.11)* : demandés comme des textes, leur forme vérifiée dès la réponse (§ 22.13), puis relance : « « anne@ » n'est pas un courriel (nom@domaine.ch). ». Avant la 2.11, le formulaire les prenait pour des liens : facultatifs, il les sautait sans rien dire ; obligatoires, il refusait l'entité. Un mot de passe (§ 16.1) n'est toujours pas demandé : facultatif, il reste absent ; obligatoire, il se donne dans le bloc du nouvel objet, où il est haché.
- Lien : la réponse est la valeur du premier champ texte unique de l'entité liée, comparée exactement (§ 16.4) : « Compositeur ? Bach » retrouve le compositeur conservé dont le nom est « Bach ». Aucun : « Aucun compositeur conservé n'a « Brahms » pour nom. », puis relance. Une entité liée sans champ texte unique : un lien facultatif reste absent ; un lien obligatoire est une erreur d'exécution, qui demande de l'initialiser dans le bloc.
- Unique : la valeur se vérifie dès la réponse, corbeille comprise : « « A-1 » est déjà pris. », puis relance. `Conserver` vérifie encore, car un autre programme peut écrire entre deux questions.
- Chaque question valide ce qui la précède (§ 17) ; dans un `Essayer` (§ 18), l'échec n'annule que depuis la dernière.
- Un point seul annule le formulaire entier (§ 17) : l'objet n'est pas rendu.
- **Modifier** : `Saisir à nouveau p.` pose les mêmes questions sur un objet existant, chaque valeur actuelle entre crochets : « Titre [L'Offrande musicale] ? ». Un lien montre sa clé, un fichier son nom d'origine. Une ligne vide garde la valeur ; un tiret seul `-` vide un champ facultatif, et le libellé l'annonce : « Arrangeur [Anton Webern] (- pour vider) ? ». Un champ sans valeur se demande comme à la création.
- **Tout ou rien** : les réponses s'accumulent, et les champs ne s'écrivent qu'après la dernière, puis la base suit (§ 16.3). Annuler au milieu (`.`) ne change aucun champ. Garder sa propre valeur unique n'est pas un doublon. Un objet de la corbeille se refuse : « … est dans la corbeille : rétablis-le d'abord. ».
- `Saisir à nouveau` est une phrase, réservée aux actions et au programme ; `saisir` ne commence pas le nom d'une action.
- Reporté : choisir les champs demandés ou leur libellé, l'autocomplétion des réponses.
- En forme compacte : `_nouveau contact _saisi`, `_nouveau partition _saisi _avec`, `_saisir p`.


## 20. Les objets à l'écran *(v2)*

Trois constructions, conçues ensemble, pour montrer les objets et passer de l'un à l'autre. Un clic reste une réponse à une question : le programme mène toujours (docs/v2.md, § 7).

### 20.1 La fiche

```
Afficher la fiche de p.
Afficher la fiche du compositeur de l'œuvre.
```

- Un titre (le nom de la classe, suivi de la valeur de sa clé s'il en a une), puis chaque champ avec sa valeur, dans l'ordre des champs : hérités, aptitudes, propres.
- Chaque valeur s'écrit comme `Afficher` l'écrirait. Un lien s'écrit par la clé de l'objet lié (son premier champ texte unique, § 19), sinon « un compositeur ». Un champ multiple (§ 16.13) liste ses objets, dans l'ordre où ils ont été gagnés, corbeille exclue, ou « aucun ». Un champ sans valeur s'écrit « absent ».
- En console, les libellés s'alignent en colonne. Dans le navigateur, la fiche est un tableau, et une image s'y montre en image.
- `Afficher p.` garde son sens (« une partition ») : le changer romprait la charte, art. 13.
- `la fiche` n'est reconnue qu'en tête d'une phrase `Afficher`, et seulement si `fiche` n'est ni un nom ni un champ déclaré : un champ `fiche` garde son sens ordinaire.
- Une valeur qui n'est pas un objet : « Une fiche montre un objet : la valeur est un nombre. »
- En forme compacte : `_fiche p`.

## 21. Fichiers utilisés *(atelier, A2)*

Un projet tient dans plusieurs fichiers : les données, les écrans, les traitements. Décidé le 24 septembre 2026 (`docs/atelier.md`, § 6.2).

```
Remarque : Partothèque, le programme principal.
Utiliser « données ».
Utiliser « écrans/fiches ».

Pour recruter :
    …
```

### 21.1 La phrase

- `Utiliser « chemin ».` rend visibles, dans tout le fichier, les déclarations du fichier désigné.
- Elle se place en tête du fichier : après les remarques éventuelles, avant toute autre phrase. Plus bas : « « Utiliser » se place en tête du fichier, avant toute autre phrase. » Les dépendances d'un fichier se lisent ainsi dans ses premières lignes.
- Le chemin s'écrit entre guillemets, sans extension : `« données »` trouve `données.grym` ou `données.grymc`. Écrire l'extension est une erreur, qui propose la tournure sans elle ; `grym traduire` n'a jamais à réécrire une phrase `Utiliser`.
- Si les deux fichiers existent, le choix serait arbitraire : « « données » désigne à la fois « données.grym » et « données.grymc » : gardes-en un seul. »
- `utiliser` devient un mot de construction (§ 10.7) : il ne commence plus le nom d'une action.

### 21.2 Un fichier de déclarations

Un fichier utilisé ne contient que des déclarations : remarques, classes, entités, aptitudes, calculs, actions, et ses propres phrases `Utiliser`. Aucune phrase qui s'exécute : ni `Afficher`, ni `vaut` au premier niveau, ni `Conserver`, ni le réglage des nombres (§ 4.1). Sinon : « « exec.grym » contient une phrase qui s'exécute (ligne 2) : seul un fichier de déclarations s'utilise. »

- Seul le programme principal s'exécute. Un fichier qui contient des phrases exécutables est un programme ; il ne s'utilise pas.
- Une action d'un fichier utilisé ne voit pas les variables du programme principal : elles n'existent pas encore quand on la lit. Elle passe par ses paramètres (§ 9.4).

### 21.3 Chemins

- Le chemin part du dossier du fichier qui écrit la phrase : si `écrans/fiches.grym` utilise `« commun »`, c'est `écrans/commun.grym`.
- Dans la boucle interactive, il part du dossier courant.
- Fichier absent : « « données » introuvable : ni « données.grym » ni « données.grymc ». »

### 21.4 Une seule lecture, aucun cercle

- Un fichier utilisé plusieurs fois, directement ou par un détour, n'est lu qu'une fois par projet : deux fichiers qui utilisent tous deux `« commun »` ne le déclarent pas deux fois.
- Dans la boucle interactive, utiliser à nouveau un fichier déjà lu ne fait rien.
- Une utilisation circulaire est une erreur : « Utilisation circulaire : « x.grym » s'utilise lui-même, directement ou par un autre fichier. » De même : « Un fichier ne s'utilise pas lui-même. »

### 21.5 Visibilité et conflits

- Ce qu'un fichier déclare est visible dans tout fichier qui l'utilise, et dans ceux qui utilisent ce dernier : les déclarations se transmettent.
- Un nom reste unique dans tout le projet. Le message nomme l'autre fichier : « La classe « compositeur » existe déjà : déclarée dans « données.grym », ligne 2. »

### 21.6 Erreurs

- Une erreur dans un fichier utilisé désigne ce fichier : `données.grym:7:18 : erreur : …`, à l'analyse comme à l'exécution.
- Dans un éditeur (l'atelier, ou VS Code par `grym lsp`), le fichier ouvert porte l'erreur sur sa phrase `Utiliser` qui y mène : « « données.grym », ligne 7 : … ».

### 21.7 Bytecode et base

- `grym compiler` produit un seul `.grymb` pour tout le projet : il tourne sans les sources. Chaque formule y garde le nom de son fichier, pour les messages (`docs/vm.md`, § 11, format 25).
- La base reste celle du programme principal : `partotheque.grymd`, même si les entités sont déclarées dans `données.grym` (§ 16.5).

### 21.8 Forme compacte et outils

- En forme compacte : `_utiliser « données »`, en tête du fichier aussi.
- `grym formater` et `grym traduire` écrivent la phrase telle quelle, jamais les déclarations du fichier utilisé.
- Les suites attendues (§ 8) proposent `Utiliser` en tête du fichier seulement, et, plus bas, les noms déclarés dans les fichiers utilisés.

### 21.9 Bibliothèques *(2.61, implémenté le 26 octobre 2026)*

Une bibliothèque est un fichier de déclarations (§ 21.2) qui sert à plusieurs programmes : la QR-facture, le Tarif 590, l'envoi d'un SMS. Elle porte un nom et une version.

```
Remarque : bibliothèques/Suisse santé/bibliothèque.grym
Cette bibliothèque est « Suisse santé », version 2.
Utiliser « arrondis ».

Une chose tarifée a :
    un montant (nombre).

Le total arrondi d'un montant vaut l'arrondi du montant à 0,05.
```

```
Remarque : Praxis.grym, le programme
Utiliser la bibliothèque « Suisse santé », version 2.

Une facture, conservée, est une chose tarifée.
```

- **Où.** Une bibliothèque est un dossier de `bibliothèques/`, à côté du programme principal : `bibliothèques/Suisse santé/`. Son entrée est `bibliothèque.grym` (ou `.grymc`). Le dossier fait partie du projet : l'atelier le recopie dans l'application qu'il fabrique, et `grym compiler` l'inclut dans le `.grymb`, comme tout fichier utilisé. Il n'y a pas de registre : chaque projet garde sa copie, figée, de la version qu'il a éprouvée.
- **Le nom** s'écrit comme le dossier, sans barre oblique. Introuvable : « La bibliothèque « X » est introuvable : il manque « bibliothèques/X/bibliothèque.grym ». »
- **Sa déclaration.** L'entrée commence, après ses remarques, par `Cette bibliothèque est « Suisse santé », version 2.` Le nom y est celui du dossier ; la version est un nombre entier, à partir de 1, sans zéro en tête. Ailleurs que dans une entrée, la phrase est une erreur : un fichier ordinaire qui se déclare bibliothèque se range dans `bibliothèques/`.
- **La version demandée** est la version de la bibliothèque, exactement : « La bibliothèque « X » est en version 3 ; le programme demande la version 2 : relis ce qui a changé, puis écris « version 3 ». » Pas d'intervalle, pas de « au moins » : changer de version est une décision écrite dans le programme.
- **Une seule version par application.** Si deux fichiers, ou deux bibliothèques, demandent la même bibliothèque en deux versions : « La bibliothèque « X » est déjà utilisée en version 2 : une application n'en utilise qu'une version. »
- **Pas d'entité, pas de règle.** Une bibliothèque déclare des aptitudes, des classes, des calculs, des actions et des XML à valeurs (§ 22.19, 2.63) ; les tables restent celles du programme (charte, art. 7 et 13). « Une bibliothèque ne déclare pas d'entité (« facture », …) : déclare une aptitude, que l'entité du programme adopte. » De même pour une règle (§ 16.14).
- **Ses fichiers.** Une bibliothèque utilise d'autres fichiers par `Utiliser « … »`, dans son dossier seulement : « Une bibliothèque n'utilise que ses propres fichiers : « ../commun » sort du dossier de « X ». » Elle utilise d'autres bibliothèques, cherchées dans le même `bibliothèques/`. Un programme n'entre pas dans une bibliothèque par un chemin, ce qui sauterait sa version : « « bibliothèques/X/arrondis » appartient à une bibliothèque : écris « Utiliser la bibliothèque « … », version … ». »
- **Les fichiers internes** *(2.62)*. Un fichier d'une bibliothèque qui commence par `Ce fichier est interne à sa bibliothèque.` (après ses remarques, parmi ses phrases `Utiliser`) ne montre ses calculs et ses actions qu'aux fichiers de cette bibliothèque. Ailleurs : « « centime suisse » est interne à la bibliothèque « Suisse santé » : elle ne le montre qu'à ses propres fichiers. » La bibliothèque peut ainsi changer ses outils sans toucher les programmes qui l'utilisent, à condition de garder ce que son entrée montre.
  - Le choix se fait par fichier, comme Java, depuis sa version 9, exporte des paquets et non des classes (JSR 376, de mémoire) : une phrase en tête de fichier se lit d'un coup d'œil, et les en-têtes de formules restent ceux du § 9.
  - Un fichier interne ne déclare que des calculs et des actions (un XML en est une) : une classe ou une aptitude se voit dans les entités du programme, elle ne peut pas être cachée.
  - L'entrée d'une bibliothèque n'est jamais interne : elle est ce que la bibliothèque montre. Un fichier hors d'une bibliothèque ne se dit pas interne.
  - Un nom interne reste réservé dans tout le projet : la machine range les formules par nom (`docs/vm.md`), et un programme qui le redéclare est refusé comme au § 21.5.
  - Forme compacte : `_interne`.
- **Les noms.** Ce qu'une bibliothèque déclare est visible comme toute déclaration d'un fichier utilisé (§ 21.5) : un nom reste unique dans tout le projet, et le message d'un doublon nomme le fichier de la bibliothèque. Un programme qui déclare un calcul du même nom qu'une bibliothèque est refusé, rien n'est remplacé en silence (charte, art. 6).
- **Ouverte seule**, dans l'atelier ou par `grym lsp`, l'entrée cherche les autres bibliothèques dans le `bibliothèques/` qui la contient.
- **Forme compacte** : `_utiliser _bibliothèque « Suisse santé » 2` et `_bibliothèque « Suisse santé » 2`.
- **Pas encore** : des écrans, des parties et des documents dans une bibliothèque (un fichier utilisé n'en contient pas, § 21.2).

## 22. Écrans *(A3 : implémenté le 25 septembre 2026)*

Conception complète et raisons : `docs/ecrans.md`. Ce paragraphe en fixe les phrases ; elles entrent dans l'analyseur tranche par tranche.

Implémenté (A3-a) : la déclaration avec liste (recherche, `dont`, `, par`), bouton, texte fixe et titre donné ; `Quand on clique sur « … »` et `Quand on choisit un … dans l'écran …` ; `Ouvrir l'écran …` (programme ou action, hors d'un événement et hors d'`Essayer`) ; `Fermer l'écran.` ; les refus d'analyse du § 22.2 ; le refus de `grym lancer` et `grym servir` ; les deux formes, l'aide à la saisie et la coloration. Implémenté (A3-b) : les zones de saisie (`un pays (texte), « Suisse » au départ, facultatif`, types des entités ; une zone liée à une entité a le menu de ses clés ; fichier et image viendront plus tard) ; `le pays de l'écran`, lu et modifié, dans un événement et dans la liste d'un écran ; `le compositeur choisi de l'écran` (la ligne sélectionnée, ou absent) ; `Quand on change le pays`, `Quand on ouvre l'écran`, `Quand on ferme l'écran`. Une zone validée est lue selon son type ; refusée (« abc » dans un nombre), l'écran le dit et aucun événement ne s'exécute. Implémenté (A3-c) : `Ouvrir la fiche de c.` (`du compositeur`, `de l'œuvre`) ouvre l'écran déduit de l'entité de `c` : un texte par champ (« Titre : Messe »), un bouton par lien (« Compositeur : Bach ») qui ouvre la fiche de l'objet désigné par-dessus, « Modifier » (le formulaire de `Saisir à nouveau`, puis la fiche relue) et « Fermer ». Un champ « plusieurs » (§ 16.13) y figure par son libellé, suivi, côte à côte, d'un bouton par élément, qui ouvre sa fiche par-dessus ; vide, il s'écrit « Genres : aucun ». `Ouvrir`, d'un écran ou d'une fiche, s'emploie aussi dans un événement : le nouvel écran s'ouvre par-dessus, et l'événement reprend à sa fermeture ; `Ouvrir` reste interdit dans `Essayer`. Implémenté (A3-d) : les colonnes choisies, `, avec le titre, le compositeur et la date de naissance du compositeur` (après `dont` et `, par`) : chaque colonne est un champ de l'entité, ou le champ d'un objet qu'elle désigne, lu de droite à gauche ; son titre est ce qu'on a écrit, sans l'article, avec une majuscule ; un lien absent laisse la case vide. Les blocs `côte à côte :` et `l'un sous l'autre :`, qui s'emboîtent et se ferment à l'élément moins indenté. Une ligne qui n'existe plus quand on la choisit ne déclenche rien. En forme compacte : `_avec titre ; compositeur.date_de_naissance`, `_côte_à_côte` … `_fin`, `_l'un_sous_l'autre` … `_fin`.

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

### Plan du § 22 *(3.07)*

Le § 22 a grandi avec les besoins de Praxis et d'Écoutille : il traite des écrans, mais aussi de ce que fait une application autour d'eux. Ses numéros ne changent pas, puisque messages, essais et code les citent ; ce plan les range par thème.

| Thème | Sections |
|-------|----------|
| **Un écran** : le déclarer, ses éléments | 22.1 Déclaration ; 22.4 Mots et forme compacte ; 22.6 États des éléments ; 22.7 Groupes et valeurs permises ; 22.8 Images ; 22.9 Position libre ; 22.10 Onglets ; 22.11 Graphiques ; 22.12 Menus ; 22.13 Courriel, téléphone, adresse web ; 22.14 Mots de passe ; 22.16 Code QR ; 22.23 Bouton destructif ; 22.24 Agenda |
| **Son déroulement** | 22.2 Événements (minuterie comprise, 3.03) ; 22.3 Ouvrir, fermer, transactions ; 22.22 Signaler |
| **Son apparence** | 22.5 Apparence |
| **Ce qu'il imprime** | 22.15 Documents |
| **Les données sensibles** | 22.17 Secrets |
| **Les échanges avec l'extérieur** | 22.18 Envoi à un service web ; 22.19 XML déclaré ; 22.27 Les courriels. Ailleurs : § 15.9 (les lignes, le JSON), § 15.10 (une commande) |
| **Le matériel et la radio** | 22.20 Écoutes ; 22.21 Les écoutes d'une entité ; 22.25 Ports série ; 22.26 Les décodages de WSJT-X |

### 22.1 Déclaration

- `le texte « Aujourd'hui », en petites capitales` *(2.20)* : un titre de section ou un surtitre, en capitales espacées, petites, en couleur secondaire. Forme compacte : `_capitales`.
- `, en cartes` *(2.19)*, après les colonnes : chaque objet devient une carte, trois par rangée. La première colonne s'y écrit en grand (une heure, un nom), les colonnes à étiquette (§ 16.15) en haut à droite, la deuxième colonne dessous, les suivantes jointes par « · ». Un clic sur une carte vaut le choix d'une ligne : `Quand on choisit …` s'exécute. Sans objet : « Rien pour l'instant. ». Pour un tableau de bord, un planning du jour ; une liste longue reste un tableau. Forme compacte : `_cartes`.
- `L'écran des compositeurs montre :`, au premier niveau, suivi de ses éléments séparés par des virgules, le dernier suivi d'un point. Un titre différent du nom se donne entre guillemets : `L'écran des compositeurs, « Nos compositeurs », montre :` ; sinon le titre vient du nom (« Compositeurs », « Accueil »).
- Éléments : `la liste des … conservés [dont …] [, par …] [, avec le …, le … et le …]` (recherche du § 16.4) ; `un bouton « … »` ; `le texte « … »` ; une zone de saisie déclarée comme un champ d'entité (`un pays (texte), « Suisse » au départ`, `, facultatif`) ; `côte à côte :` et `l'un sous l'autre :`, qui ouvrent un bloc indenté ; `le groupe « … » :` (§ 22.7) et `en position libre :` (§ 22.9), aussi.
- `, par date puis heure` *(2.66)* : une liste d'écran ou de document trie sur un second champ, croissant, à égalité du premier ; `, par date décroissant puis heure` inverse le premier seulement ; *(2.73)* `, par jour décroissant puis heure décroissant` inverse aussi le second (le plus récent d'abord, dans le jour aussi ; forme compacte `_par jour _décroissant _puis heure _décroissant`). Le second champ est, comme le premier, de texte, de nombre, de date ou vrai ou faux : « Second tri attendu sur un champ de texte, de nombre, de date ou vrai ou faux ». Les absents vont en dernier. Forme compacte : `_par date _puis heure`.
- Colonnes par défaut : les champs simples, dans l'ordre de la déclaration ; un lien montre la clé de l'objet désigné ; fichiers, images et champs « plusieurs » restent hors de la liste.
- Une zone de fichier ou d'image *(1.91)* s'écrit comme le champ qu'elle remplit : `un contrat (fichier)`, `une photo (image), facultative`. On y choisit un fichier ; sa valeur est ce fichier, avec ses octets et son nom, prête à ranger dans un champ `(fichier)` ou `(image)`. Une zone d'image refuse un fichier qui n'est pas une image (« « notes.txt » n'est pas une image (PNG, JPEG, GIF ou WebP). »). Elle n'a pas de valeur de départ écrite.
- Des choix multiples *(1.93)* s'écrivent comme le champ multiple d'une entité (§ 16.13) : `des genres (genre)`, une case par objet conservé de l'entité, désigné par sa clé. La zone se lit avec les tournures du champ multiple : `Pour chaque genre de l'écran`, `le nombre de genres de l'écran`. Elle ne se fait que parmi les objets d'une entité conservée (« Des choix multiples se font parmi les objets d'une entité conservée : « des genres (genre) » ; « couleur » n'en est pas une. »). Pour ranger les choix dans un objet : `Pour chaque genre de l'écran : Les genres de o gagnent genre.` Forme compacte : `_des genres (genre)`.
- **Le texte calculé** *(2.24)* : `le texte` suivi d'une valeur au lieu d'un texte entre guillemets, `le texte "Aujourd'hui, " suivi d'aujourd'hui en toutes lettres, en petites capitales`, `le texte "Séances de " suivi du nom de la personne ouverte`. La valeur se lit comme une condition « actif si » (§ 22.6) : zones de l'écran, variables du programme, base ; elle s'écrit comme `Afficher` l'écrit, et se relit avant chaque attente, après chaque événement, comme une liste. `suivi de` colle sans espace (§ 4.4) : l'espace d'un texte qui finit par une espace s'écrit entre `" "`. L'aperçu de l'atelier montre « … » à sa place. Une valeur qui ne se lit pas : « « le texte » attend une valeur : « le texte « Reçu de » suivi du nom de l'écran ». ». Forme compacte : `_texte_calculé « V : » _suivi _écran.ville`.
- Un écran est un objet : ses zones de saisie sont ses champs, jamais conservés.
- Une zone `(date)` a son calendrier *(19 octobre 2026, sans changement de la langue)* : dans l'atelier et l'application fabriquée, une icône au bout du champ ouvre un mois, semaine du lundi au dimanche ; le jour choisi s'écrit « 19.10.2026 », comme tapé, et `Quand on change …` s'exécute. Dans le navigateur, le champ est `type=date`, son calendrier natif ; la page le montre et le reçoit en « 2026-10-19 », la machine en « 19.10.2026 ». Le champ reste un champ de texte : on peut taper la date ou le vider. Un formulaire déduit (§ 19) a le même calendrier dans l'atelier.

### 22.2 Événements

- `Quand on ouvre l'écran X :`, `Quand on clique sur « B » dans l'écran X :`, `Quand on choisit un c dans l'écran X :`, `Quand on change la zone dans l'écran X :`, `Quand on ferme l'écran X :`, suivis d'un bloc. Ce sont des actions (§ 9).
- *(3.03)* **La minuterie** : `Toutes les 10 secondes dans l'écran de veille :`, `Toutes les secondes …`, `Toutes les minutes …`, `Toutes les 5 minutes …`, suivis d'un bloc. Tant que l'écran est ouvert et attend, son bloc s'exécute à chaque échéance, comme un événement (une transaction ; un échec s'affiche dans l'écran, qui reste ouvert). Les échéances se comptent l'une après l'autre, pas depuis le dernier geste : des clics fréquents ne la retardent pas. Une échéance ne change ni la ligne choisie d'une liste ni une saisie en cours ; elle ne vient pas pendant qu'un autre événement s'exécute, et deux échéances manquées n'en font qu'une. Une minuterie par écran (« L'écran de veille a déjà sa minuterie (ligne 3). ») ; de 1 seconde à 24 heures ; un nombre entier.
  - **Où** : dans l'atelier et l'application fabriquée, l'attente s'arrête à l'échéance. Dans le navigateur (`grym servir`), la page renvoie son formulaire à l'échéance par `grym.js` : une saisie commencée part avec lui, comme pour tout geste ; sans JavaScript, la minuterie ne bat pas. La console n'a pas d'écrans.
  - En forme compacte : `_toutes_les 10 _secondes _dans de_veille`.
- *(2.56)* **Les parties** : des éléments écrits une fois, posés dans plusieurs écrans ou documents.

```
La partie « en-tête du cabinet » montre :
    l'un sous l'autre :
        l'image de la signature du cabinet conservé, large de 220,
        le texte l'adresse professionnelle du cabinet conservé.

Le document de quittance montre :
    côte à côte :
        la partie « en-tête du cabinet »,
        le texte l'adresse du patient de la pièce en cours.
```

  `la partie « … »` s'écrit seule sur sa ligne, à la place d'un élément, avec sa virgule ou son point. Ses éléments se lisent là, dans l'écran qui l'accueille : ses zones sont les siennes, `l'écran` est le sien, et une erreur montre la ligne de la partie. La partie se déclare dans le même fichier, avant ou après ses usages ; une partie inconnue ou qui s'utilise elle-même est refusée. `grym formater` et la forme compacte (`_partie « en-tête du cabinet »`, la déclaration `_partie « … »` … `_fin`) écrivent la partie, jamais son contenu déplié.
- *(2.57)* **Une action peut rendre un objet ou une valeur** : `Rendre la q.` dans son corps. On la reçoit à l'appel : `Faire la quittance du patient et le total et le moyen, qui donne la q.` crée la variable `q`, ou la remplace si elle existe. Appelée en phrase seule, ce qu'elle rend s'oublie ; si elle finit sans rendre, elle donne absent. Un calcul, lui, ne conserve toujours rien : il peut être relu plusieurs fois, dans le texte d'un écran par exemple ; l'action qui crée, conserve et rend est la façon d'écrire une fois une création répétée. `, qui donne` sur une action qui ne rend rien : « « dire » ne rend rien : écris « Rendre … » dans l'action. » Forme compacte : `faire_la_quittance_de(p ; total ; moyen) _qui_donne _la q`.
- *(2.58)* **Une action pour plusieurs écrans** : `Pour remplir le nom avec un texte, dans l'écran d'inscription ou l'écran du patient :`. `l'écran` y désigne celui qui l'appelle. Chaque zone qu'elle lit ou écrit doit exister dans chacun des écrans nommés : « L'écran « a » n'a pas de zone « âge » : l'action y travaille aussi. » Forme compacte : `… _dans_écran d_inscription _ou du_patient`. Une action dans un seul écran est vérifiée de même.
- *(2.55)* Une action peut travailler dans un écran : `Pour remplir la fiche avec un patient, dans l'écran du patient :`. Dans son corps, `l'écran` désigne cet écran, comme dans ses événements ; elle s'appelle depuis l'un d'eux (ou depuis une action du même écran). L'écran doit être déclaré avant l'action. Forme compacte : `_action remplir_la_fiche_avec(_un patient) _dans_écran du_patient`.
- `Quand on choisit un compositeur` nomme la ligne `compositeur` dans son corps. `le compositeur choisi de l'écran` désigne la ligne sélectionnée, ou `absent`. `le pays de l'écran` lit une zone ; `Le pays de l'écran devient … .` la modifie. `de l'écran` hors d'un événement est une erreur.
- Un bouton sans événement, un événement qui cite un bouton ou une zone inexistants, deux listes de la même entité dans un écran : erreurs d'analyse. La fermeture ne se refuse pas.

- **`Faire comme un clic sur « B ».`** *(2.36)* : dans un événement d'un écran, le corps de l'événement « Quand on clique sur « B » » du même écran, déjà déclaré plus haut, s'exécute à cet endroit, dans la même transaction ; un refus plus loin annule tout. Un écran évite ainsi de recopier un événement dans un autre : « Facture ou quittance » fait d'abord ce que fait « Enregistrer ». Ailleurs qu'un événement : « « Faire comme un clic » ne vaut que dans un événement d'écran ». Forme compacte : `_comme_clic « B »`.
- **Dans le navigateur** *(2.36)* : un clic s'envoie par le rang du bouton (`clic=25`) ou par son libellé (`clic=Enregistrer`), unique dans l'écran.
- **Dans l'atelier et une application fabriquée** *(21 octobre 2026)* : un clic arrivé pendant qu'une zone envoie sa valeur (le bouton prend le focus, la zone le perd et part la première) n'est plus perdu : il suit, dès que l'écran attend de nouveau. La touche Entrée dans une zone vaut un clic sur le bouton principal de l'écran. La touche Échap *(2.67)* vaut un clic sur le bouton « Fermer » de l'écran, s'il est visible et actif, question de confirmation comprise ; un écran sans bouton de ce libellé (un accueil et son « Fermer l'application ») n'en fait rien. La croix de la fenêtre *(31 octobre 2026)* pose la même question que ce bouton « Fermer », quand il la poserait : « Annuler » laisse l'écran ouvert et sa saisie intacte ; confirmée, la croix ferme l'écran comme avant. Le navigateur, sans JavaScript, ne l'a pas.

### 22.3 Ouvrir, fermer, transactions

- `Ouvrir l'écran X.` montre l'écran et attend jusqu'à `Fermer l'écran.` ; `Ouvrir la fiche de c.` ouvre l'écran déduit de l'entité de `c`. Dans un événement, l'écran s'ouvre par-dessus ; à sa fermeture, l'événement reprend.
- `Ouvrir` valide d'abord ce qui précède ; chaque événement est ensuite sa propre transaction ; après la fermeture, la suite forme une nouvelle transaction (charte, art. 7).
- Un événement qui échoue n'écrit rien et laisse l'écran ouvert, avec le motif, **même s'il avait déjà demandé `Fermer l'écran.`** *(2.21)* : la règle violée par « Enregistrer, puis fermer » se lit sur l'écran qu'on voulait quitter. Avant le 19 octobre 2026, l'écran se fermait et le motif se perdait (l'écriture, elle, était bien annulée).
- **Les règles dans une fiche déduite** *(2.22)* : « Modifier » et « Nouvelle séance » (sous un tableau) vérifient les règles du § 16.14 avant de valider, comme la fin d'un événement. Refusée, la saisie s'annule tout entière, en base comme en mémoire ; la fiche relue montre le motif (« Le numéro GLN 123 n'a pas treize chiffres. »). Jusqu'à la 2.21, ces deux gestes validaient sans rien vérifier : une valeur qu'une règle refuse y passait. Le motif d'une saisie annulée (« . ») se montre aussi sur la fiche relue, où il se perdait.
- **Les tableaux d'une fiche** *(2.10)* : sous les champs de la fiche déduite, un tableau par lien d'une entité qui peut désigner l'objet (son entité, ou un ancêtre) : un titre au pluriel (« Séances »), la liste des objets qui le désignent, un bouton « Nouvelle séance » (« Nouveau client », « Nouvel achat »). Sans rien écrire, comme le reste de la fiche.
  - Colonnes : les champs de l'entité, sauf le lien lui-même, les champs multiples, les fichiers, images, sons et mots de passe. Tri : par la première date, la plus récente en tête ; sans date, par la clé.
  - Une ligne « Total » suit les objets quand une colonne est un `(nombre)` : la somme exacte de chacune. Un `(nombre entier)` n'est pas additionné : une année, un numéro n'ont pas de somme qui ait un sens.
  - Un double-clic sur une ligne ouvre la fiche de l'objet par-dessus ; à son retour, les tableaux se relisent. La ligne « Total » *(2.12)* se montre en gras, ne se sélectionne pas, ne mène nulle part et reste la dernière quel que soit le tri choisi à l'écran ; l'interface la reconnaît à sa première cellule, qui commence par U+001C (`src/interface.h`). « Nouvelle séance » ouvre le formulaire de l'entité, le lien vers l'objet déjà rempli, puis conserve la séance.
  - Deux liens de la même entité vers l'objet font deux tableaux, nommés par le champ : « Parrainages (parrain) », « Parrainages (filleul) ». Un lien hérité n'en fait qu'un, celui de l'entité qui le déclare.
  - Une entité `, hors des fiches,` (§ 16.1) n'a pas de tableau : des notes confidentielles ne s'affichent pas dans la fiche d'un client qu'on ouvre devant lui. Elles restent dans la fiche de l'objet qui les porte, et dans tout écran qui les cherche.
  - Pas encore : le tableau d'un champ multiple (« les œuvres d'un genre »).
- `grym lancer` refuse un programme qui ouvre des écrans, avant toute exécution : la console n'en montre pas. `grym servir` les montre dans le navigateur (`docs/web.md`, § 3 et § 5).

### 22.4 Mots et forme compacte

- `quand`, `ouvrir` et `fermer` sont des mots de construction (§ 10.7) : ils ne commencent plus le nom d'une action.
- Forme compacte : `docs/ecrans.md`, § 3.7 (`_écran des_compositeurs` … `_fin`, `_quand _clique « Nouveau » _dans des_compositeurs`, `_ouvrir`, `_fermer`, `_écran.pays`).

### 22.5 Apparence *(T3 : implémenté le 26 septembre 2026 ; thème libre, 2.13 : le 18 octobre 2026)*

```
Les écrans ont la couleur verte et le logo « logo.png ».
Les écrans ont la couleur grise.
Les écrans ont le logo « images/logo.png ».
Les écrans ont la couleur « #46433B », le fond « #E8E2D6 », la police « polices/Outfit.ttf » et le logo « logo.svg ».
```

- GrymoiR fixe les polices par défaut, les tailles, les espacements et les formes des écrans (`docs/ecrans.md`, § 5). Le programme règle cinq choses, chacune facultative, au moins une, dans cet ordre : la couleur d'accent, le fond, la police, le logo, la signature. Des virgules entre elles, `et` devant la dernière : « Le dernier réglage se relie par « et » … ». Un autre ordre : « Les réglages s'écrivent dans cet ordre : la couleur, le fond, la police, le logo … ».
- **La couleur** : nommée, `bleue` (par défaut, sans phrase), `verte`, `turquoise`, `violette`, `grise`, accordée (« Accord : « verte » (la couleur). ») ; ou libre *(2.13)*, entre guillemets, `#` et six chiffres hexadécimaux : `« #46433B »`. Mal écrite : « Couleur « 46433B » mal écrite : # et six chiffres hexadécimaux, « #46433B ». » Toute autre : « Couleur attendue : bleue, verte, turquoise, violette, grise, ou une couleur libre entre guillemets, « #46433B ». »
- **Le fond** *(2.13)* : `le fond « #E8E2D6 »`, un fond clair. Avec lui, les écrans restent clairs quel que soit le réglage du système : une image de marque se dessine dans un seul mode. Sans lui, le clair ou le sombre suit le système.
- **Les couleurs dérivées** *(2.13)* : d'une couleur libre et d'un fond, GrymoiR calcule les autres jetons du thème (`src/teinte.c`) : surfaces, séparateurs, survol et enfoncé des boutons, teinte douce, texte des boutons (blanc, ou sombre si le blanc ne se lit pas), et, sans fond, une variante éclaircie pour le mode sombre. Le même calcul sert l'analyse, l'atelier et le navigateur.
- **Les contrastes** *(2.13)*, vérifiés dès l'analyse, au seuil du WCAG 2.1 : 4,5:1 pour la couleur sur le fond, le texte des boutons sur la couleur, le texte sur la teinte douce, le texte secondaire sur les surfaces ; 3:1 pour le bord des zones. Sous le seuil, refus, le rapport mesuré arrondi vers le bas : « La couleur « #E93A69 » sur le fond « #F5EFE6 » : contraste de 3,4:1, il faut au moins 4,5:1 (WCAG 2.1). » Une couleur qui ne se lit pas ne sert qu'aux images, jamais à l'accent.
- **La signature** *(2.16)* : `et la signature « images/signature.png »`, le dernier réglage : une image (PNG, JPEG, GIF, WebP ou SVG) posée en bandeau en tête de chaque écran, sur 26 pixels de haut, un filet dessous *(2.52 : une image plus carrée qu'un nom écrit monte jusqu'à 125 pixels, pour garder environ 208 pixels de large)*, comme l'en-tête d'un site. Avec elle, le logo ne se pose plus près du titre : il reste l'icône de la fenêtre. `grym servir` la sert à `/signature`. Forme compacte : `_signature « … »`.
- **La police** *(2.13)* : un fichier TrueType ou OpenType (`.ttf`, `.otf`), relatif au dossier du programme, reconnu à son contenu dès l'analyse : « « notes.txt » n'est pas une police (TrueType ou OpenType : .ttf, .otf). » Elle remplace celle du thème pour tout le texte des écrans ; le code de l'atelier garde la sienne. `grym servir` la sert à `/police`.
- Le logo est un chemin écrit tel quel, relatif au dossier du programme. Il doit exister et être une image (PNG, JPEG, GIF, WebP, ou SVG *(2.13)*, reconnu à son texte `<svg`) dès l'analyse, pour que l'aperçu de l'atelier le montre sans exécuter le programme : « Fichier « logo.png » introuvable ou illisible. », « « notes.txt » n'est pas une image (PNG, JPEG, GIF, WebP ou SVG). »
- Forme compacte : `_écrans _couleur _verte _logo « logo.png »`, `_écrans _couleur « #46433B » _fond « #E8E2D6 » _police « … » _logo « … »`, un réglage au moins, dans l'ordre.
- Une seule fois, au premier niveau, avant le premier `Ouvrir` ; sinon, erreur d'analyse. Comme `Les nombres s'affichent …` (§ 4.1), la phrase vaut pour tout le programme.
- Le logo s'affiche à gauche du titre : grand (64 pixels) sur un écran que le programme ouvre lui-même, sans écran dessous ; petit (24 pixels) sur un écran ouvert par-dessus. Il sert aussi d'icône à la fenêtre.
- Sans écran (`grym lancer`, ou un programme qui n'en ouvre aucun), la phrase est sans effet : un programme qui ne fait qu'afficher tourne normalement. `grym servir` applique la couleur et le logo à ses écrans.
- Aide à la saisie (§ 8) : en début de phrase, `Les écrans ont` et `Les nombres s'affichent` sont proposés ; puis `la couleur`, `le fond`, `la police` ou `le logo` (les réglages qui restent possibles, dans l'ordre), puis `et le fond`, `et la police`, `et le logo`, les cinq couleurs, `et le logo`, un texte. Après `Les nombres s'affichent` : `à la suisse`, `à la française`, `sans séparateur`.
- La taille du titre d'un écran *(1.94 ; 2.17 : en grand, 40 px en gras, en petit, 15 px, mesurés sur le titre du thème et non plus sur la police de l'application, qui donnait 22 px)* : `L'écran de recherche, « Recherche » en grand, montre :`, ou sans titre donné, `L'écran de recherche, en petit, montre :`. `en petit` et `en grand` viennent du thème et valent pour tous les écrans ; sans eux, la taille du thème. Forme compacte : `_écran de_recherche « Recherche » _en_grand`.

- **Calculés** *(2.33)* : `le logo (…)` et `la signature (…)` prennent une expression entre parenthèses, lue à l'exécution, à cette phrase : une image (un champ `(image)` d'une entité, par exemple) ou rien (absente, pas de logo). `Les écrans ont la couleur « #46433B », le logo (le logo du cabinet conservé) et la signature (la signature du cabinet conservé).` La phrase se place alors après ce qui crée l'objet, toujours avant le premier `Ouvrir` ; un changement de l'image se voit au lancement suivant. L'aperçu de l'atelier, qui n'exécute rien, ne montre pas une image calculée. Forme compacte : `_logo ((_le cabinet _conservé).logo)`.
- **Le titre calculé** *(2.33)* : à la place du titre entre guillemets, une expression entre parenthèses, relue avant chaque attente comme le texte calculé (§ 22.1) : `L'écran d'accueil, (\"Bonjour \" suivi du prénom du cabinet conservé) en grand, montre :`. Il donne aussi son titre à la fenêtre. Forme compacte : `_écran d'accueil _titre (…) _en_grand`.

### 22.6 États des éléments *(implémenté le 14 octobre 2026)*

```
L'écran de saisie montre :
    un nom (texte), facultatif,
    une remarque (texte), facultative, sur plusieurs lignes,
    un total (nombre), 0 au départ, en lecture seule,
    le texte « Nom manquant », visible si le nom de l'écran est absent,
    un bouton « Valider », actif si le nom de l'écran est présent,
    un bouton « Fermer ».
```

- `sur plusieurs lignes` : une zone `(texte)` seulement ; Entrée y va à la ligne, et la zone se valide quand on la quitte (`Quand on change …`). Sur un autre type : « Seule une zone (texte) s'écrit sur plusieurs lignes. »
- `en lecture seule` : la zone se montre, se sélectionne, ne se modifie que par le programme (`Le total de l'écran devient 3.`). Un changement venu de l'interface est ignoré par la machine, et `Quand on change` ne se déclenche pas.
- `à côté du titre` *(2.17)* : un bouton sur la ligne du titre, à droite, comme l'action d'une page (« Nouvelle séance »). Pas une tuile : « Une tuile ne se met pas à côté du titre. ». Forme compacte : `_côté_titre`.
- `en tuile` *(2.15)* : un bouton en grand, comme un accueil d'application : son titre en tête, `avec la description « … »` dessous, `avec l'image « coeur.png »` en haut à droite (PNG, JPEG, GIF, WebP ou SVG, vérifiée dès l'analyse). Des tuiles qui se suivent forment une grille de quatre colonnes ; un autre élément l'arrête. Une description ou une image hors d'une tuile : « Une description ou une image va sur une tuile : … ». Une tuile principale prend la couleur d'accent. Ordre canonique : `principal`, `en tuile`, la description, l'image, puis `actif si`, `visible si`. Forme compacte : `_tuile`, `_description « … »`, `_image « … »`.
- `à confirmer par « Supprimer ce patient ? »` *(2.65)* : un bouton qui demande avant d'agir. Le clic ouvre une fenêtre avec la question, **Annuler** (le choix par défaut : Entrée annule) et un bouton au libellé du bouton ; l'événement ne part que si l'on confirme. Suivi de `en tapant « effacer »`, pour ce qui ne revient pas : la fenêtre dit que rien ne se rattrapera et le bouton reste inactif tant que le mot n'est pas tapé (espaces autour et majuscules tolérés). Dans le navigateur, sans JavaScript, la question s'affiche en haut de l'écran et le serveur vérifie la confirmation et le mot. Une confirmation par bouton : « La confirmation est déjà donnée. ». Ordre canonique : après l'image, avant `actif si`. Forme compacte : `_confirmer « … »`, `_en_tapant « … »`.
- `à confirmer par « Fermer sans enregistrer ? » si l'écran est modifié` *(2.67)* : la question ne se pose qu'après une saisie que rien n'a encore écrite dans la base. L'écran est modifié dès qu'une zone reçoit une valeur de l'utilisateur (une valeur que le programme y écrit ne compte pas) ; il ne l'est plus quand un de ses événements a écrit dans la base, à la validation de sa transaction (« Enregistrer »). Un événement qui n'écrit rien (« Voir le patient », un calcul) laisse la marque. Sans saisie en attente, le clic part sans question. L'interface reçoit la marque avec les états des éléments (`ETAT_MODIFIE`, bit 4, `src/interface.h`) et compte aussi une zone tapée qu'elle n'a pas encore envoyée ; dans le navigateur, une saisie qui arrive avec le clic pose la question. Se combine avec `en tapant « … »`, qu'elle suit. Forme compacte : `_si_modifié`.
- `au clavier « N »` *(2.68)* : le raccourci d'un bouton, Cmd+N sous macOS, Ctrl+N sous Windows et Linux, qui fait le même clic que la souris, question de confirmation comprise. Il ne part que si l'écran est dessus et le bouton visible et actif ; la zone quittée envoie sa saisie avant, comme sous la souris. Une lettre ou un chiffre, écrit en majuscule par `grym formater` ; A, C, H, J, M, Q, V, W, X et Z restent au système et à l'édition (« « C » sert déjà au système ou à l'édition… ») ; deux boutons d'un même écran n'ont pas le même (« « au clavier « N » » est déjà le raccourci de « … » dans cet écran. »). Une infobulle le montre. Dans le navigateur, c'est la touche d'accès du bouton (`accesskey`), que chaque navigateur combine à sa façon (Ctrl+Option+N dans Safari et Chrome sous macOS, Alt+N sous Windows) : Cmd+N y reste au navigateur. Forme compacte : `_clavier « N »`.
- `principal` *(2.14)* : un bouton, celui de l'action attendue (« Nouvelle séance », « Enregistrer ») : il prend la couleur d'accent, le texte des boutons dessus (§ 22.5). Un par écran suffit ; GrymoiR n'en impose pas le nombre. Ailleurs : « Seul un bouton est principal : … » ; « principale » : « Accord : « principal » (le bouton). ». Il s'écrit le premier de ses états : `un bouton « Enregistrer », principal, actif si …`. Forme compacte : `_principal`.
- `actif si …` : un bouton ou une zone ; faux, l'élément est grisé et un geste dessus ne compte pas. L'adjectif s'accorde avec la zone : `une remarque …, active si …` (« Accord : « active si ». »). Un texte fixe ne se clique pas : « visible si » seulement.
- `visible si …` : un bouton, un texte fixe ou une zone ; faux, l'élément disparaît, libellé compris. Une liste reste toujours visible, pour l'instant.
- La condition est une valeur vraie ou fausse (§ 5.3) : « « visible si » attend une condition : … ». Elle se lit à la fin de la déclaration et peut donc citer toute zone ou liste de l'écran, même écrite plus bas, par `de l'écran`. La machine la relit avant chaque attente, après la relecture des listes : après l'ouverture, après chaque événement, après chaque zone validée.
- Une zone vide est absente pour ce qui décrit l'écran : une condition, le `dont` d'une liste. `actif si le nom de l'écran est présent` vaut faux tant que rien n'est tapé, que la zone soit obligatoire ou non. L'obligation se vérifie quand on agit : un événement qui lit une zone obligatoire vide s'arrête, sans rien écrire, et l'écran montre « Remplis « Nom » : cette zone est obligatoire. ». C'est la pratique des formulaires HTML (`required`, vérifié à l'envoi) et d'Access (`Required`, vérifié à l'enregistrement).
- *(2.12)* Un événement qui teste lui-même la présence d'une zone (`Si le soin de l'écran est présent, …`, `est absent`) s'en charge : elle n'est pas exigée en tête de l'événement, et le test la lit vide sans s'arrêter. La lire vide ailleurs, sans test, arrête toujours l'événement avec « Remplis … ». Ainsi le prix d'une séance se propose dès que le client et le soin sont choisis, dans l'ordre qu'on veut, sans que ces zones perdent leur astérisque.
- Une case à cocher obligatoire sans valeur de départ vaut faux à l'ouverture : elle montre « non », elle vaut non. Les interfaces marquent d'un astérisque les autres zones obligatoires.
- Une condition qui échoue autrement à l'exécution arrête le programme, comme le `dont` d'une liste (§ 22.1).
- Ordre canonique (l'imprimeur le rétablit) : valeur de départ, `facultatif`, `sur plusieurs lignes`, `en lecture seule`, `actif si`, `visible si`. Chaque état une fois : « « actif si » est déjà dit pour cet élément. »
- Forme compacte : `_plusieurs_lignes`, `_lecture_seule`, `_actif_si …`, `_visible_si …` ; la condition va jusqu'au mot suivant de cette liste ou à la fin de la ligne.
- Aucun mot réservé : `sur`, `lignes`, `lecture`, `seule`, `actif`, `visible` ne comptent qu'ainsi placés, après la virgule d'un élément.
- `, en interrupteur` *(1.92)* : une zone `(vrai ou faux)` se montre en interrupteur plutôt qu'en case à cocher, `un actif (vrai ou faux), en interrupteur`. La valeur et les événements sont ceux d'une case ; seule la présentation change. Une autre zone le refuse : « Seule une zone (vrai ou faux) se montre en interrupteur. » Forme compacte : `_interrupteur`.
- `, avec l'indication « … »` *(1.95)* : une phrase sous le libellé d'une zone, qui dit quoi y écrire, `un code postal (texte), avec l'indication « 4 chiffres, sans espace »`. Toute zone la prend, cases à cocher, interrupteurs et choix multiples compris ; une seule par zone (« L'indication est déjà donnée. »). Forme compacte : `_indication « … »`.
- `, avec recherche` *(1.96)* : une zone qui désigne un objet conservé devient un champ où l'on tape, avec les objets qui contiennent la saisie pour propositions, `un membre (membre), avec recherche`. Au-delà de vingt objets, elle le devient d'elle-même, sans qu'on l'écrive ; vingt est un choix du projet, pas une norme. Une saisie qui ne désigne aucun objet est refusée. Une autre zone le refuse : « Seule une zone qui désigne un objet conservé se cherche ». *(2.40)* Les propositions ne tiennent compte ni des majuscules ni des accents, comme `contient` : « helene » propose « Hélène Rochat », et le choix remet le nom tel qu'il est écrit. Forme compacte : `_recherche`.
- **Des choix filtrés** *(2.48)* : `un patient (patient dont l'archivage est absent), avec recherche` ne propose que les objets qui remplissent la condition, écrite comme celle d'une recherche (§ 16.4). Elle s'évalue à l'ouverture de l'écran ; une saisie tapée garde le droit de désigner un autre objet, et l'événement qui la reçoit décide. Seule une zone qui désigne une entité conservée se filtre. Forme compacte : `(patient _dont archivage _absent)`.
- Les bornes d'une zone *(1.97)*, comme les intervalles des boucles (§ 9) : `, de 1 à 99`, `, au moins 16`, `, au plus 50` pour un nombre ; les mêmes suivies de `caractères` pour un texte, et `, de 4 caractères` pour une longueur exacte. Bornes comprises. Une saisie hors bornes est refusée avec la règle (« « Quantité » va de 1 à 99. », « « Code » a 4 caractères. »). Une zone d'un autre type n'en a pas. Forme compacte : `_min 1 _max 99`.
- `, large de 12 caractères` *(2.34)* : une zone de texte (dans l'atelier, aussi une liste de choix ou d'objets) à la largeur de N caractères moyens de la police, hors d'un bloc en position libre (où `large de N` compte des pixels) ; de 1 à 200. Le mot de passe d'une connexion, un code postal : `un code (texte), masqué, large de 12 caractères`. Dans le navigateur, `size` et une largeur en `ch`. Forme compacte : `_largeur_caractères 12`.
- `, avec le choix d'un dossier` *(2.32)* : une zone (texte) au bout de laquelle une icône ouvre le sélecteur de dossiers du système ; le chemin choisi la remplit, et la zone change comme après une frappe. Dans le navigateur, il se tape. `un dossier de sauvegarde (texte), facultatif, avec le choix d'un dossier`. Autre zone : « Seule une zone (texte) se remplit par le choix d'un dossier. ». Forme compacte : `_choix_dossier`.
- `, masqué` *(1.98)*, accordé (`masquée`) : une zone de texte sur une ligne dont la frappe est cachée, un mot de passe, `un code d'accès (texte), masqué`. La valeur ne s'affiche jamais, même au retour de l'écran ; un champ masqué laissé vide ne change pas la zone. Ce n'est qu'une affaire d'affichage : conservée, la valeur l'est en clair, GrymoiR ne chiffre pas encore les mots de passe. Forme compacte : `_masqué`.
- `, avec l'unité « … »` *(2.0)*, et `devant` pour la placer avant le champ : `une remise (nombre), avec l'unité « % »`, `un montant (nombre), avec l'unité « CHF » devant`. Elle s'affiche près du champ et n'entre pas dans la valeur ; tapée avec elle (« 12 CHF »), elle est ôtée. Une zone de nombre ou de texte seulement. Forme compacte : `_unité « CHF » _devant`.
- Les zones en faute *(2.1)* : un événement vérifie d'emblée toutes les zones obligatoires qu'il lit, et s'arrête s'il en trouve de vides, en les signalant toutes à la fois (« Remplis les zones obligatoires : « Nom » et « Ville ». »). Un événement qui n'en lit aucune, comme « Fermer », n'est jamais bloqué. L'écran montre un résumé en haut (« Il y a 2 problèmes : », chaque faute un lien vers sa zone dans le navigateur) et, sous le libellé de chaque zone en faute, son propre message ; une saisie refusée à son changement (bornes, forme d'un courriel) s'y montre de même. Les messages tiennent jusqu'au geste suivant.

### 22.7 Groupes et valeurs permises *(implémenté le 14 octobre 2026)*

```
L'écran de saisie montre :
    le groupe « Statut » :
        un état (« actif », « suspendu » ou « retiré »), « actif » au départ,
        une remarque (texte), facultative,
    un bouton « OK ».
```

- `, en couleur inversée` *(2.18)*, après le titre (et la place, s'il y en a une) : `le groupe « Prix proposé », en couleur inversée :`. Le panneau prend la couleur d'accent, son titre et ses textes celle du texte des boutons ; ses zones perdent leur cadre pour un trait dessous, en grands caractères. Pour la valeur que l'écran met en avant, comme un prix. Accord : « Accord : « inversée » (la couleur). ». Forme compacte : `_groupe « Prix » _inversé`.
- `le groupe « Adresse » :` ouvre un bloc, comme `l'un sous l'autre :` (§ 22.1), que l'interface encadre et titre. Le titre est obligatoire : « Un groupe porte un titre : « le groupe « Adresse » : ». ». Un groupe contient au moins un élément, plus indenté ; les groupes s'emboîtent avec les autres blocs.
- Une zone accepte des valeurs permises, comme un champ d'entité (§ 16.15) : `un état (« actif », « suspendu » ou « retiré »)`. La zone est de type texte ; la valeur de départ en est une (« « c » n'est pas une valeur permise de « état » : « a » ou « b ». ») ; `L'état de l'écran devient « z ».` est refusé dès l'analyse, et un choix hors liste venu de l'interface est refusé à l'exécution, l'écran restant ouvert avec le message. Une zone à valeurs permises ne s'écrit pas `sur plusieurs lignes`.
- De deux à quatre valeurs, l'interface montre des boutons radio, tous visibles ; une zone facultative en reçoit un de plus, « — », pour l'absence. Au-delà de quatre, un menu. Le choix est celui de l'interface, sans phrase : on décrit ce que la zone accepte, pas son dessin (§ 3.3 de `docs/ecrans.md`).
- Forme compacte : `_groupe « Adresse »` … `_fin` ; `_un état (« actif » ; « suspendu » ; « retiré »)`.

### 22.8 Images *(implémenté le 14 octobre 2026)*

```
L'écran d'accueil montre :
    l'image « plan.png »,
    l'image « images/alerte.png », visible si le retard de l'écran est présent,
    un bouton « Fermer ».
```

- `l'image « … »` montre un fichier image (PNG, JPEG, GIF ou WebP), à sa taille, réduite s'il le faut pour tenir dans 480 × 320 pixels (la largeur de la page dans le navigateur). Le chemin est relatif au dossier du programme, comme le logo (§ 22.5).
- Le fichier existe et c'est une image dès l'analyse, pour que l'aperçu de l'atelier le montre : « Fichier « rien.png » introuvable ou illisible. », « « notes.txt » n'est pas une image (PNG, JPEG, GIF ou WebP). ». Il est relu à chaque ouverture de l'écran ; disparu depuis, l'écran ne s'ouvre pas, avec le même message.
- `visible si …` s'y applique (§ 22.6) ; une image ne se clique pas : « Une image ne se clique pas : « visible si » seulement. ».
- Dans le navigateur, l'image est servie à part (`/ecran-image/…`), la politique de sécurité de la page n'admettant que ses propres adresses.
- Forme compacte : `_image « plan.png »`.
- **Image calculée** *(implémenté le 14 octobre 2026)* : `l'image de …` montre une valeur au lieu d'un fichier écrit, le plus souvent le champ image de la ligne choisie d'une liste :

  ```
  L'écran des compositeurs montre :
      la liste des compositeurs conservés, par nom,
      l'image du portrait du compositeur choisi de l'écran, visible si le compositeur choisi de l'écran est présent,
      un bouton « Fermer ».
  ```

  - `de` se contracte comme ailleurs (§ 5.2) : `l'image du portrait …`, `l'image de la photo …`, `l'image d'une …` ; « de le » est refusé.
  - La valeur se lit à la fin de la déclaration, comme une condition (§ 22.6) : elle peut citer toute zone ou liste de l'écran, même écrite plus bas. La machine la relit avant chaque attente : choisir une autre ligne change l'image.
  - Absente (aucune ligne choisie, champ vide), l'image ne montre rien, et sa place reste. Dans cette valeur seulement, le champ d'un objet absent vaut absent au lieu d'arrêter le programme : `le portrait du compositeur choisi de l'écran` ne demande pas `est présent`, puisque l'écran s'ouvre sans ligne choisie. C'est la règle du § 22.6 pour ce qui décrit l'écran (une zone vide y est absente), étendue à l'image. Une valeur dont le type se connaît à l'analyse et n'est pas une image est refusée : « « l'image de » attend une image : « l'image de la photo du compositeur choisi de l'écran ». ». Sinon, une valeur qui n'est pas une image (PNG, JPEG, GIF ou WebP) arrête le programme à l'exécution, avec le même message.
  - Dans le navigateur, l'adresse de l'image porte un numéro de version, qui change avec ses octets : le navigateur ne montre pas l'ancienne.
  - Forme compacte : `_image_de écran.compositeur_choisi.portrait`.

### 22.9 Position libre *(implémenté le 14 octobre 2026)*

```
L'écran de saisie montre :
    en position libre :
        le texte « Nom », à 16 de la gauche et 22 du haut,
        un nom (texte), à 96 de la gauche et 16 du haut, large de 240,
        l'image « plan.png », à 360 de la gauche et 16 du haut, large de 120, haute de 80,
        un bouton « Valider », à 96 de la gauche et 72 du haut,
    un bouton « Fermer ».
```

- `en position libre :` ouvre un bloc, comme `côte à côte :` (§ 22.1), dans lequel chaque élément se place au pixel. Il se met où se met un bloc, et s'emboîte dans les autres ; il contient au moins un élément.
- Un bloc s'y place comme un élément *(1.81)* : sa place et sa taille s'écrivent entre son nom et ses deux-points, `le groupe « Adresse », à 16 de la gauche et 8 du haut, large de 320 :`, `côte à côte, à 0 de la gauche et 200 du haut :`, `en position libre, à 360 de la gauche et 0 du haut :`. À l'intérieur, le bloc garde sa règle : un groupe ou `l'un sous l'autre` empile ses éléments, `côte à côte` les range en ligne, un bloc libre les place au pixel, relativement à lui. Un groupe qui contient `en position libre :` place donc ses zones au pixel dans son cadre.
- Chaque élément du bloc dit sa place : `, à 96 de la gauche et 16 du haut`, en pixels depuis le coin en haut à gauche du bloc. Obligatoire dans le bloc (« Un élément en position libre dit sa place : « , à 16 de la gauche et 8 du haut ». »), refusée ailleurs (« « à … de la gauche » ne vaut que dans un bloc « en position libre : ». »).
- La taille est facultative : `, large de 240`, `, haut de 80`. L'adjectif s'accorde avec l'élément : `haute` pour une image, une liste ou une zone féminine (« Accord : « haute de ». »). Sans taille, l'élément a sa taille naturelle, celle qu'il aurait hors du bloc. Refusée hors d'un bloc en position libre.
- Des nombres entiers : de 0 à 4000 pour la place, de 8 à 4000 pour la taille (« La place va de 0 à 4000 pixels. », « La taille va de 8 à 4000 pixels. »). Le pixel est celui des interfaces, le même sur un écran ordinaire et sur un écran à haute densité.
- Le bloc mesure ce que ses éléments occupent ; les éléments peuvent se chevaucher, et le dernier écrit passe dessus. Des boutons qui se suivent ne se rangent plus en ligne : chacun est à sa place.
- L'ordre écrit reste l'ordre de lecture : celui de la touche Tab et des lecteurs d'écran. L'atelier écrit les éléments du bloc de haut en bas, puis de gauche à droite.
- Dans le navigateur, une fenêtre plus étroite que le bloc empile ses éléments dans l'ordre écrit : c'est le seul écart avec les places écrites, pour qu'un téléphone reste utilisable (`docs/web.md`).
- Ordre canonique (l'imprimeur le rétablit) : valeur de départ, `facultatif`, `sur plusieurs lignes`, `en lecture seule`, la place, `large de`, `haut de`, `actif si`, `visible si`. Une liste met sa place après `, par …` et `, avec …`. Un bloc la met avant ses deux-points.
- Une zone `sur plusieurs lignes` dont la hauteur est dite la prend entière : elle n'est plus limitée à huit lignes.
- Forme compacte : `_position_libre` … `_fin` ; `_place 96 16`, `_largeur 240`, `_hauteur 80` (l'accord se fait en forme littéraire) ; un bloc placé : `_groupe « Adresse » _place 16 8 _largeur 320`.
- Aucun mot réservé : `à`, `gauche`, `haut`, `large` ne comptent qu'ainsi placés, après la virgule d'un élément.
- Ce paragraphe révise `docs/ecrans.md`, § 3.3 (« jamais des pixels ») : la disposition décrite reste la règle hors du bloc.

### 22.10 Onglets *(implémenté le 14 octobre 2026)*

```
L'écran du membre montre :
    les onglets :
        l'onglet « Identité » :
            un nom (texte),
            un prénom (texte),
        l'onglet « Cotisations » :
            la liste des cotisations conservées,
    un bouton « Fermer ».
```

- `les onglets :` ouvre un bloc qui ne contient que des onglets, au moins un : « Dans « les onglets : », seulement des « l'onglet « … » : ». ». Un `l'onglet « … » :` ne se met que là : « « l'onglet » se met dans « les onglets : ». ». Un onglet porte un titre, non vide.
- Un onglet empile ses éléments, comme un groupe (§ 22.7) ; il peut contenir tout bloc, `en position libre :` compris (§ 22.9).
- `les onglets` est un bloc comme les autres : dans un bloc libre, il dit sa place et sa taille avant ses deux-points (`les onglets, à 16 de la gauche et 8 du haut, large de 480 :`). Un onglet, lui, n'a pas de place : il occupe le cadre de ses onglets.
- Le premier onglet est montré à l'ouverture. Choisir un onglet ne déclenche aucun événement ; l'écran retient le choix tant qu'il reste ouvert. Tous les éléments de tous les onglets existent : une zone d'un onglet caché garde sa valeur, et un événement la lit.
- Forme compacte : `_onglets` … `_fin` ; `_onglet « Identité »` … `_fin`.

### 22.11 Graphiques *(implémenté le 15 octobre 2026)*

```
L'écran de bord montre :
    le graphique des tâches conservées, par état,
    le graphique des tâches conservées dont le projet est le projet choisi de l'écran, par responsable, en secteurs,
    le graphique des cotisations conservées, par catégorie du membre, avec la somme du montant.
```

- Un graphique est une liste regroupée : même entête (`le graphique des … conservés`), même `dont` (§ 22.1), et des chemins de champs comme les colonnes d'une liste.
- `par …` est obligatoire (« Un graphique dit son regroupement : « le graphique des tâches conservées, par état ». ») : une barre, ou un secteur, par valeur du chemin. Les groupes suivent l'ordre des objets dans la base, celui de leur création ; un chemin vide forme le groupe « — ». La clé s'écrit sans article, comme on dit « par état ».
- Par défaut, on compte les objets de chaque groupe ; `avec la somme de …` additionne un champ nombre (`du` et `de la` se contractent comme ailleurs, § 5.2). Un champ additionné qui n'est pas un nombre arrête le programme.
- En barres par défaut ; `en secteurs` dessine un disque et sa légende. `en barres` s'écrit aussi, et l'imprimeur l'omet.
- Il se recalcule à chaque tour, comme une liste. Il ne se choisit pas : ni « Quand on choisit », ni « … choisi de l'écran ».
- Il se place, s'agrandit, se met dans un onglet comme tout élément (§ 22.9, § 22.10).
- Forme compacte : `_graphique tâche _conservé [_dont …] _par état [_somme montant] [_secteurs]`.
- Ordre canonique : `dont`, `par`, `avec la somme de`, `en secteurs`, puis la place.
- Par période *(2.4)* : `, par mois de l'échéance`, `par semaine de …` (semaine ISO 8601, celle de son jeudi), `par année de …`. Les barres suivent l'ordre du calendrier, pas celui de la base, et se lisent « octobre 2026 », « semaine 42, 2026 », « 2026 » ; les dates absentes viennent à la fin. Forme compacte : `_par _mois échéance`.

### 22.12 Menus *(implémenté le 15 octobre 2026)*

```
L'écran d'accueil montre :
    le menu « Membres » :
        un bouton « Nouveau membre »,
        un bouton « Liste des membres »,
    le menu « Comptes » :
        un bouton « Noter une écriture »,
    la liste des membres conservés, par nom.
```

- Un menu appartient à son écran, comme tout élément. Il s'écrit au premier niveau de l'écran (« Un menu s'écrit au premier niveau de l'écran, pas dans un bloc. ») et ne contient que des boutons (« Un menu ne contient que des boutons. »), au moins un.
- Un élément de menu est un bouton : il a son événement (`Quand on clique sur « Nouveau membre » dans l'écran d'accueil :`), un libellé unique dans l'écran, `actif si` et `visible si`.
- Où qu'il soit écrit, le menu se dessine en tête de l'écran : une barre de menus dans l'application, une barre de navigation dans le navigateur, sous le titre de l'écran, où chaque menu s'ouvre sans script.
- Un menu fait partie du cadre de l'écran : il ne se place pas au pixel et ne s'agrandit pas (§ 22.9) ; ses boutons non plus.
- Forme compacte : `_menu « Membres »` … `_fin`.

### 22.13 Courriel, téléphone, adresse web *(implémenté le 16 octobre 2026)*

`(courriel)`, `(téléphone)` et `(adresse web)` sont des types, partout où l'on écrit un type : un champ d'entité, une zone d'écran.

```
Un contact, conservé, a :
    un courriel (courriel), unique,
    un portable (téléphone), facultatif,
    un site (adresse web), facultatif.
```

- La valeur est un texte : elle se range, se compare, se cherche (`dont le courriel contient « tilleul »`) et s'affiche comme un texte, et un texte s'y affecte. Un courriel unique peut servir de clé.
- Sa forme se vérifie quand elle entre dans un champ ou une zone, simplement, pour attraper les fautes de frappe : un courriel a un seul `@`, quelque chose avant, un point après, pas d'espace (« « anne@ » n'est pas un courriel (nom@domaine.ch). ») ; un téléphone, des chiffres (au moins 6), des espaces, `+`, `(`, `)`, `-`, `.`, `/` ; une adresse web commence par `http://` ou `https://`. La norme des courriels (RFC 5322) admet bien plus : le projet ne prétend pas la suivre.
- Dans une zone, l'interface propose le clavier qui convient.
- Dans une fiche *(2.8)*, affichée ou déduite, un courriel est un lien d'écriture (`mailto:`, le logiciel de courriel du système), une adresse web un lien qui s'ouvre dans le navigateur, seulement si elle commence par `http://` ou `https://`. Un téléphone reste un texte. Dans une liste, ni l'un ni l'autre : un clic sur la ligne la choisit.
- Forme compacte : `(courriel)`, `(téléphone)`, `(adresse_web)`.

### 22.14 Mots de passe *(implémenté le 16 octobre 2026)*

`(mot de passe)` est un type, pour un champ d'entité comme pour une zone d'écran.

```
Un membre, conservé, a :
    un nom (texte), unique,
    un code (mot de passe).

Quand on clique sur « Entrer » dans l'écran de connexion :
    Pour chaque membre conservé dont le nom est le nom de l'écran :
        Si le code de l'écran correspond au code du membre, ouvrir l'écran d'accueil.
```

- **Il s'écrit comme un texte** (`Le code de m vaut le code de l'écran.`) et se range en **empreinte**, jamais en clair : PBKDF2-HMAC-SHA256, 600 000 itérations, un sel aléatoire de 16 octets, selon la recommandation de l'OWASP (*Password Storage Cheat Sheet*), qui place Argon2id en tête et admet PBKDF2. L'empreinte s'écrit `pbkdf2-sha256$600000$sel$clé`, l'algorithme en tête, pour en changer un jour sans perdre les anciennes. Une empreinte prend environ 0,7 seconde : c'est voulu, c'est ce qui ralentit un attaquant.
- **Il ne se lit pas** : « « code » est un mot de passe : il ne se lit pas, il se vérifie ». La fiche et les listes le montrent « •••••• » ; la saisie d'une fiche ne le propose pas en valeur actuelle.
- **Sa présence se teste** *(2.31)* : `Si le code du membre est présent` ne lit pas le mot de passe, il dit seulement s'il y en a un (avant la 2.31, ce test était refusé comme une lecture).
- **Il se vérifie** : `x correspond au code du membre` (`à`, `au`, `à la`), vrai si le texte x est le mot de passe. La comparaison prend le même temps que les clés se ressemblent ou non.
- **Une zone `(mot de passe)`** est masquée d'elle-même ; elle garde son texte en mémoire le temps de l'événement, et ne le range nulle part : c'est en l'affectant à un champ d'entité qu'il devient une empreinte.
- Forme compacte : `x _correspond membre.code`.

---

### 22.15 Documents *(2.25, implémenté le 19 octobre 2026)*

```
Le document de reçu, « Reçu », montre :
    côte à côte :
        l'image « images/coeur-encre.png », large de 40,
        le texte "Pour " suivi du nom de la personne,
    le texte « Visites », en petites capitales,
    la liste des visites conservées dont le patient = la personne, avec la date et le prix.

Ouvrir le document de reçu.
Enregistrer le document de reçu dans « recu.pdf ».
L'exemplaire de q devient le document de reçu.
```

- Un document se déclare comme un écran (§ 22.1), avec `Le document … montre :`, pour le papier : textes fixes ou calculés, images, listes, blocs `côte à côte`, `l'un sous l'autre`, groupes. Ni bouton ni zone : « Un document se lit sur papier : des textes, des images et des listes, ni bouton ni zone. ». Une taille d'image se dit partout (`, large de 40`), pas seulement en position libre.
- `le document de reçu` est une valeur, un fichier PDF (A4), calculé à l'instant où on l'écrit : ses textes, ses listes et ses images se lisent alors, une fois. Il s'enregistre avec `Enregistrer … dans « … »` (§ 15.2), se range dans un champ `(fichier)` (l'exemplaire d'une pièce, gardé tel qu'il a été remis), s'affiche avec `Afficher`.
- `Ouvrir le document de reçu.` le montre dans la visionneuse du système (Aperçu sous macOS), d'où l'on imprime.
- La page : le titre, puis les éléments dans l'ordre ; un `côte à côte` est une rangée de colonnes ; une liste, un tableau dont l'en-tête se répète à chaque page ; « page 1 / 2 » en pied. La mise en page vient de l'atelier ou de l'application fabriquée, comme un rapport PDF (§ 15.6) ; en console et dans le navigateur : « Un document se fabrique dans l'atelier ou dans une application fabriquée. ».
- Limites : pas de total sous une liste (un texte calculé le donne, `la somme des prix des séances conservées dont …`) ; la police du document est celle des rapports, pas encore celle des écrans.
- Forme compacte : `_document de_reçu « Reçu »` … `_fin`, `_ouvrir _document de_reçu`, `_document de_reçu` (la valeur).
- **Ouvrir un fichier** *(2.26)* : `Ouvrir le justificatif du frais.`, `Ouvrir l'exemplaire de la quittance.` montre un fichier tel qu'il est rangé, sous son nom d'origine (« ticket.jpg »), dans la visionneuse du système. Autre chose qu'un fichier : « « Ouvrir » montre un fichier. » ; un fichier absent : « Rien à ouvrir : ce fichier est absent. ». En console et dans le navigateur : « Un fichier s'ouvre dans l'atelier ou dans une application fabriquée. ». Forme compacte : `_ouvrir_fichier frais.justificatif`.
- **Ouvrir une adresse web** *(2.41)* : `Ouvrir l'adresse de l'agenda du cabinet conservé.` montre une adresse `http://` ou `https://` (un champ `(adresse web)`, ou un texte) dans le navigateur du système. Un texte qui n'en est pas une : « « Ouvrir » montre une adresse web qui commence par https:// : « onedoc.ch » n'en est pas une. » ; en console et dans le navigateur de `grym` : « Une adresse web s'ouvre depuis l'atelier ou une application fabriquée. ». Autre chose qu'un fichier ou un texte : « « Ouvrir » montre un fichier ou une adresse web. ».

### 22.16 Code QR *(2.27, implémenté le 20 octobre 2026)*

```
Le q vaut le code QR de « https://nicolas-seydoux.ch ».
Le c vaut « SPC » suivi d'un saut de ligne suivi de « 0200 ».
Le paiement vaut le code QR suisse du c.
Afficher 1590 en format technique.                         →  1590
Afficher (l'arrondi de 260 à 0,01) en format technique.    →  260.00
Afficher 20.10.2026 en format technique.                   →  2026-10-20
Afficher « CH52 0483 5012 3456 7100 0 » en format technique.  →  CH5204835012345671000
```

- `le code QR de t` est une image PNG en noir et blanc, le code QR (ISO 18004) du texte `t` écrit comme `Afficher` l'écrit, en UTF-8 ; correction d'erreur M ou mieux, une zone calme de quatre modules. Elle s'affiche, s'enregistre, se range dans un champ `(image)` et se montre dans un écran ou un document (`l'image du q`).
- `le code QR suisse de t` est le Swiss QR Code des QR-factures, selon les *Swiss Implementation Guidelines for the QR-bill* de SIX (version 2.4 du 24.02.2026, § 6) : correction d'erreur M exactement, version 25 au plus (997 caractères), la croix suisse au centre, sans zone calme dans l'image (le document la laisse autour). Dans un document, `, large de 174` lui donne ses 46 mm ; il y est dessiné en traits, net à toute taille. Le texte, ses 31 lignes, est celui du tableau 8 de SIX : c'est au programme de l'écrire (Cabinet, `actions.grym`). La croix suit les proportions du drapeau suisse, pas le fichier officiel de SIX.
- Trop long : « Un code QR suisse contient au plus 997 caractères (SIX, Implementation Guidelines, § 6.2). » ; absent : « Un code QR se fait d'un texte : celui-ci est absent. ».
- `un saut de ligne` est le texte d'un retour à la ligne : il sépare les lignes d'un code QR suisse.
- `x en format technique` écrit une valeur comme une machine l'attend (code QR, XML) : un nombre sans séparateur de milliers, avec le point décimal et ses décimales (`1590.00`) ; une date en `2026-10-20` ; un texte sans ses espaces. Il se lie à la valeur juste avant lui : `(l'arrondi de x à 0,01) en format technique`.
- Forme compacte : `_code_qr t`, `_code_qr_suisse t`, `_saut_de_ligne`, `x _en_format_technique`.
- **Le texte vérifié** *(2.29)* : avant d'en faire une image ou une section paiement, la machine vérifie les lignes d'un code QR suisse (SIX IG 2.4, § 4.2.2) : au moins 31 lignes, « SPC » en tête et « EPD » à la 31ᵉ ; l'IBAN du créancier suisse ou liechtensteinois, 21 caractères sans espaces, sa clé de contrôle juste ; un QR-IBAN (IID de 30000 à 31999) seulement avec une référence QR (`QRR`), et une référence QR seulement avec lui ; *(2.84)* une référence du créancier (`SCOR`) commence par `RF`, deux chiffres de contrôle justes (ISO 11649, reste 1 modulo 97), puis 1 à 21 lettres majuscules ou chiffres ; sans référence (`NON`), la ligne de la référence reste vide ; la monnaie CHF ou EUR ; une adresse structurée (`S`) avec nom, numéro postal, localité et pays de deux lettres, une adresse combinée (`K`) avec nom et deuxième ligne ; le débiteur final entièrement vide ou complet. Chaque refus le dit : « QR-facture : « CH44 3199 … » est un QR-IBAN ; il exige une référence QR (QRR), pas « NON ». Donne l'IBAN ordinaire du compte. ». Une section paiement vide (pas d'IBAN) ne se vérifie pas.
- **`t en IBAN`** *(2.29)* : le texte sans ses espaces (ordinaires ou insécables), en capitales, s'il est un IBAN (ISO 13616) : deux lettres de pays, deux chiffres de clé, de 15 à 34 lettres et chiffres en tout, la clé vérifiée modulo 97. Sinon, une erreur rattrapable qui dit pourquoi : « « CH53 0483 … » n'est pas un IBAN : sa clé de contrôle ne correspond pas (une faute de frappe ?). ». Le pays et la longueur propres à un pays (21 pour la Suisse) restent au programme : `la longueur de (l'IBAN du cabinet en IBAN) = 21`. Écrit `IBAN`, en capitales, par la forme canonique ; compacte : `t _en_IBAN`.
- **La section paiement** *(2.28)* : dans un document, `la section paiement de la charge` met au bas de la dernière page la section paiement d'une QR-facture et son récépissé, 210 × 105 mm, au bord de la page, d'après les 31 lignes du Swiss QR Code qu'est `la charge` : « Récépissé », « Section paiement », le code de 46 mm avec sa croix, « Compte / Payable à », « Référence », « Informations supplémentaires », « Payable par » (ou un cadre à remplir), « Monnaie », « Montant », « Point de dépôt », et les lignes de découpe avec « A détacher avant le versement ». Intitulés et tailles : *Swiss Implementation Guidelines for the QR-bill* de SIX, version 2.4, chapitres 3.4 à 3.7 et annexe C ; les cotes fines ne viennent pas du Style Guide de SIX. Si la dernière page n'a plus ses 105 mm de bas libres, la section prend une page à elle. Une charge vide (pas d'IBAN) : rien. Dans un écran : « La section paiement d'une QR-facture va dans un document, pas dans un écran. ». Forme compacte : `_section_paiement charge`.

### 22.17 Secrets *(2.40, implémenté le 22 octobre 2026)*

`(secret)` est un type, pour un champ d'entité comme pour une zone d'écran : la clé d'un service, que le programme doit pouvoir renvoyer telle quelle.

```
La base est chiffrée.
Un cabinet, conservé, a :
    un identifiant SMS (texte), facultatif,
    une clé SMS (secret), facultative.

La clé SMS du cabinet devient la clé de l'écran.
Si la clé SMS du cabinet est présente, …
```

- **Il s'écrit comme un texte** et se range **tel quel** : à l'inverse d'un mot de passe (§ 22.14), qui devient une empreinte, un secret doit repartir. C'est pourquoi il exige une base chiffrée (§ 16.18) : « « clé SMS » est un secret : il se range dans une base chiffrée. Écris « La base est chiffrée. » dans le programme. », à l'écriture d'un objet conservé comme à `Conserver`. Un objet qui n'est pas conservé le garde en mémoire, le temps de l'exécution.
- **Il ne se lit que pour être envoyé** : « « clé SMS » est un secret : il ne se lit que pour être envoyé. ». Seul un envoi le lit (§ 22.18), dans ses valeurs. `correspond à` ne s'y applique pas : il se range en clair, il n'a pas d'empreinte.
- **Sa présence se teste**, comme celle d'un mot de passe : `Si la clé SMS du cabinet est présente`.
- **Il ne se montre jamais** : « •••••• » dans les fiches et les listes, une cellule vide dans un CSV, absent des rapports et du formulaire (§ 19). Une zone `(secret)` est masquée d'elle-même ; elle se lit dans son événement, pour être rangée dans un champ.
- Forme compacte : `(secret)`.

### 22.18 Envoi à un service web *(2.42, implémenté le 23 octobre 2026)*

```
La réponse vaut l'envoi à « https://webapi.exemple.ch/sms » de :
    « UserName » : l'identifiant SMS du cabinet,
    « Password » : la clé SMS du cabinet,
    « MessageText » : le texte du rappel.
Le numéro du rappel vaut le champ « TransactionReferenceNumber » de la réponse.

L'état vaut la lecture de « https://webapi.exemple.ch/etat » avec :
    « ref » : le numéro du rappel.
```

- **`l'envoi à « … » de :`** envoie un objet JSON par POST : une paire par ligne, `« clé » : valeur`, alignées, séparées par des virgules, la dernière finie par le point de la phrase. Un texte devient une chaîne, un nombre un nombre (`12.5`), une date une chaîne `AAAA-MM-JJ`, vrai ou faux `true` ou `false`, un absent `null`. Une clé envoyée deux fois : « La clé « a » est déjà envoyée. »
- **`la lecture de « … » avec :`** lit par GET : les paires s'ajoutent à l'adresse (`?ref=a%20b`), encodées. Sans valeurs, `la lecture de « … »` lit l'adresse telle quelle.
- **Des valeurs d'adresse dans un envoi** *(2.43)* : `avec :` met ses paires dans l'adresse, encodées, comme pour une lecture ; le corps suit, après une virgule et `et`, à la ligne, au retrait de la phrase. Un service qui demande ses identifiants dans l'adresse et une liste dans le corps (ASPSMS) :

```
La réponse vaut l'envoi à « https://webapi.aspsms.com/ASPSMSDeactivatePendingSMSWithTransactionReferenceNumber » avec :
    « Userkey » : l'identifiant SMS du cabinet,
    « Password » : la clé SMS du cabinet,
et du tableau :
    le numéro du rappel.
```

- **Le corps d'un envoi** : `de :` un objet JSON (des paires), `du tableau :` une liste JSON (une valeur par ligne), ou rien : `l'envoi à « … » avec : …` seul envoie un POST vide. Une lecture n'a que des valeurs d'adresse : « Une lecture n'a que des valeurs d'adresse : pas de « et de : ». » Après `et`, autre chose : « Après « et », « de : » (un objet) ou « du tableau : » (une liste). » Au plus 63 valeurs d'adresse et 127 valeurs de corps.
- **La réponse est un texte**, le corps renvoyé par le service. **`le champ « a.b » de r`** y lit un champ JSON : les objets par leur clé, les tableaux par leur rang, le premier étant 0 (`« Liste.1 »`). Il rend un texte (un nombre tel qu'écrit, vrai ou faux en mots, un objet en JSON), ou absent si le chemin n'existe pas ou vaut `null`. Une réponse qui n'est pas du JSON : « La réponse n'est pas du JSON : « a » ne s'y lit pas. »
- **HTTPS obligatoire** : « Un envoi passe par https:// : « http://… » enverrait en clair. » Seule exception, la machine elle-même (`http://127.0.0.1`, `http://localhost`) : un service local, les essais.
- **Un échec est une erreur rattrapable** (`Essayer`, § 22) : pas de réponse en 15 secondes, un réseau coupé, un certificat refusé, ou un statut HTTP de 400 ou plus (« Le service a répondu 401 : … », suivi du début de la réponse).
- **L'envoi valide ce qui précède**, comme `Ouvrir` : une erreur plus loin dans l'événement n'annule pas un message déjà parti, ni la trace de son départ.
- **Un secret (§ 22.17) se lit dans les valeurs d'un envoi**, et nulle part ailleurs.
- **L'atelier et les applications fabriquées** envoient (Qt Network, les certificats du système). La console et le navigateur de `grym` refusent : « Un envoi sur internet se fait dans l'atelier ou dans une application fabriquée. »
- Une adresse donnée par un nom s'écrit entre parenthèses, `l'envoi à (le service) de :`, sans quoi « service de » se lit comme un nom. En forme compacte *(2.45)* : `_la r << _envoi « … » _avec`, une paire `« clé » : valeur` par ligne, sans virgule, `_et_de` ou `_et_du_tableau` au retrait de la phrase, puis `_fin` ; `_lecture « … » _avec` ; `_envoi « … » _de`, `_envoi « … » _du_tableau` ; `_champ « a.b » r`.

- *(2.60)* **Les en-têtes** : dans le bloc `avec :` d'une lecture ou d'un envoi, une paire `l'en-tête « Authorization » : "Bearer " suivi du jeton,` part dans les en-têtes de la requête, pas dans l'adresse. Un secret s'y lit, comme dans les autres valeurs d'un envoi. Un nom d'en-tête ne porte pas de « : », une valeur pas de retour à la ligne. Forme compacte : `_entête « Authorization » : …`.
- *(2.60)* **Les listes d'une réponse** : `Pour chaque réservation du champ « collection » de la réponse :` parcourt les éléments d'une liste JSON, dans l'ordre, doublons compris ; chacun est un texte (un objet ou une liste en JSON), qu'on lit avec `le champ « … » de la réservation`. `le nombre d'éléments du champ « collection » de r` les compte. Un champ qui n'est pas une liste : « Le champ « x » n'est pas une liste. » Le nom de la boucle suit la règle des noms : pas de trait d'union (« rendez-vous » est refusé, « réservation » convient). Formes compactes : `_pour_chaque réservation _champ « collection » réponse`, `_nombre_éléments « collection » r`.
- *(2.84)* **Un fichier, un XML.** `le champ « … » de x` et `Pour chaque … du champ « … » de x` lisent aussi un fichier (`Le relevé vaut l'avis de l'écran.`, une zone `(fichier)`, ou `le fichier « avis.xml »`), et du XML : un texte ou un fichier dont le premier caractère, après une marque d'ordre et des blancs, est `<`. L'élément racine devient le document : son nom ne s'écrit pas dans le chemin (`« BkToCstmrDbtCdtNtfctn.Ntfctn.Ntry »` dans un `<Document>` ISO 20022). Les préfixes d'espace de noms sont ignorés (`ns:Amt` se lit `Amt`), les `xmlns` aussi. Un élément qui a des enfants se lit comme un objet, un élément sans enfant comme son texte, débarrassé des blancs à ses bords ; les entités (`&amp;`, `&#233;`), CDATA, commentaires et instructions de traitement sont compris. Un attribut d'un élément-texte se lit chez son parent, `« Amt@Ccy »` ; celui d'un élément qui a des enfants, `« Pmt.@Id »`. Un enfant répété est une liste, dans l'ordre (`« Ntry.1 »` le deuxième) ; lu sans rang, c'est le premier. Un élément seul se parcourt comme une liste d'un, et il est aussi son propre rang 0 : un relevé d'une seule écriture se lit comme un relevé de dix. Un élément qui a des enfants se rend en son XML tel qu'écrit, qu'on relit à son tour : `Pour chaque écriture du champ « Ntfctn.Ntry » du relevé :` puis `le champ « Amt » de l'écriture`. Les refus : « Ce n'est pas un XML lisible : « a » ne s'y lit pas. XML mal formé, ligne 3 : … » ; « Aucun élément « c » dans ce XML. » (une liste demandée sur un élément absent) ; « Ce fichier n'est ni du JSON ni du XML : … » ; autre chose qu'un texte ou un fichier : « « le champ « … » de » lit un texte (la réponse d'un envoi) ou un fichier. ». Le XML n'est pas validé contre un schéma ; une DTD est sautée, ses entités ne sont pas développées. Praxis y lit l'avis de crédit camt.054 et le relevé camt.053 de la banque (« Virements reçus »).
- *(2.60)* **Le temps universel** : `le champ « start_time » de r en date locale` convertit le champ, pas r : après « de », `le champ « … » de` ne prend que la réponse elle-même (une variable, ou une parenthèse). `t en date locale` et `t en heure locale` lisent un moment ISO 8601 avec son fuseau (« 2026-10-28T13:30:00Z », « …+01:00 », fractions de seconde admises) et le rendent dans le fuseau de l'ordinateur, heure d'été comprise : « 2026-10-28T13:30:00Z » devient 28.10.2026 et 14:30 en Suisse. `x en format universel` fait l'inverse, pour une date (à minuit, ici) ou une heure (d'aujourd'hui) : `maintenant en format universel` donne « 2026-10-25T07:42:00Z ». Un texte sans fuseau : « … n'est pas un moment avec son fuseau ». Formes compactes : `_en_date_locale`, `_en_heure_locale`, `_en_format_universel`.

### 22.19 XML déclaré *(2.46, implémenté le 23 octobre 2026)*

```
Le XML de facture, « invoice:request », montre :
    l'attribut « language » : « fr »,
    l'élément « invoice:payload » :
        l'attribut « type » : « invoice »,
        l'élément « invoice:invoice » :
            l'attribut « request_id » : le numéro du tiers payant en cours,
        l'élément « invoice:body » :
            Pour chaque séance conservée dont le décompte = le tiers payant en cours, par date :
                l'élément « invoice:service » :
                    l'attribut « code » : le chiffre du tarif de la séance,
                    le contenu : le prix de la séance.

Le fichier XML du d devient le XML de facture.
```

- **`Le XML de x, « racine », montre :`** déclare un document XML, au niveau du programme, avant son usage. **`le XML de x`** l'écrit et rend un fichier (`request.xml`, du nom de la racine sans son préfixe), en UTF-8, avec sa déclaration `<?xml version="1.0" encoding="UTF-8"?>`, indenté de deux espaces.
- Ses éléments, un par ligne, séparés par des virgules, le dernier fini par un point : **`l'attribut « nom » : valeur`** ; **`le contenu : valeur`** ; **`l'élément « nom »`** (vide) ; **`l'élément « nom » : valeur`** (avec son contenu) ; **`l'élément « nom » :`** suivi de ses enfants, indentés.
- Une phrase y écrit aussi : `Pour chaque … :`, `Si … :`, `Le x vaut …` ; leurs éléments vont où elles se trouvent.
- Une valeur s'écrit telle quelle pour un texte, en forme canonique pour un nombre (`105.6`), en `AAAA-MM-JJ` pour une date, `true` ou `false` pour vrai ou faux. Un attribut absent n'est pas écrit ; un contenu absent non plus. Le langage échappe `&`, `<`, `>` et, dans un attribut, `"` : le développeur n'écrit jamais un chevron.
- Les refus : un nom qui n'est pas un nom XML (« Un nom XML entre guillemets : une lettre d'abord, puis lettres, chiffres, « - », « _ », « . » ou « : » ») ; un attribut après le contenu ou un élément (« L'attribut « k » vient avant le contenu et les éléments de « x ». ») ; un XML inconnu ou déclaré deux fois.
- **Ses valeurs** *(2.63)*. Un XML prend des paramètres, comme un calcul (§ 9.3) : `Le XML de quittance d'une chose tarifée, « quittance », montre :`, puis `le montant de la chose` dans ses éléments ; on l'écrit avec ses valeurs, `le XML de quittance de la facture`. Ainsi un XML ne dépend que de ce qu'on lui donne : une bibliothèque peut le déclarer pour une aptitude, et chaque programme le remplit avec son entité qui l'adopte. Sans sa valeur : « Le XML de quittance attend sa valeur : « le XML de quittance de la facture ». » Formes compactes : `_xml quittance(_une chose_tarifée) « quittance »` … `_fin` ; `_xml quittance(f)` dans une expression.
- *(2.63)* Un XML déclaré dans un fichier utilisé (§ 21) se voit dans le programme qui l'utilise, comme ses calculs ; il restait jusque-là propre à son fichier.
- Le XML produit est bien formé ; sa conformité à un schéma (XSD) n'est pas vérifiée. En forme compacte *(2.48)* : `_xml facture « invoice:request »` … `_fin` ; `_attribut « n » v`, `_contenu v`, `_élément « n »`, `_élément « n » v`, `_élément « n » _enfants` … `_fin` ; `_xml facture` dans une expression.

### 22.20 Écoutes *(2.69, implémenté le 1er novembre 2026)*

Une écoute capte le son d'une entrée de l'ordinateur (un poste de radio branché en USB, une interface audio), le découpe en passages et donne chacun au programme. Écoutille (`projets/Écoutille`) en est l'application d'exemple.

```
Un passage, conservé, a :
    un poste (texte),
    un jour (date),
    une heure (heure),
    un enregistrement (son),
    une transcription (texte), facultative, transcrite de l'enregistrement.

L'écran de veille montre :
    une écoute « Stryker », coupée après 3 secondes de silence,
    une écoute « George II »,
    la liste des passages conservés, par jour puis heure.

Quand une écoute capte un son dans l'écran de veille :
    Le p vaut un nouveau passage.
    Le poste du p devient l'écoute.
    Le jour du p devient aujourd'hui.
    L'heure du p devient maintenant.
    L'enregistrement du p devient le son capté.
    Conserver le p.
```

- **`une écoute « … »`** est un élément d'écran : son nom est unique dans l'écran (« L'écoute « A » existe déjà dans cet écran. »). Elle accepte `visible si` et la position libre (§ 22.9), pas `actif si`. Un document la refuse, comme un bouton.
- **Le passage** commence quand le niveau du son dépasse le seuil, et finit après 1,5 seconde sous le seuil. `, coupée après 3 secondes de silence` change ce délai, de 1 à 60 secondes. Le passage garde les 300 ms qui précèdent la parole et 300 ms après ; moins de 300 ms de parole est un craquement, oublié ; au-delà de 5 minutes, il est coupé et un autre commence. C'est un WAV de 16 kHz mono, le format que lit le transcripteur (§ 15.8).
- **`Quand une écoute capte un son dans l'écran X :`** s'exécute pour chaque passage, dans sa transaction, comme un clic. `Quand l'écoute « Stryker » capte un son dans l'écran X :` ne vaut que pour celle-là et l'emporte sur l'événement de toutes les écoutes. Dans le corps, **`le son capté`** est le passage, un `(son)` (§ 15.7), et **`l'écoute`** son nom, un texte. Ranger le son dans un champ `transcrit de` le fait transcrire, comme partout (§ 15.8). Un écran sans écoute : « L'écran « x » n'a pas d'écoute ». Une autre tournure que `capte un son` : « « capte un son » attendu ».
- **Le bandeau de l'écoute**, dans l'atelier et les applications fabriquées : le nom, la case **En veille** (décochée la première fois : une écoute hors veille n'ouvre pas son entrée et ne donne aucun passage), le menu des entrées (chaque appareil, et chaque canal d'un appareil stéréo : « U-Phoria UM2, entrée 1 » ; deux appareils au même nom sont numérotés, « USB Audio CODEC (2) »), la barre de niveau et, juste dessous, à la même échelle et à la même largeur, le **squelch** *(2 novembre 2026)* : un grand curseur de −50 à +50, 0 au milieu, chiffré de 10 en 10, « ◀ prend plus de son, même faible » à gauche, « seulement les sons forts ▶ » à droite ; un cran vaut ½ dB, 0 vaut −35 dB (de −60 à −10 dB sur le niveau efficace, comme l'ancien curseur du seuil, dont les réglages gardent leur sens) ; quand la barre dépasse la poignée, un passage commence. Le bouton **Calibrer** écoute 5 secondes le bruit de fond, poste allumé sans émission, et place le squelch 6 dB au-dessus du 90e centile des niveaux mesurés par fenêtres de 20 ms (un claquement bref ne le fausse pas) ; pendant ce temps, rien ne devient un passage ; sans son reçu, il le dit et ne touche à rien. Puis le **gain** (de −30 à +30 dB, 0 au milieu) et la case **Filtre radio** (de 150 à 3000 Hz, deux filtres du second ordre : le ronflement du secteur, atténué de 19 dB à 50 Hz, et le souffle aigu ôtés, la voix et le morse gardés ; 150 et non 300 Hz depuis le 2 novembre 2026 : sur un passage réel, la fondamentale d'une voix d'homme, vers 190 Hz, perdait 5 dB, et la transcription était moins juste) : le son est affiné avant tout le reste, découpage, haut-parleur, transcription et morse, et borné à ±1 (un gain trop fort sature, et l'état le dit). Le bouton **Haut-parleur** rejoue l'entrée sur la sortie son de l'ordinateur ; l'état : en écoute, passage en cours, son qui sature. La veille, l'entrée, le squelch, le gain, le filtre et le haut-parleur sont retenus par nom d'écoute, d'un lancement à l'autre. Sous macOS, le système demande une fois l'accord pour le micro.
- Les passages captés pendant que la machine travaille attendent, dans l'ordre, et partent quand leur écran attend ; ceux d'un écran recouvert (§ 22.3) attendent son retour ; ceux d'un écran fermé se perdent.
- **La console et le navigateur** ne captent pas le son : ils refusent l'écran à son ouverture (« L'écoute « Stryker » capte le son de l'ordinateur : elle vit dans l'atelier et les applications fabriquées, pas dans la console ni le navigateur. »).
- L'onglet Écrans de l'atelier montre l'écoute à sa place ; son silence se règle dans le code.
- Forme compacte : `_écoute « Stryker » _silence 3` ; `_quand _capte « Stryker » _dans de_veille`, `_quand _capte _dans de_veille`.

### 22.21 Les écoutes d'une entité *(2.70, implémenté le 1er novembre 2026)*

Les postes deviennent des données : on les ajoute, les modifie ou les retire depuis l'application, sans toucher au code.

```
Un poste, conservé, a :
    un nom (texte), unique,
    une veille (vrai ou faux).

Un passage, conservé, a :
    un poste (poste),
    un enregistrement (son),
    une transcription (texte), facultative, transcrite de l'enregistrement.

L'écran de veille montre :
    les écoutes des postes conservés dont la veille est vraie, par nom, coupées après 3 secondes de silence,
    la liste des passages conservés, avec le nom du poste.

Quand un poste capte un son dans l'écran de veille :
    Le p vaut un nouveau passage.
    Le poste du p devient le poste.
    L'enregistrement du p devient le son capté.
    Conserver le p.
```

- **`les écoutes des … conservés`** se lit comme `la liste des … conservés` (§ 22.1) : mêmes `dont` et `, par …`. Chaque objet trouvé donne un bandeau (§ 22.20), nommé par la clé de l'entité, son premier champ texte unique ; sans clé : « « poste » n'a pas de champ texte unique pour nommer ses écoutes : « un nom (texte), unique ». ». Pas de colonnes (« Une écoute se nomme par la clé de son poste : pas de colonnes. »), pas `en cartes`. `, coupées après 3 secondes de silence` vaut pour toutes, de 1 à 60 secondes. Une seule fois par entité dans un écran ; un document les refuse.
- **Les écoutes se relisent avant chaque attente**, comme une liste : un poste ajouté, renommé ou mis en veille apparaît au tour suivant ; un poste qui sort de la liste perd son bandeau. Les bandeaux qui restent ne sont pas recréés et ne coupent pas leur passage en cours.
- **`Quand un poste capte un son dans l'écran X :`** reçoit l'objet lui-même, nommé comme dans `Quand on choisit un compositeur` (§ 22.2), et **`le son capté`**. Le passage garde ainsi un lien vers son poste : renommer le poste renomme tout son historique, et la fiche du poste montre ses passages (§ 22.3). Un écran sans écoutes de cette entité : « L'écran « x » n'a pas d'écoutes de « poste » … ».
- **Ce qui vit dans la base, ce qui reste à l'ordinateur.** Le poste (son nom, sa veille, tout ce que le programme déclare) est dans la base et suit la base d'un ordinateur à l'autre. Le branchement reste propre à chaque ordinateur, retenu par nom de poste : l'entrée son et son canal, le squelch, le gain, le filtre, le haut-parleur. La case « En veille » du bandeau disparaît : c'est le `dont` du programme qui décide.
- La console et le navigateur refusent l'écran, comme pour `une écoute` (§ 22.20). L'onglet Écrans de l'atelier montre l'élément ; il se modifie dans le code.
- Forme compacte : `_écoutes poste _conservé _dont veille _vrai _par nom _silence 3` ; `_quand _capte _un poste _dans de_veille`.

### 22.22 Signaler *(2.71, implémenté le 1er novembre 2026)*

Un geste qui réussit le dit, sans rien bloquer.

```
Quand on clique sur « Espèces » dans l'écran des séances :
    La s vaut la séance choisie de l'écran.
    Le paiement de la s devient « espèces ».
    Signaler « Séance de » puis le nom du patient de la s puis « payée en espèces. ».
```

- **`Signaler « … » [puis x …].`** s'écrit comme `Refuser` (§ 18.1) : des éléments reliés par `puis`, écrits comme `Afficher` les écrirait, séparés par une espace.
- **Dans un événement**, le message attend la fin de l'événement. Réussi, il se montre à la prochaine attente d'un écran, celui du dessous si l'événement a fermé le sien. Raté (un refus, une règle violée), il est oublié avec tout ce que l'événement a fait. Deux `Signaler` du même geste se montrent l'un sous l'autre.
- **Dans l'atelier et les applications fabriquées**, un bandeau à la couleur d'accent, au pied de la fenêtre, quatre secondes et une de plus par tranche de 40 caractères (dix au plus). Il ne prend ni le focus ni les clics. Un refus l'efface. **Dans le navigateur**, un paragraphe `role="status"` sous le titre, jusqu'au geste suivant.
- **Hors d'un écran** (console, programme sans écran ouvert), il s'écrit tout de suite, comme `Afficher`.
- Un calcul ne signale rien : « Un calcul ne signale rien : signale dans une action. »
- Forme compacte : `_signaler « Payé : » ; 20`.

### 22.23 Bouton destructif *(2.72, implémenté le 1er novembre 2026)*

`un bouton « Supprimer », destructif, à confirmer par « Supprimer ce patient ? »` : un bouton qui retire, efface ou annule se dessine en rouge (bordure et texte, fond rouge doux au survol), comme le thème réserve le rouge aux erreurs et à ce qui se perd. Il se repère d'un coup d'œil à côté du bouton principal, qui dit l'action attendue.

- Seul un bouton est destructif ; « destructive » : « Accord : « destructif » (le bouton). » ; un bouton principal ne l'est pas : « Un bouton principal n'est pas destructif : l'action attendue ne détruit rien. »
- `destructif` ne demande rien : la question reste celle de `, à confirmer par « … »` (2.65).
- Dans le navigateur : `class="destructif"`. Forme compacte : `_destructif`.

### 22.24 Agenda *(2.74, implémenté le 1er novembre 2026)*

Une liste montrée en grille horaire : une colonne par jour, chaque objet à son heure et sur sa durée. Un clic le choisit ; on le glisse ailleurs à la souris.

```
La grille de l'agenda vaut « 02.11.2026|06.11.2026|1 08:00-12:00 13:30-18:00|3 08:00-12:00 ».

L'écran de l'agenda montre :
    la liste des séances conservées, par date puis heure, avec la date, l'heure, la durée et le nom du patient, en agenda selon la grille de l'agenda.

Quand on déplace une séance dans l'écran de l'agenda :
    La date de la séance devient la date d'arrivée de l'écran.
    L'heure de la séance devient l'heure d'arrivée de l'écran.
```

- **`, en agenda selon …`** suit les colonnes d'une liste, à la place de `, en cartes` (« Une liste est en cartes ou en agenda, pas les deux. »). Ses trois premières colonnes sont **la date, l'heure et la durée** en minutes (vide : 60) ; les suivantes s'écrivent dans le bloc. Un objet sans date ou sans heure ne se place pas.
- **La grille** est une valeur, relue avant chaque attente comme un texte calculé (2.24) : `premier jour|dernier jour|plages` ; chaque plage, `n hh:mm-hh:mm [hh:mm-hh:mm …]`, n de 1 (lundi) à 7 (dimanche). Un jour de la période sans plage n'est pas montré ; sans aucune plage, tous les jours le sont, sans rien griser. Hors des plages, la colonne est hachurée. Six semaines au plus. Les heures vont de 7 h à 20 h, élargies aux plages et aux objets.
- **`Quand on choisit une séance`** : un clic sur son bloc. **`Quand on déplace une séance dans l'écran X :`** : le bloc glissé puis lâché ailleurs, au quart d'heure ; dans le corps, `la date d'arrivée de l'écran` et `l'heure d'arrivée de l'écran`. Le programme décide : écrire la date et l'heure, refuser (`Refuser « … »` : le bloc revient), ou demander.
- **`Quand on choisit un créneau dans l'écran X :`** *(2.75)* : un clic dans la grille, hors des objets ; `la date d'arrivée` et `l'heure d'arrivée` de l'écran disent le jour et le quart d'heure (celui d'avant le clic). Sans paramètre. Une entité nommée « créneau » garde la forme ordinaire de `Quand on choisit un …`. Forme compacte : `_quand _choisit _un créneau _dans …`.
- Dans l'atelier et les applications fabriquées, à la souris. **Le navigateur**, sans JavaScript : la même grille en quarts d'heure ; un clic sur un quart d'heure est un créneau ; un objet a son bouton « Déplacer », qui le met de côté, et le quart d'heure cliqué ensuite le dépose (`Quand on déplace …`) ; « Annuler » le remet. L'onglet Écrans de l'atelier la montre en grille, sur une semaine de travail d'exemple.
- Forme compacte : `_liste séance _conservé _avec date ; heure ; durée _agenda grille` ; `_quand _déplace _une séance _dans de_l_agenda`.

### 22.25 Ports série *(2.82, implémenté le 3 novembre 2026)*

Un poste de radio récent se pilote par un port série : un câble USB qui apparaît comme un port COM (Windows) ou un `/dev/cu.…` (macOS). On lui pose une question en texte, il répond en texte. Écoutille (`projets/Écoutille`) y lit la fréquence et le mode des Yaesu (protocole CAT).

```
Afficher la réponse de « /dev/cu.SLAB_USBtoUART » à « FA; », à 38400 bauds.      →  FA014074000;
Le mode vaut la réponse du port CAT du poste à « MD0; », à 4800 bauds.
Pour chaque port parmi les ports série, afficher port.
```

- **`la réponse de p à q, à v bauds`** ouvre le port `p` (un texte), y écrit la question `q` telle quelle, lit la réponse et referme le port. Elle se reconnaît au mot `bauds`, sur la même ligne, après `, à`. La vitesse : 1200, 2400, 4800, 9600, 19200, 38400, 57600 ou 115200 ; autre chose : « La vitesse d'un port série : 1200, … ou 115200 bauds. ». Le format est fixe : 8 bits, sans parité, 2 bits d'arrêt, sans contrôle de flux ; un poste réglé sur 1 bit d'arrêt les accepte. Les lignes DTR et RTS sont baissées dès l'ouverture : sur le second port d'un Yaesu (« Standard »), elles passent le poste en émission. Sous macOS et Linux, l'ouverture les lève quelques instants avant qu'on les baisse : on ne parle donc qu'au port CAT d'un poste, jamais à tous.
- **La réponse** est ce qui arrive jusqu'à 100 ms de silence après le dernier octet reçu, ou rien après 1 seconde : un texte vide (un poste éteint, une vitesse fausse) n'est pas une erreur. Rien n'est propre à une marque : la question et le sens de la réponse appartiennent au programme.
- **Les refus**, rattrapables par `Essayer` : un port qui ne s'ouvre pas (« Le port « COM3 » ne s'ouvre pas : le poste est-il branché et allumé ? ») ; un port déjà ouvert par un autre programme (WSJT-X, un carnet de trafic : un port ne s'ouvre qu'une fois) ; un port absent, un port qui n'est pas un texte, une question vide.
- **`Pour chaque p parmi les ports série :`** parcourt les ports de l'ordinateur, triés, lus une fois au début : `/dev/cu.*` sous macOS, `/dev/ttyUSB*` et `/dev/ttyACM*` sous Linux, COM1 à COM256 sous Windows. Aucun port : la boucle ne tourne pas.
- Un calcul ne parle pas à un port série, ni ne les parcourt (§ 9.4) : le résultat dépendrait de l'appareil branché.
- Sous macOS, les Yaesu à puce Silicon Labs CP2105 (FT-991A, FT-710) demandent le pilote du fabricant pour que leur port apparaisse ; le son USB n'en a pas besoin.
- Forme compacte : `_réponse_port p _question « FA; » _vitesse 38400 _bauds` ; `_pour_chaque p _parmi _ports_série`.


### 22.26 Les décodages de WSJT-X *(2.88, implémenté le 6 novembre 2026)*

```
Pour chaque d parmi les décodages WSJT :
    Afficher le champ « heure » de d puis le champ « message » de d.
Pour chaque d parmi les décodages WSJT du port 2238 :
    …
```

- **`les décodages WSJT [du port p]`** : ce que WSJT-X (FT8, FT4, JT65…) a décodé depuis la dernière lecture, une liste de textes JSON qui se lisent avec `le champ « … » de d` (§ 22.18) : `heure` (« 14:30:15 », UTC, celle de WSJT-X), `mode` (« FT8 »), `fréquence` (la fréquence de cadran, en hertz, absente tant que WSJT-X n'a pas dit son état), `rapport` (dB), `décalage` (le décalage audio, Hz), `écart` (le décalage temporel, s), `message` (« CQ HB9ABC JN36 »), `douteux` (vrai ou faux). Sans port, 2237, celui de WSJT-X. Seuls les décodages nouveaux comptent, pas ceux que WSJT-X rejoue.
- **La prise UDP** s'ouvre sur 127.0.0.1 au premier appel et le reste jusqu'à la fin du programme : ce qui arrive entre deux lectures attend dans le système, rien ne se perd tant que le programme tourne ; ce qui arrive avant le premier appel est perdu. La lecture n'attend jamais : rien d'arrivé, une liste vide. Dans WSJT-X : Réglages, Rapports, « UDP Server » 127.0.0.1, port 2237. Un seul logiciel peut écouter ce port : JTAlert ou GridTracker l'occupent peut-être déjà.
- **Les refus** (rattrapables) : « Le port UDP 2237 ne s'ouvre pas : un autre logiciel l'écoute peut-être déjà (JTAlert, GridTracker). … » ; un port hors de 1 à 65535 : « Le port de WSJT-X est un nombre entier de 1 à 65535 (2237, celui de WSJT-X). » ; `WSJT-X` écrit avec son trait d'union : « « les décodages WSJT » s'écrit sans « -X » : un trait d'union est une soustraction. ». Pas dans un calcul.
- **Le format** est celui des messages UDP de WSJT-X (NetworkMessage.hpp dans ses sources), décrit ici de mémoire et vérifié seulement contre des messages fabriqués : les messages « Status » (1) donnent la fréquence et le mode, chaque « Decode » (2) un décodage ; les autres sont ignorés. Le mode d'un décodage vient du dernier Status ; sans lui, du caractère du Decode (« ~ » FT8, « + » FT4).
- Forme compacte : `_décodages_wsjt [p]`.
### 22.27 Les courriels *(2.94)*

```
La base est chiffrée.
Un cabinet, conservé, a :
    un nom (texte), unique,
    une adresse de courriel (texte),
    un serveur de courriel (texte),
    un compte de courriel (texte),
    une clé de courriel (secret).
Pour rappeler un patient :
    Envoyer un courriel à l'adresse du patient, avec l'objet « Votre séance », depuis le cabinet conservé, et le texte suivant :
        Bonjour {prénom du patient},

        Votre séance du {date de la prochaine séance du patient en toutes lettres} est confirmée.
```

- `Envoyer un courriel à A, avec l'objet O, depuis M, et le texte T.` envoie un courriel au format texte, en UTF-8. `A` est l'adresse du destinataire ; `O`, l'objet ; `T`, le contenu, en général un texte suivant (§ 4.7), qui finit alors la phrase.
- **La messagerie** `M` est un objet dont l'entité a quatre champs, reconnus par leur nom : `adresse de courriel` (texte, l'expéditeur), `serveur de courriel` (texte, `mail.infomaniak.com`), `compte de courriel` (texte, l'identifiant de connexion, souvent l'adresse elle-même) et `clé de courriel` (secret, § 22.17 : le mot de passe de la messagerie, dans une base chiffrée). La clé ne se lit nulle part ailleurs : l'instruction la lit elle-même, au moment d'envoyer.
- **Le serveur** se joint en SMTP sur TLS implicite, port 465, avec les certificats du système ; `mail.infomaniak.com:465` choisit le port. L'authentification est `AUTH PLAIN`. STARTTLS (port 587) n'est pas fait : les grands fournisseurs (Infomaniak, Gmail, Outlook) offrent le port 465 (de mémoire, à vérifier chez chacun).
- **Où.** Comme un envoi web (§ 22.18), un courriel part de l'atelier et des applications fabriquées ; la console et le navigateur refusent : « Un courriel s'envoie dans l'atelier ou dans une application fabriquée. »
- **Les refus**, tous de sorte `réseau` (§ 18.2), donc rattrapables : une adresse sans `@`, ou qui contient une espace ou un retour à la ligne ; un objet sur plusieurs lignes (« L'objet d'un courriel tient sur une ligne. ») ; une messagerie à qui il manque un champ (« La messagerie n'a pas de champ « clé de courriel ». ») ou dont la clé n'est pas un secret (« « clé de courriel » est un secret : écris « une clé de courriel (secret) ». ») ; le serveur injoignable, un certificat refusé, une authentification refusée, un destinataire refusé, chacun avec la réponse du serveur. Rien n'est réessayé en silence : `Essayer … En cas d'échec de réseau` décide.
- **Une transaction n'annule pas un courriel parti.** Un courriel envoyé dans un `Essayer` dont la suite échoue reste envoyé : comme l'envoi web, c'est un effet hors de la base. Envoyer en dernier ce qui ne doit partir qu'après réussite.
- Un calcul n'envoie rien (§ 9.4).
- `Envoyer` n'est pas un mot réservé : une action déclarée « envoyer un courriel … » l'emporte.
- En forme compacte : `_envoyer_courriel _à a _objet o _depuis m _texte t`, ou `_texte_suivant` et son bloc à la place de `_texte t`.

## Journal des révisions

| Version | Date | Changement |
|---------|------|------------|
| 1.0 | 2026-09-20 | Spécification initiale : nommer (`vaut` / `devient`), calculer (décimal exact, arrondi bancaire à 28 chiffres), afficher (style suisse par défaut) |
| 1.1 | 2026-09-21 | Puissance avant négation (`−2 ^ 2` = `−4`). Guillemets `“ ”` et tiret `–` acceptés ; trait d'union toujours opérateur. Règles des noms (mots réservés, article initial interdit). Remarques en début de ligne. Boucle interactive : point final facultatif, saisie atomique. Nouveau § 7 : suites attendues |
| 1.2 | 2026-09-21 | Arithmétique précisée : division finie exacte, zéros de fin, multiplication exacte, puissance à exposant entier (`0 ^ 0` = 1), limite de 1'000 chiffres. Afficher : séparateur espace, signe `−`. Boucle interactive : `quitter`, atomicité étendue aux erreurs de calcul. Messages d'erreur d'exécution |
| 1.3 | 2026-09-21 | Nouveau § 5 : décider (comparaisons en mots et en symboles, sens courant de positif, accords, contractions au et du, et et ou sans mélange, Si en forme courte et en bloc, portée des blocs, booléens). Noms entre crochets, mots réservés étendus, tabulations interdites en début de ligne. Renumérotation : EBNF § 6, messages § 7, suites § 8 |
| 1.4 | 2026-09-21 | Nouveau § 9 : formules. Calculs (définis comme ils s'utilisent, forme courte et bloc avec `Rendre`), actions (`Pour`), paramètres par l'article indéfini, calculs purs, récursion limitée à 1000 appels. `rendre` réservé, `d'un` réservé aux paramètres |
| 1.5 | 2026-09-21 | Nouveau § 10 : répéter. `Tant que`, `Répéter … fois`, `Pour chaque … de … à … [par pas de …]` (bornes incluses, sens automatique, pas décimaux exacts, compteur en lecture seule), `Sortir de la boucle`, `Passer au tour suivant`, `Selon` / `Cas` / `Autrement` (valeurs séparées par `ou`, intervalles, tournures, pas de chute), interruption par Ctrl+C |
| 1.6 | 2026-09-21 | Nouveaux § 11 (forme compacte : règles de lecture, table des correspondances) et § 12 (forme canonique, garanties de la traduction). `grym formater`, `grym traduire` vers la forme compacte |
| 1.7 | 2026-09-21 | § 11 : lecture de la forme compacte (réécriture en phrases littéraires, positions conservées), outils, suffixes de comparaison, instructions sur plusieurs lignes, limite des messages. § 12 : garanties vérifiées |
| 1.8 | 2026-09-21 | Nouveau § 13 : classes, création d'objets (`un nouveau`, bloc d'initialisation), champs (`le solde du client`, `devient`), identité, ramasse-miettes. Les textes deviennent des valeurs. Correspondances compactes |
| 1.9 | 2026-09-21 | § 13.5 : héritage simple (`Un membre est une personne.`, champs propres dans la phrase suivante), forme compacte `_est` |
| 1.10 | 2026-09-21 | § 13.6 : méthodes ; versions d'une formule par classe du premier paramètre, choix à l'exécution selon la classe réelle et sa lignée |
| 1.11 | 2026-09-21 | § 13.7 : aptitudes (adjectifs, formes masculines, adoption accordée, champs apportés, versions d'aptitude, ordre de choix, conflits tranchés par la classe) ; § 13.8 : suites |
| 1.12 | 2026-09-21 | Proposition soumise à relecture : § 14 dates, § 15 fichiers et images, § 16 entités (déclaration, typage strict, conserver, retrouver, base, transaction, migrations, forme compacte, écarts avec la charte) |
| 1.13 | 2026-09-21 | § 14 à 16 validés. Un calcul ne lit pas la base (§ 16.4). § 16.9 : accord avec la charte 1.9 |
| 1.14 | 2026-09-21 | § 14 implémenté ; dates dans les boucles et `Selon` ; messages d'erreur des dates |
| 1.15 | 2026-09-21 | § 15 implémenté ; chemin entre parenthèses, dossier de référence, écritures toutes ou aucune, champs des fichiers qui ne réservent rien, égalité par contenu, arrondi des tailles |
| 1.16 | 2026-09-21 | § 16.1 et 16.2 implémentés ; accord de `conservé`, types réservés aux entités et aptitudes, héritage entre entités, lien vers soi, portée de la vérification à l'analyse, `(nombre entier)` et `3,0`, forme compacte des types |
| 1.17 | 2026-09-21 | § 16.3, 16.5, 16.6 implémentés ; lien vers soi, identifiants jamais réattribués, définition changée refusée en attendant les migrations, ordre fichiers puis base, tables créées dans la transaction |
| 1.18 | 2026-09-21 | § 16.4 implémenté : champs de l'objet examiné, `est valeur`, comparaisons permises, décimal exact et ordre du dictionnaire, héritage, liste figée, chargement à la demande. § 16.8 : champs sans préfixe dans une condition compacte, `_nombre_de` |
| 1.19 | 2026-09-21 | § 3.3 : sortie d'une exécution ratée conservée, suivie d'une phrase d'annulation. § 16.7 implémenté : valeur de départ, règles et limites imposées par SQLite |
| 1.20 | 2026-09-22 | § 16.9 : champs facultatifs, valeur `absent`, tests `est absent` et `est présent`, règles en base, en recherche, en tri et en migration ; § 16.10 : ancien § 16.9 |
| 1.21 | 2026-09-22 | § 16.10 : relations inverses (`les œuvres de bach`, `dont brel est l'auteur`) ; `dont` devient réservé ; § 16.11 : ancien § 16.10 |
| 1.22 | 2026-09-22 | § 16.12 : corbeille (`Supprimer`, `définitivement`, `Rétablir`, `supprimé`) et cascade (`, et disparaît avec elle`) ; `Supprimer` met désormais dans la corbeille ; `définitivement` réservé |
| 1.23 | 2026-09-22 | § 16.13 : plusieurs vers plusieurs (`des genres (genre)`, `gagnent`, `perdent`, `Pour chaque interprète de o`, `le nombre de … de …`, `dont … est parmi les …`) ; relation inverse étendue aux champs multiples ; EBNF et messages |
| 1.24 | 2026-09-22 | § 14.5 : années (type `(année)`, valeur à part entière, sans littéral, `l'année de d`, calculs, comparaisons, boucles, base, migrations) ; une entité ne porte pas un nom de type |
| 1.25 | 2026-09-22 | § 1.2 et § 12 : grouper les chiffres par milliers est un choix d'écriture conservé ; la forme canonique n'en normalise que le style (apostrophe) |
| 1.26 | 2026-09-22 | § 10.3 et § 12 : « du … au … » de `Pour chaque` conservé par la forme canonique, même devant une valeur sans article |
| 1.27 | 2026-09-22 | § 17 : questions à l'utilisateur (`la réponse à …`, types, relance) ; § 16.6 et § 3.3 : une question valide ce qui la précède et rend le verrou |
| 1.28 | 2026-09-22 | § 4.1 : le style des nombres se déclare dans le programme ; § 4.2 : largeur (`sur 20`, `à droite`) et `, sans passer à la ligne` ; `sur` réservé |
| 1.29 | 2026-09-22 | § 4.3 : « Effacer l'écran. », sans effet hors d'un terminal ; `effacer` réservé |
| 1.30 | 2026-09-22 | En-tête et périmètre remis à jour ; § 13.8 : l'absence de valeur n'est plus à venir |
| 1.31 | 2026-09-23 | § 18 : reprise après erreur (`Essayer`, `En cas d'échec`, `le motif de l'échec`) ; `essayer` réservé aux constructions |
| 1.32 | 2026-09-23 | § 19 : formulaire (`un nouveau client saisi`) : ordre, libellés, valeurs de départ, ligne vide, liens par champ texte unique, unicité vérifiée à la réponse |
| 1.33 | 2026-09-23 | § 17 : un point seul annule une question ou un formulaire (« Saisie annulée. »), annoncé à la première relance |
| 1.34 | 2026-09-23 | § 4.4 : assembler des textes (`suivi de`), élision de `de` et `que` selon la valeur ; `suivi` réservé |
| 1.35 | 2026-09-24 | § 19 : modifier par formulaire (`Saisir à nouveau p.`), valeurs actuelles entre crochets, `-` pour vider, écriture après la dernière réponse ; `saisir` réservé aux constructions |
| 1.36 | 2026-09-24 | § 16.5 : numéro de format des bases, base plus récente refusée (charte, art. 13) |
| 1.37 | 2026-09-24 | § 20.1 : la fiche d'un objet (`Afficher la fiche de p.`) |
| 1.38 | 2026-09-24 | § 21 : fichiers utilisés (`Utiliser « données ».`), fichiers de déclarations, chemins, lecture unique, cercles refusés, erreurs par fichier ; `utiliser` devient un mot de construction (§ 10.7) |
| 1.39 | 2026-09-25 | § 22 : écrans (A3), conçus, pas encore implémentés : déclaration, événements, ouvrir et fermer, une transaction par événement |
| 1.40 | 2026-09-25 | § 22 : A3-a implémenté (déclaration, clic, choix, ouvrir, fermer) ; § 10.7 : `quand`, `ouvrir`, `fermer` deviennent des mots de construction ; un programme qui les employait comme nom d'action doit les renommer |
| 1.41 | 2026-09-25 | § 22 : A3-b implémenté (zones de saisie, « de l'écran », ligne choisie, changement, ouverture, fermeture) |
| 1.42 | 2026-09-25 | § 22 : A3-c implémenté (fiche déduite, écrans empilés) |
| 1.43 | 2026-09-25 | § 22 : A3-d implémenté (colonnes choisies, blocs de disposition) ; A3 fait |
| 1.44 | 2026-09-26 | § 22.5 : apparence des écrans (`Les écrans ont la couleur … et le logo « … ».`) |
| 1.45 | 2026-09-26 | § 22.5 : suites attendues des phrases en `Les` (apparence des écrans, style des nombres) |
| 1.46 | 2026-09-26 | § 12 : un texte vide s'écrit `« »` dans la forme canonique (et non plus `«  »`, deux espaces) |
| 1.47 | 2026-09-27 | § 18.1 : `Refuser « … » puis x.`, une erreur voulue, rattrapable, annulée par la transaction |
| 1.48 | 2026-09-27 | § 4.1 : un entier de quatre chiffres ou moins s'affiche sans séparateur (`2026`, `1020`) ; avec des décimales ou à partir de cinq chiffres, groupé |
| 1.49 | 2026-09-28 | § 16.4 : `la somme des … des … [dont …]`, additionne un champ nombre, en décimal exact |
| 1.50 | 2026-09-28 | § 16.4 : dans `dont`, un champ lu à travers un ou plusieurs liens (`la comptabilisation de l'écriture`) |
| 1.51 | 2026-09-28 | § 9.4 et § 16.4 : un calcul lit la base, jamais il n'y écrit ; l'état de la base fait partie de son entrée |
| 1.52 | 2026-09-28 | § 16.14 : règles, `Chaque … conservé [dont …] vérifie : condition, sinon « … ».`, vérifiées à la fin de chaque transaction |
| 1.53 | 2026-09-28 | § 16.14 : coût mesuré des règles |
| 1.54 | 2026-09-28 | § 22 (A3-c) : la fiche montre les champs « plusieurs », un bouton par élément |
| 1.55 | 2026-09-28 | Messages au tutoiement, comme le guide (charte, art. 8) : « écris … », « voulais-tu … ? » |
| 1.56 | 2026-09-29 | § 14.2 : mois, années et semaines (`d + 3 mois`), ramenés au dernier jour du mois ; unités dans les boucles, sans dérive ; vérifiés contre `dateutil` |
| 1.57 | 2026-09-29 | § 22.3 et § 22.5 : `grym servir` montre les écrans dans le navigateur, avec la couleur et le logo (`docs/web.md`, W1) ; seul `grym lancer` les refuse |
| 1.58 | 2026-10-12 | Nouveau § 3.4 : arrondir à un pas (`l'arrondi de x à 0,05`), moitié loin de zéro par défaut, `vers le bas`, `vers le haut`, `au pair` ; exact, vérifié contre `decimal` ; aucun mot réservé. § 3.2 : renvoi |
| 1.59 | 2026-10-12 | Nouveau § 4.5 : `la longueur de t`, `en majuscules`, `en minuscules`, conversions `en nombre`, `en date`… ; `contient`, `commence par`, `finit par`, sans accents ni casse, aussi dans `dont` ; aucun mot réservé |
| 1.60 | 2026-10-12 | § 16.4 : `la moyenne des`, `le plus grand … des`, `le plus petit … des` (erreur sur rien, jamais 0) ; `le client conservé au plus grand solde`, l'objet au champ extrême. Impression : « de le » se contracte devant une recherche (`du membre conservé`), qui sortait en « de le » |
| 1.61 | 2026-10-12 | Nouveau § 16.15 : valeurs permises d'un champ texte, `un état (« à faire », « en cours » ou « fini »)` ; vérifiées à l'analyse et à l'exécution, menu à la saisie, retrait refusé tant qu'un objet porte la valeur |
| 1.62 | 2026-10-12 | Nouveau § 3.5 : `le reste de a par b` (signe du dividende), `la partie entière de x` (vers zéro), `la valeur absolue de x` ; vérifiés contre `decimal` ; aucun mot réservé |
| 1.63 | 2026-10-12 | Nouveau § 15.4 : `Exporter les … conservés [dont …] [, par …] [, avec …] dans « f.csv ».`, un tableau CSV (UTF-8, `;`, virgule décimale), écrit comme `Enregistrer` ; aucun mot réservé |
| 1.64 | 2026-10-12 | Nouveau § 15.5 : `Pour chaque ligne du tableau « f.csv » :`, `la colonne « Nom » de la ligne` (cellule vide : absente) ; séparateur deviné, RFC 4180 ; aucun mot réservé |
| 1.65 | 2026-10-12 | § 13.8 : `comme une personne` après l'appel d'une méthode, la version d'une classe ancêtre (le `super` de Java) ; aucun mot réservé |
| 1.66 | 2026-10-12 | Nouveau § 13.9 : ensembles (`Les couleurs valent …`, `gagnent`, `perdent`, `parmi`, `Pour chaque … parmi les …`, `le nombre de`, `jointes par`), sans doublon, copiés à l'affectation ; § 4.4 : joindre ; aucun mot réservé |
| 1.67 | 2026-10-12 | Nouveau § 9.6 : `Exemple : … vaut ….`, ignorés par `grym lancer`, vérifiés par `grym exemples` (déclarations seulement, base vide en mémoire) ; aucun mot réservé |
| 1.68 | 2026-10-12 | Nouveaux § 3.6 (hasard : `un nombre au hasard de 1 à 6`, `un élément au hasard parmi les …`, actions seulement, `GRYM_HASARD`) et § 14.6 (`le nombre de mois entre d1 et d2`, unités entières, règle de `d + n mois`) ; aucun mot réservé |
| 1.69 | 2026-10-12 | Nouveau § 14.7 : heures (`14:30`, type `(heure)`, `maintenant`, `+ 15 minutes` qui tourne comme une horloge, `h2 − h1` en minutes, écarts en minutes et en heures) ; heure locale, sans fuseau |
| 1.70 | 2026-10-12 | § 1.4 : message dédié au trait d'union collé entre deux mots (`rendez-vous`), qui propose les deux mots ; la règle ne change pas |
| 1.71 | 2026-10-13 | § 16.14 : une règle dont rien de ce qu'elle a lu n'a changé ne se revérifie pas ; mesures à 50'000 écritures |
| 1.72 | 2026-10-13 | § 4 : `suivi de` écrit un nombre comme `Afficher`, séparateur de milliers compris (`P-49'999`) ; choix confirmé, essai ajouté |
| 1.73 | 2026-10-14 | Deux corrections, les règles ne changent pas : § 4.5, `t en vrai ou faux` reconnu après un nom ; § 15.5, l'imprimeur accorde au féminin ce qui suit une colonne (`la colonne « Année » de la ligne est présente`) |
| 1.74 | 2026-10-14 | Nouveau § 22.6 : états des éléments d'écran, `sur plusieurs lignes`, `en lecture seule`, `actif si …`, `visible si …` ; conditions lues en fin de déclaration |
| 1.75 | 2026-10-14 | Nouveau § 22.7 : `le groupe « … » :` et valeurs permises d'une zone d'écran (boutons radio jusqu'à quatre valeurs) |
| 1.76 | 2026-10-14 | § 22.6 : une zone vide est absente pour une condition ou un `dont` ; un événement qui lit une zone obligatoire vide s'arrête avec « Remplis « … » » ; case à cocher obligatoire à faux au départ ; astérisque des zones obligatoires |
| 1.77 | 2026-10-14 | Nouveau § 22.8 : `l'image « … »` dans un écran, vérifiée dès l'analyse, lue à l'ouverture |
| 1.78 | 2026-10-14 | Nouveau § 22.9 : `en position libre :`, la place au pixel (`à 96 de la gauche et 16 du haut`) et la taille (`large de`, `haut de`) des éléments du bloc ; révise `docs/ecrans.md`, § 3.3 |
| 1.79 | 2026-10-14 | § 22.8 : l'image calculée, `l'image de …`, relue avant chaque attente ; absente, elle ne montre rien |
| 1.80 | 2026-10-14 | § 11.4 : la traduction de la forme compacte retrouve les articles par le genre (champs, variables, entités, colonnes choisies) ; le littéral écrit sans article reste tel quel |
| 1.81 | 2026-10-14 | § 22.9 : un bloc se place et se dimensionne dans un bloc libre (`le groupe « Adresse », à 16 de la gauche et 8 du haut :`), et garde sa règle à l'intérieur ; une zone sur plusieurs lignes prend toute la hauteur dite |
| 1.82 | 2026-10-14 | Nouveau § 22.10 : `les onglets :` et `l'onglet « … » :` |
| 1.83 | 2026-10-15 | Nouveau § 22.11 : `le graphique des … conservés, par …`, `avec la somme de …`, `en secteurs` |
| 1.84 | 2026-10-15 | Nouveau § 22.12 : `le menu « … » :`, un bloc de boutons au premier niveau de l'écran, dessiné en tête |
| 1.85 | 2026-10-15 | § 8 : l'aide à la saisie dans un écran, débuts d'élément selon le bloc ouvert, suites d'une tournure commencée, états accordés, place et taille |
| 1.86 | 2026-10-15 | Une heure comme valeur de départ, d'une zone d'écran (§ 22.1) et d'un champ (§ 16.7) : « un début (heure), 08:00 au départ » |
| 1.87 | 2026-10-15 | § 11.4 : la traduction de la forme compacte rend aussi leur article aux variables créées avec un article ; un paramètre reste nu |
| 1.88 | 2026-10-15 | § 11.4 : dans un `dont`, « = » devant une constante revient en « est » à la traduction de la forme compacte |
| 1.89 | 2026-10-15 | § 11.4 : à la traduction de la forme compacte, une branche d'une seule phrase « afficher » ou « rendre » revient en forme courte (`Si`, `Sinon`, `Cas`) |
| 1.90 | 2026-10-15 | § 8 : l'aide à la saisie après une liste ou un graphique, et les champs après « par » ; en-tête daté du 15 octobre |
| 1.91 | 2026-10-15 | § 22.1 : les zones de fichier et d'image, `un contrat (fichier)`, `une photo (image)` (elles étaient refusées, « viendra plus tard ») |
| 1.92 | 2026-10-15 | § 22.6 : `, en interrupteur`, une zone (vrai ou faux) montrée en interrupteur |
| 1.93 | 2026-10-16 | § 22.1 : des choix multiples, `des genres (genre)`, lus par les tournures du champ multiple (§ 16.13) sur l'écran, en mémoire |
| 1.94 | 2026-10-16 | § 22.5 : la taille du titre d'un écran, `« Recherche » en grand`, `en petit` |
| 1.95 | 2026-10-16 | § 22.6 : `, avec l'indication « … »`, une phrase sous le libellé d'une zone |
| 1.96 | 2026-10-16 | § 22.6 : `, avec recherche`, une zone d'objet où l'on tape, d'elle-même au-delà de vingt objets |
| 1.97 | 2026-10-16 | § 22.6 : les bornes d'une zone, `de 1 à 99`, `au moins`, `au plus`, en caractères pour un texte |
| 1.98 | 2026-10-16 | § 22.6 : `, masqué`, une zone de texte à la frappe cachée (un mot de passe), sa valeur jamais réaffichée |
| 1.99 | 2026-10-16 | Nouveau § 22.13 : les types `(courriel)`, `(téléphone)`, `(adresse web)`, des textes à la forme vérifiée, pour les champs comme pour les zones |
| 2.0 | 2026-10-16 | § 22.6 : `, avec l'unité « … » [devant]`, affichée près du champ, ôtée si on la tape |
| 2.1 | 2026-10-16 | § 22.6 : les zones en faute signalées toutes à la fois, un résumé en haut, un message sous chaque zone |
| 2.2 | 2026-10-16 | § 16.15 : une couleur d'étiquette par valeur permise, `« payée » en vert`, montrée en étiquette dans les listes |
| 2.3 | 2026-10-16 | Nouveau § 22.14 : le type `(mot de passe)`, rangé en empreinte (PBKDF2-HMAC-SHA256), illisible, vérifié par `correspond à` |
| 2.4 | 2026-10-16 | § 14.8 : le mois, le jour, le jour de la semaine d'une date, `en toutes lettres` ; § 14.6 : les jours ouvrables ; § 22.11 : le graphique par mois, semaine ou année |
| 2.5 | 2026-10-16 | Nouveau § 15.6 : un rapport, par l'extension (`.html`, `.pdf`), avec le titre et le total ; dans une application fabriquée, écrit dans Documents/<Nom> |
| 2.6 | 2026-10-16 | Nouveau § 15.7 : le type `(son)`, reconnu à sa signature, écouté dans le navigateur |
| 2.7 | 2026-10-17 | Nouveau § 15.8 : `, transcrite du mémo`, le texte d'un champ (son) rangé dans un champ de texte, par le transcripteur de l'interface |
| 2.8 | 2026-10-18 | § 16.15 et § 22.13 : dans les fiches, la valeur colorée en étiquette, le courriel en lien d'écriture, l'adresse web en lien ; rien ne change dans ce qu'on écrit. § 22.11 : la forme canonique garde la période (« par mois de l'échéance ») ; le formatage l'effaçait |
| 2.9 | 2026-10-18 | § 15.7 : l'écoute et l'enregistrement au micro dans l'atelier et les applications fabriquées ; le lecteur dans la fiche déduite, partout |
| 2.10 | 2026-10-18 | § 22.3 : les tableaux de la fiche déduite, un par lien qui désigne l'objet (tri, total, fiche au double-clic, « Nouvelle … ») ; § 16.1 : `, hors des fiches,` |
| 2.11 | 2026-10-18 | § 19 : le formulaire demande le courriel, le téléphone et l'adresse web (il les prenait pour des liens, et sautait les facultatifs) |
| 2.12 | 2026-10-18 | § 22.6 : un événement qui teste la présence d'une zone ne l'exige pas d'emblée ; § 4.4 : `suivi` après `de` est un mot de nom ; § 22.3 : la ligne « Total » marquée, ni sélectionnée ni choisie |
| 2.13 | 2026-10-18 | § 22.5 : la couleur libre en hexadécimal, le fond (clair imposé), la police embarquée, le logo SVG ; les couleurs dérivées et les contrastes vérifiés à l'analyse (charte 1.60) |
| 2.14 | 2026-10-18 | § 22.6 : `, principal`, le bouton de l'action attendue, à la couleur d'accent |
| 2.15 | 2026-10-18 | § 22.6 : `, en tuile`, `, avec la description « … »`, `, avec l'image « … »` ; des tuiles qui se suivent forment une grille |
| 2.16 | 2026-10-18 | § 22.5 : `et la signature « … »`, une image en bandeau en tête de chaque écran ; le logo devient alors l'icône seule |
| 2.17 | 2026-10-18 | § 22.6 : `, à côté du titre` ; § 22.5 : « en grand » vaut 40 px en gras (il valait 22 px, calculé sur la mauvaise police) |
| 2.18 | 2026-10-18 | § 22.7 : `le groupe « … », en couleur inversée :`, un panneau à l'encre de l'accent, son texte et ses zones en clair |
| 2.19 | 2026-10-18 | § 22.1 : `, en cartes`, une liste dont chaque objet est une carte (la première colonne en grand, les étiquettes à droite, la deuxième colonne dessous, les autres jointes par « · ») ; un clic choisit la carte |
| 2.20 | 2026-10-18 | § 22.1 : `le texte « … », en petites capitales`, un surtitre ou un titre de section |
| 2.21 | 2026-10-19 | Nouveau § 2.4 : les sigles (`un numéro RCC`), insensibles à la casse comme tout nom, en capitales dans les libellés et la forme canonique, rangés dans le module (`SIGLES`) ; § 16.4 : `le cabinet conservé`, sans `dont` ; § 22.3 : un événement qui a demandé « Fermer l'écran. » puis échoue (une règle violée) laisse l'écran ouvert, avec le motif |
| 2.22 | 2026-10-19 | § 22.3 : « Modifier » et « Nouvelle … » d'une fiche déduite vérifient les règles du § 16.14 ; refusée, la saisie s'annule et la fiche relue montre le motif (corrige un contournement présent depuis l'arrivée des règles, le 28 septembre 2026) |
| 2.23 | 2026-10-19 | § 16.7 : `anciennement « … »`, le renommage d'un champ d'entité (simple, multiple, fichier) ou d'une entité, sans perte ; le refus d'un champ disparu propose la mention |
| 2.24 | 2026-10-19 | § 22.1 : le texte calculé, `le texte` suivi d'une valeur, relu avant chaque attente (instruction `ÉCRAN_TEXTE`) ; compacte `_texte_calculé` |
| 2.25 | 2026-10-19 | Nouveau § 22.15 : les documents, `Le document … montre :`, la valeur `le document de …` (un PDF), `Ouvrir le document … .` |
| 2.26 | 2026-10-19 | § 22.15 : `Ouvrir` un fichier (`Ouvrir le justificatif du frais.`), montré par le système ; opérande 2 de `DOCUMENT` |
| 2.27 | 2026-10-20 | Nouveau § 22.16 : `le code QR de t`, `le code QR suisse de t` (SIX IG 2.4), `un saut de ligne`, `x en format technique` |
| 2.28 | 2026-10-20 | § 22.16 : `la section paiement de …` dans un document : la section paiement et le récépissé d'une QR-facture, au bas de la dernière page |
| 2.29 | 2026-10-20 | Nouveau § 15.6 bis : `Sauvegarder la base dans « … ».` ; § 22.16 : `t en IBAN` (ISO 13616, mod 97) et la vérification des lignes d'un code QR suisse avant son image ou sa section paiement (SIX IG 2.4, § 4.2.2) |
| 2.30 | 2026-10-20 | § 15.6 bis : le coffre, `Sauvegarder la base dans … avec le mot de passe m.` (Argon2id, XChaCha20-Poly1305, Monocypher) ; `Restaurer la base depuis f [avec le mot de passe m].`, vérifiée, appliquée à la prochaine ouverture, l'ancienne base gardée ; `~/` et les dossiers qui manquent, pour toutes les écritures |
| 2.31 | 2026-10-20 | Trois corrections : une parenthèse qui contient un `dont` est un calcul (`(le nombre de notes conservées dont la date ≥ d) + 1` était refusé, la comparaison du `dont` la faisant prendre pour un groupe logique) ; une action dont le nom finit par `d'un` (`Pour acquitter le solde d'un relevé :`) s'appelle avec `de`, `du`, `des` ou `d'`, et s'imprime ainsi (son nom se rangeait avec l'élision, et aucun appel ne le trouvait) ; § 22.14 : `est présent`, `est absent` sur un mot de passe |
| 2.32 | 2026-10-20 | § 22.6 : `, avec le choix d'un dossier`, une zone (texte) que remplit le sélecteur de dossiers du système |
| 2.33 | 2026-10-20 | § 22.5 : le logo et la signature calculés, `le logo (…)` ; le titre d'un écran calculé, `L'écran d'accueil, (…) en grand, montre :` ; forme compacte : `(_le cabinet _conservé).logo << x` se relit (la ligne était refusée), et « du cabinet conservé » remplace « de (le cabinet conservé) » à la traduction |
| 2.34 | 2026-10-21 | § 22.6 : `, large de 12 caractères` ; forme canonique : le membre gauche d'une opération qui finit par un `dont` garde ses parenthèses (la 2.33 les retirait après `de` : « l'arrondi de (… dont …) − (…) » ne se relisait plus) ; un appel avec ses arguments, suivi d'un autre argument, garde les siennes ; la forme compacte traduite retrouve l'article des calculs et des propriétés (« la longueur du t », « le carré de 7 ») et élide devant un « h » muet (« l'heure ») |
| 2.35 | 2026-10-21 | Forme compacte sans perte : `_alors _court`, `_sinon _court` (une branche courte), `_alors _bloc` (un bloc que la règle du guide raccourcirait) ; `x _est v` (l'égalité écrite en mots ; un `=` compact reste `=`) ; `_nu x` (un nom écrit sans article qu'une variable recevrait), `_le x`, `_la x`, `_l' x` (un article écrit qu'un paramètre ne recevrait pas) ; un collage en argument garde ses parenthèses (`de ("Rappel " suivi du numéro)`) |
| 2.36 | 2026-10-21 | § 16.7 : un champ retiré sans aucune valeur présente part ; § 22.2 : `Faire comme un clic sur « B ».`, le clic par libellé dans le navigateur, le clic qui suit une zone et la touche Entrée dans l'atelier ; forme compacte : `_le patient.nom`, `_nu date.année`, `_la s.date << …`, `_absente` |
| 2.37 | 2026-10-21 | Nouveau § 16.18 : la base chiffrée, `La base est chiffrée.` (SQLite3 Multiple Ciphers, la clé enveloppée dans `base.grymd.clés`, le mot de passe demandé par l'exécution), `Changer le mot de passe de la base pour n, l'ancien étant a.` ; § 15.6 bis : le coffre de format 2 d'une base chiffrée, la restauration sans rien en clair sur le disque ; le secours : la clé de secours du développeur, `clé-de-secours.pub` dans le projet, l'enveloppe de secours, « Rouvrir une base avec la clé de secours » |
| 2.38 | 2026-10-21 | § 9.2 : un argument d'action logique entre parenthèses, `(a > 1 et b ≤ 15)`, est un groupe logique (il était refusé : la parenthèse attendait un calcul) |
| 2.39 | 2026-10-22 | § 15.8 : le modèle de transcription entre dans toute application fabriquée, qu'elle transcrive ou non (il n'entrait que dans celles qui déclarent un champ transcrit) ; `grym-atelier --transcrit` disparaît |
| 2.40 | 2026-10-22 | Nouveau § 22.17 : le type `(secret)`, rangé tel quel dans une base chiffrée, illisible sauf pour être envoyé, caché partout ; § 22.6 : les propositions d'une zone `avec recherche` sans égard aux majuscules ni aux accents |
| 2.41 | 2026-10-22 | § 22.15 : `Ouvrir` une adresse web, dans le navigateur du système (atelier, application fabriquée) |
| 2.42 | 2026-10-23 | Nouveau § 22.18 : l'envoi à un service web (POST JSON), la lecture (GET), `le champ « a.b » de r` ; un secret se lit dans un envoi ; HTTPS obligatoire, sauf vers la machine elle-même |
| 2.43 | 2026-10-23 | § 22.18 : `avec :` met des valeurs dans l'adresse d'un envoi ; `et de :` ou `et du tableau :` en donne le corps, un objet ou une liste JSON |
| 2.44 | 2026-10-23 | Correction : `suivi du nombre de séances conservées […]` se lit comme `le nombre de …` ; « du » donnait déjà l'article, et l'analyse cherchait un nom « nombre de séances conservées » |
| 2.45 | 2026-10-23 | § 22.18 : la forme compacte de l'envoi, de la lecture et du champ JSON (`_envoi`, `_lecture`, `_avec`, `_et_de`, `_et_du_tableau`, `_fin`, `_champ`) |
| 2.46 | 2026-10-23 | Nouveau § 22.19 : le XML déclaré (`Le XML de x, « racine », montre :`, éléments, attributs, contenus, boucles ; `le XML de x`, un fichier UTF-8 échappé) |
| 2.47 | 2026-10-23 | Un nom d'écran refuse le trait d'union, comme tout nom (§ 1.4) : « l'écran des rendez-vous » cassait la forme canonique et la forme compacte ; la forme canonique garde les sigles des noms d'écran et des colonnes (« l'écran des SMS », « le numéro GLN ») et l'accord écrit d'un nom au genre inconnu (« l'ordonnance ouverte est présente ») ; `tests/formes` : chaque projet du dépôt, forme canonique stable et aller-retour compact |
| 2.48 | 2026-10-23 | Les choix d'une zone se filtrent : `un patient (patient dont l'archivage est absent)`, la condition d'une recherche (§ 16.4), évaluée à l'ouverture de l'écran ; la forme compacte du XML déclaré (§ 22.19) |
| 2.49 | 2026-10-23 | Un champ d'entité s'abandonne : `un code d'accès (mot de passe), abandonné` ; il n'est plus lisible, et la migration efface ses valeurs, même conservées, en le disant (« champ « code d'accès » abandonné ; 1 valeur effacée ») ; la ligne reste, pour les bases pas encore lancées (format du bytecode 71) ; l'adresse d'un envoi s'écrit par un nom, sans parenthèses : `l'envoi au service de :`, `la lecture du service avec :` |
| 2.50 | 2026-10-23 | Correction : les colonnes d'une liste (`avec a, b et c`) s'arrêtent après la colonne qui suit « et » ; un texte calculé placé ensuite (`le texte la note`) était lu comme une colonne de plus |
| 2.51 | 2026-10-23 | Corrections trouvées par le balayage de tous les programmes des essais (tests/balayer, dans make test) : la cible d'une modification accepte un calcul à argument littéral (`L'archivage de la fiche nommée de « A » devient …`) ; la forme compacte réécrit le champ du résultat d'un calcul (`f(r).champ << v`), garde « ans » (`_écart_ans`), écrit « l'image du fichier », n'ouvre plus de colonnes vides avant `_titre`, lit une zone multiple (`_des genres (genre)`), `_gagne` sur un objet cherché, `_écran` seul, et une remarque entre deux `_cas` ; `grym-arbre` ne plante plus sur `Ouvrir le n.` |
| 2.52 | 2026-10-24 | La signature d'un écran : 26 pixels de haut en longueur, jusqu'à 125 pour une image plus carrée (un logo en lettrage), environ 208 pixels de large |
| 2.53 | 2026-10-24 | § 15.6 bis : `Sauvegarder la base dans « p », une par jour passé.` : la copie datée `p-AAAA-MM-JJ-HHMM`, et des jours passés seule la dernière de chacun reste |
| 2.54 | 2026-10-24 | § 14.7 : une heure saisie se tape aussi `13h30`, `13.30`, `1330`, `930`, `13h` |
| 2.55 | 2026-10-24 | § 22 : une action « , dans l'écran X » lit et écrit les zones de cet écran, comme ses événements |
| 2.56 | 2026-10-24 | § 22 : les parties (`La partie « X » montre :`, `la partie « X »` dans un écran ou un document) |
| 2.57 | 2026-10-24 | § 9 : une action peut rendre (`Rendre …`), reçu à l'appel par `, qui donne la q` |
| 2.58 | 2026-10-24 | § 22 : une action « , dans l'écran a ou l'écran b » ; ses zones vérifiées dans chaque écran nommé |
| 2.59 | 2026-10-25 | § 16.7 : un programme mis à jour qui va changer les tables d'une base qui a des données en fait d'abord une copie, « … avant mise à jour AAAA-MM-JJ HHMM.grymd » (et ses clés), à côté, que rien n'efface ; sans elle, rien ne change |
| 2.60 | 2026-10-25 | § 22.18 : `l'en-tête « … » :` dans un envoi ; `Pour chaque … du champ « … » de r`, `le nombre d'éléments du champ « … » de r` ; `en date locale`, `en heure locale`, `en format universel` |
| 2.61 | 2026-10-26 | Nouveau § 21.9 : les bibliothèques, `Utiliser la bibliothèque « Suisse santé », version 2.`, dans `bibliothèques/`, à côté du programme ; `Cette bibliothèque est « Suisse santé », version 2.` en tête de son entrée ; version exacte, une seule par application ; ni entité ni règle ; ses fichiers restent dans son dossier |
| 2.62 | 2026-10-26 | § 21.9 : les fichiers internes d'une bibliothèque, `Ce fichier est interne à sa bibliothèque.` (forme compacte `_interne`) ; leurs calculs et actions ne se voient que dans les fichiers de la bibliothèque, et le message le dit |
| 2.63 | 2026-10-26 | § 22.19 : le XML à valeurs, `Le XML de quittance d'une chose tarifée, « quittance », montre :` et `le XML de quittance de la facture` (formes compactes `_xml quittance(_une chose_tarifée) « … »`, `_xml quittance(f)`) ; un XML déclaré dans un fichier utilisé ou une bibliothèque se voit dans le programme ; § 21.9 : une bibliothèque déclare des XML |
| 2.64 | 2026-10-27 | § 4.5 : `t sans « . »`, chaque occurrence retirée d'un texte (instruction `RETIRER`) ; forme compacte `_sans` |
| 2.65 | 2026-10-29 | § 22.6 : `, à confirmer par « … »` [`en tapant « … »`] sur un bouton : une question avant l'événement, Annuler par défaut, un mot à taper pour l'irréversible ; forme compacte `_confirmer`, `_en_tapant` |
| 2.66 | 2026-10-31 | § 22.1 : `, par date puis heure`, un second tri dans une liste d'écran ou de document ; forme compacte `_par date _puis heure` |
| 2.67 | 2026-10-31 | § 22.6 : `, à confirmer par « … » si l'écran est modifié`, une question seulement après une saisie pas encore écrite dans la base ; forme compacte `_si_modifié` ; § 22.2 : Échap vaut un clic sur « Fermer » dans l'atelier et une application fabriquée |
| 2.68 | 2026-11-01 | § 22.6 : `, au clavier « N »` sur un bouton : Cmd+N sous macOS, Ctrl+N ailleurs, comme un clic, tant que l'écran est dessus et le bouton visible et actif ; lettres du système et de l'édition refusées, un raccourci par bouton dans un écran ; `accesskey` dans le navigateur ; forme compacte `_clavier « N »`. |
| 2.69 | 2026-11-01 | Nouveau § 22.20 : `une écoute « … »`, `, coupée après N secondes de silence`, `Quand une écoute capte un son …` et `Quand l'écoute « … » capte un son …`, avec `le son capté` et `l'écoute` ; formes compactes `_écoute`, `_silence`, `_quand _capte` |
| 2.70 | 2026-11-01 | Nouveau § 22.21 : `les écoutes des … conservés` (`dont`, `par`, `, coupées après N secondes de silence`), un bandeau par objet nommé par sa clé, relu avant chaque attente ; `Quand un poste capte un son …` avec l'objet et `le son capté` ; formes compactes `_écoutes`, `_quand _capte _un …` |
| 2.71 | 2026-11-01 | Nouveau § 22.22 : `Signaler « … » [puis x].`, un message qui ne bloque rien, montré après un événement réussi, oublié s'il échoue ; hors écran, comme `Afficher` |
| 2.72 | 2026-11-01 | Nouveau § 22.23 : `, destructif`, un bouton qui retire ou efface, dessiné en rouge ; jamais principal |
| 2.73 | 2026-11-01 | § 22.1 : `puis heure décroissant`, le second tri d'une liste à l'envers aussi |
| 2.74 | 2026-11-01 | Nouveau § 22.24 : `, en agenda selon …`, une liste en grille horaire (date, heure, durée ; la grille : jours et plages) ; `Quand on déplace un … dans l'écran …`, avec `la date d'arrivée` et `l'heure d'arrivée` de l'écran |
| 2.75 | 2026-11-01 | § 22.24 : `Quand on choisit un créneau dans l'écran …`, un clic dans un agenda hors des objets, avec la date et l'heure d'arrivée |
| 2.76 | 2026-11-01 | § 13.2 : « un nouvel » aussi devant un h muet (« un nouvel horaire »), comme l'imprimeur l'écrivait déjà ; un h aspiré garde « un nouveau » (« un nouveau hangar ») ; « un nouveau horaire » reste accepté |
| 2.77 | 2026-11-01 | § 22.24 : l'agenda dans le navigateur, sans JavaScript : la grille en quarts d'heure, un clic pour un créneau, « Déplacer » puis un quart d'heure pour déposer |
| 2.78 | 2026-11-01 | § 15.8 : `le morse de x` (le texte lu dans un son, ou absent) et `la transcription de x` (sur demande) ; le cœur lie la bibliothèque mathématique du C |
| 2.80 | 2026-11-02 | § 15.8 : `Amorcer la transcription avec t.`, le vocabulaire attendu par le transcripteur |
| 2.81 | 2026-11-02 | § 4.5 : `t en remplaçant « a » par « b »` (instruction `REMPLACER`) ; `Pour chaque mot parmi les mots de t`, `le nombre de mots de t` (instruction `MOTS`) |
| 2.82 | 2026-11-03 | Nouveau § 22.25 : `la réponse de p à q, à v bauds` (instruction `PORT_SERIE`, src/serie.c) et `Pour chaque p parmi les ports série :` (instruction `PORTS`) ; formes compactes `_réponse_port`, `_question`, `_vitesse`, `_bauds`, `_ports_série` |
| 2.83 | 2026-11-03 | § 15.2 : `t en fichier`, un texte en fichier UTF-8 à enregistrer (conversion 14) ; forme compacte `_en_fichier` |
| 2.84 | 2026-11-04 | § 22.18 : `le champ « … »` et ses listes lisent un fichier et du XML (racine omise, préfixes ignorés, `élément@attribut`, un élément seul comme une liste d'un) ; § 22.16 : la référence du créancier (SCOR, ISO 11649) vérifiée dans un code QR suisse, et la ligne de référence vide avec NON |
| 2.85 | 2026-11-05 | § 15.6 bis : `Vérifier la sauvegarde f [avec le mot de passe m].` et `la dernière sauvegarde de « p »` ; § 16.19 : l'entité journalisée (`, journalisé,`), `l'historique de x` et `l'historique de la base` |
| 2.86 | 2026-11-05 | § 15.8 : `Amorcer la transcription avec t, en toute langue.` (la langue parlée reconnue par Whisper) et `la langue reconnue de t` (sept langues, par les mots outils) |
| 2.87 | 2026-11-06 | § 16.19 : `Signer le journal avec t.`, l'auteur des lignes du journal ; l'historique le montre |
| 2.88 | 2026-11-06 | § 22.26 : `les décodages WSJT [du port p]`, les décodages de WSJT-X reçus en UDP, en textes JSON |
| 2.89 | 2026-11-06 | Nouveau § 13.10 : les listes (`Les étapes valent la liste … .`, `une liste vide`), qui gardent leurs doublons ; `gagnent` ajoute à la fin, `perdent` retire la première occurrence ; les rangs, dans une liste ou un ensemble : `le 3e élément des …`, `le 1er`, `le premier`, `le dernier`, `l'élément de rang k des …` (instructions `LISTE` et `RANG`) ; les ordinaux `1er`, `2e` au lexeur, et la bonne graphie proposée pour `3ème`, `2nd`… ; `les mots de t`, `les éléments du champ « … » de r` et `les décodages WSJT` se rangent dans une liste ; égalité des listes dans l'ordre ; correction : `les e = les f` entre deux ensembles plantait la machine. Formes compactes `_liste`, `_liste_vide`, `_élément(l ; k)`, `_premier(l)`, `_dernier(l)`, `_mots t`, `_éléments « c » r` |
| 2.90 | 2026-11-06 | Nouveau § 4.6 : découper un texte. `le 3e caractère de t`, `le premier`, `le dernier`, `le caractère de rang k de t` ; `les 5 premiers caractères de t`, `les 3 derniers` (un texte plus court se rend entier) ; `les caractères 3 à 7 de t` ; `t après « x »`, `t avant « x »` (la première occurrence ; absente, une erreur) ; `la position de « x » dans t` (0 si absente) ; `les morceaux de t séparés par « ; »`, une liste, vides compris (instruction `EXTRAIRE`). Comparaison exacte. Compatibilité (charte, art. 13.2) : `après` et `avant` suivis d'un texte, d'un nom entre crochets ou de `le`, `la`, `l'`, et `séparés par`, ferment désormais un nom, comme `sans` depuis la 2.64 ; aucun programme du dépôt n'en avait dans un nom. Formes compactes `_caractère`, `_premier_caractère`, `_dernier_caractère`, `_premiers_caractères`, `_derniers_caractères`, `_caractères`, `_après`, `_avant`, `_position`, `_morceaux` |
| 2.91 | 2026-11-06 | Nouveau § 13.11 : les tables (`la table où « a » vaut 1 et « b » vaut 2`, `une table vide`) ; `Dans les t, k vaut v.` crée une clé, `devient` la modifie, l'un refusant ce que l'autre fait ; `la valeur de k dans les t` (clé absente : erreur) ; `les clés des t`, `les valeurs des t`, des listes ; `perdent` retire une clé ; `le nombre de t`. Type de valeur `V_TABLE` ; instructions `TABLE`, `TABLE_LIRE`, `TABLE_ECRIRE`, `TABLE_LISTE`. `dans` devient un mot de construction en tête de phrase. Correction : un symbole déclaré n'initialisait pas sa marque de liste (2.89), si bien qu'un ensemble pouvait garder ses doublons selon l'état de la mémoire. Formes compactes `_table`, `_table_vide`, `_dans t k _vaut v`, `_valeur(k ; t)`, `_clés(t)`, `_valeurs(t)` |
| 2.92 | 2026-11-06 | Nouveau § 18.2 : les sortes d'échec (`division`, `nombre`, `lecture`, `absence`, `clé`, `rang`, `texte`, `unicité`, `lien`, `fichier`, `réseau`, `port`, `saisie`, `refus`, `autre`), promises, contrairement au motif ; `En cas d'échec de réseau ou de fichier :`, plusieurs blocs, le premier qui convient ; sans bloc qui convienne, l'échec remonte ; `la sorte de l'échec`. Instructions `SORTE_ECHEC` et `RELANCER`. Formes compactes `_échec_de`, `_sorte` |
| 2.93 | 2026-11-06 | Nouveau § 4.7 : le texte suivant (`Le rappel vaut le texte suivant :` puis un bloc plus indenté, texte brut, `{expression}` insérée comme par `suivi de`, `{{` et `}}` pour une accolade) ; le lexeur découpe le bloc (jetons `GABARIT_DÉBUT`, `GABARIT`, `ACC_OUV`, `ACC_FERM`, `GABARIT_FIN`), aucune instruction nouvelle. Forme compacte `_texte_suivant` |
| 2.94 | 2026-11-06 | Nouveau § 22.27 : les courriels, `Envoyer un courriel à a, avec l'objet o, depuis m, et le texte t.` ; la messagerie, un objet aux champs `adresse de courriel`, `serveur de courriel`, `compte de courriel`, `clé de courriel (secret)` ; SMTP sur TLS implicite (465), `AUTH PLAIN` ; dans l'atelier et les applications fabriquées (fonction d'interface `courriel`, `src/atelier/courriel.cpp`) ; instruction `COURRIEL` ; refus de sorte `réseau`. Forme compacte `_envoyer_courriel` |
| 2.95 | 2026-11-06 | Nouveau § 3.7 : `la racine carrée de x` (exacte si finie, sinon 28 chiffres, comparée à Python) ; `le plus grand de a et de b`, `le plus petit de …` ; sur une liste ou un ensemble, `la somme des notes`, `la moyenne des notes`, `le plus grand élément des notes`, `le plus petit élément des notes` (instruction `MATH`). Formes compactes `_racine`, `_plus_grand`, `_plus_petit`, `_somme`, `_moyenne`, `_plus_grand_élément`, `_plus_petit_élément` |
| 2.96 | 2026-11-06 | Nouveau § 13.12 : trier une liste ou un ensemble, `les notes triées`, `par ordre décroissant`, `les inscrits triés par nom [décroissant]` ; une nouvelle liste, tri stable ; les textes dans l'ordre de la base (collation `GRYM_TEXTE`, même fonction). Instruction `TRIER`. Forme compacte `_trier(l ; champ ; _décroissant)` |
| 2.97 | 2026-11-06 | § 13.12 : trier par un calcul, `les mots triés par le calcul longueur [décroissant]` ; le compilateur écrit une boucle qui appelle le calcul une fois par élément, puis `TRIER` range la liste selon ces clés (forme 4). Forme compacte `_trier(l ; _calcul c)` |
| 2.98 | 2026-11-06 | Nouveau § 9.3 bis : les paramètres facultatifs des actions, `Pour relancer un client, avec un délai de 30 :`, `Relancer le client, avec le délai de 10.` ; valeur de départ constante, recopiée à la compilation dans l'appel : la machine ne change pas. Formes compactes `_un délai << 30`, `relancer(c ; délai << 10)` |
| 2.99 | 2026-11-06 | Nouveau § 16.16 : les types nommés, `Un état est « à faire », « en cours » ou « fini ».` ; un champ `(état)` prend ses valeurs permises (§ 16.15) ; une variable `Le prochain (état) vaut …` est vérifiée à chaque écriture (`devient`, `qui donne`), à l'analyse pour un texte écrit en clair, à l'exécution sinon (instruction `PERMIS`). Formes compactes `_type _un état « a » ; « b »`, `_le p (état) << …` |
| 3.00 | 2026-11-06 | § 16.16 : les annotations (point 8 de l'audit) ; une variable ou un paramètre de formule se type, `(nombre)`, `(texte)`, `(date)`, `(heure)`, `(vrai ou faux)` ou un type nommé ; constante vérifiée à l'analyse, à la ligne de l'appel pour un argument ; le reste à l'exécution (`PERMIS`, étendu aux types simples ; en tête de formule pour un paramètre) |
| 3.01 | 2026-11-06 | Nouveau § 13.7 bis : les contrats d'aptitude, `Une chose payable doit savoir : le montant dû d'une chose payable.` ; une version abstraite, sans code ; à la fin de l'analyse, chaque classe qui adopte l'aptitude a une version concrète (la sienne, d'un ancêtre ou d'une aptitude), hors boucle interactive. Forme compacte `_contrat payable _calcul …` |
| 3.02 | 2026-11-06 | Nouveau § 15.9 (points 12 et 13 de l'audit) : `les lignes de x`, une liste, d'un texte ou d'un fichier en UTF-8 ; `le JSON de x`, une valeur écrite en JSON, récursivement (tables en objets, listes en tableaux). Instructions `LIGNES` et `JSON`. Formes compactes `_lignes x`, `_json(x)` |
| 3.03 | 2026-11-06 | § 22.2 : la minuterie (point 18 de l'audit), `Toutes les 10 secondes dans l'écran X :` ; instruction `MINUTERIE` avant `ÉCRAN_ATTENDRE`, fonction d'interface `ecran_minuterie`, événement `EVENEMENT_MINUTERIE` ; l'atelier attend à échéance, le navigateur renvoie son formulaire par `grym.js`. Forme compacte `_toutes_les 10 _secondes _dans X` |
| 3.04 | 2026-11-06 | Nouveau § 15.10 (point 17 de l'audit) : `la sortie de la commande « p » avec les arguments`, sans shell, 30 secondes au plus ; nouvelle sorte d'échec `commande` (§ 18.2). Console (Mac, Linux : fork et exec), atelier (QProcess) ; le navigateur et la console sous Windows refusent. Instruction `COMMANDE`. Forme compacte `_commande(« p » ; arguments)` |
| 3.05 | 2026-11-06 | Nouveau § 9.7 (point 20 de l'audit) : les scénarios, `Scénario « titre » :`, `Vérifier que condition.`, joués par `grym scenarios`, chacun seul sur une base neuve en mémoire, sans exécuter le programme ; sans écran pour l'instant. Instruction `VÉRIFIER`. Forme compacte `_scénario`, `_vérifier` |
| 3.06 | 2026-11-07 | Ménage (points 21 à 23 de l'audit, première part) : `vrai` et `faux` comme éléments d'un ensemble ou d'une liste (§ 13.9) ; sept fuites de mémoire préexistantes corrigées dans la machine et le serveur, que l'audit avait relevées (25 et 49 octets) : les dix programmes d'essai tournent désormais sans aucune fuite |
| 3.07 | 2026-11-07 | Plan du § 22 : ses 27 sections rangées par thème (un écran, son déroulement, son apparence, ses documents, les données sensibles, les échanges avec l'extérieur, le matériel et la radio), sans renumérotation, ses numéros étant cités par les messages, les essais et le code (point 21 de l'audit) |
| 3.08 | 2026-11-07 | § 9.7 : les gestes d'un scénario, `On écrit … dans le nom de l'écran …`, `On choisit …`, `On clique sur « B » dans l'écran …`, `On ferme l'écran …`, préparés avant `Ouvrir` et joués par un écran simulé dans la machine ; un geste impossible arrête le scénario à sa ligne. Instruction `GESTE`. Forme compacte `_on_écrit`, `_on_choisit`, `_on_clique`, `_on_ferme` |
| 3.09 | 2026-11-07 | § 9.7 : `On confirme.` et `On renonce.`, la réponse à un bouton à confirmer ; les documents dans un scénario, fabriqués sans PDF réel, et `les documents sortis` (les documents ouverts) ; la fiche reste refusée. Éprouvé sur une copie de Praxis : `régler` avec quittance immédiate. Instruction `DOCUMENTS_SORTIS` |
| 3.10 | 2026-11-07 | § 9.7 : `Ces scénarios éprouvent « Praxis ».`, un fichier de scénarios à côté du programme ; `vrai` et `faux` à l'appel d'un paramètre facultatif ; l'exemple `est parmi les documents sortis` de la 3.09 était faux (`parmi` ne vaut encore que dans `dont`), corrigé. Première suite : `projets/Praxis/Praxis/scenarios.grym`, neuf parcours d'argent |
| 3.11 | 2026-11-07 | § 9.7 : précision, un exemplaire gardé qu'on rouvre ne compte pas parmi `les documents sortis`. La suite de Praxis passe à quatorze scénarios : la facturation (une facture réunit les séances dues du patient et seulement elles ; refacturer rouvre la même), les relances numérotées, la facture au comptoir |
| 3.12 | 2026-11-07 | § 9.7 : les gestes de l'agenda, `On choisit le créneau du … à …` et `On déplace X au … à …` ; les créations de premier niveau du programme s'exécutent avant chaque scénario, son état de départ. La suite de Praxis passe à quinze scénarios : une séance prévue se déplace dans le calendrier. Formes compactes `_on_créneau`, `_on_déplace` |
| 3.13 | 2026-11-07 | § 3.1 : limites d'une expression ; plus de 1000 opérations enchaînées sont refusées (« Expression trop longue »), au lieu d'épuiser la pile du compilateur et de l'imprimeur. `grym traduire` signale que les deux formes sont désormais côte à côte |

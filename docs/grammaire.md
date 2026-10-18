# Grammaire littéraire de GrymoiR

Version 2.12 de la spécification, révisée le 18 octobre 2026. Tout ce qui suit est implémenté.
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
- Un texte tient sur une ligne.

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
- **Contenir.** `t contient p`, `t commence par p`, `t finit par p`, et leurs négations `ne contient pas`, `ne commence pas par`, `ne finit pas par`, sont des conditions. Elles ne tiennent compte **ni des accents ni de la casse**, comme le tri des recherches (§ 16.4) : `« Élodie Müller » contient « MUL »` est vrai. Un texte contient le texte vide. L'égalité, elle, reste exacte (`=`). Deux textes sont attendus : « « contient » compare deux textes, pas un nombre. »
- Dans une condition `dont` (§ 16.4), elles s'appliquent à un champ `(texte)` : `dont le nom contient « mül »`, `dont la ville ne commence pas par « b »`. Un champ absent n'est jamais retenu (§ 16.9). Un autre type : « « âge » : contient, commence par et finit par s'appliquent à un texte. »
- Aucun mot réservé : `contient`, `commence`, `finit` et `en` ne comptent qu'après une valeur, et seulement suivis de ce qu'ils annoncent (`par`, `pas`, un texte, une cible de conversion). L'aide à la saisie (§ 8) propose `contient`, `commence par`, `finit par` après une valeur dont le type texte est connu à l'analyse.
- Effacer ou remplacer une partie d'un texte, le découper, en extraire un morceau : reportés.
- En forme compacte : `t.longueur`, `t _en_majuscules`, `t _en_minuscules`, `t _en_nombre`, `t _en_nombre_entier`, `t _en_date`, `t _en_année`, `t _en_vrai_ou_faux` ; `t _inclut p`, `t _commence p`, `t _finit p`, et `_non (t _inclut p)`. `_contient` reste réservé au champ multiple (§ 16.13).

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
- Appel : le nom de l'action commence la phrase, suivi des arguments séparés par `et`. Chaque argument est une comparaison ou une expression ; un `et` logique dans un argument demande des parenthèses.
- Une action ne rend rien : `Rendre` y est une erreur.

### 9.3 Paramètres

- L'article indéfini déclare un paramètre et fixe son genre : `d'un nombre` (calcul), `un client` (action), `une remise` (féminin).
- Plusieurs paramètres : `d'un premier nombre et d'un second nombre`, `un montant et une remise`.
- Dans le corps, un paramètre se nomme comme tout autre nom : `nombre`, `le nombre`. Deux paramètres d'une même formule portent des noms distincts.

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
- Les listes ordonnées avec doublons et positions (`le 3e élément de …`) ne sont pas faites : décision du 12 octobre 2026, l'ensemble d'abord, la liste si un programme réel la demande.
- En forme compacte : `_les couleurs << « a » ; « b »`, `_les invités << _ensemble_vide`, `_les invités _gagnent m`, `_perdent`, `_deviennent`, `invités _contient m`, `_pour_chaque invité _parmi invités`, `_nombre_de invités`, `couleurs _jointes « , »`.

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
- **Le transcripteur** (étape 2, le 17 octobre 2026) : Whisper, par whisper.cpp (v1.9.5), dans l'atelier et les applications fabriquées, sur l'ordinateur, sans rien envoyer ; le modèle large-v3-turbo, sans contexte entre les passages (`-mc 0`) et sans découpe du son, configuration retenue après essai sur une assemblée de 71 minutes (complète, sans boucle, environ 12 fois le temps réel sur un Mac à puce Apple). Les phrases que Whisper invente dans les silences sont retirées. Un WAV se lit directement ; MP3, M4A, Ogg, WebM et FLAC passent par le décodeur de Qt. Le modèle (`ggml-large-v3-turbo-q5_0.bin`) est cherché dans l'application, puis dans le dossier de données de GrymoiR ; `GRYMOIR_MODELE_TRANSCRIPTION` en désigne un autre. Le modèle est inclus dans l'atelier (il transcrit hors ligne dès l'installation), et dans une application fabriquée seulement si son programme déclare un champ transcrit : 547 Mo de plus, que les autres applications n'ont pas à porter. - **La relecture** (étape 3b) : dans la zone de saisie où on lit le texte transcrit, **tous les nombres** sont surlignés, et les **mots dont Whisper doute** (probabilité sous 0,5) sont en plus soulignés ; corriger un mot douteux retire son doute. Les marques sont des caractères Unicode invisibles, rangés dans le texte même (U+2064 en tête : « transcrit » ; U+2062 et U+2063 autour d'un mot douteux), pour qu'elles suivent le texte partout, base, écrans et copies d'un champ à l'autre. Elles ne se voient pas dans une fiche, un rapport ou un PDF, et sont retirées d'un export CSV. Elles comptent dans `la longueur de` un texte, et une recherche `contient` qui chevauche le bord d'un mot douteux ne le trouve pas. En console et dans le navigateur, pas encore de transcription.
- Forme compacte : `_une note (texte) _transcrit mémo`.

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
- `le client conservé dont …` exige exactement un objet. Aucun, ou plusieurs : erreur, avec leur nombre.
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
| champ retiré, renommé ou retypé | refusé, avec un message qui dit combien de valeurs seraient perdues |
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
- Retirer un champ pour de bon, le renommer, ou lever les limites ci-dessus demandera une commande explicite de l'outil `grym`, à concevoir.
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

### 22.1 Déclaration

- `L'écran des compositeurs montre :`, au premier niveau, suivi de ses éléments séparés par des virgules, le dernier suivi d'un point. Un titre différent du nom se donne entre guillemets : `L'écran des compositeurs, « Nos compositeurs », montre :` ; sinon le titre vient du nom (« Compositeurs », « Accueil »).
- Éléments : `la liste des … conservés [dont …] [, par …] [, avec le …, le … et le …]` (recherche du § 16.4) ; `un bouton « … »` ; `le texte « … »` ; une zone de saisie déclarée comme un champ d'entité (`un pays (texte), « Suisse » au départ`, `, facultatif`) ; `côte à côte :` et `l'un sous l'autre :`, qui ouvrent un bloc indenté ; `le groupe « … » :` (§ 22.7) et `en position libre :` (§ 22.9), aussi.
- Colonnes par défaut : les champs simples, dans l'ordre de la déclaration ; un lien montre la clé de l'objet désigné ; fichiers, images et champs « plusieurs » restent hors de la liste.
- Une zone de fichier ou d'image *(1.91)* s'écrit comme le champ qu'elle remplit : `un contrat (fichier)`, `une photo (image), facultative`. On y choisit un fichier ; sa valeur est ce fichier, avec ses octets et son nom, prête à ranger dans un champ `(fichier)` ou `(image)`. Une zone d'image refuse un fichier qui n'est pas une image (« « notes.txt » n'est pas une image (PNG, JPEG, GIF ou WebP). »). Elle n'a pas de valeur de départ écrite.
- Des choix multiples *(1.93)* s'écrivent comme le champ multiple d'une entité (§ 16.13) : `des genres (genre)`, une case par objet conservé de l'entité, désigné par sa clé. La zone se lit avec les tournures du champ multiple : `Pour chaque genre de l'écran`, `le nombre de genres de l'écran`. Elle ne se fait que parmi les objets d'une entité conservée (« Des choix multiples se font parmi les objets d'une entité conservée : « des genres (genre) » ; « couleur » n'en est pas une. »). Pour ranger les choix dans un objet : `Pour chaque genre de l'écran : Les genres de o gagnent genre.` Forme compacte : `_des genres (genre)`.
- Un écran est un objet : ses zones de saisie sont ses champs, jamais conservés.

### 22.2 Événements

- `Quand on ouvre l'écran X :`, `Quand on clique sur « B » dans l'écran X :`, `Quand on choisit un c dans l'écran X :`, `Quand on change la zone dans l'écran X :`, `Quand on ferme l'écran X :`, suivis d'un bloc. Ce sont des actions (§ 9).
- `Quand on choisit un compositeur` nomme la ligne `compositeur` dans son corps. `le compositeur choisi de l'écran` désigne la ligne sélectionnée, ou `absent`. `le pays de l'écran` lit une zone ; `Le pays de l'écran devient … .` la modifie. `de l'écran` hors d'un événement est une erreur.
- Un bouton sans événement, un événement qui cite un bouton ou une zone inexistants, deux listes de la même entité dans un écran : erreurs d'analyse. La fermeture ne se refuse pas.

### 22.3 Ouvrir, fermer, transactions

- `Ouvrir l'écran X.` montre l'écran et attend jusqu'à `Fermer l'écran.` ; `Ouvrir la fiche de c.` ouvre l'écran déduit de l'entité de `c`. Dans un événement, l'écran s'ouvre par-dessus ; à sa fermeture, l'événement reprend.
- `Ouvrir` valide d'abord ce qui précède ; chaque événement est ensuite sa propre transaction ; après la fermeture, la suite forme une nouvelle transaction (charte, art. 7).
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

### 22.5 Apparence *(T3 : implémenté le 26 septembre 2026)*

```
Les écrans ont la couleur verte et le logo « logo.png ».
Les écrans ont la couleur grise.
Les écrans ont le logo « images/logo.png ».
```

- GrymoiR fixe l'apparence des écrans (polices, tailles, espacements, formes) ; le programme n'en règle que deux choses, une couleur d'accent et un logo (`docs/ecrans.md`, § 5). Le clair ou le sombre suit le système.
- Couleurs : `bleue` (par défaut, sans phrase), `verte`, `turquoise`, `violette`, `grise`. `la couleur` est féminin : « Accord : « verte » (la couleur). » Toute autre couleur : « Couleur attendue : bleue, verte, turquoise, violette ou grise. »
- L'un des deux réglages au moins ; la couleur d'abord, le logo ensuite, reliés par `et`.
- Le logo est un chemin écrit tel quel, relatif au dossier du programme. Il doit exister et être une image (PNG, JPEG, GIF, WebP) dès l'analyse, pour que l'aperçu de l'atelier le montre sans exécuter le programme : « Fichier « logo.png » introuvable ou illisible. », « « notes.txt » n'est pas une image (PNG, JPEG, GIF ou WebP). ». Il est relu à l'exécution.
- Une seule fois, au premier niveau, avant le premier `Ouvrir` ; sinon, erreur d'analyse. Comme `Les nombres s'affichent …` (§ 4.1), la phrase vaut pour tout le programme.
- Le logo s'affiche à gauche du titre : grand (64 pixels) sur un écran que le programme ouvre lui-même, sans écran dessous ; petit (24 pixels) sur un écran ouvert par-dessus. Il sert aussi d'icône à la fenêtre.
- Sans écran (`grym lancer`, ou un programme qui n'en ouvre aucun), la phrase est sans effet : un programme qui ne fait qu'afficher tourne normalement. `grym servir` applique la couleur et le logo à ses écrans.
- Forme compacte : `_écrans _couleur _verte _logo « logo.png »`, `_écrans _couleur _grise`, `_écrans _logo « logo.png »`.
- Aide à la saisie (§ 8) : en début de phrase, `Les écrans ont` et `Les nombres s'affichent` sont proposés ; puis `la couleur` ou `le logo`, les cinq couleurs, `et le logo`, un texte. Après `Les nombres s'affichent` : `à la suisse`, `à la française`, `sans séparateur`.
- La taille du titre d'un écran *(1.94)* : `L'écran de recherche, « Recherche » en grand, montre :`, ou sans titre donné, `L'écran de recherche, en petit, montre :`. `en petit` et `en grand` viennent du thème et valent pour tous les écrans ; sans eux, la taille du thème. Forme compacte : `_écran de_recherche « Recherche » _en_grand`.

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
- `, avec recherche` *(1.96)* : une zone qui désigne un objet conservé devient un champ où l'on tape, avec les objets qui contiennent la saisie pour propositions, `un membre (membre), avec recherche`. Au-delà de vingt objets, elle le devient d'elle-même, sans qu'on l'écrive ; vingt est un choix du projet, pas une norme. Une saisie qui ne désigne aucun objet est refusée. Une autre zone le refuse : « Seule une zone qui désigne un objet conservé se cherche ». Forme compacte : `_recherche`.
- Les bornes d'une zone *(1.97)*, comme les intervalles des boucles (§ 9) : `, de 1 à 99`, `, au moins 16`, `, au plus 50` pour un nombre ; les mêmes suivies de `caractères` pour un texte, et `, de 4 caractères` pour une longueur exacte. Bornes comprises. Une saisie hors bornes est refusée avec la règle (« « Quantité » va de 1 à 99. », « « Code » a 4 caractères. »). Une zone d'un autre type n'en a pas. Forme compacte : `_min 1 _max 99`.
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
- **Il se vérifie** : `x correspond au code du membre` (`à`, `au`, `à la`), vrai si le texte x est le mot de passe. La comparaison prend le même temps que les clés se ressemblent ou non.
- **Une zone `(mot de passe)`** est masquée d'elle-même ; elle garde son texte en mémoire le temps de l'événement, et ne le range nulle part : c'est en l'affectant à un champ d'entité qu'il devient une empreinte.
- Forme compacte : `x _correspond membre.code`.

---

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

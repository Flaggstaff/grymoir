# Grammaire littéraire de GrymoiR, v0.1

Version 1.4 de la spécification, révisée le 21 septembre 2026.
Référence : Charte de GrymoiR v1.6, art. 4, 9 et 12.
Toute modification passe par une révision numérotée.

Périmètre : nommer, calculer, afficher, décider (§ 5), définir des formules (§ 9), et le calcul des suites attendues (§ 8). Tout le reste attend les versions suivantes.

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
- L'indentation délimite les blocs (§ 5.4). Une tabulation en début de ligne est une erreur : « Tabulation en début de ligne : indentez avec des espaces. » Une ligne vide peut contenir des tabulations.

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
- Les mots réservés ne peuvent pas faire partie d'un nom écrit sans crochets : `vaut`, `devient`, `puis`, `est`, `et`, `ou`, `si`, `sinon`, `vrai`, `faux`, `rendre`, ainsi que l'élision `n'` devant `est`.
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
- Une division au quotient non fini (`1 ÷ 3`) est arrondie à 28 chiffres significatifs, au plus proche, avec arrondi au pair en cas d'égalité (arrondi bancaire). Un quotient non fini ne tombe jamais exactement sur une moitié : la règle d'égalité ne sert pas en v0.1, elle vaudra pour les arrondis à venir (arrondi monétaire).
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
- `quitter`, ou la fin de l'entrée (Ctrl+D, Ctrl+Z sous Windows), termine la boucle.

---

## 4. Afficher

```
Afficher le total.
Afficher « Total à payer : » puis le total.
```

- `Afficher` suivi d'un ou plusieurs éléments reliés par `puis`.
- Un élément est un texte ou une expression. En v0.1, un texte n'apparaît que dans `Afficher`.
- Les éléments s'affichent séparés par une espace, suivis d'un saut de ligne : `Afficher « Total : » puis 3.` produit `Total : 3`.

### 4.1 Format des nombres affichés

- Réglage par application.
- Défaut : style suisse, apostrophe pour les milliers et virgule décimale (`1'234,50`). Un nombre négatif porte le signe `−` (U+2212) : `−1'000`.
- Alternative : style français, espace insécable pour les milliers (`1 234,50`).
- Le mécanisme du réglage sera défini avec les applications (v1.0). En v0.1, seul le défaut existe.

---

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
             | calcul | action | rendre | appel-action ;
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
expression   = terme { ( "+" | "−" ) terme } ;
terme        = unaire { ( "×" | "÷" ) unaire } ;
unaire       = "−" unaire | puissance ;
puissance    = base [ "^" unaire ] ;
base         = nombre | [ article ] ( nom | "[" nom "]" ) [ arguments ] | "(" expression ")" ;
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
| Nom inconnu proche d'un nom connu | « `totl` inconnu, vouliez-vous `total` ? » |
| `vaut` sur un nom existant | « `total` existe déjà (ligne 3). Pour le modifier, écrivez : Le total devient … » |
| `devient` sur un nom inconnu | « `total` n'existe pas. Pour le créer, écrivez : Le total vaut … » |
| Genre contradictoire | « `total` est masculin (déclaré ligne 3) » |
| Division par zéro | « Division par zéro. » (position de l'opérateur `÷`) |
| Résultat démesuré | « Nombre trop grand : un résultat est limité à 1000 chiffres. » |
| Exposant non entier | « Exposant non entier : en v0.1, la puissance n'accepte qu'un exposant entier (2 ^ 3, 2 ^ −1). » |
| Phrase sans point final hors boucle interactive | « Point final manquant (ligne 5) » |
| Jeton inattendu | « « 2 » inattendu, attendu : un opérateur ou un point final. » |
| Article sans nom | « Nom attendu après « le ». » |
| Remarque mal placée | « Une remarque doit commencer une ligne : passez à la ligne avant « Remarque : ». » |
| Parenthèse non refermée | « Parenthèse fermante manquante : la parenthèse ouverte ligne 2, colonne 11 n'est pas refermée. » |
| Mot réservé dans un nom | « « et » est un mot réservé : pour l'utiliser dans un nom, écrivez [frais de port et emballage]. » |
| Accord | « « quantité » est féminin (déclaré ligne 1) : écrivez « positive ». » |
| Contraction | « « à le » s'écrit « au ». » |
| Mélange de `et` et `ou` | « « et » et « ou » mélangés sans parenthèses : écrivez « (A et B) ou C » ou « A et (B ou C) » selon le sens voulu. » |
| Condition arithmétique | « Condition attendue après « Si » : une comparaison, par exemple « Si le total est supérieur à 100 ». Un calcul seul n'est ni vrai ni faux. » |
| Condition non booléenne (exécution) | « Condition ni vraie ni fausse : la valeur est un nombre. » |
| Indentation | « Indentation inattendue : seul un bloc ouvert par « : » s'indente. » |
| `Sinon` mal placé | « « Sinon » doit être aligné sur son « Si ». » |
| Variable lue dans un calcul | « « taux » n'est pas visible dans un calcul : un calcul ne voit que ses paramètres. Passez la valeur en paramètre. » |
| Nombre d'arguments | « « carré » attend 1 paramètre, 2 donnés. » |
| Calcul sans `Rendre` final | « Le calcul « valeur » doit se terminer par « Rendre … ». » |
| Affichage dans un calcul | « Un calcul n'affiche rien : il rend une valeur. Pour afficher, écrivez une action. » |
| Récursion sans fin (exécution) | « Trop d'appels imbriqués : plus de 1000. Une formule s'appelle-t-elle sans fin ? » |

Chaque message est précédé du fichier, de la ligne et de la colonne (charte, art. 8) : `facture.grym:7:18 : erreur : Division par zéro.`

---

## 8. Suites attendues

À chaque position, l'analyseur calcule l'ensemble exact des suites valides (charte, art. 9).

- Catégories : début de phrase (`Le`, `La`, `L'`, `Afficher`, `Si`, `Pour`, `Remarque :`, et les actions déclarées, avec une majuscule), nombre, nom déclaré, nouveau nom, parenthèse, négation, texte, `vrai` et `faux`, opérateurs, comparaisons (`est`, `n'est pas`, symboles), `et` et `ou`, parenthèse fermante, `vaut` et `devient`, `,` et `:` après une condition, `puis`, point final.
- Après `est`, les tournures accordées au genre du sujet (`supérieure à`, `positive`…) ; après `supérieur`, `à`, `au` ou `ou`.
- Après le nom d'un calcul, `de` ou `du`. Dans un calcul, seuls ses paramètres, ses noms locaux et les formules sont proposés.
- S'y ajoutent les mots qui prolongent un nom composé déclaré : après `prix`, `unitaire` si `prix unitaire` existe.
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

- **Un calcul ne voit que ses paramètres, ses noms locaux et les formules.** Les variables du programme lui sont invisibles, il n'affiche rien et n'appelle pas d'action. Même entrée, même résultat.
- **Une action voit et modifie les variables du programme.** Elle peut afficher et appeler d'autres formules.
- Un nom créé dans une formule lui est local et disparaît à la fin de l'appel.

### 9.5 Règles de définition

- Une formule se définit au premier niveau du programme, hors de tout bloc, et avant son premier usage.
- Une formule peut s'appeler elle-même (récursion). Au-delà de 1000 appels imbriqués, l'exécution s'arrête avec une erreur.
- Un calcul ne se modifie pas (`devient` est une erreur) ; un nom de formule ne se réutilise pas.
- Une action qui échoue n'écrit rien (charte, art. 7) : comme toute exécution, elle est annulée par le journal (docs/vm.md, § 6).
- Dans la boucle interactive, une formule définie par une saisie ratée n'existe pas.

---

## Journal des révisions

| Version | Date | Changement |
|---------|------|------------|
| 1.0 | 2026-09-20 | Spécification initiale : nommer (`vaut` / `devient`), calculer (décimal exact, arrondi bancaire à 28 chiffres), afficher (style suisse par défaut) |
| 1.1 | 2026-09-21 | Puissance avant négation (`−2 ^ 2` = `−4`). Guillemets `“ ”` et tiret `–` acceptés ; trait d'union toujours opérateur. Règles des noms (mots réservés, article initial interdit). Remarques en début de ligne. Boucle interactive : point final facultatif, saisie atomique. Nouveau § 7 : suites attendues |
| 1.2 | 2026-09-21 | Arithmétique précisée : division finie exacte, zéros de fin, multiplication exacte, puissance à exposant entier (`0 ^ 0` = 1), limite de 1'000 chiffres. Afficher : séparateur espace, signe `−`. Boucle interactive : `quitter`, atomicité étendue aux erreurs de calcul. Messages d'erreur d'exécution |
| 1.3 | 2026-09-21 | Nouveau § 5 : décider (comparaisons en mots et en symboles, sens courant de positif, accords, contractions au et du, et et ou sans mélange, Si en forme courte et en bloc, portée des blocs, booléens). Noms entre crochets, mots réservés étendus, tabulations interdites en début de ligne. Renumérotation : EBNF § 6, messages § 7, suites § 8 |
| 1.4 | 2026-09-21 | Nouveau § 9 : formules. Calculs (définis comme ils s'utilisent, forme courte et bloc avec `Rendre`), actions (`Pour`), paramètres par l'article indéfini, calculs purs, récursion limitée à 1000 appels. `rendre` réservé, `d'un` réservé aux paramètres |

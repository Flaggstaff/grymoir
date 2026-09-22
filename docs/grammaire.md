# Grammaire littéraire de GrymoiR, v0.1

Version 1.20 de la spécification, révisée le 22 septembre 2026. Les § 14 à 16 sont implémentés.
Référence : Charte de GrymoiR v1.7, art. 4, 9 et 12.
Toute modification passe par une révision numérotée.

Périmètre : nommer, calculer, afficher, décider (§ 5), définir des formules (§ 9), répéter (§ 10), le calcul des suites attendues (§ 8), la forme compacte (§ 11), la forme canonique (§ 12) et les objets (§ 13). Tout le reste attend les versions suivantes.

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
- Ce qu'une exécution ratée a affiché avant l'erreur reste affiché : afficher n'est pas écrire. Après le message d'erreur, une phrase dit ce qui a été annulé, pour qu'aucun affichage ne laisse croire le contraire. Dans la boucle interactive : « Saisie annulée : aucun nom n'a changé », complété de « rien n'a été conservé dans la base » ou « aucun fichier n'a été écrit » s'il y a lieu. Avec `grym lancer`, sur la sortie d'erreur, seulement si une base ou des fichiers étaient en jeu : « Exécution annulée : rien n'a été conservé, ni dans la base ni sur le disque. »
- `quitter`, ou la fin de l'entrée (Ctrl+D, Ctrl+Z sous Windows), termine la boucle.

---

## 4. Afficher

```
Afficher le total.
Afficher « Total à payer : » puis le total.
```

- `Afficher` suivi d'un ou plusieurs éléments reliés par `puis`.
- Un élément est un texte ou une expression. Un texte est une valeur comme une autre : il se range dans un nom ou un champ (`Le nom vaut « Dupont ».`), se compare par égalité, mais ne se calcule pas.
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
             | calcul | action | rendre | appel-action
             | tant-que | répéter | pour-chaque | sortir | passer | selon
             | classe | modif-champ ;
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
expression   = terme { ( "+" | "−" ) terme } ;
terme        = unaire { ( "×" | "÷" ) unaire } ;
unaire       = "−" unaire | puissance ;
puissance    = base [ "^" unaire ] ;
base         = nombre | texte | date | "aujourd'hui" | fichier | nouveau | champ
fichier      = "le" "fichier" ( texte | "(" expression ")" ) ;
enregistrer  = "Enregistrer" expression "dans" valeur "." ;
             | [ article ] ( nom | "[" nom "]" ) [ arguments ] | "(" expression ")" ;
nouveau      = ( "un" ( "nouveau" | "nouvel" ) | "une" "nouvelle" ) nom ;
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
| Compteur modifié | « « mois » est le compteur de la boucle : il avance tout seul et ne se modifie pas. » |
| Nombre de tours (exécution) | « Nombre de tours invalide : un entier positif ou nul est attendu, pas 2,5. » |
| Pas nul (exécution) | « Pas nul : la boucle ne finirait jamais. » |
| Hors d'une boucle | « « Sortir de la boucle » hors d'une boucle. » |
| Cas après Autrement | « « Autrement » vient après tous les cas. » |
| Interruption (exécution) | « Interrompu (Ctrl+C). » |
| Date impossible | « Le 31 février 2026 n'existe pas. » |
| Date mal écrite | « Date mal formée « 21.9.26 » : écrivez jour.mois.année, l'année sur quatre chiffres (21.09.2026). » |
| Deux dates additionnées (exécution) | « On n'additionne pas deux dates. » |
| Décalage non entier (exécution) | « Une date se décale d'un nombre entier de jours. » |
| Hors du calendrier (exécution) | « Date hors du calendrier : du 01.01.0001 au 31.12.9999. » |
| `aujourd'hui` dans un calcul | « Un calcul ne dépend pas du jour : passez la date en paramètre. » |
| Fichier absent (exécution) | « Fichier « photos/ana.jpg » introuvable ou illisible. » |
| Écrasement (exécution) | « « copie.jpg » existe déjà : il n'est jamais écrasé. » |
| Fichier lu dans un calcul | « Un calcul ne lit pas le disque : lisez le fichier dans une action. » |
| Champ d'entité sans type | « Type attendu entre parenthèses : « un nom (texte) ». » |
| Type d'une valeur (analyse ou exécution) | « Le champ « solde » attend un nombre, pas un texte. » |
| Lien vers une classe ordinaire | « « personne » n'est pas une entité : un lien pointe vers une entité conservée. » |
| Objet incomplet (exécution) | « Le champ « nom » n'a pas de valeur : un client incomplet ne se conserve pas. » |
| Lien vers un objet non conservé | « Le champ « parrain » désigne un client qui n'est pas conservé : conservez-le d'abord. » |
| Unicité | « « licence » est unique : un autre client conservé a déjà « A-1 ». » |
| Suppression refusée | « Ce client est encore désigné par le champ « parrain » de 2 clients. » |
| Champ nouveau sans valeur de départ | « « pays » est nouveau, et 12 clients sont déjà conservés : donnez-lui une valeur de départ, après son type : « (texte), … au départ ». » |
| Champ retiré | « « pays » a disparu de « client » : 12 valeurs conservées seraient perdues. … » |
| Recherche sans résultat unique | « 3 clients conservés répondent à cette condition : « le client conservé dont … » en attend un seul. » |
| Condition « dont » mal formée | « Une condition « dont » compare un champ du client à une valeur : « dont le solde est négatif ». » |
| Classe inconnue | « Classe « fournisseur » inconnue. » |
| Champ hérité redéclaré | « « nom » est déjà un champ hérité de « personne ». » |
| Version en double | « « saluer » existe déjà pour « personne ». » |
| Aucune version (exécution) | « Aucune version de « saluer » pour une ville. » |
| Accord d'une aptitude | « Accord : « personne horodatée ». » |
| Conflit d'aptitudes | « « décrire » est défini par les aptitudes « horodatée » et « numérotée » de « membre » : définissez sa version pour « membre » afin de trancher. » |
| Accord de « nouveau » | « « client » est masculin : écrivez « un nouveau client ». » |
| Champ modifié avec `vaut` | « Un champ se modifie avec « devient » : « Le solde du client devient … ». » |
| Champ absent (exécution) | « Un client n'a pas de champ « montant ». » |
| Champ vide (exécution) | « Le champ « solde » n'a pas de valeur. » |
| Pas un objet (exécution) | « « nom » : la valeur n'est pas un objet, c'est un nombre. » |
| Récursion sans fin (exécution) | « Trop d'appels imbriqués : plus de 1000. Une formule s'appelle-t-elle sans fin ? » |

Chaque message est précédé du fichier, de la ligne et de la colonne (charte, art. 8) : `facture.grym:7:18 : erreur : Division par zéro.`

---

## 8. Suites attendues

À chaque position, l'analyseur calcule l'ensemble exact des suites valides (charte, art. 9).

- Catégories : début de phrase (`Le`, `La`, `L'`, `Afficher`, `Si`, `Pour`, `Remarque :`, et les actions déclarées, avec une majuscule), nombre, nom déclaré, nouveau nom, parenthèse, négation, texte, `vrai` et `faux`, opérateurs, comparaisons (`est`, `n'est pas`, symboles), `et` et `ou`, parenthèse fermante, `vaut` et `devient`, `,` et `:` après une condition, `puis`, point final.
- Après `est`, les tournures accordées au genre du sujet (`supérieure à`, `positive`…) ; après `supérieur`, `à`, `au` ou `ou`.
- `Tant que`, `Répéter`, `Pour chaque` et `Selon` en début de phrase ; dans une boucle, `Sortir de la boucle` et `Passer au tour suivant`.
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

`tant`, `répéter`, `chaque`, `sortir`, `passer`, `selon`, `cas`, `autrement` (avec `afficher`, `si`, `sinon`, `pour`, `rendre`) commencent des constructions : ils ne peuvent pas commencer le nom d'une action.

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

Limite de la v0.2 : les erreurs de structure propres à la forme compacte (`_fin` manquant, `_alors` attendu…) s'expriment en forme compacte ; les autres (nom inconnu, accord, pureté) empruntent encore le vocabulaire de la forme littéraire (charte, art. 8).

## 12. Forme canonique

`grym formater fichier.grym` réécrit un programme dans la forme littéraire canonique.

- Le programme est conservé : même arbre, mêmes noms, mêmes valeurs, mêmes remarques, mêmes choix d'écriture (articles, crochets, tournures en mots ou en symboles, contractions, formes courtes ou en bloc).
- La présentation est normalisée : une phrase par ligne, indentation de quatre espaces, majuscule en début de phrase, espaces simples, nombres au style suisse (`1'000`, `12,50`), textes entre `« »` (ou `" "` si le texte commence ou finit par une espace, ce que `« »` rognerait), une seule ligne vide là où la source en avait une ou plusieurs.
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

### 13.8 À venir

Appeler la version de la classe parente depuis une méthode, les listes d'objets, l'absence de valeur et un vrai affichage des objets demandent chacun leur propre conception.

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
| `=`, `≠`, `<`, `≤`, `>`, `≥` entre deux dates | ordre chronologique |

- Ajouter des mois ou des années est reporté : « le 31 janvier plus un mois » n'a pas de réponse évidente.
- Toute autre opération est une erreur à l'exécution : « On n'additionne pas deux dates. »
- `jours + date` vaut `date + jours`. Le décalage accepte `3,00`, pas `2,5`.
- Les boucles et `Selon` acceptent les dates : `Pour chaque jour du 01.01.2026 au 31.01.2026` avance d'un jour, `par pas de 7` d'une semaine ; `Cas de 01.07.2026 à 31.08.2026` teste un intervalle.
- Vérification (21 septembre 2026) : les 3 652 059 jours du calendrier et 2 951 opérations tirées au hasard donnent les mêmes résultats que le module `datetime` de Python.

### 14.3 `aujourd'hui`

- `aujourd'hui` vaut la date du jour, selon l'horloge locale, lue une fois au début de l'exécution : une exécution qui franchit minuit garde la même date du début à la fin.
- Un calcul ne peut pas l'utiliser (§ 9.4, pureté) : « Un calcul ne dépend pas du jour : passez la date en paramètre. »
- En forme compacte : `_aujourd'hui`.

### 14.4 Affichage et stockage

- En base : texte ISO 8601, `2026-09-21`, qui se trie dans l'ordre chronologique.
- `Afficher` écrit la forme de la source : `21.09.2026`.
- Reporté : l'heure, un type `(moment)`, et les fuseaux horaires.

## 15. Fichiers et images *(v0.3)*

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
- `Supprimer le client.` le retire de la base. Un objet encore désigné par un lien ne se supprime pas : « Ce client est encore le parrain de 2 clients. » L'objet reste en mémoire, mais n'est plus conservé.
- Conserver, modifier et supprimer sont des effets de bord : réservés aux actions. `conserver` et `supprimer` ne commencent pas le nom d'une action.
- Un lien d'un objet conservé désigne un objet conservé : « Le champ « parrain » désigne un client qui n'est pas conservé : conservez-le d'abord. » Un objet peut se désigner lui-même (`Le parrain du a devient a.`, puis `Conserver a.`). Deux objets neufs qui se désignent l'un l'autre ne se conservent pas encore : il faudrait un champ facultatif (§ 13.8).
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
- Un même objet conservé, retrouvé deux fois dans une exécution, est le même objet en mémoire : `=` compare toujours l'identité (§ 13.4).
- Un calcul ne lit pas la base : son résultat changerait d'une exécution à l'autre, pour la même raison qu'il n'emploie pas `aujourd'hui` (§ 9.4). Règle prudente, qu'on pourra assouplir ; l'inverse serait impossible sans casser des programmes.
- Dans une condition `dont`, un champ de l'entité désigne celui de chaque objet examiné. Chaque comparaison met un champ face à une valeur calculée par le programme (une variable, une constante, un calcul) ; deux champs ne se comparent pas entre eux. `0 < solde` vaut `solde > 0`.
- `est` suivi d'une valeur vaut l'égalité : `dont la licence est « A-12 »`, `dont le parrain est a`, et `n'est pas` sa négation. Cette tournure n'existe que dans une condition `dont`.
- Ce qui se compare : texte, nombre, nombre entier et date par égalité et par ordre ; vrai ou faux et lien par égalité ; `positif`, `négatif`, `nul` pour un nombre, `vrai`, `faux` pour un vrai ou faux. Un fichier ne se compare pas. Un nombre entier se compare à un nombre à virgule (`dont le rang ≥ 2,5`).
- Les nombres se comparent et se trient en décimal exact : `100,000000000000000001 > 9`, `0,10 = 0,1`. Les textes se trient comme dans un dictionnaire, sans tenir compte des accents ni de la casse (Ana, Bob, Élodie, émile, Zoé) ; leur égalité reste exacte.
- Une entité retrouvée inclut les objets des entités qui en héritent : `Pour chaque client conservé` parcourt aussi les membres, qui restent des membres.
- La liste d'une boucle est figée à son début : un objet conservé pendant la boucle n'y entre pas. `Sortir de la boucle` et `Passer au tour suivant` s'y emploient.
- Le nom de l'objet du tour est celui de l'entité ; s'il existe déjà, erreur : « « client » existe déjà : renommez-le, car « Pour chaque client conservé » donne ce nom à l'objet de chaque tour. »
- `par nom` trie selon un champ de texte, de nombre, de date ou vrai ou faux ; à valeurs égales, dans l'ordre de conservation.
- Un objet retrouvé n'est lu qu'au premier accès à ses champs ; ses liens, à leur tour, au premier accès aux leurs.

### 16.5 La base

- Un programme `factures.grym` utilise la base `factures.grymd`, à côté de lui, créée au premier besoin.
- La boucle interactive utilise une base en mémoire, perdue à la sortie, sauf si on lui donne un fichier : `grym --base factures.grymd`.
- `factures.grymc` et `factures.grymb` utilisent aussi `factures.grymd`.
- Tant que les migrations (§ 16.7) ne sont pas là, une entité dont la définition a changé depuis la dernière exécution est refusée : « La base « factures.grymd » connaît « client » avec une autre définition… » La base n'est pas touchée.
- Aucune base n'est ouverte si le programme ne déclare aucune entité.

### 16.6 Transaction

- Une exécution, une transaction : chaque programme lancé, et chaque saisie de la boucle interactive, s'exécute dans une seule transaction.
- Si l'exécution réussit, la transaction est validée. Si elle échoue, ou si on l'interrompt par Ctrl+C, elle est annulée : le journal de la machine restaure la mémoire, SQLite restaure la base.
- Pendant l'exécution, la base est verrouillée en écriture pour les autres programmes. Après deux secondes d'attente : « La base « … » est utilisée par un autre programme. »
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
| `Pour chaque client conservé dont le solde est négatif, par nom décroissant :` | `_pour_chaque client _conservé _dont solde _négatif _par nom _décroissant` |
| `le client conservé dont la licence est « A-12 »` | `_le client _conservé _dont licence = « A-12 »` |
| `le nombre de clients conservés dont …` | `_nombre_de client _conservé _dont …` |
| `le fichier « photos/ana.jpg »` | `_fichier « photos/ana.jpg »` |
| `la taille de la photo` | `photo.taille` ; `(_fichier « a.jpg »).taille` |
| `Enregistrer la photo dans « copie.jpg ».` | `_enregistrer photo _dans « copie.jpg »` |
| `21.09.2026`, `aujourd'hui` | `21.09.2026`, `_aujourd'hui` |

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
- Une valeur absente ne se laisse pas utiliser par mégarde : un calcul, une comparaison, le champ d'un lien absent ou l'appel d'une méthode sur elle donnent « Le champ « édition » est absent : vérifiez-le d'abord avec « est présent ». » Elle s'affiche `absent`, se range dans un nom, et se copie dans un autre champ facultatif.
- Un champ obligatoire refuse `absent` : « Le champ « nom » n'est pas facultatif : il ne devient pas absent. »
- En base, un champ absent est `NULL`. Une condition `dont` autre que `est absent` ne retient jamais un objet dont le champ est absent (`dont l'édition < 01.01.2020` écarte les éditions absentes). Un tri place les absents en dernier, dans les deux sens.
- Deux objets neufs se désignent l'un l'autre en passant par un champ facultatif : conserver le premier avec le lien absent, puis le second, puis remplir le lien du premier.
- Migrations (§ 16.7) : un champ facultatif nouveau s'ajoute à une entité qui a déjà des objets, sans valeur de départ ; ils le reçoivent absent. Un champ existant ne devient pas facultatif, ni ne cesse de l'être, sur une table existante.
- En forme compacte : `_une édition (date) _facultatif`, `_absent`, `x.édition _absent`, `x.édition _présent`.

### 16.10 Accord avec la charte

La charte 1.9 reprend ce paragraphe : transaction par exécution (art. 7), typage vérifié à l'analyse ou avant toute écriture (art. 5), type `montant` retiré pour la v0.3, puisque tout `(nombre)` est déjà un décimal exact.

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

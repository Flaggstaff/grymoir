# Grammaire littéraire de GrymoiR

Version 1.32 de la spécification, révisée le 23 septembre 2026. Tout ce qui suit est implémenté.
Référence : Charte de GrymoiR v1.22, art. 4, 5, 7, 8, 9 et 12.
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
- Les mots réservés ne peuvent pas faire partie d'un nom écrit sans crochets : `vaut`, `devient`, `puis`, `est`, `et`, `ou`, `si`, `sinon`, `vrai`, `faux`, `rendre`, `dont`, `définitivement`, ainsi que l'élision `n'` devant `est`.
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
- Ce qu'une exécution ratée a affiché avant l'erreur reste affiché : afficher n'est pas écrire. Après le message d'erreur, une phrase dit ce qui a été annulé, pour qu'aucun affichage ne laisse croire le contraire. Dans la boucle interactive : « Saisie annulée : aucun nom n'a changé », complété de « rien n'a été conservé dans la base » ou « aucun fichier n'a été écrit » s'il y a lieu. Avec `grym lancer`, sur la sortie d'erreur, seulement si une base ou des fichiers étaient en jeu : « Exécution annulée : rien n'a été conservé, ni dans la base ni sur le disque. » Après une question (§ 17) : « Exécution annulée : rien n'a été conservé depuis la dernière question. »
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

### 4.1 Style des nombres affichés

- Défaut : style suisse, apostrophe pour les milliers et virgule décimale (`1'234,50`). Un nombre négatif porte le signe `−` (U+2212) : `−1'000`.
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

---

### 4.3 Effacer l'écran

```
Effacer l'écran.
```

- Efface l'écran et ramène le curseur en haut à gauche.
- Hors d'un terminal, la phrase n'écrit rien : une sortie redirigée dans un fichier ou dans un tube reste propre.
- Effet de bord, donc réservé aux actions. `effacer` ne peut pas commencer le nom d'une action.
- En forme compacte : `_effacer`.

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
             | classe | modif-champ | essayer ;
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
expression   = terme { ( "+" | "−" ) terme } ;
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
| Année hors du champ (analyse ou exécution) | « Le champ « composition » attend une année (de 1 à 9999), pas 2,5. » |
| Calcul sur une année (exécution) | « On n'additionne pas deux années. » |
| Entité nommée comme un type | « « année » est un type : une entité ne peut pas porter ce nom. » |
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
| Champ multiple lu comme une valeur | « « genres » est un champ multiple, pas une valeur : il se lit avec « Pour chaque genre de … » ou « le nombre de genres de … », et change avec « Les genres de … gagnent … ». » |
| Champ multiple facultatif, unique, avec valeur de départ | « « genres » est multiple : il n'est pas facultatif, un ensemble vide lui suffit. » |
| Objet non conservé qui gagne (exécution) | « Une œuvre qui n'est pas conservée ne gagne rien : ses « genres » vivent dans la base. Conservez-la d'abord. » |
| Élément de la corbeille (exécution) | « Le champ « genres » gagnerait un genre supprimé : rétablissez-le d'abord. » |
| Ambiguïté d'une relation (exécution) | « Plusieurs champs relient une œuvre à une personne : « compositeur » et « interprètes ». Précisez avec « dont … est le compositeur » ou « dont … est parmi les interprètes ». » |
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

`tant`, `répéter`, `chaque`, `sortir`, `passer`, `selon`, `cas`, `autrement`, `essayer` (avec `afficher`, `si`, `sinon`, `pour`, `rendre`) commencent des constructions : ils ne peuvent pas commencer le nom d'une action.

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
- La présentation est normalisée : une phrase par ligne, indentation de quatre espaces, majuscule en début de phrase, espaces simples, nombres au style suisse (`12,50` ; un nombre groupé l'est par l'apostrophe, `1 000` devient `1'000`, et un nombre écrit sans séparateur le reste, `1747`), textes entre `« »` (ou `" "` si le texte commence ou finit par une espace, ce que `« »` rognerait), une seule ligne vide là où la source en avait une ou plusieurs.
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

Appeler la version de la classe parente depuis une méthode, les listes d'objets et un vrai affichage des objets demandent chacun leur propre conception. L'absence de valeur est faite (§ 16.9).

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

- Une année se compare à une année ou à un nombre, par valeur : `année de o > 1700`, `1747 = année de o`. Elle ne se compare pas à une date : « Une année ne se compare pas à une date : comparez l'année de la date, « l'année de d ». »
- `l'année de d` est permis dans un calcul : il ne dépend que de son paramètre. `l'année d'aujourd'hui` reste réservé aux actions (§ 14.3).
- Boucles : le compteur est une année dès que le début en est une (`Pour chaque an de l'année de d à 2026`). `Selon` : `Cas de 1700 à 1750` compare par valeur.
- En base : `INTEGER`. Une condition `dont` compare le champ à une année ou à un nombre ; le tri est numérique.
- Migrations (§ 16.7) : `(nombre entier)` devient `(année)` si toutes les valeurs conservées sont entre 1 et 9999, sinon refus avec leur nombre ; `(année)` redevient `(nombre entier)` sans perte.
- `année` est un nom de type : une entité ne peut pas le porter, pas plus que `texte` ou `date`.
- En forme compacte : `(année)`, `d.année`.

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
- Le lien est choisi à l'exécution, selon la classe réelle de l'objet (un lien `(personne)` désigne aussi un membre, qui est une personne). S'il n'y en a aucun : « Aucun champ d'une œuvre ne peut désigner une œuvre : « les œuvres de … » ne désigne rien. » S'il y en a plusieurs : « Plusieurs champs d'une chanson peuvent désigner une personne : « compositeur » et « auteur ». Précisez avec « dont … est le … », par exemple « dont … est le compositeur ». »
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
- Les liens existants restent lisibles : la facture d'un client supprimé affiche toujours son client. Un nouveau lien vers un objet de la corbeille est refusé : « Le champ « client » désignerait un client supprimé : rétablissez-le d'abord. »
- Un objet de la corbeille garde ses valeurs uniques : « « cote » est unique : « A-2 » appartient à une partition supprimée. Rétablissez-la, ou supprimez-la définitivement. »
- `Rétablir x.` le fait revenir. Supprimer deux fois est une erreur ; rétablir un objet qui n'est pas dans la corbeille aussi.
- `, et disparaît avec elle` (`avec lui`, accordé avec l'entité désignée) sur un lien : l'objet suit celui qu'il désigne. Il va dans la corbeille avec lui, revient avec lui, est effacé avec lui ; de proche en proche. Il ne se rétablit pas seul : « Ce pupitre a disparu avec un autre objet : rétablissez celui-là, et il reviendra avec lui. » Un objet mis dans la corbeille pour lui-même, puis dont le lien désigne un objet supprimé, ne revient pas avant lui.
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
- Relance : une réponse qui ne convient pas est annoncée, puis la question se repose. « « x » n'est pas un nombre. », « « 21.9.26 » n'est pas une date : écrivez jour.mois.année (21.09.2026). », « Répondez par oui ou non. » Une ligne vide convient à un texte, et relance pour les autres types. Le programme ne s'arrête pas parce que l'utilisateur a tapé de travers.
- Fin de l'entrée (Ctrl+D) : erreur d'exécution, « Plus rien à lire : la réponse à « Âge ? » manque. » Ctrl+C interrompt comme partout ailleurs.
- **Une question valide ce qui la précède** : les fichiers en attente (§ 15.2) sont écrits, puis la base est validée, puis le journal se vide. Une erreur survenue plus tard n'annule que depuis la dernière question, et le dit (§ 3.3). Le verrou de la base est rendu pendant l'attente, et repris avec la réponse : un autre programme peut donc écrire entre deux questions, et les objets déjà lus gardent en mémoire les valeurs de leur lecture.
- Effet de bord, donc réservé aux actions : « Un calcul ne pose pas de question : demandez dans une action. » Et interdit dans la boucle interactive, qui lit déjà sur la même entrée.
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
- Lien : la réponse est la valeur du premier champ texte unique de l'entité liée, comparée exactement (§ 16.4) : « Compositeur ? Bach » retrouve le compositeur conservé dont le nom est « Bach ». Aucun : « Aucun compositeur conservé n'a « Brahms » pour nom. », puis relance. Une entité liée sans champ texte unique : un lien facultatif reste absent ; un lien obligatoire est une erreur d'exécution, qui demande de l'initialiser dans le bloc.
- Unique : la valeur se vérifie dès la réponse, corbeille comprise : « « A-1 » est déjà pris. », puis relance. `Conserver` vérifie encore, car un autre programme peut écrire entre deux questions.
- Chaque question valide ce qui la précède (§ 17) ; dans un `Essayer` (§ 18), l'échec n'annule que depuis la dernière.
- Reporté : modifier un objet existant par formulaire, choisir le libellé, l'autocomplétion des réponses.
- En forme compacte : `_nouveau contact _saisi`, `_nouveau partition _saisi _avec`.

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

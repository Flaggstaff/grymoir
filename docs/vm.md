# Machine virtuelle et bytecode de GrymoiR

Version 1.1 de la spécification, révisée le 21 septembre 2026.
Référence : Charte de GrymoiR v1.6, art. 2, 3, 7, 8, 10 et 12 ; grammaire 1.3, § 5.
Toute modification passe par une révision numérotée.

Périmètre : ce que la v0.2 remplace dans la v0.1 (l'évaluateur provisoire), et les principes qui guideront les instructions à venir (sauts, appels, objets).

---

## 1. Principes

1. **Machine à pile.** Les opérandes s'empilent, les opérations les consomment. Le compilateur reste simple et le bytecode se lit dans l'ordre de la phrase. La vitesse d'une machine à registres ne justifie pas sa complexité (charte, art. 2 : performance au rang 4).
2. **Un bloc par formule, lié par son nom.** Chaque formule se compile en un bloc autonome. Les appels et les variables globales passent par une table de noms, résolue au chargement. Remplacer une formule revient à remplacer une entrée de la table (charte, art. 10 : développement vivant).
3. **Un fichier portable, sans ambiguïté.** Ordre des octets fixé, aucun pointeur, constantes numériques en texte (charte, art. 3 : un bytecode unique).
4. **Ramasse-miettes par traçage.** Marquage et balayage, sans déplacement. Il arrive avec les objets (v0.2), qui forment des cycles que le comptage de références ne libère pas.
5. **Journal d'annulation.** Chaque écriture note l'ancienne valeur. Un échec rejoue le journal à l'envers : l'exécution ratée ne laisse aucune trace. Le même mécanisme servira aux transactions de la v0.3 (charte, art. 7).

Règle de nommage : instructions, outils et messages s'écrivent en toutes lettres, sans abréviation.

---

## 2. Valeurs

| Type | Contenu |
|------|---------|
| nombre | décimal exact (grammaire, § 3.2) |
| texte | chaîne UTF-8 |
| booléen | vrai ou faux |

Les objets et l'absence de valeur s'ajouteront avec les constructions qui en ont besoin.

---

## 3. Instructions

Chaque instruction commence par un octet (son code). Un opérande, s'il existe, suit sur deux octets, poids faible d'abord ; la cible d'un saut suit sur quatre octets (décalage absolu dans le code).

| Code | Instruction | Opérande | Effet sur la pile |
|------|-------------|----------|-------------------|
| 1 | `CONSTANTE` | index de constante | empile la constante |
| 2 | `LIRE` | index de nom | empile la valeur du nom |
| 3 | `ÉCRIRE` | index de nom | dépile une valeur et l'attribue au nom |
| 4 | `NÉGATION` | aucun | remplace le nombre au sommet par son opposé |
| 5 | `ADDITION` | aucun | dépile b, puis a ; empile a + b |
| 6 | `SOUSTRACTION` | aucun | dépile b, puis a ; empile a − b |
| 7 | `MULTIPLICATION` | aucun | dépile b, puis a ; empile a × b |
| 8 | `DIVISION` | aucun | dépile b, puis a ; empile a ÷ b |
| 9 | `PUISSANCE` | aucun | dépile b, puis a ; empile a ^ b |
| 10 | `AFFICHER` | nombre d'éléments n | dépile n valeurs ; les écrit dans l'ordre, séparées par une espace, puis un saut de ligne |
| 11 | `RETOUR` | aucun | termine le bloc |
| 12 | `ÉGAL` | aucun | dépile b, puis a ; empile a = b |
| 13 | `DIFFÉRENT` | aucun | dépile b, puis a ; empile a ≠ b |
| 14 | `INFÉRIEUR` | aucun | dépile b, puis a ; empile a < b |
| 15 | `SUPÉRIEUR` | aucun | dépile b, puis a ; empile a > b |
| 16 | `INFÉRIEUR_OU_ÉGAL` | aucun | dépile b, puis a ; empile a ≤ b |
| 17 | `SUPÉRIEUR_OU_ÉGAL` | aucun | dépile b, puis a ; empile a ≥ b |
| 18 | `NON` | aucun | remplace le booléen au sommet par son contraire |
| 19 | `SAUTER` | cible (4 octets) | continue à la cible |
| 20 | `SAUTER_SI_FAUX` | cible (4 octets) | dépile un booléen ; s'il est faux, continue à la cible |

- Une phrase `Afficher` à n éléments compile en `AFFICHER n`. Dans la boucle interactive, une expression seule compile en `AFFICHER 1`.
- La création et la modification compilent toutes deux en `ÉCRIRE` : la distinction entre `vaut` et `devient` se vérifie à la compilation (grammaire, § 2.1).
- `est positif`, `est négatif`, `est nul` compilent en une comparaison avec la constante 0 ; `est vrai`, `est faux` en `ÉGAL` avec une constante booléenne ; `n'est pas` ajoute `NON`.
- Les comparaisons d'ordre n'acceptent que deux nombres ; `ÉGAL` et `DIFFÉRENT` acceptent deux valeurs du même type. Sinon : erreur d'exécution.
- `SAUTER_SI_FAUX` exige un booléen : « Condition ni vraie ni fausse : la valeur est un nombre. »
- `et` et `ou` compilent en sauts (court-circuit). Chaque membre passe par `SAUTER_SI_FAUX`, qui vérifie qu'il s'agit d'un booléen :

```
a et b :  a ; SAUTER_SI_FAUX F ; b ; SAUTER_SI_FAUX F ; CONSTANTE vrai ; SAUTER Fin ; F: CONSTANTE faux ; Fin:
a ou b :  a ; SAUTER_SI_FAUX B ; CONSTANTE vrai ; SAUTER Fin ;
          B: b ; SAUTER_SI_FAUX F ; CONSTANTE vrai ; SAUTER Fin ; F: CONSTANTE faux ; Fin:
Si c, x.  Sinon, y.  :  c ; SAUTER_SI_FAUX S ; x ; SAUTER Fin ; S: y ; Fin:
```

---

## 4. Vérification avant exécution

Un fichier `.grymb` peut venir d'ailleurs. Avant toute exécution, la machine vérifie le bloc entier, en deux passes :

1. Décodage linéaire : chaque code d'instruction existe, son opérande est présent, chaque index de constante ou de nom existe, chaque cible de saut commence une instruction, et la dernière instruction est `RETOUR`.
2. Parcours de tous les chemins d'exécution : la pile ne descend jamais sous zéro, chaque `AFFICHER n` trouve n valeurs, deux chemins qui se rejoignent arrivent avec la même profondeur de pile, et chaque `RETOUR` trouve la pile vide.

Limite connue : un saut vers l'arrière forme une boucle, que la vérification n'interdit pas (les boucles viendront avec `Tant que`). Un fichier fabriqué à la main peut donc tourner sans fin.

Un bloc qui échoue à la vérification ne s'exécute pas : « Fichier .grymb invalide : … ».

---

## 5. Liaison des noms

- Un bloc contient la liste des noms qu'il utilise. `LIRE 2` désigne le troisième nom de cette liste, pas une case mémoire.
- Au chargement, la machine associe chaque nom à une case de sa table globale, en la créant si besoin.
- Les valeurs vivent dans la machine, pas dans le bloc. Charger un nouveau bloc, ou recharger une formule modifiée, conserve donc les valeurs existantes.

---

## 6. Journal d'annulation

- Chaque `ÉCRIRE` range dans le journal le nom et son ancienne valeur (ou l'absence de valeur).
- Si l'exécution échoue (division par zéro, nombre trop grand), la machine rejoue le journal du plus récent au plus ancien, puis le vide. Aucun nom ne garde de valeur écrite pendant l'exécution ratée.
- Si l'exécution réussit, le journal se vide.
- La boucle interactive exécute chaque saisie comme une unité (grammaire, § 3.3).

---

## 7. Positions

Le bloc garde, pour chaque instruction, la ligne et la colonne de la source. Pour une opération, c'est la position de l'opérateur. Une erreur d'exécution s'exprime ainsi comme une erreur de compilation (charte, art. 8) : `facture.grym:7:18 : erreur : Division par zéro.`

---

## 8. Format du fichier `.grymb`

Entiers non signés, poids faible d'abord (petit-boutiste). `u16` : deux octets ; `u32` : quatre octets.

```
en-tête       "GRYM" (4 octets ASCII), version du format : u16 = 2
constantes    nombre : u32, puis pour chacune :
                type : u8 (1 = nombre, 2 = texte, 3 = booléen), longueur : u32, octets UTF-8
noms          nombre : u32, puis pour chacun : longueur : u32, octets UTF-8
code          longueur : u32, puis les octets des instructions
positions     nombre : u32, puis pour chacune :
                décalage dans le code : u32, ligne : u32, colonne : u32
```

- Un nombre s'écrit sous sa forme canonique : chiffres, point décimal, signe `-` éventuel (`12.50`, `-3`). Le texte évite tout format binaire propre à une machine et garde la valeur exacte. Un booléen s'écrit `vrai` ou `faux`.
- La version 2 ajoute les instructions 12 à 20 et les constantes booléennes. Un fichier de version 1 reste lisible.
- En v0.2, un fichier contient un seul bloc, le programme principal. Les blocs de formules s'ajouteront avec les formules.

---

## 9. Outils

| Commande | Rôle |
|----------|------|
| `grym compiler facture.grym` | produit `facture.grymb` |
| `grym lancer facture.grym` | compile puis exécute |
| `grym lancer facture.grymb` | exécute un bytecode déjà compilé |
| `grym desassembler facture.grymb` | affiche les instructions en clair |

Chaque ligne donne la ligne source (quand elle change), le décalage de l'instruction, puis l'instruction ; les cibles des sauts renvoient aux décalages.

```
   4  0018  LIRE              0     ; total
      0021  CONSTANTE         3     ; 100
      0024  SUPÉRIEUR
      0025  SAUTER_SI_FAUX    0051
   5  0030  CONSTANTE         4     ; 10
      0033  ÉCRIRE            2     ; rabais
   4  0046  SAUTER            0080
   7  0051  LIRE              0     ; total
```

---

## Journal des révisions

| Version | Date | Changement |
|---------|------|------------|
| 1.0 | 2026-09-21 | Spécification initiale : machine à pile, onze instructions, liaison par nom, journal d'annulation, vérification, format `.grymb` |
| 1.1 | 2026-09-21 | Booléens, six comparaisons, `NON`, sauts (`SAUTER`, `SAUTER_SI_FAUX`, cible sur quatre octets), vérification de tous les chemins, constantes booléennes, format version 2, décalages au désassemblage |

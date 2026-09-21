# Machine virtuelle et bytecode de GrymoiR

Version 1.4 de la spécification, révisée le 21 septembre 2026.
Référence : Charte de GrymoiR v1.6, art. 2, 3, 7, 8, 10 et 12 ; grammaire 1.8, § 5, § 9, § 10 et § 13.
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
| objet | référence vers un objet du tas : sa classe et ses champs |

Copier une valeur objet copie la référence, jamais l'objet. L'absence de valeur s'ajoutera avec les constructions qui en ont besoin.

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
| 21 | `APPELER` | nom (2 octets), nombre d'arguments (1 octet), rend (1 octet : 1 pour un calcul) | dépile les arguments dans les premières cases locales de la formule appelée, puis l'exécute |
| 22 | `RENDRE` | aucun | termine un calcul ; la valeur au sommet devient le résultat de l'appel |
| 23 | `LIRE_LOCAL` | case locale | empile la valeur de la case |
| 24 | `ÉCRIRE_LOCAL` | case locale | dépile une valeur dans la case |
| 25 | `ÉCHOUER` | index d'une constante texte | arrête l'exécution avec ce message |
| 26 | `EXIGER_ENTIER_NATUREL` | aucun | vérifie que le sommet est un entier positif ou nul ; sinon, erreur |
| 27 | `NOUVEAU` | nom de classe | empile un nouvel objet, champs sans valeur |
| 28 | `INITIALISER_CHAMP` | nom de champ | dépile une valeur, la range dans l'objet au sommet (qui reste) |
| 29 | `LIRE_CHAMP` | nom de champ | remplace l'objet au sommet par la valeur de son champ |
| 30 | `ÉCRIRE_CHAMP` | nom de champ | dépile une valeur, puis un objet ; range la valeur dans le champ |

### 3.1 Boucles et Selon

Aucune instruction dédiée : les boucles se compilent en sauts, dont un vers l'arrière.

```
Tant que c : corps.          T: c ; SAUTER_SI_FAUX S ; corps ; SAUTER T ; S:
Répéter n fois : corps.      n ; EXIGER_ENTIER_NATUREL ; ÉCRIRE_LOCAL r ;
                             T: r > 0 ? sinon S ; r ← r − 1 ; corps ; SAUTER T ; S:
Pour chaque i de a à b       a → i ; b → fin ; pas (écrit, ou ±1 selon a ≤ b) → pas ;
  par pas de p : corps.      pas = 0 ? ÉCHOUER « Pas nul… » ;
                             T: (pas > 0 ? i ≤ fin : i ≥ fin) sinon S ; corps ;
                             P: i ← i + pas ; SAUTER T ; S:
```

`Sortir de la boucle` saute à `S`, `Passer au tour suivant` à `T` (ou à `P` pour `Pour chaque`). Un `Selon` range son sujet dans une case locale, puis teste chaque cas dans l'ordre et saute à la fin après le premier corps exécuté.

### 3.2 Formules et appels

- Un module réunit le programme (bloc 0) et un bloc par formule. Chaque bloc de formule porte son nom, sa sorte (calcul ou action), son nombre de paramètres et son nombre de cases locales (paramètres compris).
- `APPELER` désigne la formule par son nom, résolu au moment de l'appel dans la table des formules de la machine (§ 5). La machine vérifie que la formule existe, qu'elle est de la sorte attendue et qu'elle reçoit le bon nombre d'arguments ; sinon, erreur d'exécution.
- Chaque appel crée un cadre : ses cases locales, sa position. Un calcul se termine par `RENDRE`, une action par `RETOUR`, qui rend la main à l'appelant. `RETOUR` dans le programme termine l'exécution.
- Au-delà de 1000 cadres imbriqués : « Trop d'appels imbriqués : plus de 1000. »
- Le programme principal a lui aussi des cases locales : compteurs de boucle, bornes, sujets de `Selon`.
- Une variable locale s'adresse par son numéro de case, un nom global par son nom (`LIRE`, `ÉCRIRE`). Un calcul ne contient jamais `LIRE` ni `ÉCRIRE` d'une variable : l'analyse le garantit (grammaire, § 9.4).

- Une phrase `Afficher` à n éléments compile en `AFFICHER n`. Dans la boucle interactive, une expression seule compile en `AFFICHER 1`.
- La création et la modification compilent toutes deux en `ÉCRIRE` : la distinction entre `vaut` et `devient` se vérifie à la compilation (grammaire, § 2.1).
- `est positif`, `est négatif`, `est nul` compilent en une comparaison avec la constante 0 ; `est vrai`, `est faux` en `ÉGAL` avec une constante booléenne ; `n'est pas` ajoute `NON`.
- Les comparaisons d'ordre n'acceptent que deux nombres ; `ÉGAL` et `DIFFÉRENT` acceptent deux valeurs du même type. Sinon : erreur d'exécution.
- `SAUTER_SI_FAUX` exige un booléen : « Condition ni vraie ni fausse : la valeur est un nombre. »
- Les instructions de champ désignent la classe et le champ par leur nom, résolu à l'exécution : la machine vérifie que la valeur est un objet et que sa classe a ce champ. `ÉGAL` compare deux objets par identité.
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

1. Décodage linéaire : chaque code d'instruction existe, son opérande est présent, chaque index de constante, de nom ou de case locale existe, `ÉCHOUER` désigne une constante texte, chaque cible de saut commence une instruction, et la dernière instruction est `RETOUR` (programme, action) ou `RENDRE` (calcul). `RENDRE` n'apparaît que dans un calcul, `RETOUR` jamais dans un calcul.
2. Parcours de tous les chemins d'exécution : la pile ne descend jamais sous zéro, chaque `AFFICHER n` et chaque `APPELER` trouvent leurs valeurs, deux chemins qui se rejoignent arrivent avec la même profondeur de pile, chaque `RETOUR` trouve la pile vide et chaque `RENDRE` exactement une valeur.
3. Pour le module : le bloc 0 est le programme, les autres des formules nommées, sans doublon.

Limite connue : un saut vers l'arrière forme une boucle, que la vérification n'interdit pas (les boucles viendront avec `Tant que`). Un fichier fabriqué à la main peut donc tourner sans fin.

Un bloc qui échoue à la vérification ne s'exécute pas : « Fichier .grymb invalide : … ».

---

## 5. Liaison des noms

- Un bloc contient la liste des noms qu'il utilise. `LIRE 2` désigne le troisième nom de cette liste, pas une case mémoire.
- Au chargement, la machine associe chaque nom à une case de sa table globale, en la créant si besoin.
- Les valeurs vivent dans la machine, pas dans le bloc. Charger un nouveau bloc, ou recharger une formule modifiée, conserve donc les valeurs existantes.
- La machine tient une table des formules par nom. Exécuter un module y enregistre ses formules ; une formule du même nom est remplacée. C'est le mécanisme du développement vivant (charte, art. 10) : les appels suivants trouvent la nouvelle version.

---

## 6. Journal d'annulation

- Chaque `ÉCRIRE` range dans le journal le nom et son ancienne valeur (ou l'absence de valeur) ; chaque `ÉCRIRE_CHAMP`, l'objet, le champ et l'ancienne valeur. Les champs d'un objet créé pendant l'exécution n'entrent pas au journal : l'objet disparaît avec elle.
- Si l'exécution échoue (division par zéro, nombre trop grand), la machine rejoue le journal du plus récent au plus ancien, puis le vide. Aucun nom ne garde de valeur écrite pendant l'exécution ratée.
- Si l'exécution réussit, le journal se vide.
- La boucle interactive exécute chaque saisie comme une unité (grammaire, § 3.3).
- En cas d'échec, la table des formules revient aussi à son état d'avant : les formules ajoutées disparaissent, les formules remplacées retrouvent leur version précédente.
- Les cases locales ne passent pas par le journal : elles disparaissent avec leur cadre.
- Seule la première écriture d'un nom au cours d'une exécution entre au journal : c'est la valeur d'avant l'exécution qu'il faut pouvoir rendre. Une boucle qui modifie un nom un million de fois n'occupe qu'une entrée.
- Interruption : un gestionnaire de Ctrl+C lève un drapeau ; la machine le consulte à chaque saut vers l'arrière et à chaque appel, puis échoue avec « Interrompu (Ctrl+C). ». Le journal annule alors l'exécution.
- En v0.2, une erreur annule toute l'exécution ; une action qui échoue n'écrit donc rien (charte, art. 7). La reprise après erreur, qui exigera un point de reprise par appel, viendra plus tard.

---

## 7. Ramasse-miettes

- Tous les objets sont chaînés dans le tas de la machine. Le ramassage marque ce qui est atteignable depuis les racines, puis libère le reste.
- Racines : les cases globales, la pile, les cases locales de chaque cadre, les anciennes valeurs du journal et les objets qu'il mentionne.
- Le marquage suit les champs avec une pile explicite : une longue chaîne d'objets ne fait pas déborder la pile du C.
- Un ramassage a lieu à la fin de chaque exécution, et pendant l'exécution quand le nombre d'objets créés depuis le dernier dépasse un seuil (10'000, ou le double des objets vivants).
- Les cycles sont libérés comme le reste.

## 8. Positions

Le bloc garde, pour chaque instruction, la ligne et la colonne de la source. Pour une opération, c'est la position de l'opérateur. Une erreur d'exécution s'exprime ainsi comme une erreur de compilation (charte, art. 8) : `facture.grym:7:18 : erreur : Division par zéro.`

---

## 9. Format du fichier `.grymb`

Entiers non signés, poids faible d'abord (petit-boutiste). `u16` : deux octets ; `u32` : quatre octets.

```
en-tête       "GRYM" (4 octets ASCII), version du format : u16 = 4
blocs         nombre : u32, puis pour chacun :
                nom : longueur u32 et octets UTF-8 (vide pour le programme)
                sorte : u8 (0 = programme, 1 = calcul, 2 = action)
                paramètres : u16, cases locales : u16
                puis constantes, noms, code et positions :
constantes    nombre : u32, puis pour chacune :
                type : u8 (1 = nombre, 2 = texte, 3 = booléen), longueur : u32, octets UTF-8
noms          nombre : u32, puis pour chacun : longueur : u32, octets UTF-8
code          longueur : u32, puis les octets des instructions
positions     nombre : u32, puis pour chacune :
                décalage dans le code : u32, ligne : u32, colonne : u32
classes       nombre : u32, puis pour chacune :
                nom : longueur u32 et octets UTF-8, féminin : u8 (0 ou 1),
                champs : nombre u32, puis pour chacun : longueur u32 et octets UTF-8
```

- Un nombre s'écrit sous sa forme canonique : chiffres, point décimal, signe `-` éventuel (`12.50`, `-3`). Le texte évite tout format binaire propre à une machine et garde la valeur exacte. Un booléen s'écrit `vrai` ou `faux`.
- La version 2 ajoute les instructions 12 à 20 et les constantes booléennes ; la version 3, les modules à plusieurs blocs et les instructions 21 à 26 ; la version 4, les classes et les instructions 27 à 30. Les fichiers des versions 1 à 3 restent lisibles.
- Une classe déjà connue de la machine est redéclarée par un nouveau module : la nouvelle déclaration sert aux objets créés ensuite, les objets existants gardent la leur.


---

## 10. Outils

| Commande | Rôle |
|----------|------|
| `grym compiler facture.grym` | produit `facture.grymb` |
| `grym lancer facture.grym` | compile puis exécute |
| `grym lancer facture.grymb` | exécute un bytecode déjà compilé |
| `grym desassembler facture.grymb` | affiche les instructions en clair |

Chaque ligne donne la ligne source (quand elle change), le décalage de l'instruction, puis l'instruction ; les cibles des sauts renvoient aux décalages. Un module à plusieurs blocs affiche le programme, puis chaque formule sous un titre (« Calcul « carré » : 1 paramètre, 1 case locale »).

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
| 1.2 | 2026-09-21 | Formules : modules à plusieurs blocs, `APPELER`, `RENDRE`, `LIRE_LOCAL`, `ÉCRIRE_LOCAL`, cadres d'appel limités à 1000, table des formules par nom avec remplacement et restauration, vérification par sorte de bloc, format version 3 |
| 1.3 | 2026-09-21 | Boucles et Selon : `ÉCHOUER`, `EXIGER_ENTIER_NATUREL`, cases locales du programme principal, schémas de compilation, journal limité à la première écriture de chaque nom, interruption par Ctrl+C |
| 1.4 | 2026-09-21 | Objets : valeur objet, `NOUVEAU`, `INITIALISER_CHAMP`, `LIRE_CHAMP`, `ÉCRIRE_CHAMP`, journal des champs, ramasse-miettes par marquage et balayage, classes dans le module, format version 4 |

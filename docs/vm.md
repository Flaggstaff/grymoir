# Machine virtuelle et bytecode de GrymoiR

Version 1.27 de la spécification, révisée le 24 septembre 2026.
Référence : Charte de GrymoiR v1.22, art. 2, 3, 7, 8, 10 et 12 ; grammaire 1.35, § 3.3, § 4, § 5, § 9, § 10, § 13 à 19.
Toute modification passe par une révision numérotée.

Périmètre : la machine qui exécute le bytecode, le format du fichier `.grymb`, la base des entités, le ramasse-miettes et le journal d'annulation.

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
| date | jour du calendrier grégorien, du 01.01.0001 au 31.12.9999 (grammaire, § 14) |
| année | année du calendrier, de 1 à 9999 (grammaire, § 14.5) ; s'affiche sans séparateur |
| absent | valeur d'un champ facultatif sans valeur (grammaire, § 16.9) ; elle garde le nom du champ d'où elle vient, pour les messages |
| fichier | contenu d'un fichier, son nom d'origine et son format d'image s'il est reconnu (grammaire, § 15). Immuable, partagé entre les valeurs qui le désignent, libéré quand plus aucune ne le désigne |

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
| 31 | `AUJOURD'HUI` | aucun | empile la date du jour, lue une fois au début de l'exécution |
| 32 | `LIRE_FICHIER` | aucun | remplace le chemin au sommet par le fichier lu sur le disque |
| 33 | `ENREGISTRER` | aucun | dépile un chemin, puis un fichier ; prévoit leur écriture à la fin de l'exécution |
| 34 | `CONSERVER` | aucun | dépile un objet d'entité et le range dans la base |
| 35 | `SUPPRIMER` | aucun | dépile un objet conservé et le met dans la corbeille, avec ceux qui disparaissent avec lui |
| 36 | `CHERCHER` | constante de recherche | dépile les valeurs comparées ; empile une liste, un objet ou un nombre |
| 37 | `TAILLE_LISTE` | aucun | remplace une liste par son nombre d'éléments |
| 38 | `ÉLÉMENT` | aucun | dépile un rang et une liste ; empile l'objet à ce rang |
| 39 | `ABSENT` | aucun | empile la valeur absente |
| 40 | `EST_ABSENT` | aucun | remplace la valeur au sommet par vrai si elle est absente, faux sinon |
| 41 | `SUPPRIMER_DÉFINITIVEMENT` | aucun | dépile un objet conservé et l'efface de la base, avec ceux qui disparaissent avec lui |
| 42 | `RÉTABLIR` | aucun | dépile un objet de la corbeille et le rétablit, avec ceux qui ont disparu avec lui |
| 43 | `GAGNER` | nom de champ | dépile une valeur, puis un objet conservé ; ajoute la valeur à l'ensemble du champ multiple |
| 44 | `PERDRE` | nom de champ | dépile une valeur, puis un objet conservé ; retire la valeur de l'ensemble du champ multiple |
| 45 | `DEMANDER` | nom du type | valide ce qui précède, dépile la question, empile la réponse lue (grammaire, § 17) |
| 46 | `CADRER` | sens (0 défaut, 1 gauche, 2 droite) | dépile une largeur, puis une valeur ; empile le texte cadré (§ 4.2) |
| 47 | `AFFICHER_SANS_LIGNE` | nombre d'éléments n | comme `AFFICHER`, sans le saut de ligne final |
| 48 | `STYLE` | style (0 suisse, 1 française, 2 sans séparateur) | fixe l'affichage des nombres (§ 4.1) |
| 49 | `EFFACER` | aucun | écrit `ESC[2J` puis `ESC[H` si la sortie est un terminal, rien sinon (§ 4.3) |
| 50 | `ESSAYER` | cible (4 octets) : début du bloc d'échec | ouvre un point de reprise (§ 6) ; en cas d'erreur, continue à la cible avec le motif au sommet |
| 51 | `FIN_ESSAI` | aucun | referme le point de reprise le plus récent, sans échec : ce qui a été fait est gardé |
| 52 | `SAISIR` | index d'une constante texte : champs initialisés dans le bloc, séparés par « , » | remplit l'objet neuf au sommet (qui reste) par une question par champ (grammaire, § 19) ; chaque lecture valide ce qui précède, rend le verrou, puis le reprend pour vérifier un lien ou une unicité par une recherche |
| 53 | `COLLER` | aucun | dépile b, puis a ; empile le texte de a suivi de celui de b, écrits comme `AFFICHER` les écrirait (grammaire, § 4.4) |
| 54 | `ÉLIDER` | 0 : `de`, 1 : `que` | remplace la valeur au sommet par son texte précédé de `de ` ou `d'` (`que ` ou `qu'`) selon sa première lettre |
| 55 | `RESAISIR` | aucun | dépile un objet d'entité et le soumet au formulaire de modification (grammaire, § 19) ; les réponses s'accumulent, puis les champs s'écrivent (journal et base) après la dernière |

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
- Méthodes : un bloc de formule peut porter une classe ou une aptitude. Plusieurs blocs partagent alors le même nom, chacun avec une classe ou une aptitude différente. À l'appel, la machine part de la classe réelle du premier argument ; pour chaque classe de la lignée, elle prend la version de la classe, sinon celle d'une de ses aptitudes, sinon elle passe à la classe parente (grammaire, § 13.6 et § 13.7). Deux aptitudes d'une même classe qui fournissent chacune une version sont une erreur d'exécution ; l'analyse l'interdit déjà.
- Chaque appel crée un cadre : ses cases locales, sa position. Un calcul se termine par `RENDRE`, une action par `RETOUR`, qui rend la main à l'appelant. `RETOUR` dans le programme termine l'exécution.
- Au-delà de 1000 cadres imbriqués : « Trop d'appels imbriqués : plus de 1000. »
- Le programme principal a lui aussi des cases locales : compteurs de boucle, bornes, sujets de `Selon`.
- Une variable locale s'adresse par son numéro de case, un nom global par son nom (`LIRE`, `ÉCRIRE`). Un calcul ne contient jamais `LIRE` ni `ÉCRIRE` d'une variable : l'analyse le garantit (grammaire, § 9.4).

- Une phrase `Afficher` à n éléments compile en `AFFICHER n`. Dans la boucle interactive, une expression seule compile en `AFFICHER 1`.
- La création et la modification compilent toutes deux en `ÉCRIRE` : la distinction entre `vaut` et `devient` se vérifie à la compilation (grammaire, § 2.1).
- `est positif`, `est négatif`, `est nul` compilent en une comparaison avec la constante 0 ; `est vrai`, `est faux` en `ÉGAL` avec une constante booléenne ; `n'est pas` ajoute `NON`.
- Les comparaisons d'ordre n'acceptent que deux nombres ou deux dates ; `ÉGAL` et `DIFFÉRENT` acceptent deux valeurs du même type. Sinon : erreur d'exécution.
- `ADDITION` accepte une année et un nombre entier d'années, dans les deux ordres ; `SOUSTRACTION`, une année moins des années (une année) ou deux années (un nombre). Les comparaisons acceptent une année et une année ou un nombre, par valeur ; jamais une date. `LIRE_CHAMP` « année » sur une date empile son année. `INITIALISER_CHAMP` et `ÉCRIRE_CHAMP` convertissent en année un nombre entier de 1 à 9999 rangé dans un champ `(année)`.
- `ADDITION` accepte une date et un nombre entier de jours, dans les deux ordres ; `SOUSTRACTION`, une date moins des jours (une date) ou deux dates (un nombre de jours). Un résultat hors du calendrier est une erreur.
- `SAUTER_SI_FAUX` exige un booléen : « Condition ni vraie ni fausse : la valeur est un nombre. »
- Les instructions de champ désignent la classe et le champ par leur nom, résolu à l'exécution : la machine vérifie que la valeur est un objet et que sa classe a ce champ. `ÉGAL` compare deux objets par identité.
- Typage strict (grammaire, § 16.2) : un champ d'entité ou d'aptitude porte un type. `INITIALISER_CHAMP` et `ÉCRIRE_CHAMP` vérifient la valeur avant de la ranger : texte, nombre, nombre entier, vrai ou faux, date, fichier, image (format reconnu), ou objet de l'entité liée ou d'une entité qui en hérite. Sinon, erreur d'exécution, et rien n'est écrit.
- Valeur absente : `LIRE_CHAMP` d'un champ facultatif sans valeur empile la valeur absente. Les opérations, les comparaisons, `LIRE_CHAMP` et `ÉCRIRE_CHAMP` sur elle, et le choix d'une version de méthode sur elle sont des erreurs d'exécution ; `AFFICHER` écrit `absent`. `INITIALISER_CHAMP` et `ÉCRIRE_CHAMP` ne rangent la valeur absente que dans un champ facultatif.
- Héritage et aptitudes : à l'enregistrement d'une classe, la machine place les champs hérités en tête, puis ceux des aptitudes dans l'ordre d'adoption, puis les champs propres, chacun avec son type. Un champ garde ainsi le même rang dans toute la lignée. La classe parente et les aptitudes doivent être connues (déclarées plus tôt dans le module, ou par un module précédent) ; aucun champ n'est fourni deux fois. Sinon, le module est refusé avant toute exécution.
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
3. Pour le module : le bloc 0 est le programme, les autres des formules nommées. Deux blocs de même nom sont deux versions d'une méthode : chacun a une classe différente, et ils ont même sorte et même nombre de paramètres.

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
- `DEMANDER` referme l'unité annulable : il écrit les fichiers en attente, valide la base, vide le journal, puis lit la réponse ; la transaction reprend ensuite (`BEGIN IMMEDIATE`). Une erreur postérieure n'annule que depuis la dernière question. Sans lecteur installé (`machine_lecteur`), la question est une erreur d'exécution.
- Sans `Essayer`, une erreur annule toute l'exécution ; une action qui échoue n'écrit donc rien (charte, art. 7).
- Point de reprise (`ESSAYER`, grammaire, § 18) : la machine note le cadre, la profondeur de pile, la marque du journal, les écritures sur le disque déjà prévues, et photographie les cases locales du cadre ; la base ouvre un `SAVEPOINT grym_essai_n` (n : rang de l'essai). Puis l'époque avance : un nom déjà écrit repasse au journal à sa prochaine écriture.
- Une erreur, s'il y a un point de reprise, remonte au plus récent : les cadres plus profonds disparaissent, la pile revient à sa profondeur, le journal est rejoué jusqu'à la marque, les écritures prévues depuis sont retirées, la base fait `ROLLBACK TO` puis `RELEASE`, les cases locales retrouvent leur photographie. Le motif (le message, sans position) est empilé, et l'exécution continue à la cible : le bloc d'échec commence par `ÉCRIRE_LOCAL` dans la case du motif. L'époque avance encore.
- `FIN_ESSAI` fait `RELEASE` ; les entrées du journal restent, pour un échec englobant. Le compilateur émet un `FIN_ESSAI` par essai traversé avant `RENDRE` et avant le saut de `Sortir de la boucle` ou `Passer au tour suivant`. `RETOUR` et `RENDRE` referment aussi les essais du cadre qui se termine.
- `DEMANDER` dans un essai : chaque point de reprise ouvert repart de la question (marques remises à zéro, nouvelle photographie, nouveaux `SAVEPOINT` après `BEGIN IMMEDIATE`). `DEMANDER` fait aussi avancer l'époque.
- `DEMANDER` et `SAISIR` : une ligne faite d'un point seul échoue avec « Saisie annulée. », après avoir repris le verrou de la base et les points de reprise : l'essai qui la rattrape retrouve une transaction ouverte.
- Jamais rattrapées : l'interruption, la fin de l'entrée ou l'absence de lecteur pendant `DEMANDER`, et l'échec d'une opération de transaction (`COMMIT`, `BEGIN`, `SAVEPOINT`, `ROLLBACK TO`, `RELEASE`).
- Racines du ramasse-miettes : s'y ajoutent les photographies des cases locales.

---

## 7. Fichiers

- `LIRE_FICHIER` lit tout le contenu, au plus 1 000 000 000 octets (`SQLITE_MAX_LENGTH`), et reconnaît le format d'image à sa signature : PNG (`89 50 4E 47 0D 0A 1A 0A`), JPEG (`FF D8 FF`), GIF (`GIF87a`, `GIF89a`), WebP (`RIFF`, 4 octets, `WEBP`).
- Un chemin relatif part du dossier donné à la machine (`machine_dossier`) : celui du programme pour `grym lancer`, le dossier courant sinon.
- `LIRE_CHAMP` accepte un fichier : `taille`, `format` (`inconnu` sans format d'image), `nom de fichier`. `ÉCRIRE_CHAMP` le refuse.
- `ENREGISTRER` refuse un chemin déjà existant, ou déjà prévu par l'exécution. À la fin d'une exécution réussie, la machine écrit les fichiers prévus, sans jamais en écraser un ; si une écriture échoue, elle retire celles déjà faites, et l'exécution échoue. Une exécution qui échoue n'écrit rien.

## 8. Base des entités

- `src/base.c` parle à SQLite ; la machine l'ouvre au premier besoin, quand une classe enregistrée est une entité, sur le fichier donné par `machine_base` (celui du programme : `factures.grymd`), ou en mémoire.
- Schéma : une table `"e <entité>"` par entité, avec ses champs propres et ceux de ses aptitudes, en colonnes `"c <champ>"` (plus `"n <champ>"`, le nom d'origine, pour un fichier ou une image). Sa colonne `id` désigne la ligne de la classe parente, ou de `grym_objet` pour une entité sans parent, avec `ON DELETE CASCADE`. Un objet conservé a donc une ligne dans chaque table de sa lignée.
- Types SQL : texte, nombre (forme canonique, exacte), date (ISO 8601) en `TEXT` ; nombre entier en `INTEGER` ; vrai ou faux en `INTEGER` 0 ou 1 ; fichier et image en `BLOB` ; lien en `INTEGER` qui référence la table de l'entité liée. Tous `NOT NULL` ; `, unique` en `UNIQUE`.
- `grym_objet` distribue les identifiants (`AUTOINCREMENT` : jamais réattribués) et note la classe réelle ; `grym_schema` garde la définition de chaque entité.
- Un objet porte son identifiant en base, 0 s'il n'est pas conservé. `CONSERVER` et `SUPPRIMER` le changent au journal, qui le restaure si l'exécution échoue.
- `ÉCRIRE_CHAMP` sur un objet conservé écrit aussi la colonne en base.
- Recherche : une constante de type 5 décrit la recherche, « entité ␟ mode ␟ champ du tri ␟ décroissant ␟ condition », avec le séparateur U+001F. Mode 0 : liste (boucle), 1 : un seul objet, 2 : nombre ; 3, 4 et 5 : les mêmes, dans la corbeille. Toute recherche écarte (modes 0 à 2) ou ne retient (modes 3 à 5) que les objets dont `grym_objet.supprime` est rempli. La condition s'écrit en préfixe : `(I?k)` pour une relation inverse (le lien de l'entité qui peut désigner l'objet `?k`, choisi à l'exécution selon sa classe réelle ; aucun ou plusieurs : erreur), `(e A B)`, `(o A B)`, `(n A)`, `(op [champ] ?k)` pour `=`, `!`, `<`, `>`, `l` (≤), `g` (≥), et `(P [champ])`, `N`, `0`, `V`, `F` pour les tournures ; `?k` désigne la k-ième valeur dépilée. La machine traduit en SQL, jointures de la lignée comprises.
- Collations enregistrées auprès de SQLite : `GRYM_NOMBRE` compare deux nombres canoniques en décimal exact ; `GRYM_TEXTE` compare sans accents ni casse, puis octet par octet.
- Liste : valeur interne, jamais visible du langage, figée au moment de la recherche (identifiants et classes).
- Carte d'identité : identifiant en base → objet en mémoire. Un objet retrouvé est une coquille de sa classe réelle ; `LIRE_CHAMP`, `ÉCRIRE_CHAMP` et `SUPPRIMER` lisent d'abord ses champs. Ses liens deviennent des coquilles à leur tour. Le ramasse-miettes retire de la carte les objets qu'il libère.
- Migrations : à la préparation d'une entité, la machine compare sa définition à celle de `grym_schema`. Colonne nouvelle : `ALTER TABLE … ADD COLUMN`, avec la valeur de départ en `DEFAULT` (un lien ajouté reste sans `NOT NULL` en base, la machine vérifie qu'il est rempli) ; unicité nouvelle : index `"u <entité>.<champ>"` ; nombre entier devenu nombre : colonne recopiée en texte canonique ; colonne retirée d'une table vide : `DROP COLUMN`. Tout autre changement est refusé, avant toute écriture, dans la transaction de l'exécution.
- Corbeille : `grym_objet` a deux colonnes, `supprime` (date ISO) et `supprime_avec` (identifiant de l'objet dont la suppression a entraîné celle-ci). Un nouveau lien vers un objet de la corbeille est refusé. La suppression définitive calcule l'ensemble des objets qui disparaissent avec l'objet, vérifie qu'aucun lien extérieur ne les désigne, puis les efface avec `PRAGMA defer_foreign_keys` ; ceux qui vivent en mémoire cessent d'être conservés (journalisé).
- Champ multiple (grammaire, § 16.13) : bit 3 du champ ; aucune colonne dans la table de l'entité, mais une table de liaison `"m <entité>.<champ>"`, colonnes `a` (l'objet qui porte le champ, `ON DELETE CASCADE`) et `b` (l'objet gagné), clé primaire `(a, b)`, index sur `b`. `GAGNER` écrit `INSERT OR IGNORE`, `PERDRE` écrit `DELETE`. `LIRE_CHAMP`, `ÉCRIRE_CHAMP` et `INITIALISER_CHAMP` le refusent. La suppression définitive compte les liaisons qui désignent l'objet comme les liens simples. Définition dans `grym_schema` : `:multiple`.
- Conditions des champs multiples : `(I?k)` retient aussi un champ multiple de l'entité (`g.id IN (SELECT a … WHERE b = ?)`) ou de l'objet `?k` (`g.id IN (SELECT b … WHERE a = ?)`) ; `(M?k[champ])` désigne le champ multiple nommé de l'objet `?k`, et revient à `(I?k)` si l'objet ne l'a pas ; `(p[champ]?k)` : l'objet examiné a gagné `?k` dans ce champ (`parmi`).
- `machine_annulation` dit, après une exécution ratée, ce qui a été annulé (grammaire, § 3.3).
- Transaction : `BEGIN IMMEDIATE` au début de chaque exécution qui connaît une entité ; à la fin, écritures sur le disque, puis `COMMIT` ; en cas d'échec ou d'interruption, `ROLLBACK`, et les fichiers déjà écrits par cette fin d'exécution sont retirés.

## 9. Ramasse-miettes

- Tous les objets sont chaînés dans le tas de la machine. Le ramassage marque ce qui est atteignable depuis les racines, puis libère le reste.
- Racines : les cases globales, la pile, les cases locales de chaque cadre, les anciennes valeurs du journal et les objets qu'il mentionne.
- Le marquage suit les champs avec une pile explicite : une longue chaîne d'objets ne fait pas déborder la pile du C.
- Un ramassage a lieu à la fin de chaque exécution, et pendant l'exécution quand le nombre d'objets créés depuis le dernier dépasse un seuil (10'000, ou le double des objets vivants).
- Les cycles sont libérés comme le reste.

## 10. Positions

Le bloc garde, pour chaque instruction, la ligne et la colonne de la source. Pour une opération, c'est la position de l'opérateur. Une erreur d'exécution s'exprime ainsi comme une erreur de compilation (charte, art. 8) : `facture.grym:7:18 : erreur : Division par zéro.`

---

## 11. Format du fichier `.grymb`

Entiers non signés, poids faible d'abord (petit-boutiste). `u16` : deux octets ; `u32` : quatre octets.

```
en-tête       "GRYM" (4 octets ASCII), version du format : u16 = 23
blocs         nombre : u32, puis pour chacun :
                nom : longueur u32 et octets UTF-8 (vide pour le programme)
                classe du premier paramètre : longueur u32 et octets UTF-8 (vide sauf pour une méthode)
                sorte : u8 (0 = programme, 1 = calcul, 2 = action)
                paramètres : u16, cases locales : u16
                puis constantes, noms, code et positions :
constantes    nombre : u32, puis pour chacune :
                type : u8 (1 = nombre, 2 = texte, 3 = booléen, 4 = date, 5 = recherche), longueur : u32, octets UTF-8
noms          nombre : u32, puis pour chacun : longueur : u32, octets UTF-8
code          longueur : u32, puis les octets des instructions
positions     nombre : u32, puis pour chacune :
                décalage dans le code : u32, ligne : u32, colonne : u32
classes       nombre : u32, puis pour chacune :
                nom : longueur u32 et octets UTF-8, sorte : u8 (bit 0 féminin, bit 1 aptitude, bit 2 entité),
                classe parente : longueur u32 et octets UTF-8 (vide sans héritage),
                pluriel : longueur u32 et octets UTF-8 (vide sauf pluriel irrégulier),
                aptitudes adoptées : nombre u32, puis pour chacune : longueur u32 et octets UTF-8,
                champs : nombre u32, puis pour chacun : nom (longueur u32 et octets UTF-8),
                  type (longueur u32 et octets UTF-8, vide sans type), drapeaux : u8 (bit 0 unique, bit 1 facultatif,
                  bit 2 disparaît avec l'objet désigné, bit 3 multiple),
                  valeur de départ (longueur u32 et octets UTF-8, forme canonique, vide sans valeur)
```

- Un nombre s'écrit sous sa forme canonique : chiffres, point décimal, signe `-` éventuel (`12.50`, `-3`). Le texte évite tout format binaire propre à une machine et garde la valeur exacte. Un booléen s'écrit `vrai` ou `faux`, une date en ISO 8601 (`2026-09-21`).
- La version 2 ajoute les instructions 12 à 20 et les constantes booléennes ; la version 3, les modules à plusieurs blocs et les instructions 21 à 26 ; la version 4, les classes et les instructions 27 à 30 ; la version 5, la classe parente ; la version 6, la classe des méthodes ; la version 7, les aptitudes (déclarées parmi les classes, avec leur bit) et les aptitudes adoptées ; la version 8, les constantes date et l'instruction 31 ; la version 9, les instructions 32 et 33 ; la version 10, les entités (bit 2), le pluriel, le type et l'unicité des champs ; la version 11, les instructions 34 et 35 ; la version 12, les constantes de recherche et les instructions 36 à 38 ; la version 13, les valeurs de départ ; la version 14, les champs facultatifs (bit 1) et les instructions 39 et 40 ; la version 15, le bit 2 et les instructions 41 et 42 ; la version 16, le bit 3 et les instructions 43 et 44 ; la version 17, l'instruction 45 ; la version 18, les instructions 46 à 48 ; la version 19, l'instruction 49 ; la version 20, les instructions 50 et 51 ; la version 21, l'instruction 52 ; la version 22, les instructions 53 et 54 ; la version 23, l'instruction 55. Les fichiers des versions 1 à 22 restent lisibles ; leur `SUPPRIMER` met désormais dans la corbeille.
- Une classe déjà connue de la machine est redéclarée par un nouveau module : la nouvelle déclaration sert aux objets créés ensuite, les objets existants gardent la leur.


---

## 12. Outils

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
| 1.5 | 2026-09-21 | Héritage : classe parente dans le module, champs hérités en tête, format version 5 |
| 1.6 | 2026-09-21 | Méthodes : classe du premier paramètre dans le bloc, choix de la version à l'appel selon la lignée, format version 6 |
| 1.7 | 2026-09-21 | Aptitudes : enregistrées comme des classes marquées, adoptées par les classes ; champs apportés ; choix de version classe, puis aptitudes, puis parent ; format version 7 |
| 1.8 | 2026-09-21 | Dates : valeur date, constante de type 4, `AUJOURD'HUI`, addition et soustraction de dates, comparaisons ; format version 8 |
| 1.9 | 2026-09-21 | Fichiers : valeur fichier partagée, `LIRE_FICHIER`, `ENREGISTRER`, champs des fichiers, écritures différées toutes ou aucune ; format version 9 ; renumérotation des § 7 à 11 |
| 1.10 | 2026-09-21 | Entités : type et unicité des champs, bit d'entité, pluriel ; vérification des types avant toute écriture de champ ; format version 10 |
| 1.11 | 2026-09-21 | Base des entités (§ 8) : schéma, types SQL, identifiants, transaction ; `CONSERVER`, `SUPPRIMER` ; format version 11 ; renumérotation des § 9 à 12 |
| 1.12 | 2026-09-21 | Recherche : constante de type 5, `CHERCHER`, `TAILLE_LISTE`, `ÉLÉMENT`, collations exactes, carte d'identité, chargement à la demande ; format version 12 |
| 1.13 | 2026-09-21 | Migrations de schéma, valeur de départ des champs, format version 13 ; `machine_annulation` |
| 1.14 | 2026-09-22 | Valeur absente, `ABSENT`, `EST_ABSENT`, champs facultatifs (colonnes sans `NOT NULL`, `IS NULL`, absents triés en dernier) ; format version 14 |
| 1.15 | 2026-09-22 | Condition `(I?k)` : relation inverse |
| 1.16 | 2026-09-22 | Corbeille et cascade : `SUPPRIMER` met dans la corbeille, `SUPPRIMER_DÉFINITIVEMENT`, `RÉTABLIR`, modes 3 à 5, bit 2 ; format version 15 |
| 1.17 | 2026-09-22 | Champs multiples : `GAGNER`, `PERDRE`, bit 3 des champs, tables de liaison, conditions `(M?k[champ])` et `(p[champ]?k)`, `(I?k)` étendue ; format version 16 ; `CHERCHER` sans paramètre ne calcule plus d'adresse sur une pile vide |
| 1.18 | 2026-09-22 | Valeur année : calculs, comparaisons, affichage, `LIRE_CHAMP` « année » d'une date, conversion à l'écriture d'un champ `(année)`, colonne `INTEGER` |
| 1.19 | 2026-09-22 | `DEMANDER` : la question valide fichiers, base et journal, puis lit la réponse ; lecteur installé par la machine ; format version 17 |
| 1.20 | 2026-09-22 | Mise en forme : `CADRER`, `AFFICHER_SANS_LIGNE`, `STYLE` ; format version 18 |
| 1.21 | 2026-09-22 | `EFFACER` : séquence ECMA-48, seulement sur un terminal ; `machine_terminal` ; format version 19 |
| 1.22 | 2026-09-22 | En-tête et périmètre remis à jour |
| 1.23 | 2026-09-23 | Reprise après erreur : `ESSAYER`, `FIN_ESSAI`, points de reprise (journal, pile, cadres, cases locales, fichiers, `SAVEPOINT`) ; l'époque avance à chaque question ; format version 20 |
| 1.24 | 2026-09-23 | `SAISIR` : formulaire d'un objet neuf ; format version 21 |
| 1.25 | 2026-09-23 | Annulation d'une question par un point seul, rattrapable |
| 1.26 | 2026-09-23 | `COLLER`, `ÉLIDER` : assembler des textes ; format version 22 |
| 1.27 | 2026-09-24 | `RESAISIR` : modifier par formulaire ; format version 23 |

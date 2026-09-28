# Premiers pas avec GrymoiR

Ce guide te fait construire, du premier mot au dernier écran, l'application d'une petite association : ses membres, leurs cotisations, et sa comptabilité en partie double. Chaque chapitre ajoute une notion du langage et renvoie à la section de la grammaire qui la détaille (Aide > Le langage GrymoiR, ou F1 sur un mot du code).

Rien ici n'est à croire sur parole : chaque programme du guide se trouve dans le dépôt (`docs/guide/chapitre-01.grym` et les suivants), et les essais de GrymoiR l'exécutent à chaque modification du langage. Chaque extrait de code que tu lis vient de ces programmes, et chaque résultat affiché est celui qu'ils produisent.

L'association s'appelle les Amis du Tilleul. Elle n'existe pas ; ses comptes seront justes quand même.

## 1. Premiers pas

### Un projet, un fichier

Dans GrymoiR, un projet est un dossier. Crées-en un, par exemple `Amis du Tilleul` dans tes Documents, puis ouvre-le dans l'atelier : Fichier > Ouvrir un projet…

Crée ensuite ton premier fichier : Fichier > Nouveau fichier…, et nomme-le `association`. L'atelier ajoute l'extension `.grym` et l'ouvre dans l'onglet Code.

### Afficher

Tape cette phrase, puis Programme > Lancer :

```grymoir
Afficher « Bienvenue chez les Amis du Tilleul. ».
```

```sortie
Bienvenue chez les Amis du Tilleul.
```

Une phrase GrymoiR commence par une majuscule et finit par un point, comme en français. Le texte à afficher se met entre guillemets `« »`. Ton clavier n'en a pas ? Tape `"` : l'éditeur écrit `«  »` et place le curseur au milieu.

### Nommer une valeur

```grymoir
L'effectif vaut 12.
Afficher « Membres : » puis l'effectif.
```

```sortie
Membres : 12
```

`vaut` crée un nom et lui donne une valeur. `puis` sépare les éléments d'un même affichage, que GrymoiR sépare par une espace. L'article (`le`, `la`, `l'`) est obligatoire en début de phrase ; ailleurs, il est facultatif : `puis l'effectif` et `puis effectif` disent la même chose.

### Modifier une valeur

```grymoir
L'effectif devient l'effectif + 3.
Afficher « Après l'assemblée : » puis l'effectif.
```

```sortie
Après l'assemblée : 15
```

`devient` modifie un nom qui existe. GrymoiR distingue exprès les deux verbes : une faute de frappe dans `L'efectif devient 20.` ne crée jamais un nom en silence, elle produit une erreur (« `efectif` n'existe pas »). Grammaire, § 2.1.

### Les remarques

Une ligne qui commence par `Remarque :` est pour le lecteur humain ; le programme l'ignore.

```grymoir
Remarque : Premiers pas, chapitre 1 : afficher, nommer, modifier.
```

Le programme complet du chapitre : `docs/guide/chapitre-01.grym`.

## 2. Calculer et décider

### Des nombres exacts

Une association compte de l'argent : GrymoiR calcule en décimal exact, jamais en virgule flottante.

```grymoir
Afficher 0,1 + 0,2.
Afficher 100 ÷ 3.
```

```sortie
0,3
33,33333333333333333333333333
```

Beaucoup de langages affichent `0,30000000000000004` pour la première ligne ; GrymoiR, jamais. Une division qui ne tombe pas juste s'arrête à 28 chiffres significatifs. La virgule décimale s'écrit à la française : `3,5`, pas `3.5`. Grammaire, § 3.

### Les décimales se conservent

```grymoir
Le tarif adulte vaut 80.
Le rabais junior vaut 0,50.
```

`0,50` et `0,5` ont la même valeur, mais pas la même écriture, et GrymoiR garde les décimales écrites, comme un comptable : `80 × 0,50` donne `40,00`, alors que `80 × 0,5` donnerait `40,0`. Pour des montants, écris les centimes.

Attention à un piège de nom : `La cotisation d'un adulte vaut 80.` ne crée pas un nombre. `d'un` annonce un paramètre, et la phrase définit un calcul (chapitre 3). D'où `le tarif adulte`.

### Décider

```grymoir
La cotisation vaut le tarif adulte.
Si l'âge est inférieur à 18 :
    La cotisation devient le tarif adulte × le rabais junior.
Afficher « À » puis l'âge puis « ans, la cotisation est de » puis la cotisation puis « francs. ».
```

```sortie
À 16 ans, la cotisation est de 40,00 francs.
```

`Si` suivi d'une condition et de deux-points ouvre un bloc : les phrases indentées en dessous (quatre espaces) ne s'exécutent que si la condition est vraie. La condition s'écrit en mots (`est inférieur à`, `est supérieure à`, `est nul`) ou en symboles (`<`, `≥`). Les adjectifs s'accordent : `la cotisation est supérieure à 50`. Grammaire, § 5.

Pour une seule phrase, la forme courte suffit, avec une virgule, et `Sinon` pour l'autre cas :

```grymoir
Si la cotisation est supérieure à 50, afficher « Cotisation pleine. ».
Sinon, afficher « Cotisation réduite. ».
```

```sortie
Cotisation réduite.
```

Programme complet : `docs/guide/chapitre-02.grym`.

## 3. Répéter, nommer un calcul

### Un calcul

Le barème des cotisations dépend de l'âge. Plutôt que de répéter la règle partout, on la nomme :

```grymoir
La cotisation d'un âge :
    Si âge < 18, rendre 40,00.
    Si âge ≥ 65, rendre 60,00.
    Rendre 80,00.
```

Un calcul se définit comme il s'utilise : `la cotisation d'un âge` déclare un calcul `cotisation` à un paramètre, `âge` ; on l'appelle avec `la cotisation de 16`, ou `la cotisation d'âge`, si un nom `âge` contient la valeur. `Rendre` donne le résultat et termine le calcul. Un calcul ne voit que ses paramètres : il ne peut ni afficher, ni lire une variable du programme. Même entrée, même résultat, toujours. Grammaire, § 9.1 et § 9.4.

### Une action

Une action, elle, fait quelque chose : afficher, modifier, conserver.

```grymoir
Pour présenter le barème :
    Afficher « Barème des cotisations ».
    Pour chaque âge de 16 à 19 :
        Afficher âge puis « ans : » puis la cotisation d'âge.
    Afficher 65 puis « ans : » puis la cotisation de 65.
```

Elle se déclare par `Pour` suivi de son nom, un verbe à l'infinitif, et s'appelle en commençant une phrase par ce verbe :

```grymoir
Présenter le barème.
```

```sortie
Barème des cotisations
16 ans : 40,00
17 ans : 40,00
18 ans : 80,00
19 ans : 80,00
65 ans : 60,00
```

Le nom d'une action ne peut pas commencer par un mot du langage : `Pour afficher le barème` est refusé, parce que `Afficher` commence déjà une phrase. Grammaire, § 9.2 et § 10.7.

### Répéter

`Pour chaque âge de 16 à 19` répète le bloc pour 16, 17, 18 et 19 : les deux bornes sont incluses. Le compteur, `âge`, n'existe que dans la boucle. Pour accumuler, on crée le nom avant la boucle et on le modifie dedans :

```grymoir
Le total vaut 0.
Pour chaque âge de 16 à 20 :
    Le total devient le total + la cotisation d'âge.
Afficher « Cinq membres de 16 à 20 ans paient » puis le total puis « francs. ».
```

```sortie
Cinq membres de 16 à 20 ans paient 320,00 francs.
```

Retiens cette forme : c'est ainsi qu'on fera les soldes des comptes au chapitre 9. D'autres boucles existent : `Tant que`, `Répéter 3 fois`. Grammaire, § 10.

Programme complet : `docs/guide/chapitre-03.grym`.

## 4. Les membres

### Une entité

Jusqu'ici, tout disparaissait à la fin du programme. Une association veut garder ses membres d'un lancement à l'autre : on déclare une entité, une classe dont les objets se conservent.

```grymoir
Un membre, conservé, a :
    un numéro (nombre entier), unique,
    un nom (texte),
    un prénom (texte),
    une date d'entrée (date).
```

`, conservé,` fait du membre une entité. Chaque champ déclare son type entre parenthèses : `(texte)`, `(nombre)`, `(nombre entier)`, `(date)`, `(vrai ou faux)`… GrymoiR vérifie qu'aucune valeur du mauvais type n'entre jamais dans la base. `, unique` interdit que deux membres aient le même numéro. Grammaire, § 16.1 et § 16.2.

Tu n'écris jamais de SQL : GrymoiR range les membres dans un fichier à côté de ton programme, `association.grymd`, qu'il crée au premier besoin. Grammaire, § 16.5.

### Créer et conserver

```grymoir
Pour inscrire un numéro et un nom et un prénom et une entrée :
    Le nouveau vaut un nouveau membre :
        Le numéro vaut numéro.
        Le nom vaut nom.
        Le prénom vaut prénom.
        La date d'entrée vaut entrée.
    Conserver le nouveau.
```

`un nouveau membre :` crée un objet et remplit ses champs dans le bloc qui suit. Tant qu'il n'est pas conservé, il ne vit qu'en mémoire ; `Conserver` le range dans la base. Une date s'écrit `jour.mois.année`, l'année sur quatre chiffres : `15.03.2019`.

Si tu lances le programme deux fois, tu ne veux pas deux fois les mêmes membres. D'où cette garde :

```grymoir
Si le nombre de membres conservés est nul :
    Inscrire 1 et « Rochat » et « Anne » et 15.03.2019.
    Inscrire 2 et « Bapst » et « Louis » et 02.09.2021.
    Inscrire 3 et « Dupasquier » et « Élodie » et 10.01.2024.
```

`le nombre de membres conservés` compte ; le pluriel se déduit tout seul. Sans la garde, le second lancement échouerait d'ailleurs : le numéro 1 existe déjà, et `unique` l'interdit. Rien ne serait écrit : un lancement qui échoue n'écrit rien du tout, ni dans la base ni ailleurs (charte, art. 7).

### Retrouver

```grymoir
Afficher « Membres : » puis le nombre de membres conservés.
Pour chaque membre conservé, par nom :
    Afficher le numéro du membre puis le prénom du membre puis le nom du membre.
```

```sortie
Membres : 3
2 Louis Bapst
3 Élodie Dupasquier
1 Anne Rochat
```

`Pour chaque membre conservé` parcourt la base ; dans le bloc, `le membre` désigne celui du tour. `, par nom` trie, comme un dictionnaire, sans tenir compte des accents ni des majuscules. Un champ se lit comme en français : `le prénom du membre`.

Pour un seul membre, `dont` pose une condition :

```grymoir
Le trésorier vaut le membre conservé dont le numéro est 2.
Afficher « Trésorier : » puis le prénom du trésorier puis le nom du trésorier.
```

```sortie
Trésorier : Louis Bapst
```

`le membre conservé dont …` exige un et un seul résultat ; aucun, ou plusieurs, est une erreur qui dit combien. Les conditions `dont` se combinent avec le tri :

```grymoir
Pour chaque membre conservé dont la date d'entrée < 01.01.2022, par date d'entrée :
    Afficher le nom du membre puis « est membre depuis le » puis la date d'entrée du membre.
```

```sortie
Rochat est membre depuis le 15.03.2019
Bapst est membre depuis le 02.09.2021
```

Grammaire, § 16.3 et § 16.4.

Programme complet : `docs/guide/chapitre-04.grym`.

## 5. Catégories et cotisations

### Une entité liée à une autre

Chaque membre appartient à une catégorie, qui fixe son tarif :

```grymoir
Une catégorie, conservée, a :
    un nom (texte), unique,
    un tarif (nombre).
```

Le membre reçoit un lien vers sa catégorie : un champ dont le type est le nom d'une entité.

```grymoir
    une date d'entrée (date),
    une catégorie (catégorie), facultative.
```

### Changer une entité qui a déjà des objets

Ta base contient déjà trois membres, sans catégorie. GrymoiR modifie la base tout seul au lancement suivant (une migration), mais il ne détruit ni n'invente jamais une donnée en silence. Un lien obligatoire tout neuf n'aurait pas de valeur pour les trois membres existants : il serait refusé. Déclaré `, facultative`, il est accepté, et les membres existants le reçoivent absent. Grammaire, § 16.7.

Un champ absent se teste avec `est absent` ou `est présent`. On donne leur catégorie aux membres qui n'en ont pas :

```grymoir
Pour chaque membre conservé dont la catégorie est absente :
    La catégorie du membre devient la catégorie conservée dont le nom est « Actif ».
```

Modifier le champ d'un objet conservé modifie la base aussitôt : pas besoin de le conserver à nouveau. Grammaire, § 16.9.

### Les cotisations

```grymoir
Une cotisation, conservée, a :
    un membre (membre), et disparaît avec lui,
    une année (nombre entier),
    un montant (nombre),
    une date de paiement (date), facultative.
```

`, et disparaît avec lui` : si un membre est supprimé, ses cotisations le suivent (dans la corbeille, puis, s'il est supprimé définitivement, hors de la base). Grammaire, § 16.12.

Une cotisation par membre et par exercice, créée une seule fois :

```grymoir
L'exercice vaut 2026.
Pour chaque membre conservé :
    Si le nombre de cotisations du membre dont l'année = exercice est nul :
        La cotisation vaut une nouvelle cotisation :
            Le membre vaut membre.
            L'année vaut exercice.
            Le montant vaut le tarif de la catégorie du membre.
        Conserver la cotisation.
```

`les cotisations du membre` : les cotisations dont le lien désigne ce membre. Rien n'est à déclarer, GrymoiR déduit l'inverse du lien. Grammaire, § 16.10. Les compléments s'enchaînent de droite à gauche : `le tarif de la catégorie du membre`.

Pourquoi `exercice` et pas `année` ? Essaie `L'année vaut 2026.` : GrymoiR refuse, parce que dans une condition `dont`, `année` désigne le champ de la cotisation examinée, pas ta variable. Son message te demande de renommer la variable ; c'est le bon réflexe.

### Encaisser, lister les impayés

```grymoir
Le payeur vaut le membre conservé dont le numéro est 1.
Pour chaque cotisation du payeur dont l'année = exercice et la date de paiement est absente :
    La date de paiement de la cotisation devient 20.02.2026.
```

```grymoir
Afficher « Cotisations » puis exercice puis « impayées : ».
Pour chaque cotisation conservée dont l'année = exercice et la date de paiement est absente, par montant :
    Afficher le prénom du membre de la cotisation puis le nom du membre de la cotisation puis le montant de la cotisation.
Afficher « Cotisations payées : » puis le nombre de cotisations conservées dont la date de paiement est présente.
```

```sortie
Cotisations 2026 impayées :
Élodie Dupasquier 40,00
Louis Bapst 80,00
Cotisations payées : 1
```

Une année s'affiche `2026`, sans séparateur : un entier de quatre chiffres ne se groupe pas, comme en français, où l'on écrit « en 2026 » et non « en 2 026 ». Un montant, lui, se groupe : `1'234,50`. Grammaire, § 4.1.

Au chapitre 8, encaisser une cotisation passera aussi l'écriture comptable ; il nous faut d'abord des comptes.

Programme complet : `docs/guide/chapitre-05.grym`.

## 6. Le plan comptable

### Un fichier qui grandit

À partir d'ici, ton fichier `association.grym` garde tout ce qui fait l'application : les entités, les actions, les gardes qui créent les données de départ. Les affichages d'essai des chapitres précédents peuvent partir ; chaque chapitre ajoute les siens. C'est exactement le contenu de `docs/guide/chapitre-06.grym`.

### Les comptes

Une comptabilité en partie double range chaque mouvement dans des comptes. Un compte a un numéro et un intitulé :

```grymoir
Un compte, conservé, a :
    un numéro (nombre entier), unique,
    un intitulé (texte).
```

```grymoir
Si le nombre de comptes conservés est nul :
    Créer 1000 et « Caisse ».
    Créer 1020 et « Banque ».
    Créer 2000 et « Créanciers ».
    Créer 2800 et « Fortune de l'association ».
    Créer 3000 et « Cotisations ».
    Créer 3200 et « Dons ».
    Créer 4000 et « Frais des manifestations ».
    Créer 4500 et « Loyer du local ».
    Créer 4800 et « Frais bancaires ».
```

L'action `Créer` ressemble à `Inscrire` du chapitre 4 ; elle est dans le programme complet.

### La nature d'un compte

Chaque compte a une nature : un actif (ce que l'association possède), un passif (ce qu'elle doit, et sa fortune), un produit (ce qu'elle gagne), une charge (ce qu'elle dépense). Dans notre plan, le premier chiffre du numéro la donne. On pourrait l'écrire dans un champ ; mais un champ qui répète ce que le numéro dit déjà finit un jour par le contredire. On la calcule :

```grymoir
La nature d'un numéro :
    Selon numéro :
        Cas de 1000 à 1999, rendre « actif ».
        Cas de 2000 à 2999, rendre « passif ».
        Cas de 3000 à 3999, rendre « produit ».
    Rendre « charge ».
```

`Selon` compare une valeur à des cas, dans l'ordre, et n'exécute que le premier qui convient. Un cas est une valeur, un intervalle (`de 1000 à 1999`, bornes comprises) ou une comparaison (`Cas supérieur à 5000`). Ici, chaque cas rend son résultat et termine le calcul ; si aucun ne convient, on arrive au `Rendre` final. Grammaire, § 10.5.

### En colonnes

```grymoir
Afficher « Plan comptable des Amis du Tilleul ».
Pour chaque compte conservé, par numéro :
    Afficher le numéro du compte puis l'intitulé du compte sur 28 puis la nature du numéro du compte.
```

```sortie
Plan comptable des Amis du Tilleul
1000 Caisse                       actif
1020 Banque                       actif
2000 Créanciers                   passif
2800 Fortune de l'association     passif
3000 Cotisations                  produit
3200 Dons                         produit
4000 Frais des manifestations     charge
4500 Loyer du local               charge
4800 Frais bancaires              charge
```

`sur 28` réserve 28 caractères à l'intitulé, pour que la colonne suivante s'aligne ; `à droite` alignerait des montants. Grammaire, § 4.2. Les numéros s'affichent `1020`, sans séparateur, comme on les écrit.

Une condition `dont` peut porter sur un intervalle :

```grymoir
Afficher « Comptes de produits : » puis le nombre de comptes conservés dont le numéro ≥ 3000 et le numéro < 4000.
```

```sortie
Comptes de produits : 2
```

Programme complet : `docs/guide/chapitre-06.grym`.

---

Les chapitres 7 à 12 (les écritures, les soldes, la clôture, les écrans) sont en cours d'écriture.

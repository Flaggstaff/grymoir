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

---

Les chapitres 4 à 12 (les membres, les cotisations, le plan comptable, les écritures, les soldes, la clôture, les écrans) sont en cours d'écriture.

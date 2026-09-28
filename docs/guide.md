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

Retiens cette forme : c'est ainsi qu'on additionnera les soldes des comptes au chapitre 10. D'autres boucles existent : `Tant que`, `Répéter 3 fois`. Grammaire, § 10.

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
    un intitulé (texte), unique.
```

L'intitulé est unique lui aussi : deux comptes ne portent jamais le même nom, et, au chapitre 11, c'est par son intitulé qu'un écran te laissera choisir un compte dans un menu.

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

## 7. Les écritures composées

### Repartir d'une base vide

Au chapitre 5, on a marqué la cotisation d'Anne payée en changeant directement sa date de paiement. La comptabilité, elle, n'a rien reçu : aucune écriture ne dit que 80 francs sont entrés en caisse. À partir d'ici, un paiement passera toujours par une écriture. Pour que tes comptes partent justes, **efface `association.grymd`** (le fichier de données, à côté de ton programme) : au prochain lancement, les gardes recréeront membres, catégories, cotisations et comptes, sans ce paiement fantôme.

### Une écriture et ses lignes

En partie double, chaque mouvement se lit deux fois : ce qui entre quelque part sort d'ailleurs. Une écriture regroupe des lignes ; chaque ligne porte un compte et un montant au débit ou au crédit, et le total des débits doit égaler le total des crédits.

```grymoir
Une écriture, conservée, a :
    une date (date),
    une pièce (texte), unique,
    un libellé (texte),
    une comptabilisation (date), facultative.

Une ligne, conservée, a :
    une écriture (écriture), et disparaît avec elle,
    un compte (compte),
    un débit (nombre),
    un crédit (nombre).
```

Une écriture en cours de saisie est forcément déséquilibrée tant que sa dernière ligne manque. On distingue donc deux états : tant que sa date de comptabilisation est absente, l'écriture est un **brouillon**, on y ajoute des lignes librement et elle ne compte nulle part ; une fois comptabilisée, elle entre dans les comptes et ne change plus.

### Imputer, comptabiliser

```grymoir
Pour imputer une écriture et une référence et un débit et un crédit :
    Si la comptabilisation de l'écriture est présente :
        Refuser « L'écriture » puis la pièce de l'écriture puis « est comptabilisée : elle ne change plus. ».
    La nouvelle vaut une nouvelle ligne :
        L'écriture vaut écriture.
        Le compte vaut le compte conservé dont le numéro est référence.
        Le débit vaut débit.
        Le crédit vaut crédit.
    Conserver la nouvelle.
```

Le paramètre s'appelle `référence`, pas `numéro` : dans `dont le numéro est numéro`, les deux `numéro` désigneraient le champ du compte examiné, et GrymoiR te le dirait, comme au chapitre 5.

```grymoir
Pour comptabiliser une écriture et une date :
    Le total des débits vaut 0.
    Le total des crédits vaut 0.
    Pour chaque ligne de l'écriture :
        Le total des débits devient le total des débits + le débit de la ligne.
        Le total des crédits devient le total des crédits + le crédit de la ligne.
    Si le total des débits ≠ le total des crédits :
        Refuser « Écriture » puis la pièce de l'écriture puis « déséquilibrée : débits » puis le total des débits puis « et crédits » puis le total des crédits.
    La comptabilisation de l'écriture devient date.
```

`Refuser` arrête tout, avec ce message : l'erreur annule ce que l'action, et le programme qui l'a appelée, ont écrit. Une écriture déséquilibrée ne peut donc jamais être comptabilisée. Grammaire, § 18.1. Pourquoi `le total des débits`, et pas `les débits` ? Une phrase qui commence par `Les` est réservée aux champs multiples (§ 16.13) : un nom se crée au singulier.

### Une vraie écriture composée

La fortune de l'association, au 1er janvier, est répartie entre la caisse et la banque : une écriture à trois lignes.

```grymoir
    L'ouverture vaut une nouvelle écriture :
        La date vaut 01.01.2026.
        La pièce vaut « P-001 ».
        Le libellé vaut « Fortune au 1er janvier ».
    Conserver l'ouverture.
    Imputer l'ouverture et 1000 et 300,00 et 0,00.
    Imputer l'ouverture et 1020 et 2200,00 et 0,00.
    Imputer l'ouverture et 2800 et 0,00 et 2500,00.
    Comptabiliser l'ouverture et 01.01.2026.
```

Le traiteur de la fête de printemps, 450 francs, est payé 200 francs en espèces et 250 par banque : une charge au débit, deux sorties au crédit. Tout est dans le programme complet.

### Ce que la base refuse

Une écriture boiteuse, dans un `Essayer`, pour voir ce qui se passe :

```grymoir
Essayer :
    La boiteuse vaut une nouvelle écriture :
        La date vaut 20.04.2026.
        La pièce vaut « P-003 ».
        Le libellé vaut « Loyer d'avril ».
    Conserver la boiteuse.
    Imputer la boiteuse et 4500 et 300,00 et 0,00.
    Imputer la boiteuse et 1020 et 0,00 et 30,00.
    Comptabiliser la boiteuse et 20.04.2026.
En cas d'échec :
    Afficher « Refusé : » puis le motif de l'échec.
Afficher « Écritures : » puis le nombre d'écritures conservées.
```

```sortie
Refusé : Écriture P-003 déséquilibrée : débits 300,00 et crédits 30,00
Écritures : 2
```

Regarde le compte : `2`. L'écriture P-003 avait pourtant été conservée, avec ses deux lignes, avant le refus. Le refus a tout annulé, comme si l'essai n'avait jamais eu lieu : c'est la transaction (charte, art. 7), et c'est elle qui protège ta comptabilité.

Et une écriture comptabilisée ne change plus :

```sortie
Refusé : L'écriture P-002 est comptabilisée : elle ne change plus.
```

Une limite, pour être honnête : cette protection tient parce que les lignes ne se créent que par `Imputer`. Un programme qui écrirait `Le débit de la ligne devient 1000.` ailleurs la contournerait. Une règle vérifiée par le langage lui-même, à chaque conservation, est prévue (charte, art. 12) ; en attendant, la discipline, c'est toi.

### Le journal

```grymoir
Afficher « Journal ».
Pour chaque écriture conservée dont la comptabilisation est présente, par date :
    Afficher la date de l'écriture puis la pièce de l'écriture puis le libellé de l'écriture.
    Pour chaque ligne de l'écriture :
        Afficher numéro du compte de la ligne sur 10 à droite puis débit de la ligne sur 10 à droite puis crédit de la ligne sur 10 à droite.
```

```sortie
Journal
01.01.2026 P-001 Fortune au 1er janvier
      1000     300,00       0,00
      1020   2'200,00       0,00
      2800       0,00   2'500,00
12.04.2026 P-002 Fête de printemps : traiteur
      4000     450,00       0,00
      1000       0,00     200,00
      1020       0,00     250,00
```

Programme complet : `docs/guide/chapitre-07.grym`.

## 8. Encaisser une cotisation

Encaisser une cotisation, c'est trois choses à la fois : passer l'écriture (la caisse ou la banque au débit, le compte des cotisations au crédit), la comptabiliser, et noter la date de paiement. Tout ou rien :

```grymoir
Pour encaisser une cotisation et une référence et une date :
    Si la date de paiement de la cotisation est présente :
        Refuser « La cotisation de » puis le nom du membre de la cotisation puis « est déjà payée. ».
    L'encaissement vaut une nouvelle écriture :
        La date vaut date.
        La pièce vaut « C- » suivi de l'année de la cotisation suivi de « - » suivi du numéro du membre de la cotisation.
        Le libellé vaut « Cotisation » suivi de " " suivi du nom du membre de la cotisation.
    Conserver l'encaissement.
    Imputer l'encaissement et référence et le montant de la cotisation et 0,00.
    Imputer l'encaissement et 3000 et 0,00 et le montant de la cotisation.
    Comptabiliser l'encaissement et date.
    La date de paiement de la cotisation devient date.
```

`suivi de` colle des textes et des nombres en un seul texte : `C-2026-1`. Contrairement à `puis`, il n'ajoute pas d'espace ; et les guillemets `« »` rognent les espaces qu'ils contiennent. Pour une espace, on écrit donc `" "`, entre guillemets droits, qui la gardent. Grammaire, § 1.5 et § 4.4.

Une action peut en appeler une autre : `Encaisser` utilise `Imputer` et `Comptabiliser`. Si l'une d'elles refuse, tout l'encaissement est annulé, date de paiement comprise.

```grymoir
Le payeur vaut le membre conservé dont le numéro est 1.
Pour chaque cotisation du payeur dont l'année = exercice et la date de paiement est absente :
    Encaisser la cotisation et 1000 et 20.02.2026.
```

Encaisser deux fois la même cotisation est refusé :

```sortie
Refusé : La cotisation de Rochat est déjà payée.
C-2026-1 20.02.2026 Cotisation Rochat
C-2026-2 03.03.2026 Cotisation Bapst
Impayées : 1
```

Programme complet : `docs/guide/chapitre-08.grym`.

## 9. Soldes et balance

### Encore quelques mouvements

Pour les écritures à deux comptes, une petite action suffit :

```grymoir
Noter « P-003 » et 20.04.2026 et « Loyer d'avril » et 4500 et 1020 et 300,00.
Noter « P-004 » et 05.05.2026 et « Don de la boulangerie » et 1020 et 3200 et 150,00.
Noter « P-005 » et 30.06.2026 et « Frais de tenue de compte » et 4800 et 1020 et 12,00.
```

L'action `Noter` crée l'écriture, impute ses deux lignes et la comptabilise, mais seulement si la pièce n'existe pas encore : relancer le programme ne double rien.

### Le solde d'un compte

Le solde d'un compte se lit sur les lignes du compte, en ne gardant que celles des écritures comptabilisées : un brouillon ne compte pas. Deux outils du langage suffisent. `la somme des débits des lignes du compte` additionne un champ sur les objets de la base qu'un lien relie au compte ; `dont la comptabilisation de l'écriture est présente` pose une condition à travers le lien de la ligne vers son écriture. Grammaire, § 16.4 et § 16.10.

```grymoir
Le mouvement débiteur d'un compte vaut la somme des débits des lignes du compte dont la comptabilisation de l'écriture est présente.
Le mouvement créditeur d'un compte vaut la somme des crédits des lignes du compte dont la comptabilisation de l'écriture est présente.

Le solde d'un compte :
    Selon la nature du numéro du compte :
        Cas « actif » ou « charge », rendre le mouvement débiteur du compte − le mouvement créditeur du compte.
    Rendre le mouvement créditeur du compte − le mouvement débiteur du compte.
```

Ce sont des calculs, comme au chapitre 3. Un calcul peut lire la base, jamais y écrire : son résultat dépend des arguments, et de ce qui est conservé au moment où tu l'appelles. Le solde d'un actif ou d'une charge se lit au débit, celui d'un passif ou d'un produit au crédit : `Selon` et la nature calculée au chapitre 6 font le tri. La somme se fait en décimal exact, et une somme sur rien vaut 0 : un compte sans mouvement a un solde nul. Grammaire, § 9.4.

### La balance

```grymoir
Pour chaque compte conservé, par numéro :
    Si le nombre de lignes du compte n'est pas nul :
        Afficher le numéro du compte puis l'intitulé du compte sur 28 puis le mouvement débiteur du compte sur 10 à droite puis le mouvement créditeur du compte sur 10 à droite puis le solde du compte sur 10 à droite.
        Le total débit devient le total débit + le mouvement débiteur du compte.
        Le total crédit devient le total crédit + le mouvement créditeur du compte.
```

Un compte sans aucune ligne n'apparaît pas dans la balance : `le nombre de lignes du compte` compte les lignes que son lien relie au compte.

```sortie
Balance au 30.06.2026
Compte                                 Débit     Crédit      Solde
1000 Caisse                           380,00     200,00     180,00
1020 Banque                         2'430,00     562,00   1'868,00
2800 Fortune de l'association           0,00   2'500,00   2'500,00
3000 Cotisations                        0,00     160,00     160,00
3200 Dons                               0,00     150,00     150,00
4000 Frais des manifestations         450,00       0,00     450,00
4500 Loyer du local                   300,00       0,00     300,00
4800 Frais bancaires                   12,00       0,00      12,00
Totaux                              3'572,00   3'572,00
La balance est équilibrée.
```

Les totaux sont égaux, et ils le seront toujours : chaque écriture comptabilisée est équilibrée, donc leur somme aussi. Une balance qui ne tombe pas juste révélerait une ligne entrée par un autre chemin qu'`Imputer`.

L'en-tête s'écrit `« Compte » sur 33` : `puis` ajoute une espace entre deux éléments, et le numéro (4), l'espace et l'intitulé (28) occupent 33 caractères.

Programme complet : `docs/guide/chapitre-09.grym`.

## 10. Clôturer l'exercice

### Le compte de résultat

Le total d'une nature de comptes, les produits par exemple, est lui aussi un calcul : il parcourt les comptes conservés et additionne leurs soldes.

```grymoir
Le cumul d'une rubrique :
    Le total vaut 0,00.
    Pour chaque compte conservé :
        Si la nature du numéro du compte = rubrique, le total devient le total + le solde du compte.
    Rendre le total.
```

Le paramètre s'appelle `rubrique` : `nature` est déjà le nom d'un calcul, et un nom de formule ne sert qu'à une chose. Une action affiche les comptes d'une rubrique :

```grymoir
Pour présenter une rubrique :
    Pour chaque compte conservé, par numéro :
        Si la nature du numéro du compte = rubrique et le solde du compte ≠ 0 :
            Afficher le numéro du compte puis l'intitulé du compte sur 28 puis le solde du compte sur 10 à droite.
```

```grymoir
Afficher « Charges ».
Présenter « charge ».
Le résultat vaut le cumul de « produit » − le cumul de « charge ».
Si le résultat ≥ 0, afficher « Bénéfice » sur 33 puis le résultat sur 10 à droite.
Sinon, afficher « Perte » sur 33 puis −le résultat sur 10 à droite.
```

```sortie
Compte de résultat au 30.06.2026
Produits
3000 Cotisations                      160,00
3200 Dons                             150,00
Charges
4000 Frais des manifestations         450,00
4500 Loyer du local                   300,00
4800 Frais bancaires                   12,00
Perte                                 452,00
```

La fête a coûté plus que les cotisations et le don n'ont rapporté : l'exercice perd 452 francs.

### Le bilan

```grymoir
Afficher « Bilan au 30.06.2026 ».
Afficher « Actifs ».
Présenter « actif ».
Afficher « Passifs ».
Présenter « passif ».
Afficher « Résultat de l'exercice » sur 33 puis le résultat sur 10 à droite.
L'actif vaut le cumul de « actif ».
Le passif vaut le cumul de « passif » + le résultat.
```

```sortie
Bilan au 30.06.2026
Actifs
1000 Caisse                           180,00
1020 Banque                         1'868,00
Passifs
2800 Fortune de l'association       2'500,00
Résultat de l'exercice               −452,00
Actif                               2'048,00
Passif et résultat                  2'048,00
Le bilan est équilibré.
```

Ce que l'association possède (2'048 francs en caisse et en banque) égale sa fortune du début d'année, diminuée de la perte. Si ce n'était pas le cas, une écriture serait fausse quelque part : c'est tout l'intérêt de la partie double.

En général, l'assemblée générale approuve ensuite les comptes, et une dernière écriture reporte le résultat sur la fortune. Tu as maintenant tout ce qu'il faut pour l'écrire.

Programme complet : `docs/guide/chapitre-10.grym`.

## 11. Les écrans

Jusqu'ici, l'application parlait par `Afficher`. Une association veut des fenêtres, des listes, des boutons. En GrymoiR, un écran se déclare comme une entité, et chaque bouton a son événement.

Un programme qui ouvre des écrans se lance dans l'atelier : Programme > Lancer. En console, `grym lancer` le refuse.

### Les rapports deviennent des actions

La balance du chapitre 9 et les comptes annuels du chapitre 10 ne s'affichent plus au démarrage : ils deviennent deux actions, `Imprimer la balance` et `Imprimer les comptes annuels`, qu'un bouton appellera. Leur texte arrive dans le panneau d'exécution de l'atelier.

### L'accueil

```grymoir
L'écran d'accueil, « Amis du Tilleul », montre :
    un bouton « Membres »,
    un bouton « Cotisations impayées »,
    un bouton « Noter une écriture »,
    côte à côte :
        un bouton « Balance »,
        un bouton « Comptes annuels »,
    un bouton « Fermer ».
```

`côte à côte :` place ses éléments sur une ligne ; sans lui, ils s'empilent. Le titre entre guillemets est celui de la fenêtre. Chaque bouton a son événement :

```grymoir
Quand on clique sur « Membres » dans l'écran d'accueil :
    Ouvrir l'écran des membres.
```

```grymoir
Quand on clique sur « Balance » dans l'écran d'accueil :
    Imprimer la balance.
```

`Ouvrir` montre un écran par-dessus le précédent et attend qu'il se ferme. La dernière phrase du programme ouvre l'accueil :

```grymoir
Ouvrir l'écran d'accueil.
```

Tout ce qui précède s'exécute, puis est enregistré dans la base, avant que l'écran s'ouvre ; ensuite, chaque événement forme sa propre transaction : un événement qui échoue n'écrit rien, et l'écran reste ouvert avec le message. Grammaire, § 22.

### Une liste, une fiche

```grymoir
L'écran des membres montre :
    la liste des membres conservés, par nom, avec le numéro, le prénom, le nom et le nom de la catégorie,
    un bouton « Nouveau membre »,
    un bouton « Fermer ».
```

Une liste reprend la recherche du chapitre 4 ; `avec` choisit ses colonnes, et `le nom de la catégorie` va chercher la valeur à travers le lien. Choisir une ligne (double-clic ou Entrée) est un événement :

```grymoir
Quand on choisit un membre dans l'écran des membres :
    Ouvrir la fiche du membre.
```

La fiche se déduit de l'entité : un texte par champ, un bouton par lien, « Modifier » et « Fermer ». Et un nouveau membre se saisit dans un formulaire, lui aussi déduit de l'entité :

```grymoir
Quand on clique sur « Nouveau membre » dans l'écran des membres :
    Le nouveau vaut un nouveau membre saisi.
    Conserver le nouveau.
```

### Encaisser d'un clic

```grymoir
L'écran des impayés, « Cotisations impayées », montre :
    la liste des cotisations conservées dont la date de paiement est absente, par montant, avec l'année, le prénom du membre, le nom du membre et le montant,
    côte à côte :
        un bouton « Encaisser en caisse »,
        un bouton « Encaisser par banque »,
    un bouton « Fermer ».
```

```grymoir
Quand on clique sur « Encaisser en caisse » dans l'écran des impayés :
    Si la cotisation choisie de l'écran est absente, refuser « Choisis d'abord une cotisation dans la liste. ».
    Encaisser la cotisation choisie de l'écran et 1000 et aujourd'hui.
```

`la cotisation choisie de l'écran` est la ligne sélectionnée, ou `absent` si aucune ne l'est ; `aujourd'hui`, la date du jour. Après l'événement, la liste se relit : la cotisation encaissée en disparaît. Si tu cliques sans choisir, le refus s'affiche dans l'écran, et rien n'est écrit.

### Saisir une écriture

```grymoir
L'écran de saisie, « Noter une écriture », montre :
    une pièce (texte),
    une date (date),
    un libellé (texte),
    un débité (compte),
    un crédité (compte),
    un montant (nombre),
    côte à côte :
        un bouton « Noter »,
        un bouton « Fermer ».
```

Une zone de saisie se déclare comme un champ, avec son type. Une zone dont le type est une entité, comme `un débité (compte)`, devient un menu de ses objets, désignés par leur champ texte unique : l'intitulé du compte (chapitre 6). Une valeur tapée est vérifiée selon son type avant tout événement : « abc » dans le montant est refusé par l'écran.

```grymoir
Quand on clique sur « Noter » dans l'écran de saisie :
    Si le nombre d'écritures conservées dont la pièce est la pièce de l'écran n'est pas nul :
        Refuser « La pièce » puis la pièce de l'écran puis « existe déjà. ».
    Noter la pièce de l'écran et la date de l'écran et le libellé de l'écran et le numéro du débité de l'écran et le numéro du crédité de l'écran et le montant de l'écran.
    Fermer l'écran.
```

`la pièce de l'écran` lit une zone : un écran est un objet, et ses zones sont ses champs.

Programme complet : `docs/guide/chapitre-11.grym`. Les essais de l'atelier le lancent et cliquent à ta place : membres, encaissement, saisie d'une écriture, balance et comptes annuels.

## 12. Finitions

### Un fichier par sujet

Ton `association.grym` dépasse maintenant trois cents lignes. On le partage en trois fichiers, dans le même dossier :

- `données.grym` : les six entités et les calculs qui en découlent (la nature, les mouvements, le solde, le cumul) ;
- `opérations.grym` : les actions qui inscrivent, imputent, comptabilisent, encaissent, notent, et les rapports ;
- `association.grym` : le programme, qui les utilise : les données de départ et les écrans.

```grymoir
Remarque : Premiers pas, chapitre 12 : les opérations et les rapports.
Utiliser « données ».
```

```grymoir
Remarque : Premiers pas, chapitre 12 : l'application des Amis du Tilleul.
Utiliser « opérations ».
```

`Utiliser « données ».`, en tête du fichier, rend visibles ses déclarations ; elles se transmettent, et `association.grym` voit aussi les entités, à travers `opérations.grym`. Un fichier utilisé ne contient que des déclarations : entités, calculs, actions. Les données de départ et les écrans restent dans le programme. Grammaire, § 21.

Une action d'un fichier utilisé ne voit pas les variables du programme principal : elle ne passe que par ses paramètres. Les rapports y trouvent leur place parce que le solde et le cumul sont des calculs, qui lisent la base eux-mêmes ; aucun n'a besoin d'une variable du programme.

### Couleur et logo

```grymoir
Les écrans ont la couleur verte et le logo « tilleul.png ».
```

Une phrase, au début du programme, avant le premier `Ouvrir` : les écrans prennent l'accent vert, et le logo s'affiche à côté du titre, en grand sur l'accueil, en petit sur les écrans ouverts par-dessus. Cinq couleurs existent : bleue (sans phrase), verte, turquoise, violette, grise. Le logo est une image (PNG, JPEG, GIF ou WebP) à côté du programme ; GrymoiR vérifie qu'il existe dès la lecture du programme. Grammaire, § 22.5.

Programme complet : le dossier `docs/guide/chapitre-12/`.

### Et maintenant

Tu as construit une application complète : des données qui durent, des règles que la base ne peut pas violer, des écrans. La grammaire (Aide > Le langage GrymoiR) décrit tout ce que ce guide n'a fait qu'effleurer : la corbeille et le rétablissement d'objets supprimés (§ 16.12), les champs « plusieurs » (§ 16.13), les classes, l'héritage et les aptitudes (§ 13), les fichiers et images (§ 15), la reprise après erreur (§ 18). F1 sur un mot du code t'y conduit directement.

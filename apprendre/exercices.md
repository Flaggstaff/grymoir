# Exercices de l'application pour apprendre GrymoiR

Chaque exercice : un titre suivi de « (chapitre N) », l'énoncé, puis les blocs `depart` (ce que l'éditeur montre),
`solution` (une réponse juste), `attendu` (les lignes que la sortie doit contenir, dans l'ordre) et, si le programme
pose des questions, `reponses` (une par ligne). apprendre/assembler.mjs exécute chaque solution et chaque départ : une
solution qui n'affiche pas l'attendu, ou un départ qui le donne déjà, arrête la construction.

## Ta première phrase (chapitre 1)

Fais afficher exactement : Les Amis du Tilleul ouvrent leurs portes.

N'oublie pas le point de la phrase, après le guillemet fermant.

```depart
Remarque : écris ta phrase en dessous.
```

```solution
Afficher « Les Amis du Tilleul ouvrent leurs portes. ».
```

```attendu
Les Amis du Tilleul ouvrent leurs portes.
```

## Une faute de frappe (chapitre 1)

Ce programme ne tourne pas. Lance-le, lis l'erreur, corrige-la : il doit afficher « Effectif : 20 ».

```depart
L'effectif vaut 12.
L'efectif devient 20.
Afficher « Effectif : » puis l'effectif.
```

```solution
L'effectif vaut 12.
L'effectif devient 20.
Afficher « Effectif : » puis l'effectif.
```

```attendu
Effectif : 20
```

## Des départs (chapitre 1)

L'association compte 20 membres ; 4 partent. Garde le nom `effectif`, fais-le devenir le nouveau nombre avec `devient`, et affiche « Effectif : 16 ».

```depart
L'effectif vaut 20.
```

```solution
L'effectif vaut 20.
L'effectif devient l'effectif − 4.
Afficher « Effectif : » puis l'effectif.
```

```attendu
Effectif : 16
```

## Des centimes justes (chapitre 2)

Un repas coûte 12,50 francs. Calcule le prix de trois repas et affiche « Total : 37,50 ».

```depart
Le prix du repas vaut 12,50.
```

```solution
Le prix du repas vaut 12,50.
Afficher « Total : » puis le prix du repas × 3.
```

```attendu
Total : 37,50
```

## Le tarif senior (chapitre 2)

Dès 65 ans, le tarif est « senior » ; sinon, il est « normal ». Pour un âge de 70, le programme doit afficher « Tarif senior. ». Change ensuite l'âge à 40 pour vérifier l'autre cas.

```depart
L'âge vaut 70.
```

```solution
L'âge vaut 70.
Si l'âge ≥ 65, afficher « Tarif senior. ».
Sinon, afficher « Tarif normal. ».
```

```attendu
Tarif senior.
```

## Un calcul nommé (chapitre 3)

Déclare un calcul `le prix TTC d'un montant`, qui rend le montant × 1,081 (TVA suisse au taux normal), puis affiche le prix TTC de 200.

```depart
Remarque : déclare le calcul, puis appelle-le.
```

```solution
Le prix TTC d'un montant vaut montant × 1,081.
Afficher le prix TTC de 200.
```

```attendu
216,200
```

## La somme de 1 à 100 (chapitre 3)

Additionne tous les nombres de 1 à 100 avec une boucle `Pour chaque`, et affiche le total.

```depart
Le total vaut 0.
```

```solution
Le total vaut 0.
Pour chaque nombre de 1 à 100 :
    Le total devient le total + nombre.
Afficher le total.
```

```attendu
5050
```

## Une action qui salue (chapitre 3)

Déclare l'action `saluer un prénom`, qui affiche « Bonjour » puis le prénom, puis salue Anne et Louis.

```depart
Remarque : « Pour saluer un prénom : » déclare l'action.
```

```solution
Pour saluer un prénom :
    Afficher « Bonjour » puis prénom.
Saluer « Anne ».
Saluer « Louis ».
```

```attendu
Bonjour Anne
Bonjour Louis
```

## Une bibliothèque (chapitre 4)

Déclare une entité `livre`, conservée, avec un titre (texte, unique) et une année (nombre entier). Conserve « Le Petit Prince » (1943) et « Dune » (1965), puis affiche le nombre de livres conservés.

```depart
Un livre, conservé, a :
    un titre (texte), unique,
    une année (nombre entier).
```

```solution
Un livre, conservé, a :
    un titre (texte), unique,
    une année (nombre entier).
Le premier vaut un nouveau livre :
    Le titre vaut « Le Petit Prince ».
    L'année vaut 1943.
Conserver le premier.
Le second vaut un nouveau livre :
    Le titre vaut « Dune ».
    L'année vaut 1965.
Conserver le second.
Afficher le nombre de livres conservés.
```

```attendu
2
```

## Retrouver avec dont (chapitre 4)

Les livres sont en place. Affiche le titre de ceux parus avant 1950, puis la liste complète triée par année.

```depart
Un livre, conservé, a :
    un titre (texte), unique,
    une année (nombre entier).
Pour ranger un titre et une année :
    Le l vaut un nouveau livre :
        Le titre vaut titre.
        L'année vaut année.
    Conserver le l.
Ranger « Dune » et 1965.
Ranger « Le Petit Prince » et 1943.
Ranger « Le Désert des Tartares » et 1940.
```

```solution
Un livre, conservé, a :
    un titre (texte), unique,
    une année (nombre entier).
Pour ranger un titre et une année :
    Le l vaut un nouveau livre :
        Le titre vaut titre.
        L'année vaut année.
    Conserver le l.
Ranger « Dune » et 1965.
Ranger « Le Petit Prince » et 1943.
Ranger « Le Désert des Tartares » et 1940.
Pour chaque livre conservé dont l'année < 1950, par titre :
    Afficher le titre du livre.
Pour chaque livre conservé, par année :
    Afficher l'année du livre puis le titre du livre.
```

```attendu
Le Désert des Tartares
Le Petit Prince
1940 Le Désert des Tartares
1943 Le Petit Prince
1965 Dune
```

## Demander, répondre (chapitre 3)

Une page ne peut pas attendre ton clavier : les réponses aux questions s'écrivent d'avance, une par ligne, dans le champ « Réponses ». Demande un prénom, puis affiche « Bienvenue, » suivi du prénom.

```depart
Le prénom vaut la réponse à « Ton prénom ? ».
```

```reponses
Élodie
```

```solution
Le prénom vaut la réponse à « Ton prénom ? ».
Afficher « Bienvenue, » puis le prénom.
```

```attendu
Bienvenue, Élodie
```

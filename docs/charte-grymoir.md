# Charte de GrymoiR

Version 1.8, révisée le 21 septembre 2026.
Toute modification passe par une révision numérotée.

---

## 1. Raison d'être

GrymoiR sert à écrire des applications de gestion fiables, en réunissant langage, base de données et déploiement dans un seul outil, avec un modèle objet pur.

Cible : le développeur seul ou la petite équipe.
Hors cible : calcul intensif, programmation système, web à grande échelle.

## 2. Hiérarchie des principes

En cas de conflit entre deux choix, le rang supérieur l'emporte, sans débat.

1. **Intégrité des données** : une écriture réussit entièrement ou ne laisse aucune trace.
2. **Lisibilité** : un non-programmeur devine ce que fait une formule.
3. **Concision** : on ne tape rien que le compilateur peut déduire.
4. **Performance.**

## 3. Exécution

- Le compilateur `grym` produit un bytecode unique.
- Une VM écrite en C portable exécute ce bytecode sous Windows, Linux et macOS.
- Les sources sont en UTF-8, normalisées NFC à la lecture.
- Extensions : `.grym` (forme littéraire), `.grymc` (forme compacte).

## 4. Syntaxe : deux formes, un seul langage

GrymoiR s'écrit sous deux formes équivalentes.

- **Forme littéraire** (`.grym`) : français courant, selon une grammaire contrôlée où chaque phrase acceptée a un sens unique. Elle gère l'élision, les articles, le pluriel et les accords. Le pluriel irrégulier se déclare dans l'entité.
- **Forme compacte** (`.grymc`) : mots-clés français préfixés d'un souligné, assignation `<<`.

Règles communes :

- Les deux formes produisent le même arbre syntaxique. Tout ce qui suit l'analyse (vérification, bytecode, VM) ignore la forme d'origine.
- `grym traduire` convertit un fichier d'une forme à l'autre sans perte de programme ni de remarque. Compacte → littéraire → compacte redonne le texte compact à l'identique ; littéraire → compacte → littéraire redonne le même programme en forme littéraire canonique. L'arbre conserve remarques, lignes vides et choix d'écriture ; `grym formater` écrit la forme canonique (grammaire, § 12).
- Une seule forme par fichier.
- Toute construction existe dans les deux formes.
- La forme littéraire fait référence : chaque nouvelle fonction se conçoit d'abord en français courant, puis se dérive en forme compacte.
- Les calculs gardent la notation mathématique dans les deux formes (`Le total vaut prix × quantité.`).
- Identifiants avec accents autorisés, insensibles à la casse (`Solde` = `solde`).
- L'interprétation est déterministe, sans modèle de langage : même texte, même programme.

Forme littéraire :

```
Pour relancer un client :
    Si le solde du client est négatif,
        créer un rappel pour le client.
```

Forme compacte :

```
_formule relancer(client)
    _si client.solde < 0 _alors
        créer_rappel(client)
    _fin
_fin
```

## 5. Typage

- Code ordinaire : typage dynamique.
- Entités persistées : typage strict, vérifié à la compilation.

Justification : l'erreur la plus coûteuse est celle qui corrompt la base (principe 1). La rigueur se concentre là.

## 6. Modèle objet

- Tout est objet.
- Héritage simple.
- Aptitude : paquet de formules et de champs qu'une classe adopte sans en hériter.
- Si deux aptitudes définissent la même formule, la compilation échoue jusqu'à ce que le développeur tranche explicitement. Aucune résolution implicite.

Forme littéraire :

```
Une chose horodatée a :
    une date de création,
    une date de modification.

Un membre est une personne horodatée. Un membre a :
    une licence, unique.
```

Forme compacte :

```
_aptitude horodatée
    créé_le : date
    modifié_le : date
_fin

_entité Membre _hérite Personne _adopte horodatée
    licence : texte unique
_fin
```

La grammaire exacte de la forme littéraire reste à spécifier. Ces exemples montrent l'intention, pas la syntaxe définitive.

## 7. Persistance

- L'entité est une construction du langage.
- Moteur : SQLite embarqué, invisible pour le développeur, qui n'écrit jamais de SQL.
- Chaque formule forme une transaction implicite : une formule qui échoue n'écrit rien.
- Migrations de schéma automatiques.
- Type `montant` : décimal exact, jamais de flottant.
- Versions parallèles des données : horizon post-v1.

## 8. Erreurs

- Messages en français, localisés par fichier, ligne et colonne.
- Le message s'exprime dans la forme du fichier concerné.
- Correction proposée quand c'est possible (« `soldee` inconnu, vouliez-vous `solde` ? »).
- Une phrase littéraire hors grammaire produit une erreur qui propose la tournure valide la plus proche.
- Aucune erreur avalée en silence.

## 9. Aide à la saisie

À toute position d'un fichier, l'outillage propose les suites valides selon la grammaire et les noms déclarés.

- La grammaire contrôlée permet un calcul exact des suites possibles, sans heuristique.
- Le même calcul alimente les messages d'erreur (« attendu : … »).
- Diffusion dans les éditeurs par un serveur conforme au Language Server Protocol.

Justification : une grammaire contrôlée se lit facilement mais s'écrit mal si l'on ignore les tournures acceptées. L'aide à la saisie rend la grammaire visible.

## 10. Développement vivant

L'architecture de la VM prévoit la recompilation à chaud d'une formule dans une application en cours d'exécution. Livraison après la v1.

## 11. Hors périmètre v1

Interface graphique, réseau, concurrence, optimisation.

Horizon post-v1, sans date :

- Éditeur par blocs, conçu comme une troisième forme de l'arbre syntaxique (art. 4), convertible sans perte vers les deux autres.
- Éditeur visuel d'interfaces, conditionné à l'arrivée des interfaces graphiques.

## 12. Jalons

| Version | Contenu | État |
|---------|---------|------|
| v0.1 | Lexer, parser de la forme littéraire, suites attendues, boucle interactive de calcul | livrée |
| v0.2 | Bytecode et VM (remplacent l'évaluateur provisoire de la v0.1), conditions (`Si`, comparaisons, booléens), boucles et `Selon`, forme compacte et `grym traduire` dans les deux sens, forme canonique et `grym formater`, formules, objets, héritage, méthodes, aptitudes, ramasse-miettes | livrée le 21 septembre 2026 |
| v0.3 | Entités et SQLite | à venir |
| v0.4 | Serveur d'aide à la saisie (LSP) | à venir |
| v1.0 | Application console complète | à venir |

Reportés sans jalon fixé (grammaire, § 13.8) : appel de la version parente depuis une méthode, listes d'objets, absence de valeur, affichage des objets. Chacun demandera sa propre conception avant d'entrer dans un jalon.

---

## Journal des révisions

| Version | Date | Changement |
|---------|------|------------|
| 1.0 | 2026-09-20 | Charte initiale |
| 1.1 | 2026-09-20 | Double syntaxe : forme littéraire (référence) et forme compacte, traduction sans perte (art. 3, 4, 6, 8) |
| 1.2 | 2026-09-20 | Jalons : forme littéraire dès v0.1, forme compacte et traduction en v0.2 (art. 11) |
| 1.3 | 2026-09-21 | Nouvel art. 9 : aide à la saisie. Renumérotation des art. 9 à 11 en 10 à 12. Jalons : suites attendues en v0.1, serveur LSP en v0.4 |
| 1.4 | 2026-09-21 | Art. 11 : horizon post-v1, éditeur par blocs et éditeur visuel d'interfaces |
| 1.5 | 2026-09-21 | Jalons : bytecode et VM en v0.2 (art. 3 et 12) |
| 1.6 | 2026-09-21 | Jalons : conditions en v0.2 (art. 12) |
| 1.7 | 2026-09-21 | Art. 4 : traduction sans perte de programme ni de remarque, vers une forme littéraire canonique ; `grym formater` |
| 1.8 | 2026-09-21 | Art. 12 : colonne « État » ; v0.2 livrée, contenu complété (boucles, `Selon`, forme canonique, méthodes, ramasse-miettes) ; suites reportées sans jalon |

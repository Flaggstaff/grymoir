# GrymoiR : l'atelier

Proposition soumise à relecture, rédigée le 24 septembre 2026. Rien n'est implémenté.
Référence : Charte de GrymoiR v1.36, art. 1, 3, 4, 11, 12 et 13 ; grammaire 1.37, § 12, § 16 ; `docs/v2.md`.
Toute décision prise sur ce document passe dans la charte par une révision numérotée.

---

## 1. Thèse

GrymoiR devient un environnement de développement complet : on y définit ses données, on y dessine ses écrans, on y écrit ses traitements, on y lance son application. Le tout dans de vraies fenêtres, sous Windows, macOS et Linux.

**Bibliothèque graphique : Qt 6**, décidé le 24 septembre 2026. L'atelier et les applications qu'il produit utilisent la même bibliothèque : un écran dessiné dans l'atelier s'affiche à l'identique dans l'application.

## 2. Principe directeur : l'atelier n'écrit que du GrymoiR

Tout geste dans l'atelier produit des phrases GrymoiR dans les fichiers `.grym` du projet : créer une entité, ajouter un champ, tracer un lien, plus tard dessiner un écran.

- Aucune base de métadonnées cachée, aucun format propre à l'atelier pour ce qui fait le programme. Le fichier `.grym` est la seule vérité (charte, art. 4 : la forme littéraire fait référence).
- Une modification faite à la main dans le fichier apparaît dans l'atelier ; une modification faite dans l'atelier se lit dans le fichier.
- Ce que l'atelier génère (plus tard : une liste et une fiche par entité) est du code ordinaire, qui appartient au développeur et se modifie comme le reste.
- `grym lancer`, `grym formater`, `grym traduire`, `git` et l'extension VS Code continuent de fonctionner sur tout projet fait dans l'atelier.

Conséquence : l'atelier ne peut rien faire que le langage ne sache dire. Chaque nouvelle fonction de l'atelier commence par sa phrase GrymoiR.

## 3. Réécriture chirurgicale

L'atelier modifie **la seule phrase concernée**, à sa position dans la source, et laisse le reste du fichier octet pour octet.

- Ajouter un champ insère une ligne dans la déclaration de l'entité ; renommer un type réécrit la parenthèse ; rien d'autre ne bouge.
- Le texte inséré est produit par l'imprimeur (grammaire, § 12), dans la forme canonique.
- Réécrire tout le fichier par `grym formater` à chaque geste est exclu : un développeur qui a ses habitudes d'écriture les perdrait au premier clic.
- Après chaque réécriture, l'atelier réanalyse le fichier. Si le résultat ne s'analyse pas, la réécriture est annulée et l'erreur affichée : l'atelier ne laisse jamais un fichier cassé derrière lui.

## 4. Architecture

```
            ┌──────────────── cœur, C99, sans dépendance ─────────────────┐
            │ lexeur · analyseur · imprimeur · bytecode · VM · base SQLite │
            └───────┬──────────────────────┬──────────────────────┬───────┘
                    │                      │                      │
       grym (console, navigateur, LSP)   interface Qt          grym-atelier
       inchangé, aucune dépendance       (src/qt/, C++)        (C++, Qt 6)
```

- **Le cœur reste en C99 sans dépendance.** `grym` en console fonctionne partout, sans Qt, comme aujourd'hui.
- **Une interface Qt** implémente `src/interface.h`, comme la console et le navigateur l'implémentent déjà : les programmes actuels s'affichent en fenêtre sans modification.
- **`grym-atelier`** est un exécutable C++ qui lie le cœur directement : il appelle l'analyseur et l'imprimeur sans passer par un processus ni par le protocole LSP.
- **Qt Widgets**, pas QML : contrôles natifs de bureau, tout en C++, sans second langage de description.
- **Version : Qt 6.8 au moins**, première version à support long de cinq ans. La version exacte se fixe en A1, sur la plus récente à support long ; le support long annoncé par Qt vise d'abord ses clients commerciaux, et ce que reçoit la version libre reste à vérifier.
- **Licence de Qt : LGPLv3**, liée dynamiquement. Le dépôt reste sous tous droits réservés ; la LGPL impose seulement de permettre le remplacement de la bibliothèque Qt, ce que la liaison dynamique assure.
- **Construction** : le `Makefile` actuel reste pour le cœur et `grym`. La partie Qt, qui a besoin des outils de Qt (`moc`), se construit avec CMake. L'intégration continue ajoute la construction de l'atelier sur les trois systèmes.

## 5. Jalons proposés

| Jalon | Contenu |
|---|---|
| A1 | Squelette : fenêtre, ouvrir un projet (un dossier), liste de ses fichiers, affichage du texte avec coloration et erreurs en direct, bouton « Lancer » qui exécute le programme dans un panneau de l'atelier (l'interface Qt, en mode séquentiel) |
| A2 | Éditeur de données : schéma des entités (une boîte par entité, une flèche par lien), panneau de propriétés (champs, types, unique, facultatif, valeur de départ, cascade), réécriture chirurgicale (§ 3), aperçu de ce que la migration fera à la base avant de l'appliquer (§ 16.7), et refus expliqué quand elle détruirait des données |
| A3 | Les écrans dans le langage : fenêtres, listes, fiches, boutons, modèle par événements. Conception phrase par phrase dans la grammaire, avant tout code |
| A4 | Éditeur d'écrans : génération d'une liste et d'une fiche par entité, en GrymoiR modifiable ; agencement à la souris |

Chaque jalon livre un outil utilisable. A1 est volontairement mince : il pose la fenêtre, le lien avec le cœur et la construction sur trois systèmes, sur quoi tout le reste repose.

## 6. Questions ouvertes

1. **Positions des boîtes du schéma.** Où ranger qu'une entité est dessinée en haut à gauche ? Ce n'est pas du programme. Proposition : un fichier `.grymatelier` à côté du programme, texte lisible, que l'atelier recrée s'il manque (disposition automatique). Le perdre ne perd jamais rien du programme.
2. **Un projet, un fichier ?** Aujourd'hui, un programme tient dans un fichier. Une vraie application voudra plusieurs fichiers (données, écrans, traitements). Il faudra une inclusion dans le langage, à concevoir avant A2 ou au plus tard avant A3.
3. **L'éditeur de code** de l'atelier : un éditeur maison sur `QPlainTextEdit`, qui réutilise directement le calcul des suites (grammaire, § 8), ou une bibliothèque d'éditeur de code existante. À décider en A1.
4. **Distribution** : l'atelier embarque ses bibliothèques Qt (outils de déploiement fournis par Qt sur macOS et Windows). La signature des exécutables pour macOS et Windows est reportée.

## 7. Ce que la charte devra dire

- Art. 3 : deux exécutables, `grym` (sans dépendance) et `grym-atelier` (Qt 6).
- Art. 11 : l'interface graphique native entre dans le périmètre ; l'éditeur visuel d'interfaces quitte l'horizon pour les jalons A3 et A4.
- Art. 12 : les jalons A1 à A4.
- `docs/v2.md` : `grym servir` reste un mode d'affichage parmi d'autres ; les « écrans nommés » et le modèle par événements prévus en v2.x passent en A3, pour Qt d'abord.

---

## Journal des révisions

| Version | Date | Changement |
|---------|------|------------|
| 0.1 | 2026-09-24 | Proposition initiale : Qt 6, l'atelier n'écrit que du GrymoiR, réécriture chirurgicale, architecture, jalons A1 à A4, questions ouvertes |

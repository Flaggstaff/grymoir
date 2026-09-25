# GrymoiR : les écrans dans le langage

Proposition soumise à relecture, rédigée le 25 septembre 2026. Rien n'est implémenté.
Référence : Charte de GrymoiR v1.39, art. 2, 4, 7, 11, 12 et 13 ; grammaire 1.38, § 9, § 16, § 19, § 20 ; `docs/atelier.md`, jalon A3.
Toute décision prise sur ce document passe dans la grammaire et la charte par une révision numérotée.

---

## 1. Thèse

Jusqu'ici, un programme GrymoiR est une suite de phrases : il pose ses questions dans l'ordre, et l'utilisateur répond. A3 donne au langage des écrans : des fenêtres qui montrent des listes et des fiches, des boutons, et des événements. Entre l'ouverture d'un écran et sa fermeture, c'est l'utilisateur qui décide de l'ordre.

Les écrans s'affichent avec Qt, dans l'atelier et dans les applications (`docs/atelier.md`, § 1). Comme tout le reste, ils s'écrivent en GrymoiR : l'éditeur d'écrans d'A4 n'écrira que des phrases de ce document.

## 2. Exemple de référence

```
L'écran des compositeurs montre :
    la liste des compositeurs conservés, par nom,
    un bouton « Nouveau »,
    un bouton « Fermer ».

Quand on choisit un compositeur dans l'écran des compositeurs :
    Ouvrir la fiche du compositeur.

Quand on clique sur « Nouveau » dans l'écran des compositeurs :
    Le c vaut un nouveau compositeur saisi.
    Conserver c.

Quand on clique sur « Fermer » dans l'écran des compositeurs :
    Fermer l'écran.

Ouvrir l'écran des compositeurs.
```

## 3. Décisions

### 3.1 Les trois piliers *(validés le 25 septembre 2026)*

1. **Un écran se déclare**, comme une entité : `L'écran des compositeurs montre :`, suivi de ce qu'il contient. Une liste reprend la recherche du § 16.4 (`des compositeurs conservés dont …, par …`) ; elle est vivante et se relit après chaque événement.
2. **Les événements s'écrivent `Quand on …`.** Ce sont des actions (grammaire, § 9) : elles voient les variables du programme, conservent, affichent, posent des questions. Chaque événement forme une transaction (charte, art. 7) : un événement qui échoue n'écrit rien, et l'écran reste ouvert avec le message.
3. **`Ouvrir l'écran des compositeurs.`** montre la fenêtre, puis le programme attend les événements jusqu'à `Fermer l'écran.` ; la phrase suivante s'exécute ensuite.

La fiche d'un objet est l'écran déduit de son entité, comme le formulaire de `saisi` (§ 19) : `Ouvrir la fiche du compositeur.` n'exige aucune déclaration. On n'écrit un écran que pour autre chose que le défaut.

Coût accepté : `quand`, `ouvrir` et `fermer` deviennent des mots de construction (grammaire, § 10.7), inscrits au journal de la grammaire (charte, art. 13.2).

## 4. Questions à trancher

1. Les colonnes d'une liste.
2. La disposition des contrôles dans la fenêtre.
3. Les autres contrôles : champ de saisie, case à cocher, texte fixe, image.
4. Les événements possibles, et ce qu'ils nomment (`le compositeur` dans `Quand on choisit un compositeur`).
5. Plusieurs écrans ouverts, écran dans un écran.
6. La forme compacte.
7. Les écrans en console et dans le navigateur (`grym lancer`, `grym servir`) : repli, ou refus expliqué.

---

## Journal des révisions

| Version | Date | Changement |
|---------|------|------------|
| 0.1 | 2026-09-25 | Proposition initiale : thèse, exemple de référence, trois piliers validés, questions à trancher |

# GrymoiR pour VS Code

Coloration, erreurs en direct, autocomplétion et mise en forme pour les programmes GrymoiR,
dans leur forme littéraire (`.grym`) et leur forme compacte (`.grymc`).

L'extension lance le serveur d'aide à la saisie de l'outil `grym` (`grym lsp`), écrit en C
comme le reste du langage. Il faut donc avoir construit `grym` (`make` à la racine du dépôt).

## Installation

1. Construire `grym` : `make` à la racine du dépôt.
2. Dans VS Code : « Extensions », menu « … », « Installer depuis VSIX… », puis choisir
   `grymoir-0.4.1.vsix`.
3. Si `grym` n'est pas dans le `PATH`, indiquer son chemin complet dans le réglage `grymoir.chemin`,
   puis recharger la fenêtre (commande « Reload Window »).
4. Faire confiance au dossier du projet : en mode restreint (« Restricted Mode »), VS Code ne laisse pas
   l'extension lancer `grym`. Seule la coloration fonctionne alors.

## Ce que fait l'extension

- erreurs soulignées pendant la frappe, avec le message de GrymoiR (une à la fois) ;
- autocomplétion des noms, champs, classes et mots de la grammaire (forme littéraire) ;
- « Mettre en forme le document » : forme canonique, comme `grym formater` ;
- coloration des deux formes.

## Reconstruire l'extension

Dans ce dossier, avec Node.js : `npm install`, puis `npx @vscode/vsce package`.

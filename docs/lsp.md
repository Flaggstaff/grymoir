# GrymoiR : serveur d'aide à la saisie (LSP)

Version 1.0 de la spécification, rédigée le 22 septembre 2026. Jalon v0.4 (charte, art. 12).

Le serveur rend l'aide à la saisie de la charte (art. 9) disponible dans un éditeur de code, par le
protocole LSP (Language Server Protocol) 3.17 de Microsoft. Il est écrit en C99, comme le reste du
langage, sans dépendance : il se lance par `grym lsp`.

## 1. Transport

- Entrée et sortie standard. Chaque message : un en-tête `Content-Length: n`, une ligne vide, puis
  `n` octets de JSON-RPC 2.0, en UTF-8.
- Le lecteur JSON (`src/json.c`) suit la RFC 8259 : échappements `\uXXXX` et paires de substitution
  UTF-16 ; une substitution isolée ou un caractère nul deviennent U+FFFD ; imbrication limitée à 256.
- Un message JSON mal formé reçoit l'erreur −32700 ; une requête inconnue, l'erreur −32601 ; une
  notification inconnue est ignorée.
- `exit` termine le serveur : code 0 après `shutdown`, 1 sinon.

## 2. Capacités annoncées

| Capacité | Valeur |
|---|---|
| `textDocumentSync` | 1 : le client renvoie le document entier à chaque modification |
| `completionProvider` | caractères déclencheurs : espace, apostrophe |
| `documentFormattingProvider` | oui |

## 3. Positions

- LSP compte lignes et caractères à partir de 0, les caractères en unités UTF-16 ; GrymoiR compte
  à partir de 1, en caractères. Le serveur convertit : un caractère hors du plan de base (un émoji)
  compte pour deux unités.
- Une erreur est soulignée de sa position jusqu'à la fin du mot, ou sur un caractère.

## 4. Erreurs en direct

- À l'ouverture et à chaque modification d'un document, le serveur l'analyse (forme littéraire, ou
  compacte si son nom finit par `.grymc`) et publie la première erreur, avec le message de GrymoiR.
  Un document correct efface les erreurs publiées ; sa fermeture aussi.
- Une seule erreur à la fois : l'analyseur s'arrête à la première. Les erreurs qui n'apparaissent
  qu'à l'exécution (division par zéro, base) ne sont pas signalées.

## 5. Autocomplétion

- Les suites valides à la position du curseur (grammaire, § 8) : noms déclarés, champs, classes,
  mots de la grammaire. Les catégories (« (nombre) ») et les gabarits (« « … » ») ne sont pas
  proposés.
- Forme littéraire seulement ; la forme compacte n'a pas encore de suites.

## 6. Mise en forme

- « Mettre en forme le document » remplace le document par sa forme canonique (grammaire, § 12),
  comme `grym formater`, dans la forme du fichier.
- Un document incorrect n'est pas mis en forme.

## 7. Extension VS Code

- Dossier `editeurs/vscode/`, en JavaScript : elle lance `grym lsp` (réglage `grymoir.chemin`,
  `grym` par défaut) pour les langages `grymoir` (`.grym`) et `grymoir-compact` (`.grymc`).
- Coloration par grammaires TextMate (`syntaxes/`), vérifiées avec `vscode-textmate` et
  `vscode-oniguruma`, les bibliothèques de VS Code : mots de la grammaire, structures de contrôle,
  textes, nombres, dates, remarques, types entre parenthèses.
- Paquet : `npm install`, puis `npx @vscode/vsce package`. Dépendance : `vscode-languageclient`
  10.1.1, qui demande VS Code 1.91 ou plus récent.

## 8. Reporté

Survol d'un nom, aller à la définition, renommer, rechercher les usages ; plusieurs erreurs à la fois ;
suites en forme compacte ; autres éditeurs (Neovim : quelques lignes de configuration, à documenter).

## Journal des révisions

| Version | Date | Changement |
|---|---|---|
| 1.0 | 2026-09-22 | Première version : transport, erreurs en direct, autocomplétion, mise en forme, extension VS Code |

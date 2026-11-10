# GrymoiR 3.2

Publiée le 10 novembre 2026. Le langage passe à la grammaire 3.20.

- **Le premier avertissement** (grammaire 3.20, § 1.5) : collé par `suivi de`, un texte entre guillemets français qui
  commence par `:`, `;`, `!` ou `?` suivi d'un mot perd l'espace devant ce signe (`"IBAN" suivi de « : Configuration. »`
  donne `IBAN: Configuration.`). Rien n'est refusé ; `grym` (`lancer`, `compiler`, `exemples`, `scenarios`), l'aide à
  la saisie et l'éditeur de l'atelier le disent, avec la forme qui garde l'espace (`" : Configuration."`). Un signe
  seul (`« : »`, `« ; »`), souvent une donnée, n'est pas averti.
- **La page « Apprendre GrymoiR »** est refaite en grammaire 3.20.
- Les corrections de la 3.1.1 et de la 3.1.2, publiées le même jour : les parties (mémoire), l'application fabriquée
  sous Linux (Qt et modèle embarqués), le premier lancement sans console, les espaces des messages de Praxis.

# GrymoiR 3.1.2

Publiée le 10 novembre 2026, le jour de la 3.1. Des corrections de l'atelier, pas de nouveauté du langage.

- **Corrigé** : sous Linux, une application fabriquée depuis l'AppImage ne contenait que l'atelier et le programme, et
  ne démarrait sur aucune machine sans Qt 6 installé. Elle embarque désormais Qt et le modèle de transcription, comme
  sous macOS (`docs/atelier.md`, § 8).
- **Corrigé** : avant son premier écran (le mot de passe de la base), une application fabriquée montrait le fil et les
  boutons de l'atelier. La fenêtre reste vide jusqu'au premier écran, à une ligne affichée ou à une question.
- **Corrigé** : Qt signalait une taille de police négative à chaque écran qui porte une indication ; les créneaux
  libres de l'agenda gardent la taille de leur texte, à un pixel près.
- **Praxis** : huit messages collaient leur deux-points ou leur point-virgule au mot d'avant (« QR-factures:
  Configuration », « Sexe : M;N° AVS » sur le justificatif).

# GrymoiR 3.1.1

Publiée le 10 novembre 2026, le jour de la 3.1. Des corrections, pas de nouveauté du langage.

- **Corrigé** : une partie qui s'utilise elle-même, rencontrée après un premier usage, ou deux parties qui s'utilisent
  l'une l'autre, arrêtaient `grym`, `grym lsp` et l'éditeur de l'atelier (« double free ») au lieu d'un refus. Elles
  reçoivent désormais « La partie « a » s'utilise elle-même. »
- **Corrigé** : des parties imbriquées (une partie qui en montre une autre), déclarées avant l'écran qui les emploie,
  lisaient de la mémoire déjà libérée et donnaient une erreur fausse sur un programme juste.
- **Corrigé** : au-delà de 64 parties dans un fichier, les suivantes passaient pour « pas déclarées ».
- **Pour le mainteneur** : `outils/essais-par-mutation`, qui abîme au hasard les programmes du dépôt et garde ce qui
  fait tomber `grym` (manuel du mainteneur, § 2).
- **Pas encore** : la page « Apprendre GrymoiR » garde le moteur de la 3.1.0, compilé avant ces corrections.

# GrymoiR 3.1

Publiée le 10 novembre 2026. Le langage passe à la grammaire 3.19.

- **Apprendre GrymoiR dans le navigateur** : `docs/site/apprendre.html`, publiée sur le site à côté de la page de
  téléchargement. Le guide « Premiers pas » en leçons, onze exercices, et le vrai moteur de GrymoiR compilé en
  WebAssembly, exécuté dans la page : sans installation, sans compte, hors ligne une fois téléchargée. Pas d'écrans ni
  de documents (ils restent à l'atelier), un seul fichier par programme, une base vidée à chaque lancement. Éprouvée
  dans Chromium ; Firefox et Safari pas encore.
- **Le langage** (grammaire 3.19) : `vrai`, `faux` et une condition entre parenthèses en argument d'un calcul ;
  `est parmi` dans toute condition (le champ multiple d'un objet, la relation inverse, une liste calculée), plus
  seulement dans un `dont` ; dans un scénario, `On voit l'erreur « … » dans l'écran …` vérifie qu'un écran a refusé.
- **Plusieurs postes sur une base** : les conflits couvrent désormais les champs multiples (gagner un élément qu'un
  autre a retiré, perdre un élément qu'un autre a ajouté) et les suppressions (supprimer un objet qu'un autre a changé,
  écrire un objet qu'un autre a mis à la corbeille). Un champ facultatif resté vide ne fait plus de faux conflit.
- **Le démarrage automatique** d'un service : `grym servir … --systemd` (Linux) et `--tache` (Windows), à côté de
  `--plist` (macOS). Les fichiers produits sont éprouvés ; le démarrage réel ne l'est encore ni sous Linux ni sous
  Windows.
- **Corrigé** : un rang démesuré (`le 99999999999999999e caractère`) devenait négatif sous Windows, où un entier `long`
  fait 32 bits.

# GrymoiR 3.0.1

Publiée le 8 novembre 2026, le soir de la 3.0. Des corrections, pas de nouveauté du langage.

- **L'aide de `grym`** : les options `--port N` et `--sans-navigateur` retrouvent leur place, sous `grym servir`.
- **Le premier lancement sur macOS** : la commande `xattr` remplace le Contrôle-clic, qui ne suffit pas (ci-dessous).
- **L'intégration continue** de GitHub, rouge depuis le 6 novembre : ses machines Linux sont fixées à Ubuntu 24.04
  (GitHub fait passer « ubuntu-latest » à Ubuntu 26 depuis le 19 octobre 2026) ; les essais de l'atelier y tournent
  sans écran ; sous Windows, les essais du cœur se lient à Winsock et l'essai des fonctions C compile hors d'un chemin
  accentué.
- **Windows, l'atelier et les applications fabriquées** : les réglages (ceux d'une écoute, par exemple) ne se gardaient
  pas, faute d'organisation pour QSettings, qui range dans le registre ; l'application prend désormais son nom comme
  organisation, sans déplacer sa base. L'essai des courriels créait mal son certificat sous MSYS2. Corrigé d'après les
  journaux de GitHub, pas encore vérifié sur un vrai Windows.

# GrymoiR 3.0

Publiée le 8 novembre 2026. La première version de GrymoiR mise à la disposition de tous : le langage de programmation
en français, son atelier et ses exemples, pour macOS, Linux et Windows.

## Ce qu'il y a dans le paquet

| Système | Fichier | Contenu |
|---|---|---|
| macOS (Apple Silicon et Intel*) | `GrymoiR-3.0.0-macos.dmg` | `GrymoiR.app` à glisser dans Applications : l'atelier, l'outil `grym` (`GrymoiR.app/Contents/MacOS/grym`), les exemples |
| Windows 10 (1903) et 11 | `GrymoiR-3.0.0-windows.exe` | l'installateur, sans droits d'administrateur : l'atelier, `grym.exe`, les exemples |
| Linux x86_64 (distributions de 2024 et après) | `GrymoiR-3.0.0-linux-x86_64.AppImage` | l'atelier, un seul fichier : le rendre exécutable, le lancer |
| Linux x86_64 | `grym-3.0.0-linux-x86_64` | l'outil en ligne de commande seul |

\* L'architecture du `.dmg` est celle de la machine de construction de GitHub (Apple Silicon) ; je ne sais pas encore
s'il tourne sur un Mac Intel.

`SHA256SUMS.txt` donne l'empreinte de chaque fichier : `shasum -a 256 fichier` (macOS), `sha256sum fichier` (Linux),
`certutil -hashfile fichier SHA256` (Windows).

## Au premier lancement

Rien n'est signé par un certificat payant. macOS (Gatekeeper) et Windows (SmartScreen) le disent :

- **macOS** : glisser GrymoiR dans Applications, puis, une fois, dans le Terminal :
  `xattr -dr com.apple.quarantine /Applications/GrymoiR.app`. Sans cela, macOS dit que GrymoiR « est endommagé » et
  propose de le mettre à la corbeille : c'est le refus d'une application téléchargée sans signature d'Apple, pas un
  fichier abîmé. La commande retire la marque « téléchargé d'internet » ; Contrôle-clic puis « Ouvrir » ne suffit pas
  ici (constaté le 8 novembre 2026).
- **Windows** : « Informations complémentaires », puis « Exécuter quand même ».
- **Linux** : `chmod +x GrymoiR-3.0.0-linux-x86_64.AppImage`, puis le lancer.

## Ce que fait GrymoiR 3.0

- **Le langage** (grammaire 3.18) : des programmes en français courant, deux formes équivalentes (littéraire et
  compacte), des nombres décimaux exacts, une base de données intégrée, migrée toute seule, chiffrable, des écrans, des
  documents imprimés, des scénarios qui éprouvent un programme, des bibliothèques, et des calculs écrits en C.
- **L'atelier** : écrire, lancer, dessiner les écrans, éditer les données, fabriquer une application pour les trois
  systèmes.
- **Plusieurs postes** : un programme servi à plusieurs navigateurs, chacun sa session, sur une même base, avec
  comptes, conflits refusés au lieu d'écrasés, et le réseau local chiffré (`docs/installation-multiposte.md`).

## Ce qui est connu et pas encore fait

- Les paquets ne sont pas signés (ci-dessus).
- Le service à plusieurs postes n'a été éprouvé que sur une machine (et Windows sous Wine), pas encore entre deux vrais
  ordinateurs.
- Le démarrage automatique du service n'existe que sur macOS (`--plist`).
- Une erreur de mémoire intermittente a été vue une fois dans les essais de l'atelier, jamais reproduite depuis.

## Licence

GrymoiR est sous licence publique de l'Union européenne, version 1.2 (EUPL 1.2), avec une exception : les applications
écrites en GrymoiR appartiennent à leur auteur, sous la licence de son choix (`LICENCE-fr.md`). Les composants tiers
gardent la leur : Qt (LGPL v3), SQLite et SQLite3MC, Mbed TLS (Apache-2.0), Monocypher, QR Code generator, whisper.cpp
et son modèle (MIT), les polices Atkinson Hyperlegible (OFL 1.1), les icônes Lucide.

Code source : https://github.com/Flaggstaff/grymoir

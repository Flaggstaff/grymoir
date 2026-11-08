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

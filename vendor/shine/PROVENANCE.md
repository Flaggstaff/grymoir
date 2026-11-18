# Shine, l'encodeur MP3

GrymoiR embarque Shine pour `x en MP3` (grammaire 3.43, § 15.8) : alléger un enregistrement trop lourd. Shine est un
encodeur MP3 en virgule fixe, petit et rapide, d'une qualité moindre que LAME mais suffisante pour la parole.

| | |
|---|---|
| Source | https://github.com/toots/shine, dossier `src/lib`, tel quel (fins de ligne ramenées en LF par le dépôt) |
| Révision | ab5e3526b64af1a2eaa43aa6f441a7312e013519, du 2023-01-01 |
| Licence | GNU Library General Public License, version 2 (`COPYING`) |

**Un défaut connu** (18 novembre 2026) : à 16 kHz et 48 ou 56 kbit/s, le nombre d'octets par trame, calculé en virgule
flottante, tombe à 215,99999999999997 : Shine pose le bit de bourrage sur chaque trame sans ajouter l'octet, et le flux ne se
lit plus. GrymoiR encode à 64 kbit/s, qui tombe juste (`src/atelier/son.cpp`).

Seul l'atelier le compile (`CMakeLists.txt`, bibliothèque `grym_shine`) ; `grym` en console ne l'a pas.

**Licence, à décider par le mainteneur** : Shine est sous LGPL 2, GrymoiR sous EUPL 1.2. La LGPL demande que l'utilisateur
puisse remplacer la bibliothèque : ses sources sont ici, inchangées, et l'atelier se reconstruit avec. Ajouté le
18 novembre 2026 sur demande ; non vérifié par un juriste.

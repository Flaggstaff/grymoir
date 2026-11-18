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

**Licence** : Shine est sous LGPL 2, GrymoiR sous EUPL 1.2. La LGPL (article 6) demande que l'utilisateur puisse remplacer
la bibliothèque : ses sources sont ici, inchangées, et l'atelier se reconstruit avec ; et que chaque copie dise que Shine
est là et joigne sa licence : l'atelier la montre (Aide, « Licences des composants »), chaque application fabriquée
l'emporte dans son dossier « Licences » (`src/atelier/fabrication.cpp`), et `LICENCE-fr.md` le dit. Décidé par le
mainteneur le 18 novembre 2026 (garder Shine, régler la mention) ; non vérifié par un juriste.

# SQLite embarqué

GrymoiR embarque SQLite (charte, art. 7 : « SQLite embarqué, invisible pour le développeur »).

| | |
|---|---|
| Version | 3.53.4, du 24 juillet 2026 |
| Nom officiel de la version (Fossil) | `bf7c7f30031888f4e796e429ab3978879485813aaca6f641c7b33e4e09459bcc` |
| Fichiers | `sqlite3.c` (269 649 lignes), `sqlite3.h`, `LICENSE.md` |
| Licence | domaine public (voir `LICENSE.md`) ; les seuls fichiers sous licence BSD du dépôt SQLite sont des scripts de construction, qui n'atteignent jamais `sqlite3.c` |

## Obtention, le 21 septembre 2026

1. Clone du miroir Git autorisé `https://github.com/sqlite/sqlite`, étiquette `version-3.53.4`. Le commit porte la mention `FossilOrigin-Name: bf7c7f30…`.
2. Vérification de l'arbre selon la méthode du `README.md` de SQLite (« Verifying Code Authenticity ») : l'empreinte SHA3-256 du fichier `manifest` égale le nom officiel ci-dessus, et chacun des 2 205 fichiers de l'arbre correspond à son empreinte dans `manifest`.
3. Génération de l'amalgamation par les scripts de SQLite : `./configure && make sqlite3.c`.

`SQLITE_SOURCE_ID`, dans `sqlite3.h`, contient le nom officiel ; `tests/test_base.c` le vérifie.

## Vérifier soi-même

Le nom officiel doit figurer dans la chronologie du dépôt Fossil de SQLite, à l'adresse
`https://sqlite.org/src/info/bf7c7f30031888f4`, comme la version 3.53.4.

Vérifié par Flaggstaff le 21 septembre 2026 : la page désigne bien la version 3.53.4. La chaîne de confiance est complète, du dépôt Fossil officiel jusqu'à `sqlite3.c`.

## Réglages de compilation (Makefile)

- `SQLITE_THREADSAFE=0` : la machine virtuelle n'a qu'un fil d'exécution.
- `SQLITE_OMIT_LOAD_EXTENSION` : aucun code extérieur chargé à l'exécution.
- `SQLITE_DQS=0` : les guillemets doubles désignent des noms, jamais des chaînes.
- `SQLITE_DEFAULT_FOREIGN_KEYS=1` : les liens entre entités sont vérifiés.
- `SQLITE_OMIT_DEPRECATED`, `SQLITE_DEFAULT_MEMSTATUS=0` : réglages recommandés par SQLite (`https://sqlite.org/compile.html#rcmd`), comme `SQLITE_DQS=0` et `SQLITE_OMIT_LOAD_EXTENSION`.

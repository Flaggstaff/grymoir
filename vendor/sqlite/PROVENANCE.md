# SQLite embarqué, avec son chiffrement

GrymoiR embarque SQLite (charte, art. 7 : « SQLite embarqué, invisible pour le développeur »). Depuis le 21 octobre 2026,
c'est SQLite **avec** l'extension de chiffrement SQLite3 Multiple Ciphers (SQLite3MC), pour chiffrer les bases au repos
(données de santé de Praxis). Le SQLite qu'elle contient est la même version 3.53.4, au même identifiant officiel.

| | |
|---|---|
| Extension | SQLite3 Multiple Ciphers 2.5.1, d'Ulrich Telle, publiée le 27 août 2026 (https://github.com/utelle/SQLite3MultipleCiphers/releases/tag/v2.5.1) |
| Archive | `sqlite3mc-2.5.1-sqlite-3.53.4-amalgamation.zip`, SHA-256 `4125f8ff275ea953dabb3289331b20a0e76d4fc060f57148f4a5df3bf3b0d5e0` |
| Fichiers | `sqlite3.c` = `sqlite3mc_amalgamation.c`, `sqlite3.h` = `sqlite3mc_amalgamation.h`, renommés seulement (octet pour octet) |
| Licences | SQLite : domaine public (`LICENSE.md`) ; SQLite3MC : MIT (`LICENSE-SQLite3MC.txt`) ; l'amalgamation porte aussi du code CC0 et Unlicense (Argon2 : CC0 ou Apache 2.0), selon son auteur |

## Obtention et vérification, le 21 octobre 2026

1. Téléchargement de l'archive et du fichier `sqlite3mc-2.5.1-SHA256SUMS` de la publication : l'empreinte de l'archive y figure, identique.
2. La version 2.6.1 (SQLite 3.54.0), publiée le 12 octobre 2026, est écartée : moins de quinze jours, et un autre SQLite.
3. `tests/test_base.c` vérifie la version de SQLite (3.53.4), son identifiant officiel (`bf7c7f30…`), la version de SQLite3MC, et un aller-retour de chiffrement.

**La chaîne de confiance change.** Jusqu'ici, elle remontait au dépôt Fossil officiel de SQLite (section suivante). Désormais
elle s'arrête à la publication de l'auteur de SQLite3MC : l'amalgamation est la sienne, SQLite compris. Le fichier des
empreintes est signé par Sigstore ; pour le vérifier (cosign : `brew install cosign`), dans le dossier des trois fichiers
`sqlite3mc-2.5.1-SHA256SUMS`, `.pem` et `.sig` de la publication :

```
cosign verify-blob --certificate sqlite3mc-2.5.1-SHA256SUMS.pem --signature sqlite3mc-2.5.1-SHA256SUMS.sig \
  --certificate-identity-regexp '^https://github.com/utelle/SQLite3MultipleCiphers/.*' \
  --certificate-oidc-issuer https://token.actions.githubusercontent.com sqlite3mc-2.5.1-SHA256SUMS
```

Signature vérifiée par : *(à remplir par Flaggstaff)*.

## Avant le 21 octobre 2026 : SQLite seul


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

# Mbed TLS embarqué

GrymoiR embarque Mbed TLS pour chiffrer le trafic de l'aiguilleur sur le réseau local (docs/v2.md, § 12) et créer les
certificats du cabinet (`grym certificat`). Choisi le 8 novembre 2026 plutôt que Qt : sur macOS, Qt passe par Secure
Transport, qu'Apple a déprécié et que Qt n'a pas remplacé (QTBUG-85231) ; Mbed TLS se comporte de la même façon sur macOS,
Linux et Windows, en C99, et s'éprouve partout.

| | |
|---|---|
| Version | Mbed TLS 3.6.7, branche à support long terme 3.6, publiée le 7 juillet 2026 (https://github.com/Mbed-TLS/mbedtls/releases/tag/mbedtls-3.6.7) |
| Archive | `mbedtls-3.6.7.tar.bz2`, SHA-256 `a7e8bcbec0e6f761b4af24f25677626b35f762f68eef79c08677a363212d11f6` |
| Fichiers | `include/` et `library/` de l'archive, octet pour octet, sans `library/Makefile`, `library/CMakeLists.txt` ni `include/CMakeLists.txt` ; `LICENSE` |
| Configuration | celle de l'archive (`include/mbedtls/mbedtls_config.h`), inchangée : TLS 1.2 et 1.3 |
| Licence | Apache-2.0 ou GPL-2.0-or-later, au choix ; GrymoiR la prend sous Apache-2.0 (`LICENSE`) |

## Obtention et vérification, le 8 novembre 2026

1. Téléchargement de l'archive de la publication `mbedtls-3.6.7` sur GitHub ; son empreinte SHA-256 est identique à celle
   que la page de publication donne.
2. La publication a quatre mois : plus de quinze jours, la règle des dépendances.
3. La branche 4.x (4.2.0) est écartée : elle sépare la cryptographie dans un autre projet (TF-PSA-Crypto), et la 3.6
   reçoit les correctifs de sécurité.
4. `make` construit `libmbedtls.a` à partir de `library/*.c`, en C99, sans ses avertissements (`-w`) : ce code n'est pas le
   nôtre. `tests/certificats` éprouve la création des certificats (et leur chaîne, par `openssl verify` quand il est là).

## Mettre à jour

Prendre la dernière publication 3.6.x de plus de quinze jours, vérifier son empreinte, remplacer `include/` et
`library/` (sans les fichiers de construction), refaire ce tableau, jouer `make test`.

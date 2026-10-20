/* Le coffre (grammaire 2.30) : une copie de base chiffrée par un mot de passe, que rien n'ouvre sans lui.
 *
 * La clé vient du mot de passe par Argon2id (RFC 9106), 64 Mio de mémoire, 3 passes, un sel aléatoire de 16 octets ;
 * le contenu est chiffré et authentifié par XChaCha20-Poly1305, un nonce aléatoire de 24 octets. Les deux viennent
 * de Monocypher 4.0.2 (vendor/monocypher ; son audit : voir PROVENANCE.md).
 *
 * Le fichier : « GRYMCOFFRE1\n » (12 octets), puis les blocs de mémoire et les passes d'Argon2 (deux entiers de 32 bits,
 * petit-boutistes), le sel (16), le nonce (24), le code d'authentification (16), puis le texte chiffré. Tout ce qui
 * précède le code est authentifié avec lui (données associées) : un en-tête retouché ne s'ouvre pas. */
#ifndef GRYM_COFFRE_H
#define GRYM_COFFRE_H

#include <stddef.h>

#define COFFRE_ENTETE 76   /* 12 + 4 + 4 + 16 + 24 + 16 */

/* Vrai si les octets commencent comme un coffre. */
int coffre_est(const unsigned char *o, size_t n);

/* Chiffre `clair` ; rend le coffre (à libérer) et sa taille, ou NULL et *erreur. */
unsigned char *coffre_fermer(const unsigned char *clair, size_t n, const char *mdp, size_t *taille, char **erreur);

/* Déchiffre un coffre ; rend le contenu (à libérer) et sa taille, ou NULL et *erreur : mot de passe faux et coffre
 * abîmé ne se distinguent pas, c'est voulu. */
unsigned char *coffre_ouvrir(const unsigned char *coffre, size_t n, const char *mdp, size_t *taille, char **erreur);

/* La clé d'un mot de passe, exposée pour les essais. 0 si la mémoire manque. */
int coffre_cle(const char *mdp, const unsigned char sel[16], unsigned long blocs, unsigned long passes, unsigned char cle[32]);

#endif

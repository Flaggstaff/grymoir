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

/* Les clés d'une base chiffrée (grammaire, § 16.18) : la base est chiffrée par une clé aléatoire de 32 octets, que
 * personne ne tape ; elle est rangée, enveloppée, dans le fichier « base.grymd.clés », à côté de la base.
 *
 * Le fichier : « GRYMCLES1\n » (10 octets), le nombre d'enveloppes (1 octet), puis chaque enveloppe. Une enveloppe de
 * mot de passe (sorte 1) : la sorte (1), les blocs et les passes d'Argon2id (2 × 4, petit-boutistes), le sel (16), le
 * nonce (24), le code d'authentification (16), la clé enveloppée (32), soit 97 octets ; XChaCha20-Poly1305 sous la clé
 * tirée du mot de passe, avec pour données associées la magie et l'en-tête de l'enveloppe. Une enveloppe de secours
 * (sorte 2, la clé publique du développeur) viendra à l'étape 3 ; une sorte inconnue se garde telle quelle. */
#define CLES_ENVELOPPE_MDP 97
#define CLES_ENVELOPPE_SECOURS 105

/* Une clé tirée au hasard, et le fichier des clés qui l'enveloppe sous `mdp` ; 0 et *erreur sinon. */
int cles_creer(const char *mdp, unsigned char cle[32], unsigned char **cles, size_t *n, char **erreur);

/* La clé, sortie du fichier des clés par `mdp` ; 0 et *erreur sinon (« Mot de passe faux. »). */
int cles_ouvrir(const unsigned char *cles, size_t n, const char *mdp, unsigned char cle[32], char **erreur);

/* Le même fichier, l'enveloppe du mot de passe refaite sous `nouveau` (la clé ne change pas) ; NULL et *erreur si
 * `ancien` n'ouvre pas les clés. */
unsigned char *cles_changer(const unsigned char *cles, size_t n, const char *ancien, const char *nouveau, size_t *taille,
                            char **erreur);

/* Le coffre d'une base chiffrée (format 2) : « GRYMCOFFRE2\n » (12), la longueur du fichier des clés (4), le fichier
 * des clés, le nonce (24), le code d'authentification (16), la copie claire chiffrée sous la clé de la base ; tout ce
 * qui précède le code est authentifié avec lui. Il s'ouvre par toute enveloppe des clés qu'il emporte. */
int coffre2_est(const unsigned char *o, size_t n);
unsigned char *coffre2_fermer(const unsigned char *clair, size_t n, const unsigned char *cles, size_t ncles,
                              const unsigned char cle[32], size_t *taille, char **erreur);
/* Rend la copie claire (à libérer), et, si demandés, la clé et une copie du fichier des clés (à libérer). */
unsigned char *coffre2_ouvrir(const unsigned char *o, size_t n, const char *mdp, size_t *taille, unsigned char cle[32],
                              unsigned char **cles, size_t *ncles, char **erreur);

/* Le secours (étape 3) : une enveloppe de sorte 2 met la clé de la base sous la clé publique X25519 du développeur
 * (une clé éphémère, BLAKE2b du secret partagé, XChaCha20-Poly1305) ; seule sa clé secrète l'ouvre. */
int cles_ont_secours(const unsigned char *cles, size_t n);
unsigned char *cles_ajouter_secours(const unsigned char *cles, size_t n, const unsigned char cle[32],
                                    const unsigned char publique[32], size_t *taille, char **erreur);
/* Le fichier des clés, chaque enveloppe de mot de passe refaite sous `nouveau`, la clé sortie par le secours (et
 * rendue dans `cle` si non NULL) ; NULL et *erreur si la clé secrète n'ouvre pas l'enveloppe de secours. */
unsigned char *cles_secourir(const unsigned char *cles, size_t n, const unsigned char secrete[32], const char *nouveau,
                             size_t *taille, unsigned char cle[32], char **erreur);
/* Un coffre de format 2 ouvert par le secours : la copie claire, la clé, et les clés refaites sous `nouveau`. */
unsigned char *coffre2_secourir(const unsigned char *o, size_t n, const unsigned char secrete[32], const char *nouveau,
                                size_t *taille, unsigned char cle[32], unsigned char **cles, size_t *ncles, char **erreur);

/* Le fichier de la clé de secours (« GRYMSECOURS1 », 141 octets) : la clé secrète X25519 chiffrée sous le mot de passe de
 * secours (Argon2id, XChaCha20-Poly1305), la clé publique en clair, authentifiée avec. */
unsigned char *secours_creer(const char *mdp, unsigned char publique[32], size_t *taille, char **erreur);
int secours_publique(const unsigned char *fichier, size_t n, unsigned char publique[32]);
int secours_ouvrir(const unsigned char *fichier, size_t n, const char *mdp, unsigned char secrete[32], char **erreur);
/* La clé publique en 64 chiffres hexadécimaux (le fichier « clé-de-secours.pub » d'un projet). */
int secours_hex_lire(const char *texte, unsigned char publique[32]);
void secours_hex_ecrire(const unsigned char publique[32], char h[65]);

#endif

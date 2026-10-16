/* L'empreinte d'un mot de passe (grammaire, § 16.1) : PBKDF2-HMAC-SHA256, 600 000 itérations, un sel aléatoire de
 * 16 octets, selon la recommandation de l'OWASP (Password Storage Cheat Sheet). Écrite « pbkdf2-sha256$600000$sel$clé »,
 * sel et clé en hexadécimal : l'algorithme en tête, pour en changer un jour sans perdre les anciennes. */
#ifndef GRYM_EMPREINTE_H
#define GRYM_EMPREINTE_H

#include <stddef.h>

#define EMPREINTE_ITERATIONS 600000

/* SHA-256 et PBKDF2-HMAC-SHA256, exposés pour les essais (vecteurs de référence). */
void sha256(const unsigned char *donnees, size_t n, unsigned char sortie[32]);
void pbkdf2_sha256(const unsigned char *mdp, size_t nm, const unsigned char *sel, size_t ns, unsigned long iterations,
                   unsigned char *cle, size_t nc);

/* L'empreinte d'un mot de passe, avec un sel neuf ; NULL si la source aléatoire du système fait défaut. À libérer. */
char *empreinte_creer(const char *mdp);
/* Le mot de passe correspond-il à l'empreinte ? Une empreinte illisible ne correspond à rien. Comparaison en temps
 * constant : sa durée ne dit pas jusqu'où les clés se ressemblent. */
int empreinte_verifier(const char *mdp, const char *empreinte);

#endif

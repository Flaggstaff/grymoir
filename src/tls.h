/* GrymoiR : le chiffrement du trafic et les certificats du cabinet (Mbed TLS ; docs/v2.md, § 12). */
#ifndef GRYM_TLS_H
#define GRYM_TLS_H

#include <stddef.h>

typedef struct TlsServeur TlsServeur;
typedef struct TlsConnexion TlsConnexion;

/* Dans dossier : l'autorité du cabinet (autorite.crt, autorite.cle), créée si elle manque et gardée sinon, puis un
 * certificat de serveur neuf (serveur.crt, serveur.cle) pour ces noms et adresses ; nom : le cabinet, dans le nom de
 * l'autorité. Les clés ne sont lisibles que par leur propriétaire. 0 et *erreur (à libérer) en cas d'échec. */
int tls_preparer_certificats(const char *dossier, const char *nom, const char *const *noms, size_t nb, char **erreur);

/* Le serveur, avec serveur.crt et serveur.cle du dossier ; NULL et *erreur. */
TlsServeur *tls_serveur_creer(const char *dossier, char **erreur);
void tls_serveur_detruire(TlsServeur *s);

/* La poignée de main sur une prise acceptée (le délai de lecture de la prise la borne) ; NULL et *erreur. */
TlsConnexion *tls_accepter(TlsServeur *s, int fd, char **erreur);
long tls_lire(TlsConnexion *c, void *b, size_t n);        /* octets lus, 0 à la fin, -1 en cas d'erreur */
int tls_ecrire(TlsConnexion *c, const void *b, size_t n); /* 1 : tout écrit */
void tls_fermer(TlsConnexion *c, int prevenir);           /* prevenir : envoie la fin TLS (close_notify) */

#endif

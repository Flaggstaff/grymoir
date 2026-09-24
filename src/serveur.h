/* GrymoiR : l'interface par le navigateur, servie en local (v2.0-a).
 * Spécification : docs/v2.md (révision 0.7), § 4, § 5 et § 9 ; docs/vm.md, § 13.
 *
 * Le serveur n'écoute que pendant une question, et à la fin du programme pour servir la page finale.
 * Tout le traitement HTTP passe par un transport : des prises réseau pour `grym servir`, une liste de
 * requêtes pour les tests.
 */
#ifndef GRYM_SERVEUR_H
#define GRYM_SERVEUR_H

#include "interface.h"
#include "texte.h"

#include <stddef.h>

/* Une requête brute (en-têtes et corps), ou NULL : plus rien ; *interrompu : Ctrl+C pendant l'attente. */
typedef struct {
    void *contexte;
    char *(*recevoir)(void *contexte, size_t *taille, int *interrompu);
    void (*envoyer)(void *contexte, const char *donnees, size_t taille);
} Transport;

typedef struct Serveur Serveur;

/* Serveur de test : port et jeton fixés, requêtes fournies par le transport. */
Serveur *serveur_creer(int port, const char *jeton, const char *titre, Transport t);

/* Serveur réel : écoute sur 127.0.0.1 (port 0 : choisi par le système), jeton tiré au hasard.
 * NULL et *erreur en cas d'échec. */
Serveur *serveur_ouvrir(int port, const char *titre, char **erreur);

void serveur_fermer(Serveur *s);

/* « http://127.0.0.1:PORT/?jeton=… » : l'adresse à ouvrir. */
const char *serveur_adresse(const Serveur *s);

/* L'interface à donner à la machine (machine_interface). */
Interface serveur_interface(Serveur *s);

/* Fin du programme : ce qui reste à afficher, puis l'erreur éventuelle et ce qui a été annulé (NULL si
 * tout s'est bien passé). Sert la page finale une fois, puis rend la main. */
void serveur_terminer(Serveur *s, Chaine *sortie, const char *erreur, const char *annulation);

#endif

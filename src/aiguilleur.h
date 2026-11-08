/* GrymoiR : l'aiguilleur de « grym servir --sessions » (grammaire 3.18, docs/v2.md, § 11). */
#ifndef GRYM_AIGUILLEUR_H
#define GRYM_AIGUILLEUR_H

/* Sert programme, une session « grym servir » par navigateur, lancée par grym (le chemin de l'exécutable, ou son nom
 * cherché dans le PATH) ; port 0 : le système choisit ; inactivite : minutes avant qu'une session inactive s'arrête ;
 * ouvrir : ouvre l'adresse dans le navigateur (si navigateur). Rend le code de sortie. */
/* reseau : NULL (127.0.0.1, en clair), ou le dossier des certificats (grym certificat) : l'aiguilleur écoute alors sur
 * toutes les adresses de la machine, en TLS seulement, port 8443 par défaut, et n'accepte que les noms du certificat
 * (docs/v2.md, § 12). */
int aiguilleur_servir(const char *grym, const char *programme, int port, int navigateur, int inactivite,
                      const char *reseau, void (*ouvrir)(const char *adresse));

#endif

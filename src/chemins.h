/* GrymoiR : chemins et lecture des fichiers source utilisés (grammaire, § 21). */
#ifndef GRYM_CHEMINS_H
#define GRYM_CHEMINS_H

#include <stddef.h>

/* Chemin absolu et normalisé d'un fichier existant (liens symboliques résolus), ou NULL. À libérer. */
char *chemin_absolu(const char *chemin);

/* Contenu entier d'un fichier, ou NULL s'il ne s'ouvre pas. À libérer. */
char *chemin_lire(const char *chemin, size_t *taille);

/* Vrai si le fichier existe et s'ouvre en lecture. */
int chemin_existe(const char *chemin);

#endif

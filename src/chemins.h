/* GrymoiR : chemins et lecture des fichiers source utilisés (grammaire, § 21). */
#ifndef GRYM_CHEMINS_H
#define GRYM_CHEMINS_H

#include <stddef.h>
#include <stdio.h>

/* Ouvre un fichier dont le chemin est en UTF-8, comme fopen. Sous Windows, le chemin passe par l'API « large »
 * (_wfopen) : fopen y lirait le chemin dans le codage du système, et « C:\Users\Noé\données.grym » ne s'ouvrirait
 * pas. Ailleurs, c'est fopen. */
FILE *grym_fopen(const char *chemin, const char *mode);

/* Supprime un fichier dont le chemin est en UTF-8, comme remove. */
int grym_supprimer(const char *chemin);

/* Renomme un fichier (chemins en UTF-8), comme rename ; 0 si fait. */
int grym_renommer(const char *de, const char *vers);

/* Crée les dossiers qui manquent avant le dernier « / » de chemin (comme mkdir -p) ; 0 si tout existe ensuite. */
int grym_creer_dossiers(const char *chemin);

/* « ~/ » en tête : le dossier de l'utilisateur (HOME, ou USERPROFILE sous Windows). Rend une copie, à libérer. */
char *grym_chemin_utilisateur(const char *chemin);

/* Chemin absolu et normalisé d'un fichier existant (liens symboliques résolus), ou NULL. À libérer. */
char *chemin_absolu(const char *chemin);

/* Contenu entier d'un fichier, ou NULL s'il ne s'ouvre pas. À libérer. */
char *chemin_lire(const char *chemin, size_t *taille);

/* Vrai si le fichier existe et s'ouvre en lecture. */
int chemin_existe(const char *chemin);

#endif

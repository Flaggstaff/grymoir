/* GrymoiR : utilitaires de texte UTF-8 partagés (analyseur, outils). */
#ifndef GRYM_TEXTE_H
#define GRYM_TEXTE_H

#include <stddef.h>

typedef struct { char *d; size_t n, cap; } Chaine;

void chaine_ajouter(Chaine *c, const char *s);
char *chaine_rendre(Chaine *c);          /* jamais NULL ; la chaîne appartient à l'appelant */

void *grym_allouer(size_t taille);       /* quitte le programme si la mémoire manque */
char *grym_dupliquer(const char *s);
char *grym_formater(const char *fmt, ...);

/* Distance d'édition de Levenshtein, calculée sur les points de code. */
size_t distance_edition(const char *a, const char *b);
size_t longueur_utf8(const char *s);    /* en points de code */

#endif

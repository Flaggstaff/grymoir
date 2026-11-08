/* GrymoiR : l'aiguilleur de « grym servir --sessions » (grammaire 3.18, docs/v2.md, § 11). */
#ifndef GRYM_AIGUILLEUR_H
#define GRYM_AIGUILLEUR_H

/* Sert programme, une session « grym servir » par navigateur, lancée par grym (le chemin de l'exécutable, ou son nom
 * cherché dans le PATH) ; port 0 : le système choisit ; inactivite : minutes avant qu'une session inactive s'arrête ;
 * ouvrir : ouvre l'adresse dans le navigateur (si navigateur). Rend le code de sortie. */
int aiguilleur_servir(const char *grym, const char *programme, int port, int navigateur, int inactivite,
                      void (*ouvrir)(const char *adresse));

#endif

/* GrymoiR : le chargement des bibliothèques natives par le système (grammaire, § 21.10, 3.14). */
#ifndef GRYM_NATIF_INTERNE_H
#define GRYM_NATIF_INTERNE_H

/* L'extension de la bibliothèque partagée sur ce système : « dylib », « so » ou « dll ». */
const char *natif_extension(void);

/* Ouvre la bibliothèque partagée au chemin donné (UTF-8) ; NULL et *erreur (à libérer) si elle ne s'ouvre pas. */
void *natif_ouvrir(const char *chemin, char **erreur);

/* L'adresse d'une fonction exportée, ou NULL. */
void *natif_symbole(void *poignee, const char *nom);

void natif_fermer(void *poignee);

#endif

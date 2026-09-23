/* GrymoiR : lecture de la forme compacte, v0.2
 * Spécification : docs/grammaire.md (révision 1.27), § 11.
 */
#ifndef GRYM_COMPACT_H
#define GRYM_COMPACT_H

#include "analyseur.h"
#include "lexeur.h"

/* Réécrit les jetons d'un fichier compact (terminés par J_FIN) en jetons littéraires,
 * positions conservées. Renvoie 1 et un tableau à libérer, ou 0 et *diag. */
int compact_vers_litteraire(Jeton *entree, size_t n, Jeton **sortie, size_t *nb_sortie, Diagnostic *diag);

#endif

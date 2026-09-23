/* GrymoiR : compilateur de l'arbre vers le bytecode, v0.2
 * Spécification : docs/vm.md (révision 1.20), § 3.
 */
#ifndef GRYM_COMPILATEUR_H
#define GRYM_COMPILATEUR_H

#include "analyseur.h"
#include "bytecode.h"

/* Compile un programme analysé en un module : le programme (bloc 0, terminé par
 * RETOUR), puis un bloc par formule. Renvoie NULL si le programme dépasse les
 * limites du format (65'536 constantes ou noms, 255 arguments). */
Module *compiler(const Programme *p, Diagnostic *diag);

#endif

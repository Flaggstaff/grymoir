/* GrymoiR : compilateur de l'arbre vers le bytecode, v0.2
 * Spécification : docs/vm.md (révision 1.0), § 3.
 */
#ifndef GRYM_COMPILATEUR_H
#define GRYM_COMPILATEUR_H

#include "analyseur.h"
#include "bytecode.h"

/* Compile un programme analysé en un bloc terminé par RETOUR.
 * Renvoie NULL si le programme dépasse les limites du format (65'536 constantes ou noms). */
Bloc *compiler(const Programme *p, Diagnostic *diag);

#endif

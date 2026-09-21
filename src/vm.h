/* GrymoiR : machine virtuelle, v0.2
 * Spécification : docs/vm.md (révision 1.0).
 */
#ifndef GRYM_VM_H
#define GRYM_VM_H

#include "analyseur.h"
#include "bytecode.h"
#include "texte.h"

/* Une machine garde les valeurs des noms d'une exécution à l'autre
 * (boucle interactive, rechargement de blocs). */
typedef struct Machine Machine;

Machine *machine_creer(void);
void machine_detruire(Machine *m);

/* Vérifie puis exécute un bloc. Chaque AFFICHER ajoute une ligne à *sortie.
 * En cas d'échec, le journal d'annulation rend à chaque nom la valeur qu'il
 * avait avant l'exécution (docs/vm.md, § 6), et *diag décrit l'erreur. */
int machine_executer(Machine *m, const Bloc *b, Chaine *sortie, Diagnostic *diag);

#endif

/* GrymoiR : compilateur de l'arbre vers le bytecode, v0.2
 * Spécification : docs/vm.md (révision 1.32), § 3.
 */
#ifndef GRYM_COMPILATEUR_H
#define GRYM_COMPILATEUR_H

#include "analyseur.h"
#include "bytecode.h"

/* Compile un programme analysé en un module : le programme (bloc 0, terminé par
 * RETOUR), puis un bloc par formule. Renvoie NULL si le programme dépasse les
 * limites du format (65'536 constantes ou noms, 255 arguments). */
Module *compiler(const Programme *p, Diagnostic *diag);
/* Mode exemples (grym exemples, grammaire § 9.6) : le programme ne compile que ses déclarations et ses exemples. */
void compilateur_mode_exemples(int actif);
/* grym scenarios (§ 9.7) : rang (à partir de 1) du scénario à jouer, seul, après les déclarations ; 0 : mode ordinaire. Après
 * une compilation, compilateur_nb_scenarios dit combien le programme en a. */
void compilateur_mode_scenario(int rang);   /* -1 : libère le titre retenu */
int compilateur_nb_scenarios(void);
const char *compilateur_titre_scenario(void);   /* celui du scénario compilé ; valable jusqu'à la compilation suivante */

#endif

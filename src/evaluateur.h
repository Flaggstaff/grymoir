/* GrymoiR : évaluateur de la v0.1 (parcours direct de l'arbre).
 * Solution provisoire : la charte (art. 3) prévoit un bytecode et une VM,
 * qui remplaceront ce parcours sans changer le comportement observable.
 */
#ifndef GRYM_EVALUATEUR_H
#define GRYM_EVALUATEUR_H

#include "analyseur.h"
#include "texte.h"

typedef struct Environnement Environnement;   /* valeurs des noms */

Environnement *env_creer(void);
Environnement *env_cloner(const Environnement *e);
void env_detruire(Environnement *e);

/* Exécute les phrases dans l'ordre. Chaque Afficher ajoute une ligne à *sortie,
 * de même que chaque expression seule (boucle interactive, § 3.3).
 * Renvoie 1 en cas de succès ; sinon *diag décrit l'erreur d'exécution. */
int executer(const Programme *p, Environnement *env, Chaine *sortie, Diagnostic *diag);

#endif

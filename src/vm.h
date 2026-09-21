/* GrymoiR : machine virtuelle, v0.2
 * Spécification : docs/vm.md (révision 1.8).
 */
#ifndef GRYM_VM_H
#define GRYM_VM_H

#include "analyseur.h"
#include "bytecode.h"
#include "texte.h"

#include <signal.h>

/* Mis à 1 par un gestionnaire de Ctrl+C : l'exécution en cours s'arrête proprement
 * au prochain saut arrière ou appel, et le journal l'annule (docs/vm.md, § 6). */
extern volatile sig_atomic_t grym_interruption;

/* Une machine garde les valeurs des noms d'une exécution à l'autre
 * (boucle interactive, rechargement de blocs). */
typedef struct Machine Machine;

Machine *machine_creer(void);
void machine_detruire(Machine *m);

/* Vérifie un module, enregistre ses formules (la machine en prend possession :
 * les entrées correspondantes du module passent à NULL ; une formule du même nom
 * est remplacée), puis exécute le programme. Chaque AFFICHER ajoute une ligne à *sortie.
 * En cas d'échec, le journal d'annulation rend à chaque nom sa valeur d'avant,
 * la table des formules revient à son état d'avant (docs/vm.md, § 6), et *diag décrit l'erreur. */
int machine_executer(Machine *m, Module *module, Chaine *sortie, Diagnostic *diag);

/* Objets encore vivants après le dernier ramassage (pour les tests). */
size_t machine_objets_vivants(const Machine *m);

#endif

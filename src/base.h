/* GrymoiR : base de données des entités, sur SQLite embarqué
 * Spécification : docs/grammaire.md (révision 1.29), § 16 ; docs/vm.md (révision 1.21), § 8.
 *
 * Schéma : une table « e <entité> » par entité, qui porte ses champs propres et ceux de ses
 * aptitudes ; son identifiant désigne la ligne de sa classe parente, ou de « grym_objet »
 * pour une entité sans parent. Un objet conservé a donc une ligne dans chaque table de sa
 * lignée, et un lien vers « client » accepte un membre. « grym_objet » distribue les
 * identifiants (AUTOINCREMENT : jamais réattribués) ; « grym_schema » garde la définition
 * de chaque entité.
 */
#ifndef GRYM_BASE_H
#define GRYM_BASE_H

#include "vm_interne.h"

typedef struct Base Base;

/* chemin NULL : base en mémoire. NULL et *erreur si la base ne s'ouvre pas. */
Base *base_ouvrir(const char *chemin, char **erreur);
void base_fermer(Base *b);

/* Transaction d'une exécution (§ 16.6). */
int base_commencer(Base *b, char **erreur);
int base_valider(Base *b, char **erreur);
void base_annuler(Base *b);

/* Crée la table d'une entité, ou vérifie que la base la connaît sous la même définition. */
int base_preparer(Base *b, const ClasseVM *c, char **erreur);

/* Range un objet complet ; *id reçoit son identifiant. */
int base_conserver(Base *b, const Objet *o, long *id, char **erreur);

/* Écrit dans la base le champ k d'un objet conservé. */
int base_ecrire_champ(Base *b, const Objet *o, size_t k, char **erreur);

/* Ajoute v à l'ensemble du champ multiple k d'un objet conservé, ou l'en retire (grammaire, § 16.13).
 * Gagner deux fois ne compte qu'une fois ; perdre un élément absent n'est pas une erreur. */
int base_gagner(Base *b, const Objet *o, size_t k, const Valeur *v, int perdre, char **erreur);

struct Machine;

/* Lit les champs d'un objet retrouvé ; ses liens deviennent des objets à charger à leur tour. */
int base_charger(Base *b, struct Machine *m, Objet *o, char **erreur);

/* Recherche décrite par le compilateur (grammaire, § 16.4 ; docs/vm.md, § 8) :
 * « entité ␟ mode ␟ champ du tri ␟ décroissant ␟ condition ». *resultat reçoit une liste (mode 0),
 * un objet (mode 1) ou un nombre (mode 2). */
int base_chercher(Base *b, struct Machine *m, const char *descripteur, const Valeur *params, size_t nb_params,
                  Valeur *resultat, char **erreur);

/* Retire un objet ; refusé si un lien le désigne encore. classes : toutes les classes connues,
 * pour dire lequel. */
int base_supprimer(Base *b, struct Machine *m, const Objet *o, ClasseVM *const *classes, size_t nb_classes,
                   int definitif, char **erreur);
/* Rétablit un objet supprimé, et ceux qui ont disparu avec lui (grammaire, § 16.12). */
int base_retablir(Base *b, const Objet *o, ClasseVM *const *classes, size_t nb_classes, char **erreur);
/* L'objet d'identifiant id est-il dans la corbeille ? */
int base_est_supprime(Base *b, long id);

#endif

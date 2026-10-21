/* GrymoiR : base de données des entités, sur SQLite embarqué
 * Spécification : docs/grammaire.md (révision 1.37), § 16 ; docs/vm.md (révision 1.32), § 8.
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

#include "texte.h"
#include "vm_interne.h"

typedef struct Base Base;

/* chemin NULL : base en mémoire. NULL et *erreur si la base ne s'ouvre pas. */
Base *base_ouvrir(const char *chemin, char **erreur);

/* La base chiffrée (grammaire, § 16.18) : ouverte avec son mot de passe, qui sort sa clé du fichier « chemin.clés » ;
 * sans ce fichier, une clé neuve naît, enveloppée sous `mdp`, et une base en clair se chiffre sur place. Une
 * restauration en attente de format 2 s'ouvre avec le mot de passe de sa copie. */
Base *base_ouvrir_chiffree(const char *chemin, const char *mdp, char **erreur);
/* Vrai si le fichier des clés existe (ou base en mémoire) : sinon, le mot de passe demandé sera un nouveau. */
int base_cles_existent(const char *chemin);
int base_est_chiffree(const Base *b);
/* Refait l'enveloppe du mot de passe ; 0 et *erreur (« Le mot de passe actuel est faux. ») sinon. */
int base_changer_mot_de_passe(Base *b, const char *ancien, const char *nouveau, char **erreur);
/* Le coffre de format 2 de la base telle qu'elle est (ou d'une copie claire donnée), avec ses clés. */
unsigned char *base_coffre(Base *b, size_t *taille, char **erreur);
unsigned char *base_coffre_depuis(Base *b, const unsigned char *clair, size_t n, size_t *taille, char **erreur);
void base_fermer(Base *b);

/* Transaction d'une exécution (§ 16.6). */
int base_commencer(Base *b, char **erreur);
int base_valider(Base *b, char **erreur);
/* Vrai si la transaction en cours a écrit dans la base : les règles (grammaire, § 16.14) ne se revérifient que là. */
int base_modifiee(Base *b);
/* Vrai si, au début de la transaction en cours, un autre programme avait écrit dans la base depuis la précédente. */
int base_changee_ailleurs(Base *b);
void base_annuler(Base *b);

/* Points de reprise des blocs « Essayer » (grammaire, § 18), numérotés à partir de 1 dans l'ordre
 * d'ouverture : SAVEPOINT, RELEASE (garder), ROLLBACK TO puis RELEASE (annuler). Sans transaction
 * ouverte, rien à faire. */
int base_point(Base *b, size_t n, char **erreur);

/* Valeurs d'un champ texte des objets conservés d'une entité (hors corbeille), dans l'ordre du dictionnaire,
 * au plus max : les suggestions d'un lien (docs/v2.md, § 10). table : l'entité qui porte le champ (elle-même
 * ou un parent). *valeurs, à libérer avec ses éléments ; 0 et *erreur en cas d'échec. */
int base_valeurs(Base *b, const char *entite, const char *table, const char *champ, size_t max,
                 char ***valeurs, size_t *n, char **erreur);

/* Objets conservés (hors corbeille) d'un champ multiple k de l'objet id : identifiants et classes réelles, dans
 * l'ordre où ils ont été gagnés (grammaire, § 20). À libérer ; 0 et *erreur en cas d'échec. */
int base_membres(Base *b, const ClasseVM *c, size_t k, long id, long **ids, char ***classes, size_t *n,
                 char **erreur);

/* Format des bases (charte, art. 13), gravé dans PRAGMA user_version. Une base d'un format plus récent se
 * refuse avant toute écriture ; 0 : base d'avant ce numéro, lue comme le format 1. Format 2 : les octets des
 * fichiers vivent dans grym_fichier, la table de l'entité n'en garde que le numéro (docs/vm.md, § 8). */
#define BASE_FORMAT 2
int base_lacher(Base *b, size_t n, char **erreur);
int base_revenir(Base *b, size_t n, char **erreur);

/* Rapport des migrations (docs/atelier.md, A2-c) : si rapport n'est pas NULL, base_preparer y écrit, une
 * ligne par changement, ce qu'il fait à la base (table créée, champ ajouté, et combien d'objets le reçoivent). */
void base_rapport(Base *b, Chaine *rapport);

/* Crée la table d'une entité, ou vérifie que la base la connaît sous la même définition. */
int base_preparer(Base *b, const ClasseVM *c, char **erreur);
/* Les entités renommées (« Un patient, conservé, anciennement « client », a : », grammaire, § 16.7) : leur table,
 * leurs liaisons, leurs index, leurs objets et les définitions qui les désignent prennent le nouveau nom. Avant
 * base_preparer, pour toutes les classes : une entité peut en désigner une autre, déclarée plus loin. */
int base_renommer_entites(Base *b, ClasseVM *const *classes, size_t nb, char **erreur);

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
/* Lit les octets d'un fichier resté en base (f->base) ; 1 s'ils sont là. 0 et *erreur sinon. */
int base_fichier_lire(Fichier *f, char **erreur);

/* Règles ciblées (docs/vm.md, § 3.3). base_regle_utile : vrai si la règle de clé cle doit se revérifier (jamais
 * vérifiée, ou une entité qu'elle a lue a été écrite depuis) ; la base note alors tout ce qu'elle lit, jusqu'à
 * base_regle_faite, et ces lectures valent quand la transaction est validée. base_noter_lecture : un champ lu en
 * mémoire, d'un objet conservé de la classe c. */
int base_regle_utile(Base *b, const char *cle);
void base_regle_faite(Base *b, const char *cle);
void base_noter_lecture(Base *b, const ClasseVM *c);

/* Une copie complète de la base, telle que la transaction en cours la voit (sqlite3_serialize) : ses octets, à libérer
 * par free ; NULL et *erreur sinon (grammaire, 2.29). */
unsigned char *base_copie(Base *b, size_t *taille, char **erreur);

/* Une copie de base (ses octets) est-elle une base GrymoiR intacte (en-tête, table grym_schema, intégrité) ? 1, sinon 0
 * et *erreur (grammaire 2.30). */
int base_verifier_copie(const unsigned char *o, size_t n, char **erreur);

/* L'objet d'identifiant id est-il dans la corbeille ? */
int base_est_supprime(Base *b, long id);

#endif

/* GrymoiR : représentation interne des valeurs, partagée par la machine et la base
 * Spécification : docs/vm.md (révision 1.17), § 2.
 */
#ifndef GRYM_VM_INTERNE_H
#define GRYM_VM_INTERNE_H

#include "decimal.h"

#include <stddef.h>

/* V_ABSENT : un champ facultatif sans valeur (grammaire, § 16.9) ; texte : le champ d'où elle vient, ou NULL. */
typedef enum { V_NOMBRE, V_TEXTE, V_BOOLEEN, V_OBJET, V_DATE, V_FICHIER, V_LISTE, V_ABSENT } TypeValeur;

/* Liste d'objets conservés, figée par une recherche (§ 16.4) : interne, jamais visible du langage. */
typedef struct Liste {
    size_t references;
    long *ids;
    char **classes;
    size_t n;
} Liste;

struct Objet;

/* Contenu d'un fichier (grammaire, § 15) : immuable, partagé entre les valeurs qui le désignent. */
typedef struct Fichier {
    size_t references;
    unsigned char *octets;
    size_t taille;
    char *nom;            /* nom d'origine, sans dossier */
    const char *format;   /* « PNG », « JPEG », « GIF », « WebP » ou NULL */
} Fichier;

typedef struct {
    TypeValeur type;
    Decimal nombre;
    char *texte;
    int vrai;
    struct Objet *objet;   /* référence : l'objet appartient au tas, pas à la valeur */
    long jours;            /* date : jours depuis le 01.01.1970 (date.h) */
    Fichier *fichier;      /* fichier : contenu partagé, compté */
    Liste *liste;          /* liste : partagée, comptée */
} Valeur;

/* Classe connue de la machine (grammaire, § 13). */
typedef struct ClasseVM {
    char *nom;
    int feminin;
    int aptitude;
    const struct ClasseVM *parent;
    const struct ClasseVM **aptitudes;   /* aptitudes adoptées, dans l'ordre */
    size_t nb_aptitudes;
    char **champs;        /* champs hérités d'abord, puis champs propres */
    char **types;         /* type de chaque champ (grammaire, § 16.1), ou NULL */
    char **departs;       /* valeur de départ (§ 16.7), forme canonique, ou NULL */
    const struct ClasseVM **proprietaires;   /* entité dont la table porte le champ (base.c) */
    unsigned char *uniques;   /* bit 1 : unique ; bit 2 : facultatif (§ 16.9) ; bit 4 : disparaît avec (§ 16.12) */
    size_t nb_champs;
    int conserve;         /* entité */
    char *pluriel;
} ClasseVM;

/* Objet du tas : sa classe, ses champs, et de quoi le ramasser (docs/vm.md, § 9). */
typedef struct Objet {
    const ClasseVM *classe;
    Valeur *champs;
    unsigned char *definis;
    unsigned long *epoques;   /* exécution où le champ est entré au journal */
    int marque;
    long id;                  /* identifiant en base, 0 si l'objet n'est pas conservé (§ 16.3) */
    int a_charger;            /* objet retrouvé dont les champs ne sont pas encore lus (§ 16.4) */
    struct Objet *suivant;
} Objet;

/* Services de la machine pour la base (vm.c) */
struct Machine;
Valeur vi_nombre_canonique(const char *texte);
Valeur vi_texte(const char *texte);
Valeur vi_booleen(int vrai);
Valeur vi_date(long jours);
Valeur vi_fichier(const void *octets, size_t taille, const char *nom);
Valeur vi_objet(Objet *o);
Valeur vi_absent(const char *champ);
/* Objet conservé d'identifiant id : le même en mémoire tant qu'il y vit, sinon une coquille à charger. */
Objet *machine_objet_en_base(struct Machine *m, long id, const char *classe, char **erreur);
const ClasseVM *machine_classe(const struct Machine *m, const char *nom);
/* L'objet d'identifiant id vient d'être effacé de la base : s'il vit en mémoire, il n'est plus conservé. */
void machine_objet_efface(struct Machine *m, long id);
Valeur vi_liste(long *ids, char **classes, size_t n);   /* prend possession des tableaux */
void vi_liberer(Valeur *v);

#endif

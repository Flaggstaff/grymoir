/* GrymoiR : représentation interne des valeurs, partagée par la machine et la base
 * Spécification : docs/vm.md (révision 1.11), § 2.
 */
#ifndef GRYM_VM_INTERNE_H
#define GRYM_VM_INTERNE_H

#include "decimal.h"

#include <stddef.h>

typedef enum { V_NOMBRE, V_TEXTE, V_BOOLEEN, V_OBJET, V_DATE, V_FICHIER } TypeValeur;

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
    const struct ClasseVM **proprietaires;   /* entité dont la table porte le champ (base.c) */
    unsigned char *uniques;
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
    struct Objet *suivant;
} Objet;

#endif

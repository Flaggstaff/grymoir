/* GrymoiR : arbre syntaxique, v0.1
 * Spécification : docs/grammaire.md (révision 1.1), § 5.
 * Chaque nœud garde sa position dans la source (debut, fin), pour les
 * messages d'erreur et, en v0.2, pour la traduction sans perte.
 */
#ifndef GRYM_ARBRE_H
#define GRYM_ARBRE_H

#include <stddef.h>

typedef enum {
    /* expressions */
    N_NOMBRE,        /* texte : valeur canonique « 12.50 » */
    N_NOM,           /* texte : clé du nom « prix unitaire » ; article : voir ci-dessous */
    N_NEGATION,      /* enfants[0] */
    N_OPERATION,     /* op, enfants[0], enfants[1] */
    N_GROUPE,        /* parenthèses conservées pour la traduction : enfants[0] */
    N_TEXTE,         /* texte : contenu, seulement dans Afficher */
    /* phrases */
    P_CREATION,      /* texte : nom ; enfants[0] : expression */
    P_MODIFICATION,  /* texte : nom ; enfants[0] : expression */
    P_AFFICHAGE,     /* enfants : éléments */
    P_REMARQUE,      /* texte : contenu */
    P_EXPRESSION     /* boucle interactive : enfants[0] */
} TypeNoeud;

typedef enum { ART_AUCUN, ART_LE, ART_LA, ART_L } Article;

typedef struct Noeud {
    TypeNoeud type;
    char op;              /* '+', '-', '*', '/', '^' pour N_OPERATION */
    Article article;      /* article écrit devant le nom (N_NOM, P_CREATION, P_MODIFICATION) */
    char *texte;
    struct Noeud **enfants;
    size_t nb_enfants;
    int ligne, colonne;   /* position du premier jeton */
    size_t debut, fin;    /* en points de code dans la source normalisée */
} Noeud;

Noeud *noeud_creer(TypeNoeud type, int ligne, int colonne, size_t debut);
void noeud_ajouter(Noeud *parent, Noeud *enfant);
void noeud_liberer(Noeud *n);

/* Forme parenthésée, pour les tests et l'outil grym-arbre :
 * (créer [total] (× [prix] [quantité])) */
char *noeud_decrire(const Noeud *n);

#endif

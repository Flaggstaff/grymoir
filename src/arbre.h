/* GrymoiR : arbre syntaxique, v0.1
 * Spécification : docs/grammaire.md (révision 1.3), § 6.
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
    N_BOOLEEN,       /* texte : « vrai » ou « faux » */
    N_COMPARAISON,   /* op : voir ci-dessous ; enfants[0] : sujet ; enfants[1] : terme comparé (absent pour
                        positif, négatif, nul, vrai, faux) ; negation : « n'est pas » ;
                        forme : 1 si écrite avec un symbole (<, ≤…), 0 avec des mots */
    N_LOGIQUE,       /* op : 'e' (et) ou 'o' (ou) ; enfants[0], enfants[1] */
    N_BLOC,          /* enfants : phrases d'un bloc indenté ou d'une forme courte */
    /* phrases */
    P_CREATION,      /* texte : nom ; enfants[0] : expression */
    P_MODIFICATION,  /* texte : nom ; enfants[0] : expression */
    P_AFFICHAGE,     /* enfants : éléments */
    P_REMARQUE,      /* texte : contenu */
    P_EXPRESSION,    /* boucle interactive : enfants[0] */
    P_SI             /* enfants[0] : condition ; enfants[1] : N_BLOC alors ; enfants[2] : N_BLOC sinon (facultatif) ;
                        forme : bit 0 = bloc indenté (sinon forme courte), bit 1 = introduit par « Sinon si » */
} TypeNoeud;

typedef enum { ART_AUCUN, ART_LE, ART_LA, ART_L } Article;

typedef struct Noeud {
    TypeNoeud type;
    char op;              /* N_OPERATION : '+', '-', '*', '/', '^'
                             N_COMPARAISON : '=', '!' (≠), '<', '>', 'l' (≤), 'g' (≥),
                                             'P' (positif), 'N' (négatif), '0' (nul), 'V' (vrai), 'F' (faux) */
    int negation;         /* N_COMPARAISON : « n'est pas » */
    int forme;            /* variante d'écriture, conservée pour la traduction sans perte */
    int crochets;         /* N_NOM, P_CREATION, P_MODIFICATION : nom écrit entre crochets */
    Article article;      /* article écrit devant le nom (N_NOM, P_CREATION, P_MODIFICATION) */
    char *texte;
    struct Noeud **enfants;
    size_t nb_enfants;
    int ligne, colonne;   /* position du premier jeton */
    int op_ligne, op_colonne; /* position de l'opérateur (N_OPERATION), pour les erreurs d'exécution */
    size_t debut, fin;    /* en points de code dans la source normalisée */
} Noeud;

Noeud *noeud_creer(TypeNoeud type, int ligne, int colonne, size_t debut);
void noeud_ajouter(Noeud *parent, Noeud *enfant);
void noeud_liberer(Noeud *n);

/* Forme parenthésée, pour les tests et l'outil grym-arbre :
 * (créer [total] (× [prix] [quantité])) */
char *noeud_decrire(const Noeud *n);

#endif

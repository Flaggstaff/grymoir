/* GrymoiR : lexeur de la forme littéraire, v0.1
 * Spécification : docs/grammaire.md (révision 1.11), § 1.
 */
#ifndef GRYM_LEXEUR_H
#define GRYM_LEXEUR_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    J_FIN,
    J_MOT,          /* mot en minuscules : « le », « total », « prix » */
    J_ELISION,      /* mot suivi d'une apostrophe : « l' » donne « l » */
    J_NOMBRE,       /* valeur canonique : chiffres, point décimal, sans séparateur */
    J_TEXTE,        /* contenu entre guillemets */
    J_PLUS,
    J_MOINS,
    J_FOIS,
    J_DIVISE,
    J_PUISSANCE,
    J_PAR_OUV,
    J_PAR_FERM,
    J_POINT,
    J_VIRGULE,
    J_DEUX_POINTS,
    J_REMARQUE,     /* contenu d'une ligne « Remarque : … » */
    J_EGAL,         /* =  */
    J_DIFFERENT,    /* ≠  <> */
    J_INFERIEUR,    /* <  */
    J_SUPERIEUR,    /* >  */
    J_INF_EGAL,     /* ≤  <= */
    J_SUP_EGAL,     /* ≥  >= */
    J_CROCHETS,     /* nom entre crochets ; valeur : clé du nom, « frais de port et d'emballage » */
    J_ARTICLE_IMPLICITE, /* forme compacte : phrase de modification sans article (« total << 1 ») */
    J_MOT_CLE,      /* forme compacte : « _si » ; valeur : « si » */
    J_AFFECTE,      /* forme compacte : << */
    J_POINT_VIRGULE,/* forme compacte : ; */
    J_ERREUR        /* valeur : message en français */
} TypeJeton;

typedef struct {
    TypeJeton type;
    size_t debut;     /* index du premier point de code (source normalisée) */
    size_t longueur;  /* en points de code */
    int ligne;        /* à partir de 1 */
    int colonne;      /* en points de code, à partir de 1 */
    char *valeur;     /* UTF-8, appartient au jeton, NULL si sans objet */
    int retrait;      /* colonne qui compte pour les blocs (§ 5.4) ; la forme compacte la calcule */
    int synthetique;  /* jeton produit par la lecture de la forme compacte, sans équivalent écrit */
    int ligne_fin;    /* dernière ligne occupée par la construction qu'il termine (« _fin » compris) */
} Jeton;

typedef struct Lexeur Lexeur;

/* Renvoie NULL si la source n'est pas de l'UTF-8 valide ;
 * *erreur reçoit alors un message à libérer avec free(). */
Lexeur *lexeur_creer(const char *source, size_t taille, char **erreur);

/* Lecture de la forme compacte (grammaire, § 11.1) : mots-clés « _si », noms à soulignés
 * (rendus comme des jetons CROCHETS synthétiques, « prix_unitaire » → « prix unitaire »),
 * « << », « ; » et remarques « # ». */
Lexeur *lexeur_creer_compact(const char *source, size_t taille, char **erreur);

/* Renvoie le jeton suivant. Après J_FIN ou J_ERREUR, renvoie toujours J_FIN. */
Jeton lexeur_suivant(Lexeur *lx);

void jeton_liberer(Jeton *j);
void lexeur_detruire(Lexeur *lx);
const char *type_jeton_nom(TypeJeton t);

#endif

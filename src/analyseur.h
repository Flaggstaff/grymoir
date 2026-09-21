/* GrymoiR : analyseur de la forme littéraire, v0.1
 * Spécification : docs/grammaire.md (révision 1.19), § 2 à 13.
 *
 * L'analyseur construit l'arbre et résout les noms dans le même passage :
 * la plus longue correspondance des noms composés (§ 2.2) exige de connaître
 * les noms déjà déclarés. Il vérifie aussi vaut/devient (§ 2.1) et le genre (§ 2.3),
 * et calcule à chaque position les suites valides (§ 8), qui servent aux
 * messages d'erreur et à l'aide à la saisie.
 */
#ifndef GRYM_ANALYSEUR_H
#define GRYM_ANALYSEUR_H

#include "arbre.h"

/* Noms déclarés, conservés d'un appel à l'autre (boucle interactive). */
typedef struct Portee Portee;
Portee *portee_creer(void);
void portee_detruire(Portee *p);
Portee *portee_cloner(const Portee *p);

typedef struct {
    Noeud **phrases;
    size_t nb;
    int nb_locaux;   /* cases locales du programme principal (compteurs de boucles, sujets de Selon) */
} Programme;

typedef struct {
    char *message;   /* NULL si aucune erreur */
    int ligne;       /* 0 si sans objet (erreur d'encodage : la position est dans le message) */
    int colonne;
} Diagnostic;

/* Analyse une source complète. Renvoie 1 en cas de succès.
 * En cas d'échec, *diag décrit la première erreur, le programme reste vide
 * et la portée n'est pas modifiée : une phrase ratée ne déclare rien.
 * En mode interactif, une expression seule est acceptée (§ 3.3)
 * et le point final de la dernière phrase est facultatif. */
int analyser(const char *source, size_t taille, Portee *portee, int interactif,
             Programme *programme, Diagnostic *diag);

/* Même analyse pour un fichier en forme compacte (grammaire, § 11). */
int analyser_compact(const char *source, size_t taille, Portee *portee,
                     Programme *programme, Diagnostic *diag);

void programme_liberer(Programme *p);

/* Vrai si le nom contient un mot réservé : il s'écrit alors entre crochets (§ 2.2). */
int nom_exige_crochets(const char *nom);

/* Aide à la saisie (§ 8, charte art. 9) : suites valides à la fin de `source`,
 * c'est-à-dire à la position du curseur. Si un mot est en cours de frappe,
 * seules les suites qui le prolongent sont proposées.
 * Les entrées entre parenthèses, « (nombre) », décrivent une catégorie. */
typedef struct {
    char **items;
    size_t nb;
} Suggestions;

Suggestions suites_valides(const char *source, size_t taille);
void suggestions_liberer(Suggestions *s);
void diagnostic_liberer(Diagnostic *d);

#endif

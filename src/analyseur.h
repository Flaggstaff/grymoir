/* GrymoiR : analyseur de la forme littéraire, v0.1
 * Spécification : grammaire-v0.1.md, § 2 à 6.
 *
 * L'analyseur construit l'arbre et résout les noms dans le même passage :
 * la plus longue correspondance des noms composés (§ 2.2) exige de connaître
 * les noms déjà déclarés. Il vérifie aussi vaut/devient (§ 2.1) et le genre (§ 2.3).
 */
#ifndef GRYM_ANALYSEUR_H
#define GRYM_ANALYSEUR_H

#include "arbre.h"

/* Noms déclarés, conservés d'un appel à l'autre (boucle interactive). */
typedef struct Portee Portee;
Portee *portee_creer(void);
void portee_detruire(Portee *p);

typedef struct {
    Noeud **phrases;
    size_t nb;
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

void programme_liberer(Programme *p);
void diagnostic_liberer(Diagnostic *d);

#endif

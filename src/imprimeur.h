/* GrymoiR : imprimeurs de l'arbre, v0.2
 * Spécification : docs/grammaire.md (révision 1.26), § 11 et § 12.
 *
 * L'arbre retient presque tout ce que la forme littéraire exprime (articles,
 * crochets, tournures en mots ou en symboles, contractions, formes courtes) :
 * l'imprimeur littéraire le restitue, et ne normalise que la présentation
 * (espaces, casse, écriture des nombres et des textes, lignes vides multiples).
 */
#ifndef GRYM_IMPRIMEUR_H
#define GRYM_IMPRIMEUR_H

#include "analyseur.h"

/* Forme littéraire canonique (grym formater). */
char *imprimer_litteraire(const Programme *p);

/* Forme compacte (grym traduire, du littéraire vers le compact). */
char *imprimer_compact(const Programme *p);

#endif

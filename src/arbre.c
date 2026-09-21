/* GrymoiR : arbre syntaxique, v0.1 */
#include "arbre.h"
#include "texte.h"

#include <stdio.h>
#include <stdlib.h>

Noeud *noeud_creer(TypeNoeud type, int ligne, int colonne, size_t debut) {
    Noeud *n = grym_allouer(sizeof *n);
    n->type = type;
    n->op = 0;
    n->article = ART_AUCUN;
    n->texte = NULL;
    n->enfants = NULL;
    n->nb_enfants = 0;
    n->ligne = ligne;
    n->colonne = colonne;
    n->op_ligne = ligne;
    n->op_colonne = colonne;
    n->debut = debut;
    n->fin = debut;
    return n;
}

void noeud_ajouter(Noeud *parent, Noeud *enfant) {
    Noeud **e = realloc(parent->enfants, (parent->nb_enfants + 1) * sizeof *e);
    if (!e) {
        fputs("GrymoiR : mémoire épuisée\n", stderr);
        exit(EXIT_FAILURE);
    }
    parent->enfants = e;
    parent->enfants[parent->nb_enfants++] = enfant;
}

void noeud_liberer(Noeud *n) {
    if (!n) return;
    for (size_t i = 0; i < n->nb_enfants; i++) noeud_liberer(n->enfants[i]);
    free(n->enfants);
    free(n->texte);
    free(n);
}

static const char *symbole(char op) {
    switch (op) {
    case '+': return "+";
    case '-': return "−";
    case '*': return "×";
    case '/': return "÷";
    case '^': return "^";
    default:  return "?";
    }
}

static void decrire(const Noeud *n, Chaine *c) {
    switch (n->type) {
    case N_NOMBRE:
        chaine_ajouter(c, n->texte);
        return;
    case N_NOM:
        chaine_ajouter(c, "[");
        chaine_ajouter(c, n->texte);
        chaine_ajouter(c, "]");
        return;
    case N_TEXTE:
        chaine_ajouter(c, "«");
        chaine_ajouter(c, n->texte);
        chaine_ajouter(c, "»");
        return;
    case N_NEGATION:
        chaine_ajouter(c, "(− ");
        decrire(n->enfants[0], c);
        chaine_ajouter(c, ")");
        return;
    case N_OPERATION:
        chaine_ajouter(c, "(");
        chaine_ajouter(c, symbole(n->op));
        chaine_ajouter(c, " ");
        decrire(n->enfants[0], c);
        chaine_ajouter(c, " ");
        decrire(n->enfants[1], c);
        chaine_ajouter(c, ")");
        return;
    case N_GROUPE:
        chaine_ajouter(c, "(groupe ");
        decrire(n->enfants[0], c);
        chaine_ajouter(c, ")");
        return;
    case P_CREATION:
    case P_MODIFICATION:
        chaine_ajouter(c, n->type == P_CREATION ? "(créer [" : "(modifier [");
        chaine_ajouter(c, n->texte);
        chaine_ajouter(c, "] ");
        decrire(n->enfants[0], c);
        chaine_ajouter(c, ")");
        return;
    case P_AFFICHAGE:
        chaine_ajouter(c, "(afficher");
        for (size_t i = 0; i < n->nb_enfants; i++) {
            chaine_ajouter(c, " ");
            decrire(n->enfants[i], c);
        }
        chaine_ajouter(c, ")");
        return;
    case P_REMARQUE:
        chaine_ajouter(c, "(remarque «");
        chaine_ajouter(c, n->texte);
        chaine_ajouter(c, "»)");
        return;
    case P_EXPRESSION:
        chaine_ajouter(c, "(évaluer ");
        decrire(n->enfants[0], c);
        chaine_ajouter(c, ")");
        return;
    }
}

char *noeud_decrire(const Noeud *n) {
    Chaine c = {0};
    decrire(n, &c);
    return chaine_rendre(&c);
}

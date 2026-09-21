/* GrymoiR : compilateur de l'arbre vers le bytecode, v0.2 */
#include "compilateur.h"
#include "texte.h"

#include <stdlib.h>

typedef struct {
    Bloc *b;
    Diagnostic *diag;
    int echec;
} Compilation;

static void trop_grand(Compilation *c, const Noeud *n) {
    if (c->echec) return;
    c->echec = 1;
    c->diag->message = grym_dupliquer(
        "Programme trop grand : un bloc accepte au plus 65'536 constantes et 65'536 noms.");
    c->diag->ligne = n->ligne;
    c->diag->colonne = n->colonne;
}

static void emettre(Compilation *c, CodeInstruction code, long operande, int ligne, int colonne) {
    bloc_emettre(c->b, code, (uint16_t)(operande < 0 ? 0 : operande), ligne, colonne);
}

static void expression(Compilation *c, const Noeud *n) {
    if (c->echec) return;
    switch (n->type) {
    case N_NOMBRE:
    case N_TEXTE: {
        long k = bloc_constante(c->b, n->type == N_NOMBRE ? C_NOMBRE : C_TEXTE, n->texte);
        if (k < 0) { trop_grand(c, n); return; }
        emettre(c, I_CONSTANTE, k, n->ligne, n->colonne);
        return;
    }
    case N_NOM: {
        long k = bloc_nom(c->b, n->texte);
        if (k < 0) { trop_grand(c, n); return; }
        emettre(c, I_LIRE, k, n->ligne, n->colonne);
        return;
    }
    case N_GROUPE:
        expression(c, n->enfants[0]);
        return;
    case N_NEGATION:
        expression(c, n->enfants[0]);
        emettre(c, I_NEGATION, 0, n->ligne, n->colonne);
        return;
    case N_OPERATION: {
        expression(c, n->enfants[0]);
        expression(c, n->enfants[1]);
        CodeInstruction code =
            n->op == '+' ? I_ADDITION :
            n->op == '-' ? I_SOUSTRACTION :
            n->op == '*' ? I_MULTIPLICATION :
            n->op == '/' ? I_DIVISION : I_PUISSANCE;
        emettre(c, code, 0, n->op_ligne, n->op_colonne);   /* l'erreur pointera l'opérateur */
        return;
    }
    default:
        return;
    }
}

Bloc *compiler(const Programme *p, Diagnostic *diag) {
    Compilation c = { bloc_creer(), diag, 0 };
    diag->message = NULL;
    diag->ligne = diag->colonne = 0;
    for (size_t i = 0; i < p->nb && !c.echec; i++) {
        const Noeud *ph = p->phrases[i];
        switch (ph->type) {
        case P_CREATION:
        case P_MODIFICATION: {
            expression(&c, ph->enfants[0]);
            long k = bloc_nom(c.b, ph->texte);
            if (k < 0) { trop_grand(&c, ph); break; }
            emettre(&c, I_ECRIRE, k, ph->ligne, ph->colonne);
            break;
        }
        case P_AFFICHAGE:
            if (ph->nb_enfants > 0xFFFF) { trop_grand(&c, ph); break; }
            for (size_t e = 0; e < ph->nb_enfants; e++) expression(&c, ph->enfants[e]);
            emettre(&c, I_AFFICHER, (long)ph->nb_enfants, ph->ligne, ph->colonne);
            break;
        case P_EXPRESSION:
            expression(&c, ph->enfants[0]);
            emettre(&c, I_AFFICHER, 1, ph->ligne, ph->colonne);
            break;
        default:   /* P_REMARQUE : rien à exécuter */
            break;
        }
    }
    if (c.echec) {
        bloc_detruire(c.b);
        return NULL;
    }
    int ligne = p->nb ? p->phrases[p->nb - 1]->ligne : 0;
    bloc_emettre(c.b, I_RETOUR, 0, ligne, 0);
    return c.b;
}

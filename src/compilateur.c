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

static void constante(Compilation *c, TypeConstante type, const char *texte, const Noeud *n, int ligne, int colonne) {
    long k = bloc_constante(c->b, type, texte);
    if (k < 0) { trop_grand(c, n); return; }
    emettre(c, I_CONSTANTE, k, ligne, colonne);
}

static void expression(Compilation *c, const Noeud *n);

/* Condition : après son code, la pile porte vrai ou faux. SAUTER_SI_FAUX vérifie
 * au passage que chaque membre de « et » et « ou » est bien un booléen. */
static void logique(Compilation *c, const Noeud *n) {
    int l = n->op_ligne, col = n->op_colonne;
    expression(c, n->enfants[0]);
    if (n->op == 'e') {
        /* g ; si faux → faux ; d ; si faux → faux ; vrai */
        size_t s1 = bloc_emettre_saut(c->b, I_SAUTER_SI_FAUX, l, col);
        expression(c, n->enfants[1]);
        size_t s2 = bloc_emettre_saut(c->b, I_SAUTER_SI_FAUX, l, col);
        constante(c, C_BOOLEEN, "vrai", n, l, col);
        size_t s3 = bloc_emettre_saut(c->b, I_SAUTER, l, col);
        bloc_corriger_saut(c->b, s1, c->b->taille_code);
        bloc_corriger_saut(c->b, s2, c->b->taille_code);
        constante(c, C_BOOLEEN, "faux", n, l, col);
        bloc_corriger_saut(c->b, s3, c->b->taille_code);
    } else {
        /* g ; si faux → examiner d ; vrai ; … d ; si faux → faux ; vrai */
        size_t s1 = bloc_emettre_saut(c->b, I_SAUTER_SI_FAUX, l, col);
        constante(c, C_BOOLEEN, "vrai", n, l, col);
        size_t s2 = bloc_emettre_saut(c->b, I_SAUTER, l, col);
        bloc_corriger_saut(c->b, s1, c->b->taille_code);
        expression(c, n->enfants[1]);
        size_t s3 = bloc_emettre_saut(c->b, I_SAUTER_SI_FAUX, l, col);
        constante(c, C_BOOLEEN, "vrai", n, l, col);
        size_t s4 = bloc_emettre_saut(c->b, I_SAUTER, l, col);
        bloc_corriger_saut(c->b, s3, c->b->taille_code);
        constante(c, C_BOOLEEN, "faux", n, l, col);
        bloc_corriger_saut(c->b, s2, c->b->taille_code);
        bloc_corriger_saut(c->b, s4, c->b->taille_code);
    }
}

static void comparaison(Compilation *c, const Noeud *n) {
    int l = n->op_ligne, col = n->op_colonne;
    expression(c, n->enfants[0]);
    CodeInstruction code;
    switch (n->op) {
    case '=': code = I_EGAL; break;
    case '!': code = I_DIFFERENT; break;
    case '<': code = I_INFERIEUR; break;
    case '>': code = I_SUPERIEUR; break;
    case 'l': code = I_INFERIEUR_OU_EGAL; break;
    case 'g': code = I_SUPERIEUR_OU_EGAL; break;
    case 'P': code = I_SUPERIEUR; break;   /* positif : > 0 (sens courant, § 5.1) */
    case 'N': code = I_INFERIEUR; break;   /* négatif : < 0 */
    case '0': code = I_EGAL; break;        /* nul : = 0 */
    default:  code = I_EGAL; break;        /* vrai, faux */
    }
    if (n->nb_enfants > 1)                        expression(c, n->enfants[1]);
    else if (n->op == 'V' || n->op == 'F')        constante(c, C_BOOLEEN, n->op == 'V' ? "vrai" : "faux", n, l, col);
    else                                          constante(c, C_NOMBRE, "0", n, l, col);
    emettre(c, code, 0, l, col);
    if (n->negation) emettre(c, I_NON, 0, l, col);
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
    case N_BOOLEEN:
        constante(c, C_BOOLEEN, n->texte, n, n->ligne, n->colonne);
        return;
    case N_COMPARAISON:
        comparaison(c, n);
        return;
    case N_LOGIQUE:
        logique(c, n);
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

static void phrase(Compilation *c, const Noeud *ph);

static void phrases(Compilation *c, Noeud *const *liste, size_t nb) {
    for (size_t i = 0; i < nb && !c->echec; i++) phrase(c, liste[i]);
}

static void phrase(Compilation *c, const Noeud *ph) {
    switch (ph->type) {
    case P_CREATION:
    case P_MODIFICATION: {
        expression(c, ph->enfants[0]);
        long k = bloc_nom(c->b, ph->texte);
        if (k < 0) { trop_grand(c, ph); return; }
        emettre(c, I_ECRIRE, k, ph->ligne, ph->colonne);
        return;
    }
    case P_AFFICHAGE:
        if (ph->nb_enfants > 0xFFFF) { trop_grand(c, ph); return; }
        for (size_t e = 0; e < ph->nb_enfants; e++) expression(c, ph->enfants[e]);
        emettre(c, I_AFFICHER, (long)ph->nb_enfants, ph->ligne, ph->colonne);
        return;
    case P_EXPRESSION:
        expression(c, ph->enfants[0]);
        emettre(c, I_AFFICHER, 1, ph->ligne, ph->colonne);
        return;
    case P_SI: {
        /* condition ; si faux → sinon ; alors ; → fin ; sinon ; fin */
        const Noeud *cond = ph->enfants[0];
        expression(c, cond);
        size_t vers_sinon = bloc_emettre_saut(c->b, I_SAUTER_SI_FAUX, cond->ligne, cond->colonne);
        phrase(c, ph->enfants[1]);
        if (ph->nb_enfants > 2) {
            size_t vers_fin = bloc_emettre_saut(c->b, I_SAUTER, ph->ligne, ph->colonne);
            bloc_corriger_saut(c->b, vers_sinon, c->b->taille_code);
            phrase(c, ph->enfants[2]);
            bloc_corriger_saut(c->b, vers_fin, c->b->taille_code);
        } else {
            bloc_corriger_saut(c->b, vers_sinon, c->b->taille_code);
        }
        return;
    }
    case N_BLOC:
        phrases(c, ph->enfants, ph->nb_enfants);
        return;
    default:   /* P_REMARQUE : rien à exécuter */
        return;
    }
}

Bloc *compiler(const Programme *p, Diagnostic *diag) {
    Compilation c = { bloc_creer(), diag, 0 };
    diag->message = NULL;
    diag->ligne = diag->colonne = 0;
    phrases(&c, p->phrases, p->nb);
    if (c.echec) {
        bloc_detruire(c.b);
        return NULL;
    }
    int ligne = p->nb ? p->phrases[p->nb - 1]->ligne : 0;
    bloc_emettre(c.b, I_RETOUR, 0, ligne, 0);
    return c.b;
}

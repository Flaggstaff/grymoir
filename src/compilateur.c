/* GrymoiR : compilateur de l'arbre vers le bytecode, v0.2 */
#include "compilateur.h"
#include "texte.h"

#include <stdlib.h>

typedef struct {
    Bloc *b;           /* bloc en cours de compilation */
    Module *module;
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
static void appel(Compilation *c, const Noeud *n, int rend);

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

/* Arguments empilés dans l'ordre, puis APPELER (docs/vm.md, § 3). */
static void appel(Compilation *c, const Noeud *n, int rend) {
    if (n->nb_enfants > 255) { trop_grand(c, n); return; }
    for (size_t k = 0; k < n->nb_enfants; k++) expression(c, n->enfants[k]);
    long nom = bloc_nom(c->b, n->texte);
    if (nom < 0) { trop_grand(c, n); return; }
    bloc_emettre_appel(c->b, (uint16_t)nom, (uint8_t)n->nb_enfants, rend, n->ligne, n->colonne);
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
        if (n->local >= 0) {
            emettre(c, I_LIRE_LOCAL, n->local, n->ligne, n->colonne);
            return;
        }
        long k = bloc_nom(c->b, n->texte);
        if (k < 0) { trop_grand(c, n); return; }
        emettre(c, I_LIRE, k, n->ligne, n->colonne);
        return;
    }
    case N_APPEL:
        appel(c, n, 1);
        return;
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
        if (ph->local >= 0) {
            emettre(c, I_ECRIRE_LOCAL, ph->local, ph->ligne, ph->colonne);
            return;
        }
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
    case P_RENDRE:
        expression(c, ph->enfants[0]);
        emettre(c, I_RENDRE, 0, ph->ligne, ph->colonne);
        return;
    case P_APPEL:
        appel(c, ph, 0);
        return;
    case P_CALCUL:
    case P_ACTION: {
        /* Une formule se compile dans son propre bloc ; le programme n'en garde aucune trace. */
        Bloc *prec = c->b;
        Bloc *f = bloc_creer();
        f->nom = grym_dupliquer(ph->texte);
        f->sorte = ph->type == P_CALCUL ? B_CALCUL : B_ACTION;
        f->nb_parametres = (int)ph->enfants[0]->nb_enfants;
        f->nb_locaux = ph->entier;
        module_ajouter(c->module, f);
        c->b = f;
        const Noeud *corps = ph->enfants[1];
        if (ph->type == P_CALCUL && ph->forme == 0) {
            expression(c, corps);
            emettre(c, I_RENDRE, 0, corps->ligne, corps->colonne);
        } else {
            phrase(c, corps);
            if (ph->type == P_ACTION) emettre(c, I_RETOUR, 0, ph->ligne, 0);
        }
        c->b = prec;
        return;
    }
    default:   /* P_REMARQUE : rien à exécuter */
        return;
    }
}

Module *compiler(const Programme *p, Diagnostic *diag) {
    Module *m = module_creer();
    Compilation c = { bloc_creer(), m, diag, 0 };
    module_ajouter(m, c.b);
    diag->message = NULL;
    diag->ligne = diag->colonne = 0;
    phrases(&c, p->phrases, p->nb);
    if (c.echec) {
        module_detruire(m);
        return NULL;
    }
    int ligne = p->nb ? p->phrases[p->nb - 1]->ligne : 0;
    bloc_emettre(m->blocs[0], I_RETOUR, 0, ligne, 0);
    return m;
}

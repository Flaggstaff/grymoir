/* GrymoiR : compilateur de l'arbre vers le bytecode, v0.2 */
#include "compilateur.h"
#include "texte.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Boucle en cours : où va « Passer au tour suivant », quels sauts « Sortir » doit corriger. */
typedef struct {
    size_t suivant;          /* cible de « Passer », si elle est déjà connue */
    int suivant_connu;
    size_t *sorties, nb_sorties;
    size_t *suivants, nb_suivants;   /* « Passer » en attente de leur cible */
} Boucle;

typedef struct {
    Bloc *b;           /* bloc en cours de compilation */
    Module *module;
    Diagnostic *diag;
    int echec;
    Boucle *boucles;
    size_t nb_boucles;
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
    if (n->op == 'A' || n->op == 'R') {   /* est absent, est présent (§ 16.9) */
        emettre(c, I_EST_ABSENT, 0, l, col);
        if ((n->op == 'R') != (n->negation != 0)) emettre(c, I_NON, 0, l, col);
        return;
    }
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

/* ---------------------------------------------------------------- */
/* Recherche dans la base (grammaire, § 16.4)                       */
/* ---------------------------------------------------------------- */

static void expression(Compilation *c, const Noeud *n);

/* Condition « dont » en descripteur ; chaque valeur comparée est calculée ici et devient « ?n ». */
static void condition_dont(Compilation *c, const Noeud *n, Chaine *d, int *parametres) {
    if (n->type == N_GROUPE) { condition_dont(c, n->enfants[0], d, parametres); return; }
    if (n->type == N_LOGIQUE) {
        chaine_ajouter(d, n->op == 'e' ? "(e" : "(o");
        condition_dont(c, n->enfants[0], d, parametres);
        condition_dont(c, n->enfants[1], d, parametres);
        chaine_ajouter(d, ")");
        return;
    }
    /* N_COMPARAISON, champ à gauche (vérifié à l'analyse) */
    if (n->negation) chaine_ajouter(d, "(n");
    char op[4] = { '(', n->op, '[', 0 };
    chaine_ajouter(d, op);
    chaine_ajouter(d, n->enfants[0]->texte);
    chaine_ajouter(d, "]");
    if (n->nb_enfants == 2) {
        expression(c, n->enfants[1]);
        char t[16];
        snprintf(t, sizeof t, "?%d", ++*parametres);
        chaine_ajouter(d, t);
    }
    chaine_ajouter(d, ")");
    if (n->negation) chaine_ajouter(d, ")");
}

static void chercher(Compilation *c, const Noeud *n) {
    Chaine d = {0};
    chaine_ajouter(&d, n->texte);
    char t[8];
    snprintf(t, sizeof t, "\x1f%d\x1f", n->forme + (n->negation ? 3 : 0));   /* 3 à 5 : la corbeille (§ 16.12) */
    chaine_ajouter(&d, t);
    if (n->texte2) chaine_ajouter(&d, n->texte2);
    chaine_ajouter(&d, n->entier ? "\x1f" "1\x1f" : "\x1f" "0\x1f");
    int parametres = 0;
    /* « les œuvres de bach » (§ 16.10), « les interprètes de o » (§ 16.13) : l'objet en dernier enfant */
    int inverse = n->op == 'I' || n->op == 'M';
    int condition = n->nb_enfants > (size_t)inverse;
    if (inverse) {
        if (condition) chaine_ajouter(&d, "(e");
        expression(c, n->enfants[n->nb_enfants - 1]);
        char t[16];
        snprintf(t, sizeof t, "(%c?%d", n->op, ++parametres);
        chaine_ajouter(&d, t);
        if (n->op == 'M') { chaine_ajouter(&d, "["); chaine_ajouter(&d, n->texte3); chaine_ajouter(&d, "]"); }
        chaine_ajouter(&d, ")");
    }
    if (condition) condition_dont(c, n->enfants[0], &d, &parametres);
    if (inverse && condition) chaine_ajouter(&d, ")");
    char *texte = chaine_rendre(&d);
    long k = bloc_constante(c->b, C_RECHERCHE, texte);
    free(texte);
    if (k < 0) { trop_grand(c, n); return; }
    emettre(c, I_CHERCHER, k, n->ligne, n->colonne);
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
    case N_SUJET:
        emettre(c, I_LIRE_LOCAL, n->local, n->ligne, n->colonne);
        return;
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
    case N_NOUVEAU: {
        long k = bloc_nom(c->b, n->texte);
        if (k < 0) { trop_grand(c, n); return; }
        emettre(c, I_NOUVEAU, k, n->ligne, n->colonne);
        for (size_t q = 0; q < n->nb_enfants; q++) {
            const Noeud *init = n->enfants[q];
            expression(c, init->enfants[0]);
            long ch = bloc_nom(c->b, init->texte);
            if (ch < 0) { trop_grand(c, n); return; }
            emettre(c, I_INITIALISER_CHAMP, ch, init->ligne, init->colonne);
        }
        return;
    }
    case N_DATE: {
        long k = bloc_constante(c->b, C_DATE, n->texte);
        if (k < 0) { trop_grand(c, n); return; }
        emettre(c, I_CONSTANTE, k, n->ligne, n->colonne);
        return;
    }
    case N_AUJOURDHUI:
        emettre(c, I_AUJOURDHUI, 0, n->ligne, n->colonne);
        return;
    case N_ABSENT:
        emettre(c, I_ABSENT, 0, n->ligne, n->colonne);
        return;
    case N_CHERCHER:
        chercher(c, n);
        return;
    case N_CADRE:   /* la valeur, la largeur, puis CADRER avec le sens (§ 4.2) */
        expression(c, n->enfants[0]);
        expression(c, n->enfants[1]);
        emettre(c, I_CADRER, (long)n->forme, n->op_ligne, n->op_colonne);
        return;
    case N_REPONSE: {   /* la question, puis DEMANDER avec le type (§ 17) */
        expression(c, n->enfants[0]);
        long t = bloc_nom(c->b, n->texte2);
        if (t < 0) { trop_grand(c, n); return; }
        emettre(c, I_DEMANDER, t, n->ligne, n->colonne);
        return;
    }
    case N_FICHIER:
        expression(c, n->enfants[0]);
        emettre(c, I_LIRE_FICHIER, 0, n->ligne, n->colonne);
        return;
    case N_CHAMP: {
        expression(c, n->enfants[0]);
        long ch = bloc_nom(c->b, n->texte);
        if (ch < 0) { trop_grand(c, n); return; }
        emettre(c, I_LIRE_CHAMP, ch, n->op_ligne, n->op_colonne);
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

static void ajouter_saut(size_t **liste, size_t *nb, size_t pos) {
    size_t *t = grym_allouer((*nb + 1) * sizeof *t);
    if (*nb) memcpy(t, *liste, *nb * sizeof *t);
    free(*liste);
    *liste = t;
    (*liste)[(*nb)++] = pos;
}

static void entrer_boucle(Compilation *c, size_t suivant, int connu) {
    Boucle *t = grym_allouer((c->nb_boucles + 1) * sizeof *t);
    if (c->nb_boucles) memcpy(t, c->boucles, c->nb_boucles * sizeof *t);
    free(c->boucles);
    c->boucles = t;
    Boucle *b = &c->boucles[c->nb_boucles++];
    b->suivant = suivant;
    b->suivant_connu = connu;
    b->sorties = b->suivants = NULL;
    b->nb_sorties = b->nb_suivants = 0;
}

/* Fixe la cible des « Passer » en attente (tour suivant). */
static void cible_suivant(Compilation *c, size_t cible) {
    Boucle *b = &c->boucles[c->nb_boucles - 1];
    for (size_t k = 0; k < b->nb_suivants; k++) bloc_corriger_saut(c->b, b->suivants[k], cible);
    b->nb_suivants = 0;
    b->suivant = cible;
    b->suivant_connu = 1;
}

/* Termine la boucle : les « Sortir » sautent ici. */
static void sortir_boucle(Compilation *c) {
    Boucle *b = &c->boucles[--c->nb_boucles];
    for (size_t k = 0; k < b->nb_sorties; k++) bloc_corriger_saut(c->b, b->sorties[k], c->b->taille_code);
    free(b->sorties);
    free(b->suivants);
}

static void echouer_si(Compilation *c, const char *message, const Noeud *n) {
    long k = bloc_constante(c->b, C_TEXTE, message);
    if (k < 0) { trop_grand(c, n); return; }
    emettre(c, I_ECHOUER, k, n->ligne, n->colonne);
}

/* Selon : le sujet est rangé une fois ; chaque cas teste ses conditions dans l'ordre. */
static void condition_de_cas(Compilation *c, const Noeud *e, int case_sujet) {
    if (e->type == N_INTERVALLE) {
        /* Entre deux bornes, dans un ordre quelconque : (s ≥ a et s ≤ b) ou (s ≤ a et s ≥ b).
         * Les bornes peuvent être calculées deux fois : ce sont des expressions sans effet. */
        int l = e->ligne, col = e->colonne;
        emettre(c, I_LIRE_LOCAL, case_sujet, l, col);
        expression(c, e->enfants[0]);
        emettre(c, I_SUPERIEUR_OU_EGAL, 0, l, col);
        size_t autre1 = bloc_emettre_saut(c->b, I_SAUTER_SI_FAUX, l, col);
        emettre(c, I_LIRE_LOCAL, case_sujet, l, col);
        expression(c, e->enfants[1]);
        emettre(c, I_INFERIEUR_OU_EGAL, 0, l, col);
        size_t autre2 = bloc_emettre_saut(c->b, I_SAUTER_SI_FAUX, l, col);
        constante(c, C_BOOLEEN, "vrai", e, l, col);
        size_t fin1 = bloc_emettre_saut(c->b, I_SAUTER, l, col);
        bloc_corriger_saut(c->b, autre1, c->b->taille_code);
        bloc_corriger_saut(c->b, autre2, c->b->taille_code);
        emettre(c, I_LIRE_LOCAL, case_sujet, l, col);
        expression(c, e->enfants[0]);
        emettre(c, I_INFERIEUR_OU_EGAL, 0, l, col);
        size_t faux = bloc_emettre_saut(c->b, I_SAUTER_SI_FAUX, l, col);
        emettre(c, I_LIRE_LOCAL, case_sujet, l, col);
        expression(c, e->enfants[1]);
        emettre(c, I_SUPERIEUR_OU_EGAL, 0, l, col);
        size_t fin2 = bloc_emettre_saut(c->b, I_SAUTER, l, col);
        bloc_corriger_saut(c->b, faux, c->b->taille_code);
        constante(c, C_BOOLEEN, "faux", e, l, col);
        bloc_corriger_saut(c->b, fin1, c->b->taille_code);
        bloc_corriger_saut(c->b, fin2, c->b->taille_code);
        return;
    }
    expression(c, e);   /* N_COMPARAISON dont le sujet est un N_SUJET */
}

static void phrase(Compilation *c, const Noeud *ph);

static void selon(Compilation *c, const Noeud *ph) {
    int cs = ph->entier;
    expression(c, ph->enfants[0]);
    emettre(c, I_ECRIRE_LOCAL, cs, ph->ligne, ph->colonne);
    size_t *vers_fin = NULL, nb_fin = 0;
    for (size_t k = 1; k < ph->nb_enfants && !c->echec; k++) {
        const Noeud *cas = ph->enfants[k];
        if (cas->type != N_CAS) continue;   /* remarques */
        const Noeud *corps = cas->enfants[cas->nb_enfants - 1];
        if (cas->forme == 1) {               /* Autrement */
            phrase(c, corps);
            continue;
        }
        size_t *vers_corps = NULL, nb_corps = 0;
        size_t vers_suivant = 0;
        size_t nb_cond = cas->nb_enfants - 1;
        for (size_t q = 0; q < nb_cond; q++) {
            condition_de_cas(c, cas->enfants[q], cs);
            size_t faux = bloc_emettre_saut(c->b, I_SAUTER_SI_FAUX, cas->ligne, cas->colonne);
            if (q + 1 < nb_cond) {
                ajouter_saut(&vers_corps, &nb_corps, bloc_emettre_saut(c->b, I_SAUTER, cas->ligne, cas->colonne));
                bloc_corriger_saut(c->b, faux, c->b->taille_code);
            } else {
                vers_suivant = faux;
            }
        }
        for (size_t q = 0; q < nb_corps; q++) bloc_corriger_saut(c->b, vers_corps[q], c->b->taille_code);
        free(vers_corps);
        phrase(c, corps);
        ajouter_saut(&vers_fin, &nb_fin, bloc_emettre_saut(c->b, I_SAUTER, cas->ligne, cas->colonne));
        bloc_corriger_saut(c->b, vers_suivant, c->b->taille_code);
    }
    for (size_t q = 0; q < nb_fin; q++) bloc_corriger_saut(c->b, vers_fin[q], c->b->taille_code);
    free(vers_fin);
}

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
    case P_EFFACER:   /* « Effacer l'écran. » (§ 4.3) */
        emettre(c, I_EFFACER, 0, ph->ligne, ph->colonne);
        return;
    case P_STYLE:   /* « Les nombres s'affichent à la française. » (§ 4.1) */
        emettre(c, I_STYLE, ph->entier, ph->ligne, ph->colonne);
        return;
    case P_AFFICHAGE:
        if (ph->nb_enfants > 0xFFFF) { trop_grand(c, ph); return; }
        for (size_t e = 0; e < ph->nb_enfants; e++) expression(c, ph->enfants[e]);
        emettre(c, ph->forme ? I_AFFICHER_SANS_LIGNE : I_AFFICHER, (long)ph->nb_enfants, ph->ligne, ph->colonne);
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
    case P_TANT_QUE: {
        /* test ; si faux → sortie ; corps ; → test */
        size_t test = c->b->taille_code;
        const Noeud *cond = ph->enfants[0];
        expression(c, cond);
        size_t sortie = bloc_emettre_saut(c->b, I_SAUTER_SI_FAUX, cond->ligne, cond->colonne);
        entrer_boucle(c, test, 1);
        phrase(c, ph->enfants[1]);
        size_t retour = bloc_emettre_saut(c->b, I_SAUTER, ph->ligne, ph->colonne);
        bloc_corriger_saut(c->b, retour, test);
        bloc_corriger_saut(c->b, sortie, c->b->taille_code);
        sortir_boucle(c);
        return;
    }
    case P_REPETER: {
        /* reste ← n (entier ≥ 0) ; test : reste > 0 ? ; reste ← reste − 1 ; corps ; → test */
        int r = ph->entier, l = ph->ligne, col = ph->colonne;
        expression(c, ph->enfants[0]);
        emettre(c, I_EXIGER_ENTIER_NATUREL, 0, ph->enfants[0]->ligne, ph->enfants[0]->colonne);
        emettre(c, I_ECRIRE_LOCAL, r, l, col);
        size_t test = c->b->taille_code;
        emettre(c, I_LIRE_LOCAL, r, l, col);
        constante(c, C_NOMBRE, "0", ph, l, col);
        emettre(c, I_SUPERIEUR, 0, l, col);
        size_t sortie = bloc_emettre_saut(c->b, I_SAUTER_SI_FAUX, l, col);
        emettre(c, I_LIRE_LOCAL, r, l, col);
        constante(c, C_NOMBRE, "1", ph, l, col);
        emettre(c, I_SOUSTRACTION, 0, l, col);
        emettre(c, I_ECRIRE_LOCAL, r, l, col);
        entrer_boucle(c, test, 1);
        phrase(c, ph->enfants[1]);
        size_t retour = bloc_emettre_saut(c->b, I_SAUTER, l, col);
        bloc_corriger_saut(c->b, retour, test);
        bloc_corriger_saut(c->b, sortie, c->b->taille_code);
        sortir_boucle(c);
        return;
    }
    case P_POUR_CHAQUE: {
        int i = ph->local, f = ph->entier, p = ph->entier + 1, l = ph->ligne, col = ph->colonne;
        const Noeud *corps = ph->enfants[ph->nb_enfants - 1];
        expression(c, ph->enfants[0]);
        emettre(c, I_ECRIRE_LOCAL, i, l, col);
        expression(c, ph->enfants[1]);
        emettre(c, I_ECRIRE_LOCAL, f, l, col);
        if (ph->forme & 1) {
            expression(c, ph->enfants[2]);
        } else {
            /* sens automatique : +1 si début ≤ fin, sinon −1 */
            emettre(c, I_LIRE_LOCAL, i, l, col);
            emettre(c, I_LIRE_LOCAL, f, l, col);
            emettre(c, I_INFERIEUR_OU_EGAL, 0, l, col);
            size_t desc = bloc_emettre_saut(c->b, I_SAUTER_SI_FAUX, l, col);
            constante(c, C_NOMBRE, "1", ph, l, col);
            size_t apres = bloc_emettre_saut(c->b, I_SAUTER, l, col);
            bloc_corriger_saut(c->b, desc, c->b->taille_code);
            constante(c, C_NOMBRE, "-1", ph, l, col);
            bloc_corriger_saut(c->b, apres, c->b->taille_code);
        }
        emettre(c, I_ECRIRE_LOCAL, p, l, col);
        /* un pas nul ne finirait jamais */
        emettre(c, I_LIRE_LOCAL, p, l, col);
        constante(c, C_NOMBRE, "0", ph, l, col);
        emettre(c, I_EGAL, 0, l, col);
        size_t non_nul = bloc_emettre_saut(c->b, I_SAUTER_SI_FAUX, l, col);
        echouer_si(c, "Pas nul : la boucle ne finirait jamais.", ph);
        bloc_corriger_saut(c->b, non_nul, c->b->taille_code);
        /* test : compteur ≤ fin (pas positif) ou compteur ≥ fin (pas négatif) */
        size_t test = c->b->taille_code;
        emettre(c, I_LIRE_LOCAL, p, l, col);
        constante(c, C_NOMBRE, "0", ph, l, col);
        emettre(c, I_SUPERIEUR, 0, l, col);
        size_t descendant = bloc_emettre_saut(c->b, I_SAUTER_SI_FAUX, l, col);
        emettre(c, I_LIRE_LOCAL, i, l, col);
        emettre(c, I_LIRE_LOCAL, f, l, col);
        emettre(c, I_INFERIEUR_OU_EGAL, 0, l, col);
        size_t verdict = bloc_emettre_saut(c->b, I_SAUTER, l, col);
        bloc_corriger_saut(c->b, descendant, c->b->taille_code);
        emettre(c, I_LIRE_LOCAL, i, l, col);
        emettre(c, I_LIRE_LOCAL, f, l, col);
        emettre(c, I_SUPERIEUR_OU_EGAL, 0, l, col);
        bloc_corriger_saut(c->b, verdict, c->b->taille_code);
        size_t sortie = bloc_emettre_saut(c->b, I_SAUTER_SI_FAUX, l, col);
        entrer_boucle(c, 0, 0);
        phrase(c, corps);
        cible_suivant(c, c->b->taille_code);
        emettre(c, I_LIRE_LOCAL, i, l, col);
        emettre(c, I_LIRE_LOCAL, p, l, col);
        emettre(c, I_ADDITION, 0, l, col);
        emettre(c, I_ECRIRE_LOCAL, i, l, col);
        size_t retour = bloc_emettre_saut(c->b, I_SAUTER, l, col);
        bloc_corriger_saut(c->b, retour, test);
        bloc_corriger_saut(c->b, sortie, c->b->taille_code);
        sortir_boucle(c);
        return;
    }
    case P_SORTIR:
    case P_PASSER: {
        Boucle *b = &c->boucles[c->nb_boucles - 1];
        size_t saut = bloc_emettre_saut(c->b, I_SAUTER, ph->ligne, ph->colonne);
        if (ph->type == P_SORTIR) ajouter_saut(&b->sorties, &b->nb_sorties, saut);
        else if (b->suivant_connu) bloc_corriger_saut(c->b, saut, b->suivant);
        else ajouter_saut(&b->suivants, &b->nb_suivants, saut);
        return;
    }
    case P_SELON:
        selon(c, ph);
        return;
    case P_CLASSE:
    case P_APTITUDE: {
        ClasseModule *cm = module_ajouter_classe(c->module, ph->texte, ph->type == P_APTITUDE || (ph->forme & 3) == 2);
        cm->aptitude = ph->type == P_APTITUDE;
        cm->conserve = ph->type == P_CLASSE && (ph->forme & 32);
        if (ph->type == P_CLASSE && ph->texte3) cm->pluriel = grym_dupliquer(ph->texte3);
        if (ph->type == P_CLASSE && ph->texte2) cm->parent = grym_dupliquer(ph->texte2);
        for (size_t q = 0; q < ph->nb_enfants; q++) {
            const Noeud *ch = ph->enfants[q];
            if (ch->type == N_TEXTE) classe_ajouter_aptitude(cm, ch->texte);
            else {
                classe_ajouter_champ(cm, ch->texte);
                classe_typer_dernier_champ(cm, ch->texte2, ch->op == 'U');
                if (ch->entier & 1) classe_facultatif_dernier_champ(cm);
                if (ch->entier & 2) classe_cascade_dernier_champ(cm);
                if (ch->forme == 3) classe_multiple_dernier_champ(cm);   /* champ multiple (§ 16.13) */
                if (ch->nb_enfants) {   /* valeur de départ, sous forme canonique */
                    const Noeud *v = ch->enfants[0];
                    char *t = v->type == N_NEGATION ? grym_formater("-%s", v->enfants[0]->texte) : grym_dupliquer(v->texte);
                    classe_depart_dernier_champ(cm, t);
                    free(t);
                }
            }
        }
        return;
    }
    case P_POUR_CONSERVE: {
        /* liste figée au début de la boucle, parcourue par rang (§ 16.4) */
        int v = ph->local, liste = ph->entier, rang = ph->entier + 1, l = ph->ligne, col = ph->colonne;
        chercher(c, ph->enfants[0]);
        emettre(c, I_ECRIRE_LOCAL, liste, l, col);
        constante(c, C_NOMBRE, "0", ph, l, col);
        emettre(c, I_ECRIRE_LOCAL, rang, l, col);
        size_t test = c->b->taille_code;
        emettre(c, I_LIRE_LOCAL, rang, l, col);
        emettre(c, I_LIRE_LOCAL, liste, l, col);
        emettre(c, I_TAILLE_LISTE, 0, l, col);
        emettre(c, I_INFERIEUR, 0, l, col);
        size_t sortie = bloc_emettre_saut(c->b, I_SAUTER_SI_FAUX, l, col);
        emettre(c, I_LIRE_LOCAL, liste, l, col);
        emettre(c, I_LIRE_LOCAL, rang, l, col);
        emettre(c, I_ELEMENT, 0, l, col);
        emettre(c, I_ECRIRE_LOCAL, v, l, col);
        entrer_boucle(c, 0, 0);
        phrase(c, ph->enfants[1]);
        cible_suivant(c, c->b->taille_code);
        emettre(c, I_LIRE_LOCAL, rang, l, col);
        constante(c, C_NOMBRE, "1", ph, l, col);
        emettre(c, I_ADDITION, 0, l, col);
        emettre(c, I_ECRIRE_LOCAL, rang, l, col);
        size_t retour = bloc_emettre_saut(c->b, I_SAUTER, l, col);
        bloc_corriger_saut(c->b, retour, test);
        bloc_corriger_saut(c->b, sortie, c->b->taille_code);
        sortir_boucle(c);
        return;
    }
    case P_CONSERVER:
    case P_SUPPRIMER:
        expression(c, ph->enfants[0]);
        emettre(c, ph->type == P_CONSERVER ? I_CONSERVER : ph->entier == 2 ? I_RETABLIR
                   : ph->entier == 1 ? I_SUPPRIMER_DEFINITIVEMENT : I_SUPPRIMER, 0, ph->ligne, ph->colonne);
        return;
    case P_ENREGISTRER:
        expression(c, ph->enfants[0]);
        expression(c, ph->enfants[1]);
        emettre(c, I_ENREGISTRER, 0, ph->ligne, ph->colonne);
        return;
    case P_GAGNER: {   /* « Les genres de o gagnent baroque. » (§ 16.13) */
        expression(c, ph->enfants[0]);
        expression(c, ph->enfants[1]);
        long ch = bloc_nom(c->b, ph->texte);
        if (ch < 0) { trop_grand(c, ph); return; }
        emettre(c, ph->forme ? I_PERDRE : I_GAGNER, ch, ph->op_ligne, ph->op_colonne);
        return;
    }
    case P_MODIF_CHAMP: {
        expression(c, ph->enfants[0]);
        expression(c, ph->enfants[1]);
        long ch = bloc_nom(c->b, ph->texte);
        if (ch < 0) { trop_grand(c, ph); return; }
        emettre(c, I_ECRIRE_CHAMP, ch, ph->ligne, ph->colonne);
        return;
    }
    case P_CALCUL:
    case P_ACTION: {
        /* Une formule se compile dans son propre bloc ; le programme n'en garde aucune trace. */
        Bloc *prec = c->b;
        Bloc *f = bloc_creer();
        f->nom = grym_dupliquer(ph->texte);
        f->classe = ph->texte2 ? grym_dupliquer(ph->texte2) : NULL;
        f->sorte = ph->type == P_CALCUL ? B_CALCUL : B_ACTION;
        f->nb_parametres = (int)ph->enfants[0]->nb_enfants;
        f->nb_locaux = ph->entier;
        module_ajouter(c->module, f);
        c->b = f;
        Boucle *boucles = c->boucles;
        size_t nb_boucles = c->nb_boucles;
        c->boucles = NULL;
        c->nb_boucles = 0;
        const Noeud *corps = ph->enfants[1];
        if (ph->type == P_CALCUL && ph->forme == 0) {
            expression(c, corps);
            emettre(c, I_RENDRE, 0, corps->ligne, corps->colonne);
        } else {
            phrase(c, corps);
            if (ph->type == P_ACTION) emettre(c, I_RETOUR, 0, ph->ligne, 0);
        }
        free(c->boucles);
        c->boucles = boucles;
        c->nb_boucles = nb_boucles;
        c->b = prec;
        return;
    }
    default:   /* P_REMARQUE : rien à exécuter */
        return;
    }
}

Module *compiler(const Programme *p, Diagnostic *diag) {
    Module *m = module_creer();
    Compilation c = { bloc_creer(), m, diag, 0, NULL, 0 };
    c.b->nb_locaux = p->nb_locaux;
    module_ajouter(m, c.b);
    diag->message = NULL;
    diag->ligne = diag->colonne = 0;
    phrases(&c, p->phrases, p->nb);
    free(c.boucles);
    if (c.echec) {
        module_detruire(m);
        return NULL;
    }
    int ligne = p->nb ? p->phrases[p->nb - 1]->ligne : 0;
    bloc_emettre(m->blocs[0], I_RETOUR, 0, ligne, 0);
    return m;
}

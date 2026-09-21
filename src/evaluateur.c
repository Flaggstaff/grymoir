/* GrymoiR : évaluateur de la v0.1 (parcours direct de l'arbre). */
#include "evaluateur.h"
#include "decimal.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    char *nom;
    Decimal v;
} Valeur;

struct Environnement {
    Valeur *v;
    size_t n, cap;
};

Environnement *env_creer(void) {
    Environnement *e = grym_allouer(sizeof *e);
    e->v = NULL;
    e->n = e->cap = 0;
    return e;
}

Environnement *env_cloner(const Environnement *e) {
    Environnement *c = env_creer();
    c->n = c->cap = e->n;
    c->v = e->n ? grym_allouer(e->n * sizeof *c->v) : NULL;
    for (size_t i = 0; i < e->n; i++) {
        c->v[i].nom = grym_dupliquer(e->v[i].nom);
        c->v[i].v = dec_copier(&e->v[i].v);
    }
    return c;
}

void env_detruire(Environnement *e) {
    if (!e) return;
    for (size_t i = 0; i < e->n; i++) {
        free(e->v[i].nom);
        dec_liberer(&e->v[i].v);
    }
    free(e->v);
    free(e);
}

static Valeur *env_chercher(Environnement *e, const char *nom) {
    for (size_t i = 0; i < e->n; i++)
        if (strcmp(e->v[i].nom, nom) == 0) return &e->v[i];
    return NULL;
}

/* Prend possession de v. */
static void env_definir(Environnement *e, const char *nom, Decimal v) {
    Valeur *x = env_chercher(e, nom);
    if (x) {
        dec_liberer(&x->v);
        x->v = v;
        return;
    }
    if (e->n == e->cap) {
        size_t cap = e->cap ? e->cap * 2 : 16;
        Valeur *nv = grym_allouer(cap * sizeof *nv);
        if (e->n) memcpy(nv, e->v, e->n * sizeof *nv);
        free(e->v);
        e->v = nv;
        e->cap = cap;
    }
    e->v[e->n].nom = grym_dupliquer(nom);
    e->v[e->n].v = v;
    e->n++;
}

static int echouer(Diagnostic *d, int ligne, int colonne, char *message) {
    d->message = message;
    d->ligne = ligne;
    d->colonne = colonne;
    return 0;
}

static int evaluer(const Noeud *n, Environnement *e, Decimal *r, Diagnostic *d) {
    switch (n->type) {
    case N_NOMBRE:
        *r = dec_depuis_canonique(n->texte);
        return 1;
    case N_NOM: {
        Valeur *v = env_chercher(e, n->texte);
        if (!v) return echouer(d, n->ligne, n->colonne,
                               grym_formater("Erreur interne : « %s » n'a pas de valeur.", n->texte));
        *r = dec_copier(&v->v);
        return 1;
    }
    case N_GROUPE:
        return evaluer(n->enfants[0], e, r, d);
    case N_NEGATION: {
        Decimal x;
        if (!evaluer(n->enfants[0], e, &x, d)) return 0;
        *r = dec_negation(&x);
        dec_liberer(&x);
        return 1;
    }
    case N_OPERATION: {
        Decimal a, b;
        if (!evaluer(n->enfants[0], e, &a, d)) return 0;
        if (!evaluer(n->enfants[1], e, &b, d)) { dec_liberer(&a); return 0; }
        StatutDecimal st;
        switch (n->op) {
        case '+': st = dec_addition(&a, &b, r); break;
        case '-': st = dec_soustraction(&a, &b, r); break;
        case '*': st = dec_multiplication(&a, &b, r); break;
        case '/': st = dec_division(&a, &b, r); break;
        default:  st = dec_puissance(&a, &b, r); break;
        }
        dec_liberer(&a);
        dec_liberer(&b);
        switch (st) {
        case DEC_OK:
            return 1;
        case DEC_DIVISION_PAR_ZERO:
            dec_liberer(r);
            return echouer(d, n->op_ligne, n->op_colonne, grym_dupliquer("Division par zéro."));
        case DEC_TROP_GRAND:
            dec_liberer(r);
            return echouer(d, n->op_ligne, n->op_colonne, grym_formater(
                "Nombre trop grand : un résultat est limité à %d chiffres.", DEC_CHIFFRES_MAX));
        case DEC_EXPOSANT_NON_ENTIER:
            dec_liberer(r);
            return echouer(d, n->op_ligne, n->op_colonne, grym_dupliquer(
                "Exposant non entier : en v0.1, la puissance n'accepte qu'un exposant entier "
                "(2 ^ 3, 2 ^ −1)."));
        }
        return 0;
    }
    default:
        return echouer(d, n->ligne, n->colonne, grym_dupliquer("Erreur interne : expression inattendue."));
    }
}

int executer(const Programme *p, Environnement *env, Chaine *sortie, Diagnostic *diag) {
    diag->message = NULL;
    diag->ligne = diag->colonne = 0;
    for (size_t i = 0; i < p->nb; i++) {
        const Noeud *ph = p->phrases[i];
        switch (ph->type) {
        case P_REMARQUE:
            break;
        case P_CREATION:
        case P_MODIFICATION: {
            Decimal v;
            if (!evaluer(ph->enfants[0], env, &v, diag)) return 0;
            env_definir(env, ph->texte, v);
            break;
        }
        case P_AFFICHAGE: {
            /* Les éléments sont séparés par une espace. */
            Chaine ligne = {0};
            for (size_t k = 0; k < ph->nb_enfants; k++) {
                const Noeud *el = ph->enfants[k];
                if (k) chaine_ajouter(&ligne, " ");
                if (el->type == N_TEXTE) {
                    chaine_ajouter(&ligne, el->texte);
                    continue;
                }
                Decimal v;
                if (!evaluer(el, env, &v, diag)) { free(ligne.d); return 0; }
                char *s = dec_formater(&v);
                chaine_ajouter(&ligne, s);
                free(s);
                dec_liberer(&v);
            }
            char *l = chaine_rendre(&ligne);
            chaine_ajouter(sortie, l);
            chaine_ajouter(sortie, "\n");
            free(l);
            break;
        }
        case P_EXPRESSION: {
            Decimal v;
            if (!evaluer(ph->enfants[0], env, &v, diag)) return 0;
            char *s = dec_formater(&v);
            chaine_ajouter(sortie, s);
            chaine_ajouter(sortie, "\n");
            free(s);
            dec_liberer(&v);
            break;
        }
        default:
            return echouer(diag, ph->ligne, ph->colonne, grym_dupliquer("Erreur interne : phrase inattendue."));
        }
    }
    return 1;
}

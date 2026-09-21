/* GrymoiR : analyseur de la forme littéraire, v0.1
 * Spécification : docs/grammaire.md (révision 1.3), § 2 à 8.
 * Descente récursive écrite à la main, une fonction par règle de l'EBNF (§ 6).
 */
#include "analyseur.h"
#include "lexeur.h"
#include "texte.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define PROFONDEUR_MAX 500

/* ---------------------------------------------------------------- */
/* Portée : noms déclarés et leur genre (§ 2.3)                     */
/* ---------------------------------------------------------------- */

typedef enum { GENRE_LIBRE, GENRE_MASCULIN, GENRE_FEMININ } Genre;

typedef struct {
    char *nom;
    Genre genre;
    int ligne_decl;    /* ligne de la création */
    int ligne_genre;   /* ligne où le genre a été fixé */
} Symbole;

struct Portee {
    Symbole *s;
    size_t n, cap;
};

Portee *portee_creer(void) {
    Portee *p = grym_allouer(sizeof *p);
    p->s = NULL;
    p->n = p->cap = 0;
    return p;
}

static void portee_vider(Portee *p) {
    for (size_t i = 0; i < p->n; i++) free(p->s[i].nom);
    free(p->s);
    p->s = NULL;
    p->n = p->cap = 0;
}

void portee_detruire(Portee *p) {
    if (!p) return;
    portee_vider(p);
    free(p);
}

static void portee_copier(Portee *dst, const Portee *src);

Portee *portee_cloner(const Portee *p) {
    Portee *c = grym_allouer(sizeof *c);
    portee_copier(c, p);
    return c;
}

static void portee_copier(Portee *dst, const Portee *src) {
    dst->n = src->n;
    dst->cap = src->n;
    dst->s = src->n ? grym_allouer(src->n * sizeof *dst->s) : NULL;
    for (size_t i = 0; i < src->n; i++) {
        dst->s[i] = src->s[i];
        dst->s[i].nom = grym_dupliquer(src->s[i].nom);
    }
}

static Symbole *portee_chercher(Portee *p, const char *nom) {
    for (size_t i = 0; i < p->n; i++)
        if (strcmp(p->s[i].nom, nom) == 0) return &p->s[i];
    return NULL;
}

static void portee_declarer(Portee *p, const char *nom, Genre g, int ligne) {
    if (p->n == p->cap) {
        size_t cap = p->cap ? p->cap * 2 : 16;
        Symbole *s = grym_allouer(cap * sizeof *s);
        if (p->n) memcpy(s, p->s, p->n * sizeof *s);
        free(p->s);
        p->s = s;
        p->cap = cap;
    }
    Symbole *s = &p->s[p->n++];
    s->nom = grym_dupliquer(nom);
    s->genre = g;
    s->ligne_decl = ligne;
    s->ligne_genre = ligne;
}

/* Fin d'un bloc : les noms créés dans le bloc disparaissent (grammaire, § 5). */
static void portee_tronquer(Portee *p, size_t n) {
    while (p->n > n) free(p->s[--p->n].nom);
}

/* ---------------------------------------------------------------- */
/* État de l'analyse                                                */
/* ---------------------------------------------------------------- */

typedef struct {
    Jeton *j;
    size_t n;          /* le dernier jeton est toujours J_FIN */
    size_t i;
    Portee *portee;    /* copie de travail, validée seulement en cas de succès */
    int interactif;
    int profondeur;
    Article article_force;   /* article contenu dans « au » ou « du » (grammaire, § 5.2) */
    const Jeton *jeton_force;
    Diagnostic *diag;
    int echec;
    /* Suites attendues à la position la plus avancée atteinte (§ 8) */
    size_t att_pos;    /* index de jeton */
    unsigned att;      /* catégories A_… */
    char **att_mots;   /* mots qui prolongent un nom composé déclaré */
    size_t att_nb;
} Analyse;

enum {
    A_DEBUT      = 1u << 0,  /* Le, La, L', Afficher, Remarque */
    A_VALEUR     = 1u << 1,  /* nombre, nom déclaré, parenthèse, négation */
    A_NOM        = 1u << 2,  /* nom déclaré seul (après un article) */
    A_NOUVEAU    = 1u << 3,  /* nom nouveau (création) */
    A_TEXTE      = 1u << 4,
    A_OP_PUISS   = 1u << 5,
    A_OP_MUL     = 1u << 6,
    A_OP_ADD     = 1u << 7,
    A_PAR_FERM   = 1u << 8,
    A_VERBE      = 1u << 9,  /* vaut, devient */
    A_PUIS       = 1u << 10,
    A_POINT      = 1u << 11,
    A_COMPARAISON= 1u << 12, /* est, n'est pas, =, ≠, <, >, ≤, ≥ */
    A_LOGIQUE    = 1u << 13, /* et, ou */
    A_SUITE_SI   = 1u << 14, /* « , » ou « : » après la condition */
    A_BOOLEEN    = 1u << 15  /* vrai, faux */
};
#define A_OPERATEUR (A_OP_PUISS | A_OP_MUL | A_OP_ADD)

static void attendre_en(Analyse *a, size_t pos, unsigned m) {
    if (pos > a->att_pos) {
        a->att_pos = pos;
        a->att = 0;
        for (size_t k = 0; k < a->att_nb; k++) free(a->att_mots[k]);
        a->att_nb = 0;
    }
    if (pos == a->att_pos) a->att |= m;
}

static void attendre(Analyse *a, unsigned m) { attendre_en(a, a->i, m); }

static void attendre_mot(Analyse *a, size_t pos, const char *mot, size_t longueur) {
    attendre_en(a, pos, 0);
    if (pos != a->att_pos) return;
    for (size_t k = 0; k < a->att_nb; k++)
        if (strlen(a->att_mots[k]) == longueur && strncmp(a->att_mots[k], mot, longueur) == 0) return;
    char **m = grym_allouer((a->att_nb + 1) * sizeof *m);
    if (a->att_nb) memcpy(m, a->att_mots, a->att_nb * sizeof *m);
    free(a->att_mots);
    a->att_mots = m;
    char *w = grym_allouer(longueur + 1);
    memcpy(w, mot, longueur);
    w[longueur] = '\0';
    a->att_mots[a->att_nb++] = w;
}

static Jeton *cour(Analyse *a) { return &a->j[a->i]; }

static Jeton *voir(Analyse *a, size_t k) {
    size_t p = a->i + k;
    return &a->j[p < a->n ? p : a->n - 1];
}

static void avancer(Analyse *a) {
    if (a->j[a->i].type != J_FIN) a->i++;
}

static size_t fin_jeton(const Jeton *t) { return t->debut + t->longueur; }

static int est_mot(const Jeton *t, const char *m) {
    return t->type == J_MOT && strcmp(t->valeur, m) == 0;
}

/* Mots qui structurent la phrase et ne peuvent pas entrer dans un nom
 * (sauf entre crochets, § 2.2). */
static const char *const RESERVES[] = {
    "vaut", "devient", "puis", "est", "et", "ou", "si", "sinon", "vrai", "faux"
};

static int est_mot_reserve(const char *m) {
    for (size_t k = 0; k < sizeof RESERVES / sizeof *RESERVES; k++)
        if (strcmp(m, RESERVES[k]) == 0) return 1;
    return 0;
}

static int est_reserve(const Jeton *t) {
    return t->type == J_MOT && est_mot_reserve(t->valeur);
}

/* Vrai si le nom contient un mot réservé : il s'écrit alors entre crochets. */
static int nom_a_crochets(const char *nom) {
    char mot[64];
    size_t k = 0;
    for (const char *p = nom;; p++) {
        if (*p == ' ' || *p == '\'' || *p == '\0') {
            mot[k] = '\0';
            if (est_mot_reserve(mot)) return 1;
            if (*p == '\'' && k == 1 && mot[0] == 'n' && strncmp(p + 1, "est", 3) == 0
                && (p[4] == ' ' || p[4] == '\0')) return 1;
            k = 0;
            if (!*p) return 0;
        } else if (k + 1 < sizeof mot) {
            mot[k++] = *p;
        }
    }
}

static Article article_de(const Jeton *t) {
    if (est_mot(t, "le")) return ART_LE;
    if (est_mot(t, "la")) return ART_LA;
    if (t->type == J_ELISION && strcmp(t->valeur, "l") == 0) return ART_L;
    return ART_AUCUN;
}

/* Le jeton d'index k peut-il faire partie d'un nom écrit sans crochets ?
 * « n' » suivi de « est » ouvre une négation, pas un nom. */
static int mot_de_nom(const Analyse *a, size_t k) {
    const Jeton *t = &a->j[k];
    if (t->type == J_MOT) return !est_reserve(t);
    if (t->type == J_ELISION)
        return !(strcmp(t->valeur, "n") == 0 && k + 1 < a->n && est_mot(&a->j[k + 1], "est"));
    return 0;
}

/* Début d'un nom : un mot, une élision ou un nom entre crochets. */
static int debut_de_nom(const Analyse *a, size_t k) {
    return a->j[k].type == J_CROCHETS || mot_de_nom(a, k);
}

static Genre genre_de(Article art) {
    return art == ART_LE ? GENRE_MASCULIN : art == ART_LA ? GENRE_FEMININ : GENRE_LIBRE;
}

/* « Le total », « La quantité », « L'addition » */
static char *ecrire_avec_article(Article art, const char *nom) {
    switch (art) {
    case ART_LA: return grym_formater("La %s", nom);
    case ART_L:  return grym_formater("L'%s", nom);
    default:     return grym_formater("Le %s", nom);
    }
}

/* Texte d'un jeton tel que l'utilisateur l'a écrit (à la casse près). */
static char *texte_jeton(const Jeton *t) {
    switch (t->type) {
    case J_MOT:       return grym_dupliquer(t->valeur);
    case J_ELISION:   return grym_formater("%s'", t->valeur);
    case J_NOMBRE: {
        char *s = grym_dupliquer(t->valeur);
        for (char *p = s; *p; p++) if (*p == '.') *p = ',';
        return s;
    }
    case J_TEXTE:     return grym_formater("« %s »", t->valeur);
    case J_PLUS:      return grym_dupliquer("+");
    case J_MOINS:     return grym_dupliquer("−");
    case J_FOIS:      return grym_dupliquer("×");
    case J_DIVISE:    return grym_dupliquer("÷");
    case J_PUISSANCE: return grym_dupliquer("^");
    case J_PAR_OUV:   return grym_dupliquer("(");
    case J_PAR_FERM:  return grym_dupliquer(")");
    case J_POINT:     return grym_dupliquer(".");
    case J_VIRGULE:   return grym_dupliquer(",");
    case J_DEUX_POINTS: return grym_dupliquer(":");
    case J_REMARQUE:  return grym_dupliquer("Remarque");
    case J_EGAL:      return grym_dupliquer("=");
    case J_DIFFERENT: return grym_dupliquer("≠");
    case J_INFERIEUR: return grym_dupliquer("<");
    case J_SUPERIEUR: return grym_dupliquer(">");
    case J_INF_EGAL:  return grym_dupliquer("≤");
    case J_SUP_EGAL:  return grym_dupliquer("≥");
    case J_CROCHETS:  return grym_formater("[%s]", t->valeur);
    default:          return grym_dupliquer("fin du texte");
    }
}

/* Enregistre la première erreur ; renvoie NULL pour faciliter la propagation. */
static void *erreur_a(Analyse *a, int ligne, int colonne, char *message) {
    if (a->echec) {
        free(message);
        return NULL;
    }
    a->echec = 1;
    a->diag->message = message;
    a->diag->ligne = ligne;
    a->diag->colonne = colonne;
    return NULL;
}

static void *erreur(Analyse *a, const Jeton *t, char *message) {
    return erreur_a(a, t->ligne, t->colonne, message);
}

static char *decrire_attendus(unsigned m, char **mots, size_t nb_mots);

/* « X » inattendu, attendu : … (liste calculée, § 8). */
static void *erreur_inattendu(Analyse *a, const Jeton *t) {
    if (t->type == J_REMARQUE)
        return erreur(a, t, grym_dupliquer(
            "Une remarque ne peut pas couper une phrase : terminez la phrase avant « Remarque : »."));
    char *x = t->type == J_TEXTE ? grym_formater("Texte « %s »", t->valeur)
                                 : grym_formater("« %s »", "");
    if (t->type != J_TEXTE) {
        char *brut = texte_jeton(t);
        free(x);
        x = grym_formater("« %s »", brut);
        free(brut);
    }
    char *m;
    if ((size_t)(t - a->j) == a->att_pos && (a->att || a->att_nb)) {
        char *att = decrire_attendus(a->att, a->att_mots, a->att_nb);
        m = grym_formater("%s inattendu, attendu : %s.", x, att);
        free(att);
    } else {
        m = grym_formater("%s inattendu.", x);
    }
    free(x);
    return erreur(a, t, m);
}

/* Clé d'un nom composé : mots séparés par une espace, sauf après une élision. */
static char *cle(Analyse *a, size_t d, size_t f) {
    Chaine c = {0};
    for (size_t k = d; k < f; k++) {
        if (k > d && a->j[k - 1].type != J_ELISION) chaine_ajouter(&c, " ");
        chaine_ajouter(&c, a->j[k].valeur);
        if (a->j[k].type == J_ELISION) chaine_ajouter(&c, "'");
    }
    return chaine_rendre(&c);
}

/* Mots qui prolongent `debut` vers un nom composé déclaré plus long :
 * après « prix », propose « unitaire » si « prix unitaire » existe. */
static void attendre_suites_nom(Analyse *a, size_t pos, const char *debut) {
    size_t ld = strlen(debut);
    for (size_t i = 0; i < a->portee->n; i++) {
        const char *nom = a->portee->s[i].nom;
        if (strncmp(nom, debut, ld) != 0) continue;
        const char *r = nom + ld;
        if (ld > 0 && debut[ld - 1] != '\'') {
            if (*r != ' ') continue;
            r++;
        }
        if (!*r) continue;
        size_t k = 0;
        while (r[k] && r[k] != ' ' && r[k] != '\'') k++;
        if (r[k] == '\'') k++;
        attendre_mot(a, pos, r, k);
    }
}

/* « un nombre, un nom ou une parenthèse » : forme française d'un ensemble attendu. */
static char *decrire_attendus(unsigned m, char **mots, size_t nb_mots) {
    const char *at[32];
    int n = 0;
    if (m & A_DEBUT)              at[n++] = "le début d'une phrase (Le, La, L', Afficher, Si)";
    if (m & A_VALEUR)             at[n++] = "un nombre";
    if (m & (A_VALEUR | A_NOM))   at[n++] = "un nom";
    if (m & A_NOUVEAU && !(m & (A_VALEUR | A_NOM))) at[n++] = "un nom";
    if (m & A_VALEUR)             at[n++] = "une parenthèse";
    if (m & A_TEXTE)              at[n++] = "un texte";
    if (m & A_BOOLEEN)          { at[n++] = "« vrai »"; at[n++] = "« faux »"; }
    if (m & A_OPERATEUR)          at[n++] = "un opérateur";
    if (m & A_COMPARAISON)        at[n++] = "une comparaison (est, =, <…)";
    if (m & A_LOGIQUE)          { at[n++] = "« et »"; at[n++] = "« ou »"; }
    if (m & A_PAR_FERM)           at[n++] = "« ) »";
    if (m & A_VERBE)            { at[n++] = "« vaut »"; at[n++] = "« devient »"; }
    if (m & A_SUITE_SI)         { at[n++] = "« , »"; at[n++] = "« : »"; }
    if (m & A_PUIS)               at[n++] = "« puis »";
    char *cites[16];
    size_t nc = 0;
    for (size_t k = 0; k < nb_mots && nc < 16 && n < 31; k++) {
        cites[nc] = grym_formater("« %s »", mots[k]);
        at[n++] = cites[nc++];
    }
    if (m & A_POINT)              at[n++] = "un point final";
    Chaine c = {0};
    for (int k = 0; k < n; k++) {
        if (k > 0) chaine_ajouter(&c, k == n - 1 ? " ou " : ", ");
        chaine_ajouter(&c, at[k]);
    }
    for (size_t k = 0; k < nc; k++) free(cites[k]);
    return chaine_rendre(&c);
}

/* Nom déclaré le plus proche, ou NULL (charte, art. 8). */
static const char *suggerer(Analyse *a, const char *nom) {
    const char *meilleur = NULL;
    size_t meilleure = (size_t)-1;
    size_t ln = longueur_utf8(nom);
    for (size_t i = 0; i < a->portee->n; i++) {
        const char *candidat = a->portee->s[i].nom;
        size_t d = distance_edition(nom, candidat);
        size_t lc = longueur_utf8(candidat);
        size_t lmax = ln > lc ? ln : lc;
        if (d > 0 && d <= 2 && d * 3 <= lmax && d < meilleure) {
            meilleure = d;
            meilleur = candidat;
        }
    }
    return meilleur;
}

static void *erreur_inconnu(Analyse *a, const Jeton *t, const char *nom) {
    const char *s = suggerer(a, nom);
    if (s) return erreur(a, t, grym_formater("« %s » inconnu, vouliez-vous « %s » ?", nom, s));
    return erreur(a, t, grym_formater("« %s » inconnu.", nom));
}

/* Accord de l'article avec le genre du nom (§ 2.3). */
static int verifier_genre(Analyse *a, Symbole *s, Article art, const Jeton *t) {
    Genre g = genre_de(art);
    if (g == GENRE_LIBRE) return 1;
    if (s->genre == GENRE_LIBRE) {
        s->genre = g;
        s->ligne_genre = t->ligne;
        return 1;
    }
    if (s->genre != g) {
        erreur(a, t, grym_formater("« %s » est %s (déclaré ligne %d).", s->nom,
                                   s->genre == GENRE_MASCULIN ? "masculin" : "féminin",
                                   s->ligne_genre));
        return 0;
    }
    return 1;
}

/* ---------------------------------------------------------------- */
/* Expressions (§ 3.1, § 6)                                         */
/* ---------------------------------------------------------------- */

static Noeud *expression(Analyse *a);

static Noeud *feuille(TypeNoeud type, const Jeton *t) {
    Noeud *n = noeud_creer(type, t->ligne, t->colonne, t->debut);
    n->fin = fin_jeton(t);
    if (t->valeur) n->texte = grym_dupliquer(t->valeur);
    return n;
}

static Noeud *operation(char op, const Jeton *top, Noeud *g, Noeud *d) {
    Noeud *n = noeud_creer(N_OPERATION, g->ligne, g->colonne, g->debut);
    n->op = op;
    n->op_ligne = top->ligne;
    n->op_colonne = top->colonne;
    noeud_ajouter(n, g);
    noeud_ajouter(n, d);
    n->fin = d->fin;
    return n;
}

/* [ article ] nom, résolu par plus longue correspondance (§ 2.2).
 * L'article peut venir d'une contraction : « au » (à le), « du » (de le). */
static Noeud *nom_expression(Analyse *a) {
    const Jeton *tart = NULL;
    Article art = ART_AUCUN;
    if (a->article_force != ART_AUCUN) {
        art = a->article_force;
        tart = a->jeton_force;
        a->article_force = ART_AUCUN;
        if (article_de(cour(a)) != ART_AUCUN) {
            char *x = texte_jeton(tart), *y = texte_jeton(cour(a));
            char *m = grym_formater("« %s %s » : l'article est déjà contenu dans « %s ».", x, y, x);
            free(x);
            free(y);
            return erreur(a, cour(a), m);
        }
    } else if (article_de(cour(a)) != ART_AUCUN) {
        art = article_de(cour(a));
        attendre_en(a, a->i + 1, A_NOM);
        if (!debut_de_nom(a, a->i + 1)) {
            char *x = texte_jeton(cour(a));
            char *m = grym_formater("Nom attendu après « %s ».", x);
            free(x);
            return erreur(a, cour(a), m);
        }
        tart = cour(a);
        avancer(a);
    }

    Symbole *s = NULL;
    size_t d = a->i, fin;
    if (a->j[d].type == J_CROCHETS) {
        /* Nom entre crochets : correspondance exacte. */
        s = portee_chercher(a->portee, a->j[d].valeur);
        if (!s) {
            char *x = texte_jeton(&a->j[d]);
            const char *sug = suggerer(a, a->j[d].valeur);
            if (sug) erreur(a, &a->j[d], grym_formater("« %s » inconnu, vouliez-vous « %s » ?", x, sug));
            else     erreur(a, &a->j[d], grym_formater("« %s » inconnu.", x));
            free(x);
            return NULL;
        }
        fin = d + 1;
    } else {
        size_t f = d;
        while (f < a->n && mot_de_nom(a, f)) f++;
        fin = d;
        for (size_t k = f; k > d; k--) {
            char *c = cle(a, d, k);
            s = portee_chercher(a->portee, c);
            free(c);
            if (s) { fin = k; break; }
        }
        if (!s || fin < f) {
            char *tout = cle(a, d, f);
            if (a->j[f].type == J_FIN) attendre_suites_nom(a, f, tout); /* nom en cours de frappe */
            erreur_inconnu(a, &a->j[d], tout);
            free(tout);
            return NULL;
        }
    }
    if (tart && !verifier_genre(a, s, art, tart)) return NULL;

    const Jeton *premier = tart ? tart : &a->j[d];
    Noeud *n = noeud_creer(N_NOM, premier->ligne, premier->colonne, premier->debut);
    n->texte = grym_dupliquer(s->nom);
    n->article = art;
    n->crochets = a->j[d].type == J_CROCHETS;
    n->fin = fin_jeton(&a->j[fin - 1]);
    a->i = fin;
    if (!n->crochets) attendre_suites_nom(a, a->i, s->nom);
    return n;
}

static Noeud *unaire(Analyse *a);

static Noeud *base(Analyse *a) {
    Jeton *t = cour(a);
    if (a->article_force != ART_AUCUN && !debut_de_nom(a, a->i) && article_de(t) == ART_AUCUN) {
        char *x = texte_jeton(a->jeton_force);
        a->article_force = ART_AUCUN;
        char *m = grym_formater("Nom attendu après « %s ».", x);
        free(x);
        return erreur(a, t, m);
    }
    switch (t->type) {
    case J_NOMBRE: {
        Noeud *n = feuille(N_NOMBRE, t);
        avancer(a);
        return n;
    }
    case J_PAR_OUV: {
        if (++a->profondeur > PROFONDEUR_MAX)
            return erreur(a, t, grym_dupliquer("Expression trop imbriquée."));
        avancer(a);
        Noeud *e = expression(a);
        a->profondeur--;
        if (!e) return NULL;
        attendre(a, A_PAR_FERM);
        if (cour(a)->type != J_PAR_FERM) {
            noeud_liberer(e);
            return erreur(a, cour(a), grym_formater(
                "Parenthèse fermante manquante : la parenthèse ouverte ligne %d, "
                "colonne %d n'est pas refermée.", t->ligne, t->colonne));
        }
        Noeud *g = noeud_creer(N_GROUPE, t->ligne, t->colonne, t->debut);
        noeud_ajouter(g, e);
        g->fin = fin_jeton(cour(a));
        avancer(a);
        return g;
    }
    case J_TEXTE:
        return erreur(a, t, grym_dupliquer(
            "Un texte ne peut apparaître que dans une phrase Afficher."));
    case J_MOT:
    case J_ELISION:
        if (!mot_de_nom(a, a->i) && article_de(t) == ART_AUCUN) return erreur_inattendu(a, t);
        return nom_expression(a);
    case J_CROCHETS:
        return nom_expression(a);
    case J_POINT:
    case J_FIN:
        return erreur(a, t, grym_dupliquer(
            "Expression incomplète : il manque un nombre, un nom ou une parenthèse."));
    default:
        return erreur_inattendu(a, t);
    }
}

/* puissance = base [ "^" unaire ]  (associative à droite, § 3.1) */
static Noeud *puissance(Analyse *a) {
    Noeud *g = base(a);
    if (!g) return NULL;
    attendre(a, A_OP_PUISS);
    if (cour(a)->type != J_PUISSANCE) return g;
    if (++a->profondeur > PROFONDEUR_MAX) {
        noeud_liberer(g);
        return erreur(a, cour(a), grym_dupliquer("Expression trop imbriquée."));
    }
    Jeton *top = cour(a);
    avancer(a);
    Noeud *d = unaire(a);
    a->profondeur--;
    if (!d) { noeud_liberer(g); return NULL; }
    return operation('^', top, g, d);
}

/* unaire = "−" unaire | puissance   (−2 ^ 2 vaut −4) */
static Noeud *unaire(Analyse *a) {
    Jeton *t = cour(a);
    attendre(a, A_VALEUR);
    if (t->type != J_MOINS) return puissance(a);
    if (++a->profondeur > PROFONDEUR_MAX)
        return erreur(a, t, grym_dupliquer("Expression trop imbriquée."));
    avancer(a);
    Noeud *x = unaire(a);
    a->profondeur--;
    if (!x) return NULL;
    Noeud *n = noeud_creer(N_NEGATION, t->ligne, t->colonne, t->debut);
    noeud_ajouter(n, x);
    n->fin = x->fin;
    return n;
}

/* terme = unaire { ( "×" | "÷" ) unaire } */
static Noeud *terme(Analyse *a) {
    Noeud *g = unaire(a);
    while (g) {
        attendre(a, A_OP_MUL);
        if (cour(a)->type != J_FOIS && cour(a)->type != J_DIVISE) break;
        char op = cour(a)->type == J_FOIS ? '*' : '/';
        Jeton *top = cour(a);
        avancer(a);
        Noeud *d = unaire(a);
        if (!d) { noeud_liberer(g); return NULL; }
        g = operation(op, top, g, d);
    }
    return g;
}

/* expression = terme { ( "+" | "−" ) terme } */
static Noeud *expression(Analyse *a) {
    Noeud *g = terme(a);
    while (g) {
        attendre(a, A_OP_ADD);
        if (cour(a)->type != J_PLUS && cour(a)->type != J_MOINS) break;
        char op = cour(a)->type == J_PLUS ? '+' : '-';
        Jeton *top = cour(a);
        avancer(a);
        Noeud *d = terme(a);
        if (!d) { noeud_liberer(g); return NULL; }
        g = operation(op, top, g, d);
    }
    return g;
}

/* ---------------------------------------------------------------- */
/* Phrases (§ 2, § 4, § 5)                                          */
/* ---------------------------------------------------------------- */

/* ---------------------------------------------------------------- */
/* Conditions (§ 5)                                                 */
/* ---------------------------------------------------------------- */

static Noeud *valeur(Analyse *a);

/* Une expression purement arithmétique n'est jamais vraie ni fausse. */
static int est_arithmetique(const Noeud *n) {
    switch (n->type) {
    case N_NOMBRE: case N_OPERATION: case N_NEGATION: case N_TEXTE:
        return 1;
    case N_GROUPE:
        return est_arithmetique(n->enfants[0]);
    default:
        return 0;
    }
}

/* Un nom peut contenir un booléen : il reste acceptable comme condition. */
static int peut_etre_condition(const Noeud *n) {
    return !est_arithmetique(n);
}

typedef struct {
    const char *m, *f;   /* forme masculine, forme féminine */
    char op;
    char complement;     /* 0 : aucun ; 'a' : à ; 'd' : de ; 'c' : à, ou « ou égal à » */
} Relation;

static const Relation RELATIONS[] = {
    { "égal",      "égale",      '=', 'a' },
    { "différent", "différente", '!', 'd' },
    { "inférieur", "inférieure", '<', 'c' },
    { "supérieur", "supérieure", '>', 'c' },
    { "positif",   "positive",   'P', 0 },
    { "négatif",   "négative",   'N', 0 },
    { "nul",       "nulle",      '0', 0 },
    { "vrai",      "vraie",      'V', 0 },
    { "faux",      "fausse",     'F', 0 },
};
#define NB_RELATIONS (sizeof RELATIONS / sizeof *RELATIONS)

/* Suites possibles après « est » : les tournures, accordées au genre du sujet. */
static void attendre_relations(Analyse *a, Genre g) {
    for (size_t k = 0; k < NB_RELATIONS; k++) {
        const char *mot = g == GENRE_FEMININ ? RELATIONS[k].f : RELATIONS[k].m;
        char *t;
        switch (RELATIONS[k].complement) {
        case 'a': t = grym_formater("%s à", mot); break;
        case 'd': t = grym_formater("%s de", mot); break;
        case 'c':
            t = grym_formater("%s à", mot);
            attendre_mot(a, a->i, t, strlen(t));
            free(t);
            t = grym_formater("%s ou %s à", mot, g == GENRE_FEMININ ? "égale" : "égal");
            break;
        default:  t = grym_dupliquer(mot); break;
        }
        attendre_mot(a, a->i, t, strlen(t));
        free(t);
    }
}

/* Accord de l'adjectif avec un nom de genre connu ; un genre libre se fixe ici. */
static int accorder(Analyse *a, Symbole *s, Genre g, const Jeton *t, const char *forme_juste) {
    if (!s) return 1;
    if (s->genre == GENRE_LIBRE) {
        s->genre = g;
        s->ligne_genre = t->ligne;
        return 1;
    }
    if (s->genre != g) {
        erreur(a, t, grym_formater("« %s » est %s (déclaré ligne %d) : écrivez « %s ».", s->nom,
                                   s->genre == GENRE_MASCULIN ? "masculin" : "féminin",
                                   s->ligne_genre, forme_juste));
        return 0;
    }
    return 1;
}

/* « à » ou « au » (à le) ; « de », « d' » ou « du » (de le). */
static int complement(Analyse *a, char quoi) {
    const char *simple = quoi == 'a' ? "à" : "de";
    const char *contracte = quoi == 'a' ? "au" : "du";
    attendre_mot(a, a->i, simple, strlen(simple));
    attendre_mot(a, a->i, contracte, strlen(contracte));
    Jeton *t = cour(a);
    if (est_mot(t, simple) || (quoi == 'd' && t->type == J_ELISION && strcmp(t->valeur, "d") == 0)) {
        avancer(a);
        if (est_mot(cour(a), "le")) {
            erreur(a, t, grym_formater("« %s le » s'écrit « %s ».", simple, contracte));
            return 0;
        }
        return 1;
    }
    if (est_mot(t, contracte)) {
        avancer(a);
        a->article_force = ART_LE;
        a->jeton_force = t;
        return 1;
    }
    erreur_inattendu(a, t);
    return 0;
}

/* Ce qui suit « est » ou « n'est pas » (§ 5.1). */
static Noeud *relation(Analyse *a, Noeud *sujet, int negation, const Jeton *test) {
    Symbole *s = sujet->type == N_NOM ? portee_chercher(a->portee, sujet->texte) : NULL;
    attendre_relations(a, s ? s->genre : GENRE_LIBRE);
    Jeton *t = cour(a);
    const Relation *r = NULL;
    Genre g = GENRE_MASCULIN;
    for (size_t k = 0; k < NB_RELATIONS && !r; k++) {
        if (est_mot(t, RELATIONS[k].m)) { r = &RELATIONS[k]; g = GENRE_MASCULIN; }
        else if (est_mot(t, RELATIONS[k].f)) { r = &RELATIONS[k]; g = GENRE_FEMININ; }
    }
    if (!r) { noeud_liberer(sujet); return erreur_inattendu(a, t); }

    char op = r->op, quoi = r->complement;
    const Jeton *adjectif = t;
    avancer(a);
    int ou_egal = 0;
    if (quoi == 'c') {
        quoi = 'a';
        attendre_mot(a, a->i, "ou", 2);
        if (est_mot(cour(a), "ou")) {
            avancer(a);
            Jeton *te = cour(a);
            Genre ge = est_mot(te, "égale") ? GENRE_FEMININ : GENRE_MASCULIN;
            const char *egal = g == GENRE_FEMININ ? "égale" : "égal";
            attendre_mot(a, a->i, egal, strlen(egal));
            if (!est_mot(te, "égal") && !est_mot(te, "égale")) {
                noeud_liberer(sujet);
                return erreur_inattendu(a, te);
            }
            if (ge != g) {
                noeud_liberer(sujet);
                return erreur(a, te, grym_formater("Accord : écrivez « %s ou %s ».",
                                                   g == GENRE_FEMININ ? r->f : r->m,
                                                   g == GENRE_FEMININ ? "égale" : "égal"));
            }
            avancer(a);
            ou_egal = 1;
            op = op == '<' ? 'l' : 'g';
        }
    }
    char *juste = ou_egal ? grym_formater("%s ou %s", g == GENRE_FEMININ ? r->m : r->f,
                                          g == GENRE_FEMININ ? "égal" : "égale")
                          : grym_dupliquer(g == GENRE_FEMININ ? r->m : r->f);
    int ok = accorder(a, s, g, adjectif, juste);
    free(juste);
    if (!ok) { noeud_liberer(sujet); return NULL; }

    Noeud *droite = NULL;
    if (quoi) {
        if (!complement(a, quoi)) { noeud_liberer(sujet); return NULL; }
        droite = expression(a);
        a->article_force = ART_AUCUN;
        if (!droite) { noeud_liberer(sujet); return NULL; }
    }
    Noeud *n = noeud_creer(N_COMPARAISON, sujet->ligne, sujet->colonne, sujet->debut);
    n->op = op;
    n->negation = negation;
    n->op_ligne = test->ligne;
    n->op_colonne = test->colonne;
    noeud_ajouter(n, sujet);
    if (droite) noeud_ajouter(n, droite);
    n->fin = droite ? droite->fin : fin_jeton(&a->j[a->i - 1]);
    return n;
}

/* comparaison = expression [ comparateur expression | "est" relation | "n'est pas" relation ] */
static Noeud *comparaison(Analyse *a) {
    Noeud *x = expression(a);
    if (!x) return NULL;
    attendre(a, A_COMPARAISON);
    Jeton *t = cour(a);
    char op = 0;
    switch (t->type) {
    case J_EGAL:      op = '='; break;
    case J_DIFFERENT: op = '!'; break;
    case J_INFERIEUR: op = '<'; break;
    case J_SUPERIEUR: op = '>'; break;
    case J_INF_EGAL:  op = 'l'; break;
    case J_SUP_EGAL:  op = 'g'; break;
    default: break;
    }
    if (op) {
        avancer(a);
        Noeud *y = expression(a);
        if (!y) { noeud_liberer(x); return NULL; }
        Noeud *n = noeud_creer(N_COMPARAISON, x->ligne, x->colonne, x->debut);
        n->op = op;
        n->forme = 1;
        n->op_ligne = t->ligne;
        n->op_colonne = t->colonne;
        noeud_ajouter(n, x);
        noeud_ajouter(n, y);
        n->fin = y->fin;
        return n;
    }
    if (est_mot(t, "est")) {
        avancer(a);
        return relation(a, x, 0, t);
    }
    if (t->type == J_ELISION && strcmp(t->valeur, "n") == 0 && est_mot(voir(a, 1), "est")) {
        avancer(a);
        avancer(a);
        attendre_mot(a, a->i, "pas", 3);
        if (!est_mot(cour(a), "pas")) {
            noeud_liberer(x);
            return erreur(a, cour(a), grym_dupliquer("« n'est » doit être suivi de « pas »."));
        }
        avancer(a);
        return relation(a, x, 1, t);
    }
    return x;
}

/* Une parenthèse ouvre-t-elle un groupe logique plutôt qu'un calcul ?
 * Oui si elle contient, à son premier niveau, une comparaison, « et » ou « ou ». */
static int groupe_logique(Analyse *a) {
    int profondeur = 0;
    for (size_t k = a->i; k < a->n; k++) {
        const Jeton *t = &a->j[k];
        if (t->type == J_PAR_OUV) profondeur++;
        else if (t->type == J_PAR_FERM) { if (--profondeur == 0) return 0; }
        else if (t->type == J_POINT || t->type == J_FIN || t->type == J_VIRGULE
                 || t->type == J_DEUX_POINTS) return 0;
        else if (profondeur == 1 &&
                 (est_mot(t, "est") || est_mot(t, "et") || est_mot(t, "ou") || est_mot(t, "vrai")
                  || est_mot(t, "faux") || (t->type >= J_EGAL && t->type <= J_SUP_EGAL)
                  || (t->type == J_ELISION && strcmp(t->valeur, "n") == 0)))
            return 1;
    }
    return 0;
}

static Noeud *element_logique(Analyse *a) {
    Jeton *t = cour(a);
    attendre(a, A_BOOLEEN);
    if (est_mot(t, "vrai") || est_mot(t, "faux")) {
        Noeud *n = feuille(N_BOOLEEN, t);
        avancer(a);
        return n;
    }
    if (t->type == J_PAR_OUV && groupe_logique(a)) {
        if (++a->profondeur > PROFONDEUR_MAX)
            return erreur(a, t, grym_dupliquer("Expression trop imbriquée."));
        avancer(a);
        Noeud *v = valeur(a);
        a->profondeur--;
        if (!v) return NULL;
        attendre(a, A_PAR_FERM);
        if (cour(a)->type != J_PAR_FERM) {
            noeud_liberer(v);
            return erreur(a, cour(a), grym_formater(
                "Parenthèse fermante manquante : la parenthèse ouverte ligne %d, "
                "colonne %d n'est pas refermée.", t->ligne, t->colonne));
        }
        Noeud *g = noeud_creer(N_GROUPE, t->ligne, t->colonne, t->debut);
        noeud_ajouter(g, v);
        g->fin = fin_jeton(cour(a));
        avancer(a);
        return g;
    }
    return comparaison(a);
}

/* valeur = élément { ("et" | "ou") élément }, sans mélange de « et » et « ou » (§ 5.3). */
static Noeud *valeur(Analyse *a) {
    Noeud *g = element_logique(a);
    char mode = 0;
    while (g) {
        if (peut_etre_condition(g)) attendre(a, A_LOGIQUE);
        Jeton *t = cour(a);
        char op = est_mot(t, "et") ? 'e' : est_mot(t, "ou") ? 'o' : 0;
        if (!op) break;
        if (mode && op != mode) {
            noeud_liberer(g);
            return erreur(a, t, grym_dupliquer(
                "« et » et « ou » mélangés sans parenthèses : écrivez « (A et B) ou C » "
                "ou « A et (B ou C) » selon le sens voulu."));
        }
        mode = op;
        if (!peut_etre_condition(g)) {
            noeud_liberer(g);
            return erreur(a, t, grym_formater(
                "« %s » relie deux conditions : ce qui précède n'est ni vrai ni faux.",
                op == 'e' ? "et" : "ou"));
        }
        avancer(a);
        Noeud *d = element_logique(a);
        if (!d) { noeud_liberer(g); return NULL; }
        if (!peut_etre_condition(d)) {
            noeud_liberer(g);
            noeud_liberer(d);
            return erreur(a, t, grym_formater(
                "« %s » relie deux conditions : ce qui suit n'est ni vrai ni faux.",
                op == 'e' ? "et" : "ou"));
        }
        Noeud *n = noeud_creer(N_LOGIQUE, g->ligne, g->colonne, g->debut);
        n->op = op;
        n->op_ligne = t->ligne;
        n->op_colonne = t->colonne;
        noeud_ajouter(n, g);
        noeud_ajouter(n, d);
        n->fin = d->fin;
        g = n;
    }
    return g;
}

/* ---------------------------------------------------------------- */
/* Phrases (§ 2, § 4, § 5)                                          */
/* ---------------------------------------------------------------- */

static int fin_phrase(Analyse *a, int avec_puis) {
    Jeton *t = cour(a);
    attendre(a, A_POINT | (avec_puis ? A_PUIS : 0));
    if (t->type == J_POINT) {
        avancer(a);
        return 1;
    }
    Jeton *prec = a->i > 0 ? &a->j[a->i - 1] : t;
    if (t->type == J_FIN || t->ligne > prec->ligne) {
        if (t->type == J_FIN && a->interactif) return 1;
        erreur_a(a, prec->ligne, prec->colonne + (int)prec->longueur,
                 grym_formater("Point final manquant (ligne %d).", prec->ligne));
        return 0;
    }
    erreur_inattendu(a, t);
    return 0;
}

static Noeud *phrase_expression(Analyse *a) {
    Jeton *t = cour(a);
    Noeud *e = valeur(a);
    if (!e) return NULL;
    if (!fin_phrase(a, 0)) { noeud_liberer(e); return NULL; }
    Noeud *n = noeud_creer(P_EXPRESSION, t->ligne, t->colonne, t->debut);
    noeud_ajouter(n, e);
    n->fin = e->fin;
    return n;
}

/* affichage = "Afficher" élément { "puis" élément } "." */
static Noeud *affichage(Analyse *a) {
    Jeton *t = cour(a);
    avancer(a);
    Noeud *n = noeud_creer(P_AFFICHAGE, t->ligne, t->colonne, t->debut);
    for (;;) {
        Jeton *e = cour(a);
        attendre(a, A_VALEUR | A_TEXTE | A_BOOLEEN);
        if (e->type == J_POINT || e->type == J_FIN) {
            noeud_liberer(n);
            return erreur(a, e, grym_dupliquer(a->i > 0 && est_mot(&a->j[a->i - 1], "puis")
                ? "Élément manquant après « puis »."
                : "Rien à afficher : ajoutez un texte ou une expression après « Afficher »."));
        }
        Noeud *el;
        if (e->type == J_TEXTE) {
            el = feuille(N_TEXTE, e);
            avancer(a);
        } else {
            el = valeur(a);
            if (!el) { noeud_liberer(n); return NULL; }
        }
        noeud_ajouter(n, el);
        n->fin = el->fin;
        if (!est_mot(cour(a), "puis")) break;
        avancer(a);
    }
    if (!fin_phrase(a, 1)) { noeud_liberer(n); return NULL; }
    return n;
}

/* création     = article nom "vaut" valeur "."
 * modification = article nom "devient" valeur "." */
static Noeud *declaration(Analyse *a, size_t iverbe) {
    Jeton *tart = cour(a);
    Article art = article_de(tart);
    avancer(a);
    size_t d = a->i, f = iverbe;
    Jeton *verbe = &a->j[f];

    if (d == f) {
        char *x = texte_jeton(tart);
        char *m = grym_formater("Nom manquant entre « %s » et « %s ».", x, verbe->valeur);
        free(x);
        return erreur(a, verbe, m);
    }
    char *nom;
    int crochets = a->j[d].type == J_CROCHETS;
    if (crochets) {
        if (f != d + 1) {
            char *x = texte_jeton(&a->j[d + 1]);
            char *m = grym_formater("« %s » inattendu : un nom entre crochets s'écrit seul.", x);
            free(x);
            return erreur(a, &a->j[d + 1], m);
        }
        nom = grym_dupliquer(a->j[d].valeur);
        const char *p = nom;
        int article = strncmp(p, "le ", 3) == 0 || strncmp(p, "la ", 3) == 0 || strncmp(p, "l'", 2) == 0
                   || strcmp(p, "le") == 0 || strcmp(p, "la") == 0;
        if (article) {
            free(nom);
            return erreur(a, &a->j[d], grym_dupliquer("Un nom ne peut pas commencer par un article."));
        }
    } else {
        for (size_t k = d; k < f; k++) {
            Jeton *t = &a->j[k];
            if (est_reserve(t) || (t->type == J_ELISION && !mot_de_nom(a, k))) {
                char *nomx = cle(a, d, f);
                char *x = texte_jeton(t);
                char *m = grym_formater(
                    "« %s » est un mot réservé : pour l'utiliser dans un nom, écrivez [%s].", x, nomx);
                free(x);
                free(nomx);
                return erreur(a, t, m);
            }
            if (!mot_de_nom(a, k)) {
                char *x = texte_jeton(t);
                char *m = grym_formater("« %s » ne peut pas faire partie d'un nom.", x);
                free(x);
                return erreur(a, t, m);
            }
        }
        if (article_de(&a->j[d]) != ART_AUCUN || a->j[d].type == J_ELISION) {
            char *x = texte_jeton(&a->j[d]);
            char *m = grym_formater("Un nom ne peut pas commencer par « %s ».", x);
            free(x);
            return erreur(a, &a->j[d], m);
        }
        nom = cle(a, d, f);
    }

    int creation = est_mot(verbe, "vaut");
    Symbole *s = portee_chercher(a->portee, nom);
    char *ecrit_nom = crochets || nom_a_crochets(nom) ? grym_formater("[%s]", nom) : grym_dupliquer(nom);

    if (creation && s) {
        char *ecrit = ecrire_avec_article(art, ecrit_nom);
        erreur(a, &a->j[d], grym_formater(
            "« %s » existe déjà (ligne %d). Pour le modifier, écrivez : %s devient …",
            nom, s->ligne_decl, ecrit));
        free(ecrit);
        free(ecrit_nom);
        free(nom);
        return NULL;
    }
    if (!creation) {
        if (!s) {
            const char *sug = suggerer(a, nom);
            if (sug) {
                erreur(a, &a->j[d], grym_formater("« %s » inconnu, vouliez-vous « %s » ?", nom, sug));
            } else {
                char *ecrit = ecrire_avec_article(art, ecrit_nom);
                erreur(a, &a->j[d], grym_formater(
                    "« %s » n'existe pas. Pour le créer, écrivez : %s vaut …", nom, ecrit));
                free(ecrit);
            }
            free(ecrit_nom);
            free(nom);
            return NULL;
        }
        if (!verifier_genre(a, s, art, tart)) { free(ecrit_nom); free(nom); return NULL; }
    }
    free(ecrit_nom);

    a->i = f + 1; /* après le verbe */
    Noeud *e = valeur(a);
    if (!e) { free(nom); return NULL; }
    if (!fin_phrase(a, 0)) { noeud_liberer(e); free(nom); return NULL; }

    /* Le nom n'existe qu'après la phrase : « Le total vaut total + 1. » échoue. */
    if (creation) portee_declarer(a->portee, nom, genre_de(art), tart->ligne);

    Noeud *n = noeud_creer(creation ? P_CREATION : P_MODIFICATION,
                           tart->ligne, tart->colonne, tart->debut);
    n->texte = nom;
    n->article = art;
    n->crochets = crochets;
    noeud_ajouter(n, e);
    n->fin = e->fin;
    return n;
}

static Noeud *phrase(Analyse *a, int colonne);
static Noeud *bloc(Analyse *a, int colonne, int racine);

static int premier_de_ligne(const Analyse *a, size_t k) {
    return k == 0 || a->j[k - 1].ligne < a->j[k].ligne;
}

/* Forme courte : une seule phrase simple après la virgule. */
static Noeud *phrase_courte(Analyse *a, int colonne) {
    Jeton *t = cour(a);
    attendre(a, A_DEBUT);
    if (est_mot(t, "si") || est_mot(t, "sinon") || t->type == J_REMARQUE) {
        char *x = texte_jeton(t);
        char *m = grym_formater("« %s » inattendu : la forme courte n'accepte qu'une phrase simple "
                                "(Le, La, L', Afficher). Pour davantage, ouvrez un bloc avec « : ».", x);
        free(x);
        return erreur(a, t, m);
    }
    size_t sauve = a->portee->n;
    Noeud *p = phrase(a, colonne);
    portee_tronquer(a->portee, sauve);
    if (!p) return NULL;
    Noeud *b = noeud_creer(N_BLOC, p->ligne, p->colonne, p->debut);
    noeud_ajouter(b, p);
    b->fin = p->fin;
    return b;
}

/* Corps d'une branche : « , phrase » ou « : » suivi d'un bloc indenté. */
static Noeud *branche(Analyse *a, int colonne, const Jeton *mot, int *forme_bloc) {
    attendre(a, A_SUITE_SI);
    Jeton *t = cour(a);
    if (t->type == J_VIRGULE) {
        avancer(a);
        *forme_bloc = 0;
        return phrase_courte(a, colonne);
    }
    if (t->type != J_DEUX_POINTS) return erreur_inattendu(a, t);
    avancer(a);
    *forme_bloc = 1;
    Jeton *suivant = cour(a);
    if (suivant->type == J_FIN || suivant->ligne == t->ligne || suivant->colonne <= colonne
        || est_mot(suivant, "sinon")) {
        char *m = grym_formater("Bloc vide : après « : », écrivez les phrases du bloc sur les lignes "
                                "suivantes, plus indentées que « %s ».",
                                est_mot(mot, "sinon") ? "Sinon" : "Si");
        return erreur(a, suivant->type == J_FIN || suivant->ligne == t->ligne ? t : suivant, m);
    }
    return bloc(a, suivant->colonne, 0);
}

/* si = "Si" valeur branche [ "Sinon" ( "si" … | branche ) ] (§ 5.4) */
static Noeud *si(Analyse *a, int colonne, int sinon_si) {
    Jeton *t = cour(a);
    avancer(a);
    Noeud *cond = valeur(a);
    if (!cond) return NULL;
    if (!peut_etre_condition(cond)) {
        noeud_liberer(cond);
        return erreur(a, t, grym_dupliquer(
            "Condition attendue après « Si » : une comparaison, par exemple "
            "« Si le total est supérieur à 100 ». Un calcul seul n'est ni vrai ni faux."));
    }
    int forme_bloc = 0;
    Noeud *alors = branche(a, colonne, t, &forme_bloc);
    if (!alors) { noeud_liberer(cond); return NULL; }
    Noeud *n = noeud_creer(P_SI, t->ligne, t->colonne, t->debut);
    n->forme = (forme_bloc ? 1 : 0) | (sinon_si ? 2 : 0);
    noeud_ajouter(n, cond);
    noeud_ajouter(n, alors);
    n->fin = alors->fin;

    /* « Sinon » : sur la même ligne qu'une forme courte, ou aligné sur « Si ». */
    Jeton *s = cour(a);
    if (!est_mot(s, "sinon")) return n;
    if (premier_de_ligne(a, a->i) && s->colonne != colonne) {
        if (s->colonne < colonne) return n;   /* appartient à un bloc englobant */
        noeud_liberer(n);
        return erreur(a, s, grym_dupliquer("« Sinon » doit être aligné sur son « Si »."));
    }
    avancer(a);
    Noeud *autre;
    if (est_mot(cour(a), "si")) {
        Noeud *imbrique = si(a, colonne, 1);
        if (!imbrique) { noeud_liberer(n); return NULL; }
        autre = noeud_creer(N_BLOC, imbrique->ligne, imbrique->colonne, imbrique->debut);
        noeud_ajouter(autre, imbrique);
        autre->fin = imbrique->fin;
    } else {
        int f = 0;
        autre = branche(a, colonne, s, &f);
        if (!autre) { noeud_liberer(n); return NULL; }
    }
    noeud_ajouter(n, autre);
    n->fin = autre->fin;
    return n;
}

static Noeud *phrase(Analyse *a, int colonne) {
    Jeton *t = cour(a);
    attendre(a, A_DEBUT | (a->interactif ? A_VALEUR | A_BOOLEEN : 0));

    if (t->type == J_REMARQUE) {
        Noeud *n = feuille(P_REMARQUE, t);
        avancer(a);
        return n;
    }
    if (est_mot(t, "afficher")) return affichage(a);
    if (est_mot(t, "si")) return si(a, colonne, 0);
    if (est_mot(t, "sinon"))
        return erreur(a, t, grym_dupliquer("« Sinon » sans « Si » correspondant."));
    if (est_mot(t, "remarque") && voir(a, 1)->type == J_DEUX_POINTS)
        return erreur(a, t, grym_dupliquer(
            "Une remarque doit commencer une ligne : passez à la ligne avant « Remarque : »."));

    Article art = article_de(t);
    if (art != ART_AUCUN) {
        size_t k = a->i + 1;
        while (a->j[k].type != J_POINT && a->j[k].type != J_FIN && a->j[k].type != J_REMARQUE
               && !est_mot(&a->j[k], "vaut") && !est_mot(&a->j[k], "devient"))
            k++;
        if (est_mot(&a->j[k], "vaut") || est_mot(&a->j[k], "devient"))
            return declaration(a, k);

        /* Aide à la saisie (§ 8) : après l'article, un nom ; après le nom, le verbe. */
        size_t d = a->i + 1;
        int que_des_mots = 1;
        for (size_t q = d; q < k; q++) if (!mot_de_nom(a, q)) que_des_mots = 0;
        if (k == d) {
            attendre_en(a, k, A_NOM | A_NOUVEAU);
        } else if (k == d + 1 && a->j[d].type == J_CROCHETS) {
            attendre_en(a, k, A_VERBE);
        } else if (que_des_mots) {
            attendre_en(a, k, A_VERBE);
            char *debut = cle(a, d, k);
            attendre_suites_nom(a, k, debut);
            free(debut);
        }
        if (a->interactif) return phrase_expression(a);
        char *x = texte_jeton(t);
        char *m = grym_formater("Verbe manquant : une phrase qui commence par « %s » attend "
                                "« vaut » (créer) ou « devient » (modifier).", x);
        free(x);
        return erreur(a, t, m);
    }

    if (a->interactif) return phrase_expression(a);
    char *x = texte_jeton(t);
    char *m = grym_formater("« %s » ne peut pas commencer une phrase : "
                            "une phrase commence par Le, La, L', Afficher ou Si.", x);
    free(x);
    return erreur(a, t, m);
}

/* Suite de phrases alignées sur une même colonne (§ 5.4). À la racine, les noms
 * restent déclarés ; dans un bloc, ils disparaissent à la fin du bloc. */
static Noeud *bloc(Analyse *a, int colonne, int racine) {
    Jeton *t0 = cour(a);
    Noeud *b = noeud_creer(N_BLOC, t0->ligne, t0->colonne, t0->debut);
    size_t sauve = a->portee->n;
    for (;;) {
        attendre(a, A_DEBUT | (a->interactif ? A_VALEUR | A_BOOLEEN : 0));
        Jeton *t = cour(a);
        if (t->type == J_FIN) break;
        if (t->type != J_REMARQUE && premier_de_ligne(a, a->i)) {
            if (t->colonne < colonne) {
                if (!racine) break;
                noeud_liberer(b);
                return erreur(a, t, grym_dupliquer(
                    "Indentation incohérente : cette ligne est moins indentée que le début du programme."));
            }
            if (t->colonne > colonne) {
                noeud_liberer(b);
                return erreur(a, t, grym_dupliquer(
                    "Indentation inattendue : seul un bloc ouvert par « : » s'indente."));
            }
        }
        if (est_mot(t, "sinon") && !racine) break;
        Noeud *p = phrase(a, colonne);
        if (!p) { noeud_liberer(b); return NULL; }
        noeud_ajouter(b, p);
        b->fin = p->fin;
    }
    if (!racine) portee_tronquer(a->portee, sauve);
    return b;
}

/* ---------------------------------------------------------------- */
/* Point d'entrée                                                   */
/* ---------------------------------------------------------------- */

static void liberer_jetons(Jeton *j, size_t n) {
    for (size_t k = 0; k < n; k++) jeton_liberer(&j[k]);
    free(j);
}

/* Ce que l'analyse laisse derrière elle pour l'aide à la saisie. */
typedef struct {
    int valide;          /* vrai si les suites concernent la fin de la source */
    unsigned att;
    char **mots;
    size_t nb_mots;
    char **noms;         /* noms déclarés à la fin de la source */
    size_t nb_noms;
} Capture;

static int analyser_interne(const char *source, size_t taille, Portee *portee, int interactif,
                            Programme *programme, Diagnostic *diag, Capture *capture) {
    programme->phrases = NULL;
    programme->nb = 0;
    diag->message = NULL;
    diag->ligne = diag->colonne = 0;

    char *err = NULL;
    Lexeur *lx = lexeur_creer(source, taille, &err);
    if (!lx) {
        diag->message = err;
        return 0;
    }

    /* Lecture de tous les jetons ; une erreur de lecture arrête tout. */
    Jeton *j = NULL;
    size_t n = 0, cap = 0;
    for (;;) {
        Jeton t = lexeur_suivant(lx);
        if (t.type == J_ERREUR) {
            diag->message = t.valeur;
            diag->ligne = t.ligne;
            diag->colonne = t.colonne;
            lexeur_detruire(lx);
            liberer_jetons(j, n);
            return 0;
        }
        if (n == cap) {
            cap = cap ? cap * 2 : 64;
            Jeton *nj = grym_allouer(cap * sizeof *nj);
            if (n) memcpy(nj, j, n * sizeof *nj);
            free(j);
            j = nj;
        }
        j[n++] = t;
        if (t.type == J_FIN) break;
    }
    lexeur_detruire(lx);

    Portee copie;
    portee_copier(&copie, portee);
    Analyse a;
    a.j = j;
    a.n = n;
    a.i = 0;
    a.portee = &copie;
    a.interactif = interactif;
    a.profondeur = 0;
    a.diag = diag;
    a.echec = 0;
    a.att_pos = 0;
    a.att = 0;
    a.att_mots = NULL;
    a.att_nb = 0;
    a.article_force = ART_AUCUN;
    a.jeton_force = NULL;

    /* Colonne de référence : celle de la première phrase (les remarques ne comptent pas). */
    int colonne = 1;
    for (size_t k = 0; k < n; k++)
        if (j[k].type != J_REMARQUE) { colonne = j[k].type == J_FIN ? 1 : j[k].colonne; break; }
    Noeud *racine = bloc(&a, colonne, 1);
    if (racine) {
        programme->phrases = racine->enfants;
        programme->nb = racine->nb_enfants;
        racine->enfants = NULL;
        racine->nb_enfants = 0;
        noeud_liberer(racine);
    } else if (!a.echec) {
        erreur(&a, cour(&a), grym_dupliquer("Erreur interne de l'analyseur."));
    }

    if (capture) {
        capture->valide = a.att_pos == n - 1;
        capture->att = a.att;
        capture->mots = a.att_mots;
        capture->nb_mots = a.att_nb;
        capture->nb_noms = copie.n;
        capture->noms = copie.n ? grym_allouer(copie.n * sizeof *capture->noms) : NULL;
        for (size_t k = 0; k < copie.n; k++) capture->noms[k] = grym_dupliquer(copie.s[k].nom);
    } else {
        for (size_t k = 0; k < a.att_nb; k++) free(a.att_mots[k]);
        free(a.att_mots);
    }
    liberer_jetons(j, n);

    if (a.echec) {
        programme_liberer(programme);
        portee_vider(&copie);
        return 0;
    }
    portee_vider(portee);
    *portee = copie;
    return 1;
}

int analyser(const char *source, size_t taille, Portee *portee, int interactif,
             Programme *programme, Diagnostic *diag) {
    return analyser_interne(source, taille, portee, interactif, programme, diag, NULL);
}

/* ---------------------------------------------------------------- */
/* Aide à la saisie (§ 8, charte art. 9)                            */
/* ---------------------------------------------------------------- */

/* Lettre au sens du lexeur (Latin-1, œ, Ÿ) ou chiffre. */
static int car_de_mot(uint32_t c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
        || (c >= 0xC0 && c <= 0xFF && c != 0xD7 && c != 0xF7)
        || c == 0x152 || c == 0x153 || c == 0x178;
}

/* Début du mot en cours de frappe à la fin de la source (taille si aucun). */
static size_t debut_mot_partiel(const char *s, size_t taille) {
    size_t i = taille;
    while (i > 0) {
        size_t k = i - 1;
        while (k > 0 && ((unsigned char)s[k] & 0xC0) == 0x80) k--;
        const unsigned char *p = (const unsigned char *)s + k;
        uint32_t c;
        if (*p < 0x80) c = *p;
        else if ((*p & 0xE0) == 0xC0 && k + 1 < taille) c = ((uint32_t)(p[0] & 0x1F) << 6) | (p[1] & 0x3F);
        else break;
        if (!car_de_mot(c)) break;
        i = k;
    }
    if (i < taille && s[i] >= '0' && s[i] <= '9') return taille; /* un nombre, pas un mot */
    if (i > 0 && s[i - 1] == '[') i--;                           /* nom entre crochets commencé */
    return i;
}

/* Minuscules pour comparer : ASCII, Latin-1 (À → à), Œ → œ. */
static char *plier(const char *s, size_t n) {
    char *r = grym_allouer(n + 1);
    memcpy(r, s, n);
    r[n] = '\0';
    for (size_t k = 0; k < n; k++) {
        unsigned char c = (unsigned char)r[k];
        if (c >= 'A' && c <= 'Z') r[k] = (char)(c + 32);
        else if (c == 0xC3 && k + 1 < n) {
            unsigned char d = (unsigned char)r[k + 1];
            if (d >= 0x80 && d <= 0x9E && d != 0x97) r[k + 1] = (char)(d + 0x20);
            k++;
        } else if (c == 0xC5 && k + 1 < n && (unsigned char)r[k + 1] == 0x92) {
            r[k + 1] = (char)0x93;
            k++;
        }
    }
    return r;
}

static void proposer(Suggestions *r, const char *item, const char *prefixe, size_t lp, int generique) {
    if (lp) {
        if (generique || strlen(item) < lp) return;
        char *a = plier(item, lp), *b = plier(prefixe, lp);
        int ok = strcmp(a, b) == 0;
        free(a);
        free(b);
        if (!ok) return;
    }
    for (size_t k = 0; k < r->nb; k++)
        if (strcmp(r->items[k], item) == 0) return;
    char **it = grym_allouer((r->nb + 1) * sizeof *it);
    if (r->nb) memcpy(it, r->items, r->nb * sizeof *it);
    free(r->items);
    r->items = it;
    r->items[r->nb++] = grym_dupliquer(item);
}

Suggestions suites_valides(const char *source, size_t taille) {
    Suggestions r = { NULL, 0 };
    size_t d = debut_mot_partiel(source, taille);
    const char *pre = source + d;
    size_t lp = taille - d;

    Portee *portee = portee_creer();
    Programme prog;
    Diagnostic diag;
    Capture c = { 0, 0, NULL, 0, NULL, 0 };
    if (analyser_interne(source, d, portee, 0, &prog, &diag, &c)) programme_liberer(&prog);
    else diagnostic_liberer(&diag);
    portee_detruire(portee);

    if (c.valide) {
        unsigned m = c.att;
        if (m & A_DEBUT) {
            proposer(&r, "Le", pre, lp, 0);
            proposer(&r, "La", pre, lp, 0);
            proposer(&r, "L'", pre, lp, 0);
            proposer(&r, "Afficher", pre, lp, 0);
            proposer(&r, "Si", pre, lp, 0);
            proposer(&r, "Remarque :", pre, lp, 0);
        }
        if (m & (A_VALEUR | A_NOM))
            for (size_t k = 0; k < c.nb_noms; k++) {
                /* Un nom qui contient un mot réservé se propose entre crochets (§ 2.2). */
                if (nom_a_crochets(c.noms[k])) {
                    char *x = grym_formater("[%s]", c.noms[k]);
                    proposer(&r, x, pre, lp, 0);
                    free(x);
                } else {
                    proposer(&r, c.noms[k], pre, lp, 0);
                }
            }
        for (size_t k = 0; k < c.nb_mots; k++) proposer(&r, c.mots[k], pre, lp, 0);
        if (m & A_NOUVEAU) proposer(&r, "(nouveau nom)", pre, lp, 1);
        if (m & A_VALEUR) {
            proposer(&r, "(nombre)", pre, lp, 1);
            proposer(&r, "(", pre, lp, 1);
            proposer(&r, "−", pre, lp, 1);
        }
        if (m & A_BOOLEEN) { proposer(&r, "vrai", pre, lp, 0); proposer(&r, "faux", pre, lp, 0); }
        if (m & A_TEXTE) proposer(&r, "« … »", pre, lp, 1);
        if (m & A_OP_ADD) { proposer(&r, "+", pre, lp, 1); proposer(&r, "−", pre, lp, 1); }
        if (m & A_OP_MUL) { proposer(&r, "×", pre, lp, 1); proposer(&r, "÷", pre, lp, 1); }
        if (m & A_OP_PUISS) proposer(&r, "^", pre, lp, 1);
        if (m & A_COMPARAISON) {
            proposer(&r, "est", pre, lp, 0);
            proposer(&r, "n'est pas", pre, lp, 0);
            static const char *const SYMBOLES[] = { "=", "≠", "<", ">", "≤", "≥" };
            for (size_t k = 0; k < 6; k++) proposer(&r, SYMBOLES[k], pre, lp, 1);
        }
        if (m & A_LOGIQUE) { proposer(&r, "et", pre, lp, 0); proposer(&r, "ou", pre, lp, 0); }
        if (m & A_PAR_FERM) proposer(&r, ")", pre, lp, 1);
        if (m & A_SUITE_SI) { proposer(&r, ",", pre, lp, 1); proposer(&r, ":", pre, lp, 1); }
        if (m & A_VERBE) { proposer(&r, "vaut", pre, lp, 0); proposer(&r, "devient", pre, lp, 0); }
        if (m & A_PUIS) proposer(&r, "puis", pre, lp, 0);
        if (m & A_POINT) proposer(&r, ".", pre, lp, 1);
    }
    for (size_t k = 0; k < c.nb_mots; k++) free(c.mots[k]);
    free(c.mots);
    for (size_t k = 0; k < c.nb_noms; k++) free(c.noms[k]);
    free(c.noms);
    return r;
}

void suggestions_liberer(Suggestions *s) {
    for (size_t k = 0; k < s->nb; k++) free(s->items[k]);
    free(s->items);
    s->items = NULL;
    s->nb = 0;
}

void programme_liberer(Programme *p) {
    for (size_t i = 0; i < p->nb; i++) noeud_liberer(p->phrases[i]);
    free(p->phrases);
    p->phrases = NULL;
    p->nb = 0;
}

void diagnostic_liberer(Diagnostic *d) {
    free(d->message);
    d->message = NULL;
}

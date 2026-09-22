/* GrymoiR : analyseur de la forme littéraire, v0.1
 * Spécification : docs/grammaire.md (révision 1.22), § 2 à 13.
 * Descente récursive écrite à la main, une fonction par règle de l'EBNF (§ 6).
 */
#include "analyseur.h"
#include "compact.h"
#include "lexeur.h"
#include "texte.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PROFONDEUR_MAX 500

/* ---------------------------------------------------------------- */
/* Portée : noms déclarés et leur genre (§ 2.3)                     */
/* ---------------------------------------------------------------- */

typedef enum { GENRE_LIBRE, GENRE_MASCULIN, GENRE_FEMININ } Genre;

typedef enum { S_VARIABLE, S_CALCUL, S_ACTION } Sorte;

typedef struct {
    char *nom;
    Genre genre;
    int ligne_decl;    /* ligne de la création */
    int ligne_genre;   /* ligne où le genre a été fixé */
    Sorte sorte;
    int lecture_seule; /* compteur d'une boucle Pour chaque */
    int nb_parametres; /* calculs et actions */
    int local;         /* case locale dans une formule, −1 pour un nom global */
    char *classe;      /* méthode : classe de son premier paramètre (§ 13.6), sinon NULL */
} Symbole;

/* Classe déclarée (§ 11 de la charte, grammaire § 13) : son nom, son genre, ses champs. */
typedef struct {
    char *nom;         /* pour une aptitude : sa forme féminine (« horodatée ») */
    Genre genre;
    int aptitude;      /* 1 : aptitude (§ 13.7), pas une classe */
    char *masculin;    /* aptitude : forme masculine (« horodaté ») */
    char **aptitudes;  /* classe : aptitudes adoptées (formes féminines) */
    size_t nb_aptitudes;
    int ligne;         /* ligne de déclaration */
    char *parent;      /* classe dont elle hérite, ou NULL */
    int conserve;      /* entité (§ 16) */
    char *pluriel;     /* entité : pluriel irrégulier déclaré, ou NULL */
    char **champs;     /* champs propres (les champs hérités restent dans la classe parente) */
    Genre *genres;     /* genre de chaque champ */
    char **types;      /* type de chaque champ, ou NULL (classe ordinaire) */
    int *uniques;      /* champ unique */
    int *facultatifs;  /* champ facultatif (§ 16.9) */
    size_t nb;
} Classe;

struct Portee {
    Symbole *s;
    size_t n, cap;
    Classe *classes;
    size_t nb_classes;
};

Portee *portee_creer(void) {
    Portee *p = grym_allouer(sizeof *p);
    p->s = NULL;
    p->n = p->cap = 0;
    p->classes = NULL;
    p->nb_classes = 0;
    return p;
}

static void classe_champs_liberer(Classe *c) {
    for (size_t k = 0; k < c->nb; k++) {
        free(c->champs[k]);
        if (c->types) free(c->types[k]);
    }
    free(c->champs);
    free(c->genres);
    free(c->types);
    free(c->uniques);
    free(c->facultatifs);
    c->facultatifs = NULL;
    c->champs = NULL;
    c->genres = NULL;
    c->types = NULL;
    c->uniques = NULL;
    c->nb = 0;
}

static void classe_liberer(Classe *c) {
    free(c->nom);
    free(c->pluriel);
    free(c->masculin);
    for (size_t k = 0; k < c->nb_aptitudes; k++) free(c->aptitudes[k]);
    free(c->aptitudes);
    free(c->parent);
    classe_champs_liberer(c);
}

static void portee_vider(Portee *p) {
    for (size_t i = 0; i < p->n; i++) { free(p->s[i].nom); free(p->s[i].classe); }
    free(p->s);
    p->s = NULL;
    p->n = p->cap = 0;
    for (size_t i = 0; i < p->nb_classes; i++) classe_liberer(&p->classes[i]);
    free(p->classes);
    p->classes = NULL;
    p->nb_classes = 0;
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
        dst->s[i].classe = src->s[i].classe ? grym_dupliquer(src->s[i].classe) : NULL;
    }
    dst->nb_classes = src->nb_classes;
    dst->classes = src->nb_classes ? grym_allouer(src->nb_classes * sizeof *dst->classes) : NULL;
    for (size_t i = 0; i < src->nb_classes; i++) {
        const Classe *c = &src->classes[i];
        Classe *d = &dst->classes[i];
        d->nom = grym_dupliquer(c->nom);
        d->genre = c->genre;
        d->aptitude = c->aptitude;
        d->ligne = c->ligne;
        d->masculin = c->masculin ? grym_dupliquer(c->masculin) : NULL;
        d->nb_aptitudes = c->nb_aptitudes;
        d->aptitudes = c->nb_aptitudes ? grym_allouer(c->nb_aptitudes * sizeof *d->aptitudes) : NULL;
        for (size_t k = 0; k < c->nb_aptitudes; k++) d->aptitudes[k] = grym_dupliquer(c->aptitudes[k]);
        d->parent = c->parent ? grym_dupliquer(c->parent) : NULL;
        d->conserve = c->conserve;
        d->pluriel = c->pluriel ? grym_dupliquer(c->pluriel) : NULL;
        d->types = c->types ? grym_allouer((c->nb ? c->nb : 1) * sizeof *d->types) : NULL;
        d->uniques = grym_allouer((c->nb ? c->nb : 1) * sizeof *d->uniques);
        d->facultatifs = grym_allouer((c->nb ? c->nb : 1) * sizeof *d->facultatifs);
        for (size_t k = 0; k < c->nb; k++) {
            if (d->types) d->types[k] = c->types[k] ? grym_dupliquer(c->types[k]) : NULL;
            d->uniques[k] = c->uniques ? c->uniques[k] : 0;
            d->facultatifs[k] = c->facultatifs ? c->facultatifs[k] : 0;
        }
        d->nb = c->nb;
        d->champs = grym_allouer((c->nb ? c->nb : 1) * sizeof *d->champs);
        d->genres = grym_allouer((c->nb ? c->nb : 1) * sizeof *d->genres);
        for (size_t k = 0; k < c->nb; k++) {
            d->champs[k] = grym_dupliquer(c->champs[k]);
            d->genres[k] = c->genres[k];
        }
    }
}

static Classe *classe_de(Portee *p, const char *nom) {
    for (size_t i = 0; i < p->nb_classes; i++)
        if (!p->classes[i].aptitude && strcmp(p->classes[i].nom, nom) == 0) return &p->classes[i];
    return NULL;
}

/* Aptitude désignée par l'une de ses formes ; *feminin reçoit 1 si c'est la forme féminine. */
static Classe *aptitude_de(Portee *p, const char *forme, int *feminin) {
    for (size_t i = 0; i < p->nb_classes; i++) {
        Classe *c = &p->classes[i];
        if (!c->aptitude) continue;
        if (strcmp(c->nom, forme) == 0) { if (feminin) *feminin = 1; return c; }
        if (strcmp(c->masculin, forme) == 0) { if (feminin) *feminin = 0; return c; }
    }
    return NULL;
}

static int champ_propre(const Classe *c, const char *nom, Genre *g) {
    for (size_t k = 0; k < c->nb; k++)
        if (strcmp(c->champs[k], nom) == 0) {
            if (g) *g = c->genres[k];
            return 1;
        }
    return 0;
}

/* Champ de la classe, de ses aptitudes ou de sa lignée ; *g reçoit son genre, *origine qui le déclare. */
static int classe_champ(Portee *p, const Classe *c, const char *nom, Genre *g, const Classe **origine) {
    while (c) {
        if (champ_propre(c, nom, g)) { if (origine) *origine = c; return 1; }
        for (size_t k = 0; k < c->nb_aptitudes; k++) {
            const Classe *ap = aptitude_de(p, c->aptitudes[k], NULL);
            if (ap && champ_propre(ap, nom, g)) { if (origine) *origine = ap; return 1; }
        }
        c = c->parent ? classe_de(p, c->parent) : NULL;
    }
    return 0;
}

/* Champs de toute valeur fichier (§ 15.3) : ils ne réservent pas leur nom. */
static int champ_integre(const char *nom, Genre *g) {
    if (strcmp(nom, "taille") == 0) { if (g) *g = GENRE_FEMININ; return 1; }
    if (strcmp(nom, "format") == 0 || strcmp(nom, "nom de fichier") == 0) { if (g) *g = GENRE_MASCULIN; return 1; }
    return 0;
}

/* Champ déclaré dans au moins une classe ; *g reçoit son genre. */
static int champ_connu(Portee *p, const char *nom, Genre *g) {
    for (size_t i = 0; i < p->nb_classes; i++)
        for (size_t k = 0; k < p->classes[i].nb; k++)
            if (strcmp(p->classes[i].champs[k], nom) == 0) {
                if (g) *g = p->classes[i].genres[k];
                return 1;
            }
    return 0;
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
    s->sorte = S_VARIABLE;
    s->lecture_seule = 0;
    s->classe = NULL;
    s->nb_parametres = 0;
    s->local = -1;
}

/* Fin d'un bloc : les noms créés dans le bloc disparaissent (grammaire, § 5). */
static void portee_tronquer(Portee *p, size_t n) {
    while (p->n > n) {
        --p->n;
        free(p->s[p->n].nom);
        free(p->s[p->n].classe);
    }
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
    int formule;             /* 0 : hors formule ; 1 : dans un calcul ; 2 : dans une action (§ 9) */
    size_t barriere;         /* dans un calcul, les variables d'index inférieur sont invisibles */
    int nb_locaux;           /* cases locales allouées dans la formule en cours */
    int niveau;              /* 0 : premier niveau du programme ; > 0 : dans un bloc */
    char *a_completer;       /* classe déclarée par « est » à la phrase précédente, sans champs encore */
    const char *dont;        /* entité dont une condition « dont » examine les champs (§ 16.4), ou NULL */
    int corbeille;           /* la dernière tournure « … conservé » lue était « … supprimé » (§ 16.12) */
    int boucle;              /* boucles englobantes dans la formule ou le programme en cours (§ 10) */
    const char *arrets[3];   /* mots qui peuvent suivre un nom dans le contexte courant (« à », « fois »…) */
    int nb_arrets;
    char **noms_fin;         /* noms visibles au dernier passage à la fin de la source (§ 8) */
    int *sortes_fin;
    size_t nb_noms_fin;
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
    A_BOOLEEN    = 1u << 15, /* vrai, faux */
    A_BOUCLE     = 1u << 16  /* Sortir de la boucle, Passer au tour suivant */
};
#define A_OPERATEUR (A_OP_PUISS | A_OP_MUL | A_OP_ADD)

static Symbole *visible(Analyse *a, const char *nom);

static void liberer_noms_fin(Analyse *a) {
    for (size_t k = 0; k < a->nb_noms_fin; k++) free(a->noms_fin[k]);
    free(a->noms_fin);
    free(a->sortes_fin);
    a->noms_fin = NULL;
    a->sortes_fin = NULL;
    a->nb_noms_fin = 0;
}

/* À la fin de la source, les noms proposés sont ceux visibles à cet endroit :
 * dans un calcul, ses paramètres, pas les variables du programme. */
static void photographier_noms(Analyse *a) {
    liberer_noms_fin(a);
    size_t n = a->portee->n;
    a->noms_fin = grym_allouer((n ? n : 1) * sizeof *a->noms_fin);
    a->sortes_fin = grym_allouer((n ? n : 1) * sizeof *a->sortes_fin);
    for (size_t i = 0; i < n; i++) {
        Symbole *s = &a->portee->s[i];
        if (visible(a, s->nom) != s) continue;
        a->noms_fin[a->nb_noms_fin] = grym_dupliquer(s->nom);
        a->sortes_fin[a->nb_noms_fin++] = (int)s->sorte;
    }
}

static void attendre_en(Analyse *a, size_t pos, unsigned m) {
    if (pos == a->n - 1 && m) photographier_noms(a);
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

static int nom_a_crochets(const char *nom);

/* Mots qui structurent la phrase et ne peuvent pas entrer dans un nom
 * (sauf entre crochets, § 2.2). */
static const char *const RESERVES[] = {
    "vaut", "devient", "puis", "est", "et", "ou", "si", "sinon", "vrai", "faux", "rendre", "dont", "définitivement"
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
int nom_exige_crochets(const char *nom) {
    return nom_a_crochets(nom);
}

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
    if (t->type == J_ARTICLE_IMPLICITE) return ART_IMPLICITE;
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
    case J_CROCHETS:  return t->synthetique ? grym_dupliquer(t->valeur) : grym_formater("[%s]", t->valeur);
    case J_ARTICLE_IMPLICITE: return grym_dupliquer("");
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

static const char *const TYPES[] = { "texte", "nombre", "nombre entier", "vrai ou faux", "date", "fichier", "image" };

/* ---------------------------------------------------------------- */
/* Typage strict des entités, à l'analyse (§ 16.2)                  */
/* ---------------------------------------------------------------- */

/* Type d'un champ de la classe (ou de sa lignée, de ses aptitudes) ; NULL si le champ n'a pas de type. */
static const char *type_du_champ(Portee *p, const Classe *c, const char *champ) {
    const Classe *o = NULL;
    if (!classe_champ(p, c, champ, NULL, &o) || !o->types) return NULL;
    for (size_t k = 0; k < o->nb; k++) if (strcmp(o->champs[k], champ) == 0) return o->types[k];
    return NULL;
}

/* Type d'un champ, quelle que soit la classe de l'objet : seulement si toutes les déclarations
 * de ce nom de champ lui donnent le même type. */
static const char *type_commun(Portee *p, const char *champ) {
    const char *t = NULL;
    for (size_t i = 0; i < p->nb_classes; i++) {
        const Classe *c = &p->classes[i];
        for (size_t k = 0; k < c->nb; k++) {
            if (strcmp(c->champs[k], champ) != 0) continue;
            if (!c->types || !c->types[k]) return NULL;
            if (t && strcmp(t, c->types[k]) != 0) return NULL;
            t = c->types[k];
        }
    }
    return t;
}

/* Type d'une valeur connu à l'analyse, ou NULL. */
static const char *type_statique(const Noeud *n) {
    switch (n->type) {
    case N_NOMBRE: {
        const char *p = strchr(n->texte, '.');
        if (p) for (p++; *p; p++) if (*p != '0') return "nombre";
        return "nombre entier";
    }
    case N_TEXTE: return "texte";
    case N_BOOLEEN: case N_COMPARAISON: case N_LOGIQUE: return "vrai ou faux";
    case N_DATE: case N_AUJOURDHUI: return "date";
    case N_FICHIER: return "fichier";
    case N_NOUVEAU: return n->texte;
    case N_GROUPE: case N_NEGATION: return type_statique(n->enfants[0]);
    default: return NULL;
    }
}

static int type_de_base(const char *t) {
    for (size_t q = 0; q < sizeof TYPES / sizeof *TYPES; q++) if (strcmp(t, TYPES[q]) == 0) return 1;
    return 0;
}

static char *nommer_type(Portee *p, const char *t) {
    if (strcmp(t, "vrai ou faux") == 0) return grym_dupliquer("vrai ou faux");
    if (strcmp(t, "date") == 0 || strcmp(t, "image") == 0) return grym_formater("une %s", t);
    if (type_de_base(t)) return grym_formater("un %s", t);
    const Classe *c = classe_de(p, t);
    return grym_formater("%s %s", c && c->genre == GENRE_FEMININ ? "une" : "un", t);
}

/* « La date devient absente. » : « absent » s'accorde avec le champ qu'il remplit (§ 16.9). */
static int accorder_absent(Analyse *a, Noeud *v, Genre g) {
    if (v->type != N_ABSENT || g == GENRE_LIBRE) return 1;
    int forme = g == GENRE_FEMININ ? 2 : 1;
    if (!v->entier && v->forme != forme) {
        erreur_a(a, v->ligne, v->colonne, grym_formater("Accord : « %s ».", forme == 2 ? "absente" : "absent"));
        return 0;
    }
    v->forme = forme;
    return 1;
}

/* Vérifie qu'une valeur convient au type d'un champ ; sinon, erreur à la position de la valeur. */
static int verifier_type(Analyse *a, const char *champ, const char *attendu, const Noeud *v) {
    const char *vu = attendu ? type_statique(v) : NULL;
    if (!vu || strcmp(attendu, vu) == 0) return 1;
    if (strcmp(attendu, "nombre") == 0 && strcmp(vu, "nombre entier") == 0) return 1;
    if (strcmp(attendu, "image") == 0 && strcmp(vu, "fichier") == 0) return 1;   /* vérifié à l'exécution */
    if (!type_de_base(attendu) && !type_de_base(vu)) {
        for (const Classe *c = classe_de(a->portee, vu); c; c = c->parent ? classe_de(a->portee, c->parent) : NULL)
            if (strcmp(c->nom, attendu) == 0) return 1;
    }
    char *x = nommer_type(a->portee, attendu), *y = nommer_type(a->portee, vu);
    if (strcmp(attendu, "nombre entier") == 0 && strcmp(vu, "nombre") == 0) {
        free(y);
        y = grym_dupliquer("un nombre à virgule");
    } else if (strcmp(vu, "nombre entier") == 0) {
        free(y);
        y = grym_dupliquer("un nombre");
    }
    erreur_a(a, v->ligne, v->colonne, grym_formater("Le champ « %s » attend %s, pas %s.", champ, x, y));
    free(x);
    free(y);
    return 0;
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
        if (a->j[k].valeur) chaine_ajouter(&c, a->j[k].valeur);
        else {   /* signe de ponctuation égaré dans un nom en cours d'écriture : on le montre tel quel */
            char *x = texte_jeton(&a->j[k]);
            chaine_ajouter(&c, x);
            free(x);
        }
        if (a->j[k].type == J_ELISION) chaine_ajouter(&c, "'");
    }
    return chaine_rendre(&c);
}

/* Nom visible depuis la position courante : dans un calcul, les variables du
 * programme sont invisibles (calculs purs, § 9.4) ; formules et paramètres restent visibles.
 * La recherche part de la fin : un nom local masque un nom global homonyme. */
static Symbole *visible(Analyse *a, const char *nom) {
    for (size_t i = a->portee->n; i > 0; i--) {
        Symbole *s = &a->portee->s[i - 1];
        if (strcmp(s->nom, nom) != 0) continue;
        if (a->formule == 1 && i - 1 < a->barriere && s->sorte == S_VARIABLE) continue;
        return s;
    }
    return NULL;
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
    if (m & A_DEBUT)              at[n++] = "le début d'une phrase (Le, La, L', Afficher, Si, Pour)";
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

static Noeud *unaire(Analyse *a);

static char *pluriel(int n, const char *mot) {
    return grym_formater("%d %s%s", n, mot, n > 1 ? "s" : "");
}

/* Appel d'un calcul : « le carré de 7 », « la moyenne de 4 et de 6 », « le carré du prix » (§ 9.1).
 * Un argument se lie plus fort que les opérateurs : « le carré de 3 + 1 » vaut 10. */
static Noeud *appel_calcul(Analyse *a, Symbole *s, const Jeton *premier) {
    Noeud *n = noeud_creer(N_APPEL, premier->ligne, premier->colonne, premier->debut);
    n->texte = grym_dupliquer(s->nom);
    int nb = 0;
    for (;;) {
        attendre_mot(a, a->i, "de", 2);
        attendre_mot(a, a->i, "du", 2);
        Jeton *t = cour(a);
        if (est_mot(t, "de") || (t->type == J_ELISION && strcmp(t->valeur, "d") == 0)) {
            avancer(a);
            if (est_mot(cour(a), "le")) {
                noeud_liberer(n);
                return erreur(a, t, grym_dupliquer("« de le » s'écrit « du »."));
            }
        } else if (est_mot(t, "du")) {
            avancer(a);
            a->article_force = ART_LE;
            a->jeton_force = t;
        } else if (nb == 0) {
            noeud_liberer(n);
            return erreur(a, t, grym_formater(
                "« %s » est un calcul : donnez-lui sa valeur, par exemple « le %s de 7 ».", s->nom, s->nom));
        } else {
            break;
        }
        Noeud *arg = unaire(a);
        a->article_force = ART_AUCUN;
        if (!arg) { noeud_liberer(n); return NULL; }
        noeud_ajouter(n, arg);
        n->fin = arg->fin;
        nb++;
        attendre_mot(a, a->i, "et de", 5);
        Jeton *suite = voir(a, 1);
        if (est_mot(cour(a), "et") && (est_mot(suite, "de") || est_mot(suite, "du")
                                        || (suite->type == J_ELISION && strcmp(suite->valeur, "d") == 0))) {
            avancer(a);
            continue;
        }
        break;
    }
    if (nb != s->nb_parametres) {
        char *att = pluriel(s->nb_parametres, "paramètre");
        erreur(a, premier, grym_formater("« %s » attend %s, %d donné%s.", s->nom, att, nb, nb > 1 ? "s" : ""));
        free(att);
        noeud_liberer(n);
        return NULL;
    }
    return n;
}

static Noeud *base(Analyse *a);
static int bloc_initialisation(Analyse *a, Noeud *nv, const Jeton *tphrase);

/* « de », « d' » ou « du » à l'index k : introduit l'objet dont on lit un champ. */
static int complement_de(const Analyse *a, size_t k) {
    const Jeton *t = &a->j[k];
    return est_mot(t, "de") || est_mot(t, "du") || (t->type == J_ELISION && strcmp(t->valeur, "d") == 0);
}

/* « le solde du client » : champ, puis l'objet après « de » / « du » (grammaire, § 13.3). */
static Noeud *acces_champ(Analyse *a, char *champ, Genre g, const Jeton *tart, Article art,
                          const Jeton *premier, size_t k) {
    if (tart && !tart->synthetique && (art == ART_LE || art == ART_LA) && g != GENRE_LIBRE && genre_de(art) != g) {
        erreur(a, tart, grym_formater("« %s » est un champ %s.", champ, g == GENRE_MASCULIN ? "masculin" : "féminin"));
        free(champ);
        return NULL;
    }
    a->i = k;
    Jeton *t = cour(a);
    avancer(a);
    if (est_mot(t, "du")) {
        a->article_force = ART_LE;
        a->jeton_force = t;
    } else if (est_mot(cour(a), "le")) {
        erreur(a, t, grym_dupliquer("« de le » s'écrit « du »."));
        free(champ);
        return NULL;
    }
    Noeud *objet = base(a);
    a->article_force = ART_AUCUN;
    if (!objet) { free(champ); return NULL; }
    Noeud *n = noeud_creer(N_CHAMP, premier->ligne, premier->colonne, premier->debut);
    n->texte = champ;
    n->article = art == ART_IMPLICITE ? ART_AUCUN : art;
    n->op_ligne = premier->ligne;
    n->op_colonne = premier->colonne;
    noeud_ajouter(n, objet);
    n->fin = objet->fin;
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
    const Jeton *premier_jeton = tart ? tart : &a->j[d];
    if (a->dont) {
        /* Dans « dont », un champ de l'entité examinée désigne celui de chaque objet (§ 16.4). */
        const Classe *e = classe_de(a->portee, a->dont);
        size_t f = d;
        if (a->j[d].type == J_CROCHETS) f = d + 1;
        else while (f < a->n && mot_de_nom(a, f)) f++;
        for (size_t k = f; e && k > d; k--) {
            char *c = a->j[d].type == J_CROCHETS ? grym_dupliquer(a->j[d].valeur) : cle(a, d, k);
            Genre g;
            if (classe_champ(a->portee, e, c, &g, NULL)) {
                if (tart && !tart->synthetique && (art == ART_LE || art == ART_LA) && genre_de(art) != g) {
                    erreur(a, tart, grym_formater("« %s » est un champ %s.", c, g == GENRE_MASCULIN ? "masculin" : "féminin"));
                    free(c);
                    return NULL;
                }
                Noeud *n = noeud_creer(N_CHAMP_DONT, premier_jeton->ligne, premier_jeton->colonne, premier_jeton->debut);
                n->texte = c;
                n->article = art == ART_IMPLICITE ? ART_AUCUN : art;
                a->i = k;
                n->fin = fin_jeton(&a->j[k - 1]);
                return n;
            }
            free(c);
            if (a->j[d].type == J_CROCHETS) break;
        }
    }
    if (a->j[d].type == J_CROCHETS && complement_de(a, d + 1)) {
        Genre g;
        if (champ_connu(a->portee, a->j[d].valeur, &g)
            || (!visible(a, a->j[d].valeur) && champ_integre(a->j[d].valeur, &g)))
            return acces_champ(a, grym_dupliquer(a->j[d].valeur), g, tart, art, premier_jeton, d + 1);
    }
    if (a->j[d].type == J_CROCHETS) {
        /* Nom entre crochets : correspondance exacte. */
        s = visible(a, a->j[d].valeur);
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
            s = visible(a, c);
            free(c);
            if (s) { fin = k; break; }
        }
        /* Un champ suivi de « de » l'emporte sur un nom au moins aussi court (§ 13.3). */
        for (size_t k = f; k > d; k--) {
            if (!complement_de(a, k) || (s && fin > k)) continue;
            char *c = cle(a, d, k);
            Genre g;
            if (champ_connu(a->portee, c, &g) || (!s && champ_integre(c, &g)))
                return acces_champ(a, c, g, tart, art, premier_jeton, k);
            free(c);
        }
        int arret = 0;
        for (int q = 0; q < a->nb_arrets && fin < f; q++) if (est_mot(&a->j[fin], a->arrets[q])) arret = 1;
        if (!s || (fin < f && s->sorte != S_CALCUL && !arret)) {
            char *tout = cle(a, d, f);
            if (!s && a->formule == 1) {
                /* Le nom existe peut-être hors du calcul : les calculs sont purs (§ 9.4). */
                int dehors = 0;
                for (size_t k = f; k > d && !dehors; k--) {
                    char *c = cle(a, d, k);
                    for (size_t i = 0; i < a->barriere; i++)
                        if (a->portee->s[i].sorte == S_VARIABLE && strcmp(a->portee->s[i].nom, c) == 0) dehors = 1;
                    if (dehors) {
                        erreur(a, &a->j[d], grym_formater(
                            "« %s » n'est pas visible dans un calcul : un calcul ne voit que ses paramètres. "
                            "Passez la valeur en paramètre.", c));
                    }
                    free(c);
                }
                if (dehors) { free(tout); return NULL; }
            }
            if (a->j[f].type == J_FIN) attendre_suites_nom(a, f, tout); /* nom en cours de frappe */
            erreur_inconnu(a, &a->j[d], tout);
            free(tout);
            return NULL;
        }
    }
    if (tart && !verifier_genre(a, s, art, tart)) return NULL;

    const Jeton *premier = tart ? tart : &a->j[d];
    if (s->sorte == S_CALCUL) {
        a->i = fin;
        Noeud *appel = appel_calcul(a, s, premier);
        if (appel) appel->article = art;
        return appel;
    }
    if (s->sorte == S_ACTION)
        return erreur(a, &a->j[d], grym_formater(
            "« %s » est une action : elle s'emploie en début de phrase (« %c%s … »).",
            s->nom, s->nom[0] >= 'a' && s->nom[0] <= 'z' ? s->nom[0] - 32 : s->nom[0], s->nom + 1));
    Noeud *n = noeud_creer(N_NOM, premier->ligne, premier->colonne, premier->debut);
    n->texte = grym_dupliquer(s->nom);
    n->article = art;
    n->crochets = a->j[d].type == J_CROCHETS && !a->j[d].synthetique;
    n->local = s->local;
    n->fin = fin_jeton(&a->j[fin - 1]);
    a->i = fin;
    if (!n->crochets) attendre_suites_nom(a, a->i, s->nom);
    return n;
}

static Noeud *unaire(Analyse *a);

/* Commence par une voyelle : « un nouvel employé », « l'addition ». Le « h » est laissé de côté. */
static int voyelle_initiale(const char *s) {
    static const char *const V[] = { "a", "e", "i", "o", "u", "y", "à", "â", "ä", "é", "è", "ê", "ë",
                                     "î", "ï", "ô", "ö", "ù", "û", "ü", "œ" };
    for (size_t k = 0; k < sizeof V / sizeof *V; k++)
        if (strncmp(s, V[k], strlen(V[k])) == 0) return 1;
    return 0;
}

/* ---------------------------------------------------------------- */
/* Retrouver les objets conservés (§ 16.4)                          */
/* ---------------------------------------------------------------- */

static Noeud *valeur(Analyse *a);
static Noeud *base(Analyse *a);
static int de_ou_d(const Jeton *t);

static int contient_champ_dont(const Noeud *n) {
    if (n->type == N_CHAMP_DONT) return 1;
    for (size_t k = 0; k < n->nb_enfants; k++) if (contient_champ_dont(n->enfants[k])) return 1;
    return 0;
}

static int est_supprime_mot(const Jeton *t) {
    return est_mot(t, "supprimé") || est_mot(t, "supprimée") || est_mot(t, "supprimés") || est_mot(t, "supprimées");
}

/* « conservé », ou « supprimé » pour la corbeille (§ 16.12) */
static int est_conserve_mot(const Jeton *t) {
    return est_mot(t, "conservé") || est_mot(t, "conservée") || est_mot(t, "conservés") || est_mot(t, "conservées")
        || est_supprime_mot(t);
}

static char *pluriel_de(const Classe *c) {
    return c->pluriel ? grym_dupliquer(c->pluriel) : grym_formater("%ss", c->nom);
}

/* À l'index k : « client conservé » (pluriel 0) ou « clients conservés » (pluriel 1).
 * Renvoie l'entité et *apres, l'index qui suit « conservé » ; NULL si la tournure n'y est pas.
 * Une faute d'accord est signalée (a->echec). */
static Classe *entite_conservee(Analyse *a, size_t k, int pluriel, size_t *apres) {
    if (k >= a->n) return NULL;
    size_t f = k;
    if (a->j[k].type == J_CROCHETS) f = k + 1;
    else while (f < a->n && mot_de_nom(a, f) && !est_conserve_mot(&a->j[f])) f++;
    if (f == k || f >= a->n || !est_conserve_mot(&a->j[f])) return NULL;
    char *nom = a->j[k].type == J_CROCHETS ? grym_dupliquer(a->j[k].valeur) : cle(a, k, f);
    Classe *e = NULL;
    for (size_t i = 0; i < a->portee->nb_classes && !e; i++) {
        Classe *c = &a->portee->classes[i];
        if (c->aptitude || !c->conserve) continue;
        char *pl = pluriel_de(c);
        if (strcmp(pluriel ? pl : c->nom, nom) == 0 || (a->j[k].synthetique && strcmp(c->nom, nom) == 0)) e = c;
        free(pl);
    }
    free(nom);
    if (!e) return NULL;
    const Jeton *tc = &a->j[f];
    a->corbeille = est_supprime_mot(tc);
    if (!tc->synthetique) {
        const char *radical = a->corbeille ? "supprimé" : "conservé";
        char juste[24];
        snprintf(juste, sizeof juste, "%s%s%s", radical, e->genre == GENRE_FEMININ ? "e" : "", pluriel ? "s" : "");
        if (!est_mot(tc, juste)) {
            erreur(a, tc, grym_formater("Accord : « %s ».", juste));
            return NULL;
        }
    }
    *apres = f + 1;
    return e;
}

/* Une condition « dont » compare un champ de l'objet examiné à une valeur (§ 16.4). */
static int verifier_dont(Analyse *a, const Classe *e, Noeud *n) {
    if (n->type == N_GROUPE) return verifier_dont(a, e, n->enfants[0]);
    if (n->type == N_LOGIQUE) return verifier_dont(a, e, n->enfants[0]) && verifier_dont(a, e, n->enfants[1]);
    if (n->type == N_COMPARAISON) {
        if (n->nb_enfants == 2 && n->enfants[1]->type == N_CHAMP_DONT && n->enfants[0]->type != N_CHAMP_DONT
            && n->forme == 1) {
            /* « 0 < solde » : le champ passe à gauche, l'opérateur se retourne */
            Noeud *x = n->enfants[0];
            n->enfants[0] = n->enfants[1];
            n->enfants[1] = x;
            n->op = n->op == '<' ? '>' : n->op == '>' ? '<' : n->op == 'l' ? 'g' : n->op == 'g' ? 'l' : n->op;
        }
        const Noeud *champ = n->enfants[0];
        if (champ->type == N_CHAMP_DONT && (n->nb_enfants == 1 || !contient_champ_dont(n->enfants[1]))) {
            const char *t = type_du_champ(a->portee, e, champ->texte);
            char op = n->op;
            const char *refus = NULL;
            if (op == 'A' || op == 'R')
                refus = NULL;   /* « est absent », « est présent » : tout champ */
            else if (!t || strcmp(t, "fichier") == 0 || strcmp(t, "image") == 0)
                refus = "un fichier ne se compare pas";
            else if ((op == 'P' || op == 'N' || op == '0') && strcmp(t, "nombre") != 0 && strcmp(t, "nombre entier") != 0)
                refus = "positif, négatif et nul s'appliquent à un nombre";
            else if ((op == 'V' || op == 'F') && strcmp(t, "vrai ou faux") != 0)
                refus = "vrai et faux s'appliquent à un champ vrai ou faux";
            else if ((op == '<' || op == '>' || op == 'l' || op == 'g')
                     && (strcmp(t, "vrai ou faux") == 0 || !type_de_base(t)))
                refus = "ce champ ne se compare que par égalité";
            if (refus) {
                erreur_a(a, n->op_ligne, n->op_colonne, grym_formater("« %s » : %s.", champ->texte, refus));
                return 0;
            }
            if (n->nb_enfants == 1) return 1;
            const char *vu = type_statique(n->enfants[1]);
            if (vu && strcmp(t, "nombre entier") == 0 && strcmp(vu, "nombre") == 0) return 1;   /* « rang ≥ 2,5 » */
            return verifier_type(a, champ->texte, t, n->enfants[1]);
        }
    }
    erreur_a(a, n->ligne, n->colonne, grym_formater(
        "Une condition « dont » compare un champ %s %s à une valeur : « dont le solde est négatif ».",
        e->genre == GENRE_FEMININ ? "de la" : "du", e->nom));
    return 0;
}

/* « dont … » facultatif ; *cond reçoit la condition, ou NULL. Renvoie 0 en cas d'erreur. */
static int clause_dont(Analyse *a, const Classe *e, Noeud **cond) {
    *cond = NULL;
    if (!est_mot(cour(a), "dont")) return 1;
    avancer(a);
    const char *avant = a->dont;
    a->dont = e->nom;
    Noeud *c = valeur(a);
    a->dont = avant;
    if (!c) return 0;
    if (!verifier_dont(a, e, c)) { noeud_liberer(c); return 0; }
    *cond = c;
    return 1;
}

/* L'entité a-t-elle au moins un lien (champ dont le type est une entité), hérité ou apporté compris ? */
static int a_un_lien(Portee *p, const Classe *c) {
    for (; c; c = c->parent ? classe_de(p, c->parent) : NULL) {
        for (size_t k = 0; k < c->nb; k++) if (c->types && c->types[k] && !type_de_base(c->types[k])) return 1;
        for (size_t q = 0; q < c->nb_aptitudes; q++) {
            const Classe *ap = aptitude_de(p, c->aptitudes[q], NULL);
            for (size_t k = 0; ap && k < ap->nb; k++) if (ap->types && ap->types[k] && !type_de_base(ap->types[k])) return 1;
        }
    }
    return 0;
}

/* À l'index k : « œuvre de », « œuvres du » : une entité (au pluriel si demandé) suivie de « de ».
 * Renvoie l'entité, et *de, l'index de « de » ; NULL si la tournure n'y est pas. */
static Classe *entite_de(Analyse *a, size_t k, int pluriel, size_t *de) {
    if (k >= a->n) return NULL;
    size_t f = k;
    if (a->j[k].type == J_CROCHETS) f = k + 1;
    else while (f < a->n && mot_de_nom(a, f)) f++;
    for (size_t q = f; q > k; q--) {
        if (!complement_de(a, q)) continue;
        char *nom = a->j[k].type == J_CROCHETS ? grym_dupliquer(a->j[k].valeur) : cle(a, k, q);
        Classe *e = NULL;
        for (size_t i = 0; i < a->portee->nb_classes && !e; i++) {
            Classe *c = &a->portee->classes[i];
            if (c->aptitude || !c->conserve) continue;
            char *pl = pluriel_de(c);
            if (strcmp(pluriel ? pl : c->nom, nom) == 0 || (a->j[k].synthetique && strcmp(c->nom, nom) == 0)) e = c;
            free(pl);
        }
        free(nom);
        if (e) { *de = q; return e; }
        if (a->j[k].type == J_CROCHETS) break;
    }
    return NULL;
}

/* L'objet après « de » : « de bach », « du compositeur », « de l'arrangeur de p » (§ 16.10). */
static Noeud *objet_de(Analyse *a, const Classe *e, size_t de) {
    if (!a_un_lien(a->portee, e)) {
        char *pl = pluriel_de(e);
        erreur(a, &a->j[de], grym_formater("%s « %s » n'a aucun lien vers un autre objet : « les %s de … » ne désigne rien.",
                                           e->genre == GENRE_FEMININ ? "Une" : "Un", e->nom, pl));
        free(pl);
        return NULL;
    }
    Jeton *t = &a->j[de];
    a->i = de + 1;
    if (est_mot(t, "du")) { a->article_force = ART_LE; a->jeton_force = t; }
    Noeud *o = base(a);
    a->article_force = ART_AUCUN;
    return o;
}

/* « le client conservé dont … », « le nombre de clients conservés dont … », « le nombre d'œuvres de bach » :
 * objet non NULL pour une relation inverse (§ 16.10), rangé en dernier enfant, op = 'I'. */
static Noeud *chercher(Analyse *a, const Jeton *t, const Classe *e, int mode, size_t apres, Noeud *objet) {
    int corbeille = !objet && a->corbeille;
    if (a->formule == 1) {
        noeud_liberer(objet);
        return erreur(a, t, grym_dupliquer("Un calcul ne lit pas la base : cherchez dans une action."));
    }
    if (!objet) a->i = apres;
    a->article_force = ART_AUCUN;
    Noeud *cond;
    if (!clause_dont(a, e, &cond)) { noeud_liberer(objet); return NULL; }
    Noeud *n = noeud_creer(N_CHERCHER, t->ligne, t->colonne, t->debut);
    n->texte = grym_dupliquer(e->nom);
    n->forme = mode;
    n->negation = corbeille;   /* dans la corbeille (§ 16.12) */
    if (cond) noeud_ajouter(n, cond);
    if (objet) { n->op = 'I'; noeud_ajouter(n, objet); }
    n->fin = fin_jeton(&a->j[a->i - 1]);
    return n;
}

static int est_nouveau(const Jeton *t) {
    return est_mot(t, "nouveau") || est_mot(t, "nouvel") || est_mot(t, "nouvelle");
}

/* « un nouveau client », « une nouvelle facture », « un nouvel employé » (§ 13.2). */
static Noeud *nouveau(Analyse *a) {
    Jeton *tun = cour(a);
    Genre ga = est_mot(tun, "une") ? GENRE_FEMININ : GENRE_MASCULIN;
    avancer(a);
    Jeton *tn = cour(a);
    avancer(a);
    size_t d = a->i, f = d;
    Classe *c = NULL;
    if (a->j[d].type == J_CROCHETS) {
        c = classe_de(a->portee, a->j[d].valeur);
        f = d + 1;
    } else {
        size_t k = d;
        while (k < a->n && mot_de_nom(a, k)) k++;
        for (f = k; f > d && !c; f--) {
            char *cl = cle(a, d, f);
            c = classe_de(a->portee, cl);
            free(cl);
            if (c) break;
        }
    }
    if (!c) {
        if (d == a->n - 1 || !debut_de_nom(a, d))
            return erreur(a, &a->j[d], grym_dupliquer("Nom de classe attendu : « un nouveau client »."));
        size_t k = d + 1;
        while (k < a->n && mot_de_nom(a, k)) k++;
        char *nom = a->j[d].type == J_CROCHETS ? grym_dupliquer(a->j[d].valeur) : cle(a, d, k);
        erreur(a, &a->j[d], grym_formater("Classe « %s » inconnue.", nom));
        free(nom);
        return NULL;
    }
    if (!tun->synthetique) {
        const char *juste = c->genre == GENRE_FEMININ ? "une nouvelle"
                          : voyelle_initiale(c->nom) ? "un nouvel" : "un nouveau";
        char *ecrit = grym_formater("%s %s", tun->valeur, tn->valeur);
        int ok = strcmp(ecrit, juste) == 0 || (c->genre == GENRE_MASCULIN && ga == GENRE_MASCULIN
                                               && est_mot(tn, "nouveau") && voyelle_initiale(c->nom));
        free(ecrit);
        if (!ok)
            return erreur(a, tun, grym_formater("« %s » est %s : écrivez « %s %s ».", c->nom,
                                               c->genre == GENRE_FEMININ ? "féminin" : "masculin", juste, c->nom));
    }
    Noeud *n = noeud_creer(N_NOUVEAU, tun->ligne, tun->colonne, tun->debut);
    n->texte = grym_dupliquer(c->nom);
    a->i = a->j[d].type == J_CROCHETS ? d + 1 : f;
    n->fin = fin_jeton(&a->j[a->i - 1]);
    return n;
}

static Noeud *base(Analyse *a) {
    Jeton *t = cour(a);
    {
        /* « le client conservé dont … » ; « le nombre de clients conservés dont … » (§ 16.4) */
        size_t apres;
        Classe *e = NULL;
        if (a->article_force == ART_LE && (e = entite_conservee(a, a->i, 0, &apres)) != NULL)
            return chercher(a, a->jeton_force, e, 1, apres, NULL);
        if (a->echec) return NULL;
        if (article_de(t) != ART_AUCUN && article_de(t) != ART_IMPLICITE
            && (e = entite_conservee(a, a->i + 1, 0, &apres)) != NULL) {
            Article art = article_de(t);
            if (!t->synthetique && (art == ART_LE || art == ART_LA) && genre_de(art) != e->genre)
                return erreur(a, t, grym_formater("« %s » est %s : « %s %s conservé%s ».", e->nom,
                                                  e->genre == GENRE_FEMININ ? "féminin" : "masculin",
                                                  e->genre == GENRE_FEMININ ? "la" : "le", e->nom,
                                                  e->genre == GENRE_FEMININ ? "e" : ""));
            return chercher(a, t, e, 1, apres, NULL);
        }
        if (a->echec) return NULL;
        if (est_mot(t, "le") && est_mot(voir(a, 1), "nombre") && de_ou_d(voir(a, 2))
            && (e = entite_conservee(a, a->i + 3, 1, &apres)) != NULL)
            return chercher(a, t, e, 2, apres, NULL);
        if (a->echec) return NULL;
        size_t de;
        if (est_mot(t, "le") && est_mot(voir(a, 1), "nombre") && de_ou_d(voir(a, 2))
            && (e = entite_de(a, a->i + 3, 1, &de)) != NULL) {   /* « le nombre d'œuvres de bach » (§ 16.10) */
            if (a->formule == 1)
                return erreur(a, t, grym_dupliquer("Un calcul ne lit pas la base : cherchez dans une action."));
            Noeud *objet = objet_de(a, e, de);
            if (!objet) return NULL;
            return chercher(a, t, e, 2, 0, objet);
        }
    }
    if (t->type == J_DATE) {
        Noeud *n = feuille(N_DATE, t);
        avancer(a);
        return n;
    }
    if (est_mot(t, "absent") || est_mot(t, "absente")) {   /* § 16.9 */
        Noeud *n = noeud_creer(N_ABSENT, t->ligne, t->colonne, t->debut);
        n->forme = est_mot(t, "absente") ? 2 : 1;
        n->entier = t->synthetique;   /* forme compacte : genre non écrit */
        n->fin = fin_jeton(t);
        avancer(a);
        return n;
    }
    /* « le fichier « chemin » », « du fichier (…) » (§ 15.2) */
    size_t f0 = est_mot(t, "le") ? 1 : a->article_force == ART_LE ? 0 : 2;
    if (f0 < 2 && est_mot(voir(a, (int)f0), "fichier")
        && (voir(a, (int)f0 + 1)->type == J_TEXTE || voir(a, (int)f0 + 1)->type == J_PAR_OUV)) {
        if (a->formule == 1)
            return erreur(a, t, grym_dupliquer("Un calcul ne lit pas le disque : lisez le fichier dans une action."));
        a->article_force = ART_AUCUN;
        for (size_t q = 0; q <= f0; q++) avancer(a);
        Noeud *chemin = base(a);
        if (!chemin) return NULL;
        Noeud *n = noeud_creer(N_FICHIER, t->ligne, t->colonne, t->debut);
        noeud_ajouter(n, chemin);
        n->fin = chemin->fin;
        return n;
    }
    if (t->type == J_ELISION && strcmp(t->valeur, "aujourd") == 0 && est_mot(voir(a, 1), "hui")) {
        if (a->formule == 1)
            return erreur(a, t, grym_dupliquer("Un calcul ne dépend pas du jour : passez la date en paramètre."));
        Noeud *n = noeud_creer(N_AUJOURDHUI, t->ligne, t->colonne, t->debut);
        avancer(a);
        n->fin = fin_jeton(cour(a));
        avancer(a);
        return n;
    }
    if ((est_mot(t, "un") || est_mot(t, "une")) && est_nouveau(voir(a, 1))) return nouveau(a);
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
        if (t->synthetique && cour(a)->type == J_PAR_FERM) {   /* argument compact : f(a ; b) */
            avancer(a);
            return e;
        }
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
    case J_TEXTE: {             /* un texte est une valeur (§ 13 : les champs en contiennent) */
        Noeud *n = feuille(N_TEXTE, t);
        avancer(a);
        return n;
    }
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
    { "absent",    "absente",    'A', 0 },   /* champ facultatif sans valeur (§ 16.9) */
    { "présent",   "présente",   'R', 0 },
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
    Symbole *s = sujet->type == N_NOM ? visible(a, sujet->texte) : NULL;
    attendre_relations(a, s ? s->genre : GENRE_LIBRE);
    Jeton *t = cour(a);
    const Relation *r = NULL;
    Genre g = GENRE_MASCULIN;
    for (size_t k = 0; k < NB_RELATIONS && !r; k++) {
        if (est_mot(t, RELATIONS[k].m)) { r = &RELATIONS[k]; g = GENRE_MASCULIN; }
        else if (est_mot(t, RELATIONS[k].f)) { r = &RELATIONS[k]; g = GENRE_FEMININ; }
    }
    if (!r && a->dont && sujet->type != N_CHAMP_DONT && article_de(cour(a)) != ART_AUCUN) {
        /* « dont brel est l'auteur » : la relation renversée, pour dire quel lien (§ 16.10) */
        Noeud *champ = nom_expression(a);
        if (!champ) { noeud_liberer(sujet); return NULL; }
        if (champ->type != N_CHAMP_DONT) {
            noeud_liberer(champ);
            noeud_liberer(sujet);
            return erreur(a, t, grym_dupliquer("Champ attendu : « dont brel est l'auteur »."));
        }
        Noeud *n = noeud_creer(N_COMPARAISON, sujet->ligne, sujet->colonne, sujet->debut);
        n->op = '=';
        n->forme = 4;   /* renversée : réimprimée « brel est l'auteur » */
        n->negation = negation;
        n->op_ligne = test->ligne;
        n->op_colonne = test->colonne;
        noeud_ajouter(n, champ);
        noeud_ajouter(n, sujet);
        n->fin = champ->fin;
        return n;
    }
    if (!r && a->dont && sujet->type == N_CHAMP_DONT) {
        /* « dont la licence est « A-12 » » : égalité (§ 16.4) */
        Noeud *droite = expression(a);
        if (!droite) { noeud_liberer(sujet); return NULL; }
        Noeud *n = noeud_creer(N_COMPARAISON, sujet->ligne, sujet->colonne, sujet->debut);
        n->op = '=';
        n->forme = 3;   /* « est valeur » : réimprimé tel quel */
        n->negation = negation;
        n->op_ligne = test->ligne;
        n->op_colonne = test->colonne;
        noeud_ajouter(n, sujet);
        noeud_ajouter(n, droite);
        n->fin = droite->fin;
        return n;
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
            if (ge != g && !te->synthetique) {
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
    /* Une tournure produite par la forme compacte n'a pas de genre écrit : pas d'accord à vérifier. */
    int ok = adjectif->synthetique ? 1 : accorder(a, s, g, adjectif, juste);
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
    if (crochets && f > d + 1 && complement_de(a, d + 1) && champ_connu(a->portee, a->j[d].valeur, NULL))
        crochets = 0;   /* « [nom] du client devient … » : un champ, traité plus bas */
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
    } else if (a->j[d].type == J_CROCHETS) {
        nom = grym_dupliquer(a->j[d].valeur);   /* champ entre crochets, suivi de « de » */
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
    Symbole *s = visible(a, nom);

    /* « Le solde du client devient … » : modification d'un champ (§ 13.3). Avec « vaut »,
     * c'est un champ seulement si ce qui suit « de » désigne un nom existant : « Le prix de
     * vente vaut 3. » crée un nom, même si « prix » est un champ. */
    if (!s && !crochets) {
        int champ = a->j[d].type == J_CROCHETS;
        for (size_t k = d + 1; k < f && !champ; k++) {
            if (!complement_de(a, k)) continue;
            char *c = cle(a, d, k);
            champ = champ_connu(a->portee, c, NULL);
            free(c);
            if (champ && creation) {
                size_t r = k + 1;
                if (r < f && article_de(&a->j[r]) != ART_AUCUN) r++;
                char *reste = r < f ? cle(a, r, f) : grym_dupliquer("");
                champ = r < f && visible(a, reste) != NULL;
                free(reste);
            }
        }
        if (champ) {
            free(nom);
            if (creation)
                return erreur(a, verbe, grym_dupliquer("Un champ se modifie avec « devient » : "
                                                       "« Le solde du client devient … »."));
            if (a->formule == 1)
                return erreur(a, tart, grym_dupliquer("Un calcul ne modifie pas les champs d'un objet."));
            a->i = (size_t)(tart - a->j);
            Noeud *cible = nom_expression(a);
            if (!cible) return NULL;
            if (cible->type != N_CHAMP || a->i != f) {
                noeud_liberer(cible);
                return erreur(a, &a->j[a->i], grym_dupliquer("« devient » attendu après le champ."));
            }
            a->i = f + 1;
            Noeud *v = valeur(a);
            if (!v) { noeud_liberer(cible); return NULL; }
            Genre gchamp = GENRE_LIBRE;
            champ_connu(a->portee, cible->texte, &gchamp);
            if (!verifier_type(a, cible->texte, type_commun(a->portee, cible->texte), v) || !accorder_absent(a, v, gchamp)) {
                noeud_liberer(cible);
                noeud_liberer(v);
                return NULL;
            }
            if (v->type == N_NOUVEAU && cour(a)->type == J_DEUX_POINTS) {
                if (!bloc_initialisation(a, v, tart)) { noeud_liberer(cible); noeud_liberer(v); return NULL; }
            } else if (!fin_phrase(a, 0)) {
                noeud_liberer(cible);
                noeud_liberer(v);
                return NULL;
            }
            Noeud *n = noeud_creer(P_MODIF_CHAMP, tart->ligne, tart->colonne, tart->debut);
            n->texte = cible->texte;
            n->article = cible->article;
            cible->texte = NULL;
            noeud_ajouter(n, cible->enfants[0]);
            cible->nb_enfants = 0;
            noeud_liberer(cible);
            noeud_ajouter(n, v);
            n->fin = v->fin;
            return n;
        }
    }
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
        if (s->lecture_seule) {
            erreur(a, &a->j[d], grym_formater(
                "« %s » est le compteur de la boucle : il avance tout seul et ne se modifie pas.", nom));
            free(ecrit_nom);
            free(nom);
            return NULL;
        }
        if (s->sorte != S_VARIABLE) {
            erreur(a, &a->j[d], grym_formater("« %s » est %s ne se modifie pas.", nom,
                                              s->sorte == S_CALCUL ? "un calcul : il" : "une action : elle"));
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
    if (e->type == N_NOUVEAU && cour(a)->type == J_DEUX_POINTS) {
        if (!bloc_initialisation(a, e, tart)) { noeud_liberer(e); free(nom); return NULL; }
    } else if (!fin_phrase(a, 0)) { noeud_liberer(e); free(nom); return NULL; }

    /* Le nom n'existe qu'après la phrase : « Le total vaut total + 1. » échoue. */
    int local = -1;
    if (creation) {
        portee_declarer(a->portee, nom, genre_de(art), tart->ligne);
        if (a->formule) a->portee->s[a->portee->n - 1].local = local = a->nb_locaux++;
    } else {
        local = s->local;
    }

    Noeud *n = noeud_creer(creation ? P_CREATION : P_MODIFICATION,
                           tart->ligne, tart->colonne, tart->debut);
    n->local = local;
    n->texte = nom;
    n->article = art == ART_IMPLICITE ? ART_AUCUN : art;
    n->crochets = crochets && !a->j[d].synthetique;
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
static Noeud *branche(Analyse *a, int colonne, const char *mot, int *forme_bloc) {
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
    if (suivant->type == J_FIN || suivant->ligne == t->ligne || suivant->retrait <= colonne
        || est_mot(suivant, "sinon")) {
        char *m = grym_formater("Bloc vide : après « : », écrivez les phrases du bloc sur les lignes "
                                "suivantes, plus indentées que « %s ».",
                                mot);
        return erreur(a, suivant->type == J_FIN || suivant->ligne == t->ligne ? t : suivant, m);
    }
    return bloc(a, suivant->retrait, 0);
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
    Noeud *alors = branche(a, colonne, "Si", &forme_bloc);
    if (!alors) { noeud_liberer(cond); return NULL; }
    Noeud *n = noeud_creer(P_SI, t->ligne, t->colonne, t->debut);
    n->forme = (forme_bloc ? 1 : 0) | (sinon_si ? 2 : 0);
    noeud_ajouter(n, cond);
    noeud_ajouter(n, alors);
    n->fin = alors->fin;

    /* « Sinon » : sur la même ligne qu'une forme courte, ou aligné sur « Si ». */
    Jeton *s = cour(a);
    if (!est_mot(s, "sinon")) return n;
    if (premier_de_ligne(a, a->i) && s->retrait != colonne) {
        if (s->retrait < colonne) return n;   /* appartient à un bloc englobant */
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
        autre = branche(a, colonne, "Sinon", &f);
        if (!autre) { noeud_liberer(n); return NULL; }
        if (f) n->forme |= 4;   /* « Sinon » en bloc */
    }
    noeud_ajouter(n, autre);
    n->fin = autre->fin;
    return n;
}

/* ---------------------------------------------------------------- */
/* Formules (§ 9)                                                   */
/* ---------------------------------------------------------------- */

static int mot_de_construction(const Jeton *t);

static int est_un(const Jeton *t, Genre *g) {
    if (est_mot(t, "un"))  { *g = GENRE_MASCULIN; return 1; }
    if (est_mot(t, "une")) { *g = GENRE_FEMININ;  return 1; }
    return 0;
}

static int marque_parametre(const Analyse *a, size_t k) {
    Genre g;
    return a->j[k].type == J_ELISION && strcmp(a->j[k].valeur, "d") == 0 && est_un(&a->j[k + 1], &g);
}

/* Paramètres entre d et f : « d'un nombre et d'une remise » (calcul, avec_de)
 * ou « un client et une remise » (action). Renvoie un N_BLOC de N_NOM ; forme 1 masculin, 2 féminin. */
static Noeud *parametres(Analyse *a, size_t d, size_t f, int avec_de) {
    Noeud *liste = noeud_creer(N_BLOC, a->j[d].ligne, a->j[d].colonne, a->j[d].debut);
    size_t k = d;
    while (k < f) {
        if (avec_de) {
            if (!marque_parametre(a, k)) break;
            k++;
        }
        Genre g;
        if (!est_un(&a->j[k], &g)) break;
        Jeton *tun = &a->j[k++];
        size_t debut = k;
        if (k < f && a->j[k].type == J_CROCHETS) k++;
        else while (k < f && mot_de_nom(a, k)) k++;
        if (k == debut) {
            noeud_liberer(liste);
            char *x = texte_jeton(tun);
            char *m = grym_formater("Nom de paramètre attendu après « %s ».", x);
            free(x);
            return erreur(a, &a->j[k], m);
        }
        if (article_de(&a->j[debut]) != ART_AUCUN) {
            noeud_liberer(liste);
            return erreur(a, &a->j[debut], grym_dupliquer("Un paramètre ne commence pas par un article : écrivez « un nombre »."));
        }
        char *nom = a->j[debut].type == J_CROCHETS ? grym_dupliquer(a->j[debut].valeur) : cle(a, debut, k);
        char *aptitude = NULL;
        if (strncmp(nom, "chose ", 6) == 0) {
            /* « une chose horodatée » : paramètre nommé « chose », de l'aptitude « horodatée » (§ 13.7) */
            int feminin;
            Classe *ap = aptitude_de(a->portee, nom + 6, &feminin);
            if (ap && (feminin || tun->synthetique)) {
                aptitude = grym_dupliquer(ap->nom);
                free(nom);
                nom = grym_dupliquer("chose");
            }
        }
        for (size_t q = 0; q < liste->nb_enfants; q++)
            if (strcmp(liste->enfants[q]->texte, nom) == 0) {
                noeud_liberer(liste);
                erreur(a, &a->j[debut], grym_formater("Paramètre « %s » déjà nommé.", nom));
                free(nom);
                return NULL;
            }
        Noeud *p = noeud_creer(N_NOM, tun->ligne, tun->colonne, tun->debut);
        p->texte = nom;
        p->texte2 = aptitude;
        p->forme = g == GENRE_FEMININ ? 2 : 1;
        noeud_ajouter(liste, p);
        if (k < f && est_mot(&a->j[k], "et")) { k++; continue; }
        break;
    }
    if (k < f) {
        noeud_liberer(liste);
        char *x = texte_jeton(&a->j[k]);
        char *m = grym_formater("« %s » inattendu dans la liste des paramètres : écrivez %s.", x,
                                avec_de ? "« d'un nombre et d'une remise »" : "« un client et une remise »");
        free(x);
        return erreur(a, &a->j[k], m);
    }
    return liste;
}

/* Une formule peut avoir plusieurs versions, une par classe de son premier paramètre (§ 13.6).
 * Renvoie la classe de la nouvelle version (NULL si son premier paramètre n'est pas une classe),
 * ou met *refus à 1 après avoir signalé l'erreur. */
static char *version_de_formule(Analyse *a, const char *nom, Sorte sorte, const Noeud *params,
                                const Jeton *t, int *refus) {
    *refus = 0;
    char *classe = NULL;
    if (params->nb_enfants && params->enfants[0]->texte2)
        classe = grym_dupliquer(params->enfants[0]->texte2);          /* aptitude */
    else if (params->nb_enfants && classe_de(a->portee, params->enfants[0]->texte))
        classe = grym_dupliquer(params->enfants[0]->texte);
    for (size_t i = 0; i < a->portee->n; i++) {
        const Symbole *s = &a->portee->s[i];
        if (strcmp(s->nom, nom) != 0) continue;
        char *m = NULL;
        if (s->sorte == S_VARIABLE || !s->classe || !classe)
            m = s->sorte != S_VARIABLE && (s->classe || classe)
              ? grym_formater("« %s » existe déjà : chaque version d'une formule a pour premier paramètre "
                              "une classe différente.", nom)
              : grym_formater("« %s » existe déjà.", nom);
        else if (strcmp(s->classe, classe) == 0)
            m = grym_formater("« %s » existe déjà pour « %s ».", nom, classe);
        else if (s->sorte != sorte)
            m = grym_formater("« %s » est déjà %s : toutes ses versions sont de la même sorte.", nom,
                              s->sorte == S_CALCUL ? "un calcul" : "une action");
        else if (s->nb_parametres != (int)params->nb_enfants)
            m = grym_formater("« %s » a déjà %d paramètre%s : toutes ses versions en ont autant.", nom,
                              s->nb_parametres, s->nb_parametres > 1 ? "s" : "");
        if (m) {
            erreur(a, t, m);
            free(classe);
            *refus = 1;
            return NULL;
        }
    }
    return classe;
}

typedef struct { int formule, nb_locaux, niveau, boucle; size_t barriere; } Contexte;

/* Entre dans le corps d'une formule : les paramètres deviennent les premières cases locales. */
static Contexte entrer_formule(Analyse *a, int sorte, Noeud *params) {
    Contexte c = { a->formule, a->nb_locaux, a->niveau, a->boucle, a->barriere };
    a->formule = sorte;
    a->boucle = 0;
    a->nb_locaux = 0;
    a->barriere = a->portee->n;
    for (size_t k = 0; k < params->nb_enfants; k++) {
        Noeud *p = params->enfants[k];
        portee_declarer(a->portee, p->texte, p->forme == 2 ? GENRE_FEMININ : GENRE_MASCULIN, p->ligne);
        a->portee->s[a->portee->n - 1].local = p->local = a->nb_locaux++;
    }
    return c;
}

static void sortir_formule(Analyse *a, Contexte c) {
    portee_tronquer(a->portee, a->barriere);
    a->formule = c.formule;
    a->nb_locaux = c.nb_locaux;
    a->niveau = c.niveau;
    a->boucle = c.boucle;
    a->barriere = c.barriere;
}

static int premier_niveau(Analyse *a, const Jeton *t) {
    if (a->niveau == 0 && a->formule == 0) return 1;
    erreur(a, t, grym_dupliquer("Une formule se définit au premier niveau du programme, hors de tout bloc."));
    return 0;
}

/* Corps en bloc après « : » (même règles qu'un Si, § 5.4). */
static Noeud *corps_en_bloc(Analyse *a, const Jeton *deux_points, int colonne) {
    Jeton *suivant = cour(a);
    if (suivant->type == J_FIN || suivant->ligne == deux_points->ligne || suivant->retrait <= colonne)
        return erreur(a, suivant->type == J_FIN || suivant->ligne == deux_points->ligne ? deux_points : suivant,
                      grym_dupliquer("Bloc vide : après « : », écrivez les phrases de la formule sur les "
                                     "lignes suivantes, indentées."));
    return bloc(a, suivant->retrait, 0);
}

/* Calcul : « Le carré d'un nombre vaut nombre × nombre. » ou « … d'un nombre : » suivi d'un bloc. */
static Noeud *definition_calcul(Analyse *a, size_t marque, size_t fin_entete, int colonne) {
    Jeton *tart = cour(a);
    Article art = article_de(tart);
    if (!premier_niveau(a, tart)) return NULL;
    avancer(a);
    size_t d = a->i;
    if (d == marque)
        return erreur(a, &a->j[d], grym_dupliquer("Nom du calcul attendu : « Le carré d'un nombre vaut … »."));
    int crochets = a->j[d].type == J_CROCHETS && marque == d + 1;
    for (size_t k = d; k < marque && !crochets; k++)
        if (!mot_de_nom(a, k)) {
            char *x = texte_jeton(&a->j[k]);
            char *m = grym_formater("« %s » ne peut pas faire partie du nom d'un calcul.", x);
            free(x);
            return erreur(a, &a->j[k], m);
        }
    char *nom = crochets ? grym_dupliquer(a->j[d].valeur) : cle(a, d, marque);
    if (champ_connu(a->portee, nom, NULL)) {
        erreur(a, &a->j[d], grym_formater("« %s » est un champ : un calcul ne peut pas porter ce nom.", nom));
        free(nom);
        return NULL;
    }
    Noeud *params = parametres(a, marque, fin_entete, 1);
    if (!params) { free(nom); return NULL; }
    int refus;
    char *classe = version_de_formule(a, nom, S_CALCUL, params, &a->j[d], &refus);
    if (refus) { noeud_liberer(params); free(nom); return NULL; }

    portee_declarer(a->portee, nom, genre_de(art), tart->ligne);
    Symbole *s = &a->portee->s[a->portee->n - 1];
    s->sorte = S_CALCUL;
    s->nb_parametres = (int)params->nb_enfants;
    s->classe = classe;

    Contexte ctx = entrer_formule(a, 1, params);
    Jeton *verbe = &a->j[fin_entete];
    a->i = fin_entete + 1;
    Noeud *corps;
    int forme;
    if (est_mot(verbe, "vaut")) {
        forme = 0;
        corps = valeur(a);
        if (corps && !fin_phrase(a, 0)) { noeud_liberer(corps); corps = NULL; }
    } else {
        forme = 1;
        corps = corps_en_bloc(a, verbe, colonne);
        if (corps) {
            const Noeud *dernier = NULL;
            for (size_t k = corps->nb_enfants; k > 0 && !dernier; k--)
                if (corps->enfants[k - 1]->type != P_REMARQUE) dernier = corps->enfants[k - 1];
            if (!dernier || dernier->type != P_RENDRE) {
                noeud_liberer(corps);
                corps = NULL;
                erreur(a, tart, grym_formater("Le calcul « %s » doit se terminer par « Rendre … ».", nom));
            }
        }
    }
    int locaux = a->nb_locaux;
    sortir_formule(a, ctx);
    if (!corps) {
        noeud_liberer(params);
        free(nom);
        return NULL;   /* le calcul reste déclaré dans la copie de travail, abandonnée en cas d'échec */
    }
    Noeud *n = noeud_creer(P_CALCUL, tart->ligne, tart->colonne, tart->debut);
    n->texte = nom;
    n->texte2 = classe ? grym_dupliquer(classe) : NULL;
    n->article = art;
    n->forme = forme;
    n->entier = locaux;
    noeud_ajouter(n, params);
    noeud_ajouter(n, corps);
    n->fin = corps->fin;
    return n;
}

/* Action : « Pour relancer un client : » suivi d'un bloc. */
static Noeud *definition_action(Analyse *a, int colonne) {
    Jeton *tpour = cour(a);
    if (!premier_niveau(a, tpour)) return NULL;
    avancer(a);
    size_t d = a->i, k = d;
    Genre g;
    if (a->j[k].type == J_CROCHETS) k++;
    else while (mot_de_nom(a, k) && !est_un(&a->j[k], &g)) k++;
    if (k == d)
        return erreur(a, cour(a), grym_dupliquer(
            "Nom d'action attendu après « Pour » : « Pour relancer un client : »."));
    if (mot_de_construction(&a->j[d])) {
        char *x = texte_jeton(&a->j[d]);
        char *m = grym_formater("« %s » commence une construction du langage : il ne peut pas "
                                "commencer le nom d'une action.", x);
        free(x);
        return erreur(a, &a->j[d], m);
    }
    size_t fin = k;
    while (a->j[fin].type != J_DEUX_POINTS && a->j[fin].type != J_POINT && a->j[fin].type != J_FIN
           && a->j[fin].type != J_VIRGULE)
        fin++;
    attendre_en(a, fin, A_SUITE_SI);
    if (a->j[fin].type != J_DEUX_POINTS)
        return erreur(a, &a->j[fin], grym_dupliquer(
            "« : » attendu : une action s'écrit en bloc (« Pour relancer un client : »)."));
    char *nom = a->j[d].type == J_CROCHETS ? grym_dupliquer(a->j[d].valeur) : cle(a, d, k);
    Noeud *params = parametres(a, k, fin, 0);
    if (!params) { free(nom); return NULL; }
    int refus;
    char *classe = version_de_formule(a, nom, S_ACTION, params, &a->j[d], &refus);
    if (refus) { noeud_liberer(params); free(nom); return NULL; }

    portee_declarer(a->portee, nom, GENRE_LIBRE, tpour->ligne);
    Symbole *s = &a->portee->s[a->portee->n - 1];
    s->sorte = S_ACTION;
    s->nb_parametres = (int)params->nb_enfants;
    s->classe = classe;

    Contexte ctx = entrer_formule(a, 2, params);
    a->i = fin + 1;
    Noeud *corps = corps_en_bloc(a, &a->j[fin], colonne);
    int locaux = a->nb_locaux;
    sortir_formule(a, ctx);
    if (!corps) { noeud_liberer(params); free(nom); return NULL; }
    Noeud *n = noeud_creer(P_ACTION, tpour->ligne, tpour->colonne, tpour->debut);
    n->texte = nom;
    n->texte2 = classe ? grym_dupliquer(classe) : NULL;
    n->forme = 1;
    n->entier = locaux;
    noeud_ajouter(n, params);
    noeud_ajouter(n, corps);
    n->fin = corps->fin;
    return n;
}

/* « Relancer le client. » : arguments séparés par « et » (§ 9.2). */
static Noeud *appel_action(Analyse *a, Symbole *s, size_t fin) {
    Jeton *t = cour(a);
    if (a->formule == 1)
        return erreur(a, t, grym_formater("Un calcul n'appelle pas d'action : « %s » est une action.", s->nom));
    a->i = fin;
    Noeud *n = noeud_creer(P_APPEL, t->ligne, t->colonne, t->debut);
    n->texte = grym_dupliquer(s->nom);
    int nb = 0;
    if (cour(a)->type != J_POINT && cour(a)->type != J_FIN) {
        for (;;) {
            Noeud *arg = comparaison(a);
            if (!arg) { noeud_liberer(n); return NULL; }
            noeud_ajouter(n, arg);
            n->fin = arg->fin;
            nb++;
            if (!est_mot(cour(a), "et")) break;
            avancer(a);
        }
    }
    if (nb != s->nb_parametres) {
        char *att = pluriel(s->nb_parametres, "paramètre");
        erreur(a, t, grym_formater("« %s » attend %s, %d donné%s.", s->nom, att, nb, nb > 1 ? "s" : ""));
        free(att);
        noeud_liberer(n);
        return NULL;
    }
    if (!fin_phrase(a, 0)) { noeud_liberer(n); return NULL; }
    return n;
}

/* Action visible dont le nom commence la phrase (plus longue correspondance). */
static Symbole *action_en_tete(Analyse *a, size_t *fin) {
    size_t f = a->i;
    while (f < a->n && mot_de_nom(a, f)) f++;
    for (size_t k = f; k > a->i; k--) {
        char *c = cle(a, a->i, k);
        Symbole *s = visible(a, c);
        free(c);
        if (s && s->sorte == S_ACTION) { *fin = k; return s; }
    }
    return NULL;
}

/* ---------------------------------------------------------------- */
/* Boucles et Selon (§ 10)                                          */
/* ---------------------------------------------------------------- */

/* Consomme une suite de mots fixes (« de la boucle ») ; 0 et une erreur sinon. */
static int mots_fixes(Analyse *a, const char *const *mots, int nb) {
    for (int k = 0; k < nb; k++) {
        attendre_mot(a, a->i, mots[k], strlen(mots[k]));
        if (!est_mot(cour(a), mots[k])) {
            erreur_inattendu(a, cour(a));
            return 0;
        }
        avancer(a);
    }
    return 1;
}

static void arrets(Analyse *a, const char *m1, const char *m2) {
    a->nb_arrets = 0;
    if (m1) a->arrets[a->nb_arrets++] = m1;
    if (m2) a->arrets[a->nb_arrets++] = m2;
}

/* Expression suivie d'un mot de liaison : « de 1 à 12 », « 3 fois ». */
static Noeud *expression_avant(Analyse *a, const char *m1, const char *m2) {
    arrets(a, m1, m2);
    Noeud *e = expression(a);
    arrets(a, NULL, NULL);
    return e;
}

static int de_ou_d(const Jeton *t) {
    return est_mot(t, "de") || (t->type == J_ELISION && strcmp(t->valeur, "d") == 0);
}

/* Tant que condition , phrase | : bloc */
static Noeud *tant_que(Analyse *a, int colonne) {
    Jeton *t = cour(a);
    avancer(a);
    static const char *const QUE[] = { "que" };
    if (!mots_fixes(a, QUE, 1)) return NULL;
    Noeud *cond = valeur(a);
    if (!cond) return NULL;
    if (!peut_etre_condition(cond)) {
        noeud_liberer(cond);
        return erreur(a, t, grym_dupliquer("Condition attendue après « Tant que » : un calcul seul "
                                           "n'est ni vrai ni faux."));
    }
    int forme = 0;
    a->boucle++;
    Noeud *corps = branche(a, colonne, "Tant que", &forme);
    a->boucle--;
    if (!corps) { noeud_liberer(cond); return NULL; }
    Noeud *n = noeud_creer(P_TANT_QUE, t->ligne, t->colonne, t->debut);
    n->forme = forme;
    noeud_ajouter(n, cond);
    noeud_ajouter(n, corps);
    n->fin = corps->fin;
    return n;
}

/* Répéter expression fois , phrase | : bloc */
static Noeud *repeter(Analyse *a, int colonne) {
    Jeton *t = cour(a);
    avancer(a);
    Noeud *nb = expression_avant(a, "fois", NULL);
    if (!nb) return NULL;
    static const char *const FOIS[] = { "fois" };
    if (!mots_fixes(a, FOIS, 1)) { noeud_liberer(nb); return NULL; }
    int case_reste = a->nb_locaux++;
    int forme = 0;
    a->boucle++;
    Noeud *corps = branche(a, colonne, "Répéter", &forme);
    a->boucle--;
    if (!corps) { noeud_liberer(nb); return NULL; }
    Noeud *n = noeud_creer(P_REPETER, t->ligne, t->colonne, t->debut);
    n->forme = forme;
    n->entier = case_reste;
    noeud_ajouter(n, nb);
    noeud_ajouter(n, corps);
    n->fin = corps->fin;
    return n;
}

/* Pour chaque nom de début à fin [ par pas de pas ] , phrase | : bloc */
/* « Pour chaque client conservé [dont …] [, par nom [décroissant]] : » (§ 16.4) */
static Noeud *pour_chaque_conserve(Analyse *a, int colonne, const Jeton *t, const Classe *e, size_t apres, size_t de) {
    int corbeille = !de && a->corbeille;
    if (a->formule == 1)
        return erreur(a, t, grym_dupliquer("Un calcul ne lit pas la base : cherchez dans une action."));
    const Jeton *tnom = cour(a);
    char *nom = grym_dupliquer(e->nom);
    if (visible(a, nom)) {
        erreur(a, tnom, grym_formater("« %s » existe déjà : renommez-le, car « Pour chaque %s %s » donne ce nom "
                                      "à l'objet de chaque tour.", nom, nom,
                                      e->genre == GENRE_FEMININ ? "conservée" : "conservé"));
        free(nom);
        return NULL;
    }
    Noeud *objet = NULL;
    if (de) {   /* « Pour chaque œuvre de bach » (§ 16.10) */
        objet = objet_de(a, e, de);
        if (!objet) { free(nom); return NULL; }
    } else {
        a->i = apres;
    }
    Noeud *cond;
    if (!clause_dont(a, e, &cond)) { free(nom); noeud_liberer(objet); return NULL; }
    Noeud *cherche = noeud_creer(N_CHERCHER, t->ligne, t->colonne, t->debut);
    cherche->texte = grym_dupliquer(e->nom);
    cherche->forme = 0;
    cherche->negation = corbeille;
    if (cond) noeud_ajouter(cherche, cond);
    if (objet) { cherche->op = 'I'; noeud_ajouter(cherche, objet); }
    if (cour(a)->type == J_VIRGULE && est_mot(voir(a, 1), "par")) {
        avancer(a);
        avancer(a);
        size_t d = a->i, k = d;
        if (a->j[k].type == J_CROCHETS) k++;
        else while (mot_de_nom(a, k) && !est_mot(&a->j[k], "décroissant") && !est_mot(&a->j[k], "croissant")) k++;
        if (k > d && article_de(&a->j[d]) != ART_AUCUN) d++;   /* « par le nom » */
        char *champ = k > d ? (a->j[d].type == J_CROCHETS ? grym_dupliquer(a->j[d].valeur) : cle(a, d, k)) : NULL;
        const char *type = champ ? type_du_champ(a->portee, e, champ) : NULL;
        if (!champ || !type || strcmp(type, "fichier") == 0 || strcmp(type, "image") == 0 || !type_de_base(type)) {
            erreur(a, &a->j[d], champ && !type
                ? grym_formater("« %s » n'est pas un champ %s %s.", champ, e->genre == GENRE_FEMININ ? "de la" : "du", e->nom)
                : grym_dupliquer("Tri attendu sur un champ de texte, de nombre, de date ou vrai ou faux : « , par nom »."));
            free(champ);
            free(nom);
            noeud_liberer(cherche);
            return NULL;
        }
        cherche->texte2 = champ;
        a->i = k;
        if (est_mot(cour(a), "décroissant")) { cherche->entier = 1; avancer(a); }
        else if (est_mot(cour(a), "croissant")) avancer(a);
    }
    size_t sauve = a->portee->n;
    portee_declarer(a->portee, nom, e->genre, t->ligne);
    Symbole *tour = &a->portee->s[a->portee->n - 1];
    tour->lecture_seule = 1;
    int case_objet = tour->local = a->nb_locaux++;
    int case_liste = a->nb_locaux++;
    a->nb_locaux++;   /* rang dans la liste */
    int forme = 0;
    a->boucle++;
    Noeud *corps = branche(a, colonne, "Pour chaque", &forme);
    a->boucle--;
    portee_tronquer(a->portee, sauve);
    if (!corps) { noeud_liberer(cherche); free(nom); return NULL; }
    Noeud *n = noeud_creer(P_POUR_CONSERVE, t->ligne, t->colonne, t->debut);
    n->texte = nom;
    n->local = case_objet;
    n->entier = case_liste;
    n->forme = forme;
    noeud_ajouter(n, cherche);
    noeud_ajouter(n, corps);
    n->fin = corps->fin;
    return n;
}

static Noeud *pour_chaque(Analyse *a, int colonne) {
    Jeton *t = cour(a);
    avancer(a);
    avancer(a);   /* chaque */
    {
        size_t apres;
        Classe *e = entite_conservee(a, a->i, 0, &apres);
        if (e) return pour_chaque_conserve(a, colonne, t, e, apres, 0);
        if (a->echec) return NULL;
        size_t de;
        e = entite_de(a, a->i, 0, &de);
        if (e) {
            /* « Pour chaque i de 1 à 9 » garde son sens : un « à » avant « dont », « , » ou « : » en fait un compteur */
            int compteur = 0;
            for (size_t q = de + 1; q < a->n; q++) {
                const Jeton *x = &a->j[q];
                if (x->type == J_DEUX_POINTS || x->type == J_VIRGULE || x->type == J_FIN || est_mot(x, "dont")) break;
                if (est_mot(x, "à") || est_mot(x, "au")) { compteur = 1; break; }
            }
            if (!compteur) return pour_chaque_conserve(a, colonne, t, e, 0, de);
        }
        if (a->echec) return NULL;
    }
    size_t d = a->i, k = d;
    if (a->j[k].type == J_CROCHETS) k++;
    else while (mot_de_nom(a, k) && !de_ou_d(&a->j[k]) && !est_mot(&a->j[k], "du")) k++;
    if (k == d)
        return erreur(a, cour(a), grym_dupliquer(
            "Nom du compteur attendu : « Pour chaque mois de 1 à 12 : »."));
    if (article_de(&a->j[d]) != ART_AUCUN)
        return erreur(a, &a->j[d], grym_dupliquer(
            "Le compteur se nomme sans article : « Pour chaque mois de 1 à 12 : »."));
    char *nom = a->j[d].type == J_CROCHETS ? grym_dupliquer(a->j[d].valeur) : cle(a, d, k);
    if (visible(a, nom)) {
        erreur(a, &a->j[d], grym_formater("« %s » existe déjà : choisissez un autre nom de compteur.", nom));
        free(nom);
        return NULL;
    }
    a->i = k;
    attendre_mot(a, a->i, "de", 2);
    attendre_mot(a, a->i, "du", 2);
    if (est_mot(cour(a), "du")) {             /* « du début » : de + le */
        a->article_force = ART_LE;
        a->jeton_force = cour(a);
    } else if (!de_ou_d(cour(a))) {
        free(nom);
        return erreur_inattendu(a, cour(a));
    }
    avancer(a);
    Noeud *debut = expression_avant(a, "à", "au");
    a->article_force = ART_AUCUN;
    Noeud *fin = NULL, *pas = NULL;
    if (debut && complement(a, 'a')) {
        fin = expression_avant(a, "par", NULL);
        a->article_force = ART_AUCUN;
    }
    if (fin) {
        attendre_mot(a, a->i, "par pas de", 10);
        if (est_mot(cour(a), "par")) {
            static const char *const PAS[] = { "par", "pas" };
            if (mots_fixes(a, PAS, 2)) {
                attendre_mot(a, a->i, "de", 2);
                if (!de_ou_d(cour(a))) erreur_inattendu(a, cour(a));
                else { avancer(a); pas = expression(a); }
            }
            if (!pas) { noeud_liberer(fin); fin = NULL; }
        }
    }
    if (!fin) { noeud_liberer(debut); free(nom); return NULL; }

    size_t sauve = a->portee->n;
    portee_declarer(a->portee, nom, GENRE_LIBRE, t->ligne);
    Symbole *compteur = &a->portee->s[a->portee->n - 1];
    compteur->lecture_seule = 1;
    int case_compteur = compteur->local = a->nb_locaux++;
    int case_fin = a->nb_locaux++;
    a->nb_locaux++;   /* case du pas */
    int forme = 0;
    a->boucle++;
    Noeud *corps = branche(a, colonne, "Pour chaque", &forme);
    a->boucle--;
    portee_tronquer(a->portee, sauve);
    if (!corps) {
        noeud_liberer(debut);
        noeud_liberer(fin);
        noeud_liberer(pas);
        free(nom);
        return NULL;
    }
    Noeud *n = noeud_creer(P_POUR_CHAQUE, t->ligne, t->colonne, t->debut);
    n->texte = nom;
    n->local = case_compteur;
    n->entier = case_fin;
    n->forme = pas ? 1 : 0;
    noeud_ajouter(n, debut);
    noeud_ajouter(n, fin);
    if (pas) noeud_ajouter(n, pas);
    noeud_ajouter(n, corps);
    n->fin = corps->fin;
    return n;
}

/* Sortir de la boucle. | Passer au tour suivant. */
static Noeud *sortie_de_boucle(Analyse *a) {
    Jeton *t = cour(a);
    int sortir = est_mot(t, "sortir");
    avancer(a);
    static const char *const SORTIR[] = { "de", "la", "boucle" };
    static const char *const PASSER[] = { "au", "tour", "suivant" };
    if (!mots_fixes(a, sortir ? SORTIR : PASSER, 3)) return NULL;
    if (!fin_phrase(a, 0)) return NULL;
    if (a->boucle == 0)
        return erreur(a, t, grym_formater("« %s » hors d'une boucle.",
                                          sortir ? "Sortir de la boucle" : "Passer au tour suivant"));
    return noeud_creer(sortir ? P_SORTIR : P_PASSER, t->ligne, t->colonne, t->debut);
}

static Noeud *sujet(const Jeton *t, int case_sujet) {
    Noeud *n = noeud_creer(N_SUJET, t->ligne, t->colonne, t->debut);
    n->local = case_sujet;
    return n;
}

/* Une condition de cas : une valeur, « de a à b », ou une tournure (« négatif », « supérieur à 100 »). */
static Noeud *element_cas(Analyse *a, int case_sujet) {
    Jeton *t = cour(a);
    if (de_ou_d(t)) {
        avancer(a);
        Noeud *x = expression_avant(a, "à", "au");
        if (!x) return NULL;
        if (!complement(a, 'a')) { noeud_liberer(x); return NULL; }
        Noeud *y = expression(a);
        a->article_force = ART_AUCUN;
        if (!y) { noeud_liberer(x); return NULL; }
        Noeud *n = noeud_creer(N_INTERVALLE, t->ligne, t->colonne, t->debut);
        noeud_ajouter(n, x);
        noeud_ajouter(n, y);
        n->fin = y->fin;
        return n;
    }
    for (size_t k = 0; k < NB_RELATIONS; k++)
        if (est_mot(t, RELATIONS[k].m) || est_mot(t, RELATIONS[k].f))
            return relation(a, sujet(t, case_sujet), 0, t);
    Noeud *v = expression(a);
    if (!v) return NULL;
    Noeud *n = noeud_creer(N_COMPARAISON, v->ligne, v->colonne, v->debut);
    n->op = '=';
    n->forme = 2;   /* égalité implicite d'un cas */
    n->op_ligne = v->ligne;
    n->op_colonne = v->colonne;
    noeud_ajouter(n, sujet(t, case_sujet));
    noeud_ajouter(n, v);
    n->fin = v->fin;
    return n;
}

/* Selon valeur : puis, indentés, des « Cas … » et un « Autrement » final facultatif. */
static Noeud *selon(Analyse *a, int colonne) {
    Jeton *t = cour(a);
    avancer(a);
    Noeud *s = valeur(a);
    if (!s) return NULL;
    attendre_en(a, a->i, 0);
    attendre_mot(a, a->i, ":", 1);
    if (cour(a)->type != J_DEUX_POINTS) {
        noeud_liberer(s);
        return erreur_inattendu(a, cour(a));
    }
    Jeton *dp = cour(a);
    avancer(a);
    Jeton *premier = cour(a);
    if (premier->type == J_FIN || premier->ligne == dp->ligne || premier->retrait <= colonne) {
        noeud_liberer(s);
        return erreur(a, premier->type == J_FIN || premier->ligne == dp->ligne ? dp : premier,
                      grym_dupliquer("« Selon » sans cas : écrivez les « Cas … » sur les lignes suivantes, indentés."));
    }
    int c = premier->retrait;
    Noeud *n = noeud_creer(P_SELON, t->ligne, t->colonne, t->debut);
    n->entier = a->nb_locaux++;
    noeud_ajouter(n, s);
    int autrement = 0;
    for (;;) {
        Jeton *u = cour(a);
        if (u->type == J_FIN) break;
        if (u->type == J_REMARQUE) {
            noeud_ajouter(n, feuille(P_REMARQUE, u));
            avancer(a);
            continue;
        }
        if (!premier_de_ligne(a, a->i) || u->retrait < c) break;
        if (u->retrait > c) {
            noeud_liberer(n);
            return erreur(a, u, grym_dupliquer("Indentation inattendue : les cas s'alignent les uns sous les autres."));
        }
        attendre_mot(a, a->i, "Cas", 3);
        attendre_mot(a, a->i, "Autrement", 9);
        Noeud *cas;
        if (est_mot(u, "cas")) {
            if (autrement) {
                noeud_liberer(n);
                return erreur(a, u, grym_dupliquer("« Autrement » vient après tous les cas."));
            }
            avancer(a);
            cas = noeud_creer(N_CAS, u->ligne, u->colonne, u->debut);
            for (;;) {
                Noeud *e = element_cas(a, n->entier);
                if (!e) { noeud_liberer(cas); noeud_liberer(n); return NULL; }
                noeud_ajouter(cas, e);
                attendre_mot(a, a->i, "ou", 2);
                if (!est_mot(cour(a), "ou")) break;
                avancer(a);
            }
        } else if (est_mot(u, "autrement")) {
            if (autrement) {
                noeud_liberer(n);
                return erreur(a, u, grym_dupliquer("Un seul « Autrement » par « Selon »."));
            }
            autrement = 1;
            avancer(a);
            cas = noeud_creer(N_CAS, u->ligne, u->colonne, u->debut);
            cas->forme = 1;
        } else {
            noeud_liberer(n);
            return erreur(a, u, grym_dupliquer("« Cas » ou « Autrement » attendu dans un « Selon »."));
        }
        int forme = 0;
        Noeud *corps = branche(a, c, est_mot(u, "cas") ? "Cas" : "Autrement", &forme);
        if (!corps) { noeud_liberer(cas); noeud_liberer(n); return NULL; }
        noeud_ajouter(cas, corps);
        cas->fin = corps->fin;
        noeud_ajouter(n, cas);
        n->fin = corps->fin;
    }
    return n;
}

/* ---------------------------------------------------------------- */
/* Classes et objets (§ 13)                                         */
/* ---------------------------------------------------------------- */

/* Liste des champs « un nom, une date. » ; base : classe (avec lignée et aptitudes) dont
 * les champs ne peuvent pas être repris, ou NULL. */
/* « (texte) », « (nombre entier) », « (client) » : type d'un champ d'entité (§ 16.1). soi : l'entité
 * en cours de déclaration, qui peut se désigner elle-même (« un parrain (client) »). */
static char *lire_type(Analyse *a, const char *soi) {
    Jeton *ouv = cour(a);
    avancer(a);
    size_t d = a->i, k = d;
    while (a->j[k].type == J_MOT || a->j[k].type == J_CROCHETS || a->j[k].type == J_ELISION) k++;
    if (k == d || a->j[k].type != J_PAR_FERM) {
        erreur(a, a->j[k].type == J_PAR_FERM ? ouv : &a->j[k], grym_dupliquer(
            "Type attendu entre parenthèses : texte, nombre, nombre entier, vrai ou faux, date, fichier, image, "
            "ou le nom d'une entité."));
        return NULL;
    }
    Chaine c = {0};
    for (size_t q = d; q < k; q++) {
        if (q > d && a->j[q - 1].type != J_ELISION) chaine_ajouter(&c, " ");
        chaine_ajouter(&c, a->j[q].valeur);
        if (a->j[q].type == J_ELISION) chaine_ajouter(&c, "'");
    }
    char *type = chaine_rendre(&c);
    int connu = 0;
    for (size_t q = 0; q < sizeof TYPES / sizeof *TYPES; q++) connu |= strcmp(type, TYPES[q]) == 0;
    if (!connu) {
        const Classe *e = classe_de(a->portee, type);
        if ((soi && strcmp(type, soi) == 0) || (e && e->conserve)) connu = 1;
        else {
            erreur(a, &a->j[d], e ? grym_formater("« %s » n'est pas une entité : un lien pointe vers une entité "
                                                  "conservée.", type)
                                  : grym_formater("Type « %s » inconnu : texte, nombre, nombre entier, vrai ou faux, "
                                                  "date, fichier, image, ou le nom d'une entité.", type));
            free(type);
            return NULL;
        }
    }
    a->i = k + 1;
    return type;
}

static Noeud *unaire(Analyse *a);

static int est_fichier_type(const char *t) {
    return t && (strcmp(t, "fichier") == 0 || strcmp(t, "image") == 0);
}

/* Champs : mode 0 classe ordinaire (sans type), 1 entité (type obligatoire, « unique » permis),
 * 2 aptitude (type facultatif). */
static int lire_champs(Analyse *a, Noeud *n, const Classe *base, int mode, const char *soi) {
    for (;;) {
        Jeton *u = cour(a);
        Genre gc;
        attendre_mot(a, a->i, "un", 2);
        attendre_mot(a, a->i, "une", 3);
        if (!est_un(u, &gc)) {
            erreur(a, u, grym_dupliquer("Champ attendu : « un nom », « une date »."));
            return 0;
        }
        avancer(a);
        size_t dc = a->i, kc = dc;
        if (a->j[kc].type == J_CROCHETS) kc++;
        else while (mot_de_nom(a, kc)) kc++;
        if (kc == dc) {
            erreur(a, cour(a), grym_dupliquer("Nom de champ attendu après « un »."));
            return 0;
        }
        if (article_de(&a->j[dc]) != ART_AUCUN) {
            erreur(a, &a->j[dc], grym_dupliquer("Un nom de champ ne commence pas par un article."));
            return 0;
        }
        char *champ = a->j[dc].type == J_CROCHETS ? grym_dupliquer(a->j[dc].valeur) : cle(a, dc, kc);
        char *probleme = NULL;
        Genre autre;
        for (size_t q = 0; q < n->nb_enfants && !probleme; q++)
            if (strcmp(n->enfants[q]->texte, champ) == 0) probleme = grym_formater("Champ « %s » déjà nommé.", champ);
        Symbole *sy = visible(a, champ);
        if (!probleme && sy && sy->sorte != S_VARIABLE)
            probleme = grym_formater("« %s » est %s : un champ ne peut pas porter ce nom.", champ,
                                     sy->sorte == S_CALCUL ? "un calcul" : "une action");
        const Classe *origine = NULL;
        if (!probleme && base && classe_champ(a->portee, base, champ, NULL, &origine))
            probleme = grym_formater("« %s » est déjà un champ %s « %s ».", champ,
                                     origine->aptitude ? "de l'aptitude" : "hérité de", origine->nom);
        if (!probleme && champ_connu(a->portee, champ, &autre) && autre != gc)
            probleme = grym_formater("« %s » est déjà un champ %s dans une autre classe.", champ,
                                     autre == GENRE_MASCULIN ? "masculin" : "féminin");
        if (probleme) {
            free(champ);
            erreur(a, &a->j[dc], probleme);
            return 0;
        }
        Noeud *c = noeud_creer(N_NOM, u->ligne, u->colonne, u->debut);
        c->texte = champ;
        c->forme = gc == GENRE_FEMININ ? 2 : 1;
        noeud_ajouter(n, c);
        a->i = kc;
        if (cour(a)->type == J_PAR_OUV) {
            if (mode == 0) {
                erreur(a, cour(a), grym_dupliquer("Seuls les champs d'une entité ou d'une aptitude ont un type : "
                                                  "« Un client, conservé, a : »."));
                return 0;
            }
            c->texte2 = lire_type(a, soi);
            if (!c->texte2) return 0;
        } else if (mode == 1) {
            erreur(a, cour(a), grym_formater("Type attendu entre parenthèses : « un %s (texte) ».", champ));
            return 0;
        }
        for (;;) {
            if (cour(a)->type == J_VIRGULE && (est_mot(voir(a, 1), "facultatif") || est_mot(voir(a, 1), "facultative"))
                && !(c->entier & 1)) {
                /* « , facultatif » : le champ peut rester absent (§ 16.9) */
                const Jeton *tf = voir(a, 1);
                if (!tf->synthetique && est_mot(tf, "facultative") != (gc == GENRE_FEMININ)) {
                    erreur(a, tf, grym_formater("Accord : « %s ».", gc == GENRE_FEMININ ? "facultative" : "facultatif"));
                    return 0;
                }
                c->entier |= 1;
                avancer(a);
                avancer(a);
                continue;
            }
            if (cour(a)->type == J_VIRGULE && est_mot(voir(a, 1), "et") && est_mot(voir(a, 2), "disparaît")
                && est_mot(voir(a, 3), "avec") && (est_mot(voir(a, 4), "elle") || est_mot(voir(a, 4), "lui"))
                && !(c->entier & 2)) {
                /* « , et disparaît avec elle » : supprimé avec l'objet que le lien désigne (§ 16.12) */
                const Jeton *pron = voir(a, 4);
                if (!c->texte2 || type_de_base(c->texte2) || mode != 1) {
                    erreur(a, voir(a, 2), grym_dupliquer("Seul un lien d'entité « disparaît avec » l'objet qu'il désigne."));
                    return 0;
                }
                const Classe *cible = classe_de(a->portee, c->texte2);
                if (cible && !pron->synthetique && est_mot(pron, "elle") != (cible->genre == GENRE_FEMININ)) {
                    erreur(a, pron, grym_formater("Accord : « avec %s ».", cible->genre == GENRE_FEMININ ? "elle" : "lui"));
                    return 0;
                }
                c->entier |= 2;
                for (int q = 0; q < 5; q++) avancer(a);
                continue;
            }
            if (cour(a)->type == J_VIRGULE && est_mot(voir(a, 1), "unique") && c->op != 'U') {
                if (mode != 1) {
                    erreur(a, voir(a, 1), grym_dupliquer("Seul un champ d'entité est unique."));
                    return 0;
                }
                c->op = 'U';
                avancer(a);
                avancer(a);
                continue;
            }
            /* « , « Suisse » au départ » : valeur des objets déjà conservés quand le champ apparaît (§ 16.7) */
            size_t q = a->i + 1;
            if (a->j[q].type == J_MOINS) q++;
            int litteral = a->j[q].type == J_TEXTE || a->j[q].type == J_NOMBRE || a->j[q].type == J_DATE
                           || est_mot(&a->j[q], "vrai") || est_mot(&a->j[q], "faux");
            if (cour(a)->type == J_VIRGULE && litteral && est_mot(&a->j[q + 1], "au") && est_mot(&a->j[q + 2], "départ")
                && !c->nb_enfants) {
                if (mode != 1) {
                    erreur(a, &a->j[q], grym_dupliquer("Seul un champ d'entité a une valeur de départ."));
                    return 0;
                }
                avancer(a);
                Noeud *v;
                if (est_mot(cour(a), "vrai") || est_mot(cour(a), "faux")) {
                    v = feuille(N_BOOLEEN, cour(a));
                    avancer(a);
                } else {
                    v = unaire(a);
                }
                if (!v) return 0;
                if (!verifier_type(a, champ, c->texte2, v)) { noeud_liberer(v); return 0; }
                if (est_fichier_type(c->texte2) || !type_de_base(c->texte2)) {
                    erreur(a, cour(a), grym_formater("« %s » : un fichier ou un lien n'a pas de valeur de départ.", champ));
                    noeud_liberer(v);
                    return 0;
                }
                noeud_ajouter(c, v);
                avancer(a);
                avancer(a);
                continue;
            }
            break;
        }
        attendre(a, A_POINT);
        attendre_mot(a, a->i, ",", 1);
        if (cour(a)->type == J_VIRGULE) { avancer(a); continue; }
        if (cour(a)->type == J_POINT) { avancer(a); break; }
        erreur_inattendu(a, cour(a));
        return 0;
    }
    return 1;
}

/* Nouvelle classe dans la portée (conservée d'une saisie à l'autre). */
static Classe *ajouter_classe(Portee *p, const char *nom, Genre g, const char *parent) {
    Classe *t = grym_allouer((p->nb_classes + 1) * sizeof *t);
    if (p->nb_classes) memcpy(t, p->classes, p->nb_classes * sizeof *t);
    free(p->classes);
    p->classes = t;
    Classe *c = &p->classes[p->nb_classes++];
    c->nom = grym_dupliquer(nom);
    c->genre = g;
    c->parent = parent ? grym_dupliquer(parent) : NULL;
    c->aptitude = 0;
    c->masculin = NULL;
    c->aptitudes = NULL;
    c->nb_aptitudes = 0;
    c->ligne = 0;
    c->conserve = 0;
    c->pluriel = NULL;
    c->champs = NULL;
    c->genres = NULL;
    c->types = NULL;
    c->uniques = NULL;
    c->facultatifs = NULL;
    c->nb = 0;
    return c;
}

/* Champs d'une classe ou d'une aptitude, pris dans sa déclaration (N_NOM ; les N_TEXTE sont des aptitudes). */
static void classe_remplir(Classe *c, const Noeud *n) {
    classe_champs_liberer(c);
    size_t nb = 0;
    for (size_t q = 0; q < n->nb_enfants; q++) nb += n->enfants[q]->type == N_NOM;
    c->champs = grym_allouer((nb ? nb : 1) * sizeof *c->champs);
    c->genres = grym_allouer((nb ? nb : 1) * sizeof *c->genres);
    c->types = grym_allouer((nb ? nb : 1) * sizeof *c->types);
    c->uniques = grym_allouer((nb ? nb : 1) * sizeof *c->uniques);
    c->facultatifs = grym_allouer((nb ? nb : 1) * sizeof *c->facultatifs);
    for (size_t q = 0; q < n->nb_enfants; q++) {
        const Noeud *ch = n->enfants[q];
        if (ch->type != N_NOM) continue;
        c->champs[c->nb] = grym_dupliquer(ch->texte);
        c->genres[c->nb] = ch->forme == 2 ? GENRE_FEMININ : GENRE_MASCULIN;
        c->types[c->nb] = ch->texte2 ? grym_dupliquer(ch->texte2) : NULL;
        c->uniques[c->nb] = ch->op == 'U';
        c->facultatifs[c->nb] = ch->entier & 1;
        c->nb++;
    }
}

/* Forme masculine régulière : « horodatée » → « horodaté ». */
static char *masculin_regulier(const char *feminin) {
    size_t l = strlen(feminin);
    if (l > 1 && feminin[l - 1] == 'e') return grym_formater("%.*s", (int)(l - 1), feminin);
    return grym_dupliquer(feminin);
}

/* « Une chose horodatée a : » ; forme masculine irrégulière entre parenthèses : « Une chose active (actif) a : ». */
static Noeud *definition_aptitude(Analyse *a, const Jeton *tun) {
    if (!tun->synthetique && !est_mot(tun, "une"))
        return erreur(a, tun, grym_dupliquer("« chose » est féminin : « Une chose horodatée a : »."));
    avancer(a);   /* chose */
    Jeton *tadj = cour(a);
    if (!(tadj->type == J_CROCHETS || (tadj->type == J_MOT && !est_reserve(tadj) && !est_mot(tadj, "a"))))
        return erreur(a, tadj, grym_dupliquer("Adjectif attendu : « Une chose horodatée a : »."));
    char *fem = grym_dupliquer(tadj->valeur);
    avancer(a);
    char *masc = NULL;
    if (cour(a)->type == J_PAR_OUV) {
        Jeton *tm = voir(a, 1);
        if (!(tm->type == J_MOT || tm->type == J_CROCHETS) || voir(a, 2)->type != J_PAR_FERM) {
            free(fem);
            return erreur(a, cour(a), grym_dupliquer("Forme masculine attendue entre parenthèses : « (actif) »."));
        }
        masc = grym_dupliquer(tm->valeur);
        avancer(a); avancer(a); avancer(a);
    }
    char *probleme = NULL;
    if (aptitude_de(a->portee, fem, NULL) || (masc && aptitude_de(a->portee, masc, NULL)))
        probleme = grym_formater("L'aptitude « %s » existe déjà.", fem);
    else if (classe_de(a->portee, fem))
        probleme = grym_formater("« %s » est déjà une classe.", fem);
    if (!probleme && (!est_mot(cour(a), "a") || voir(a, 1)->type != J_DEUX_POINTS)) {
        free(fem);
        free(masc);
        attendre_mot(a, a->i, "a :", 3);
        return erreur_inattendu(a, cour(a));
    }
    if (probleme) { free(fem); free(masc); return erreur(a, tadj, probleme); }
    avancer(a); avancer(a);
    Noeud *n = noeud_creer(P_APTITUDE, tun->ligne, tun->colonne, tun->debut);
    n->texte = fem;
    n->texte2 = masc;
    if (!lire_champs(a, n, NULL, 2, NULL)) { noeud_liberer(n); return NULL; }
    Classe *c = ajouter_classe(a->portee, fem, GENRE_FEMININ, NULL);
    c->aptitude = 1;
    c->masculin = masc ? grym_dupliquer(masc) : masculin_regulier(fem);
    c->ligne = tun->ligne;
    classe_remplir(c, n);
    return n;
}

/* « Un membre est une personne. » (§ 13.5) */
static Noeud *heritage(Analyse *a, const Jeton *tun, Genre g, char *nom, size_t k, int conserve, const char *pluriel) {
    a->i = k + 1;   /* après « est » */
    Jeton *tp = cour(a);
    Genre gp;
    attendre_mot(a, a->i, "un", 2);
    attendre_mot(a, a->i, "une", 3);
    if (!est_un(tp, &gp)) { free(nom); return erreur_inattendu(a, tp); }
    avancer(a);
    size_t d = a->i, f = d;
    Classe *parent = NULL;
    int chose = est_mot(&a->j[d], "chose");
    if (chose) {
        f = d + 1;
        if (!tp->synthetique && gp != GENRE_FEMININ) {
            free(nom);
            return erreur(a, tp, grym_dupliquer("« chose » est féminin : écrivez « une chose »."));
        }
    } else if (a->j[d].type == J_CROCHETS) {
        parent = classe_de(a->portee, a->j[d].valeur);
        f = d + 1;
    } else {
        size_t q = d;
        while (q < a->n && mot_de_nom(a, q)) q++;
        for (f = q; f > d; f--) {
            char *c = cle(a, d, f);
            parent = classe_de(a->portee, c);
            free(c);
            if (parent) break;
        }
    }
    if (!parent && !chose && !(a->j[d].type == J_CROCHETS || mot_de_nom(a, d))) {
        free(nom);
        return erreur(a, &a->j[d], grym_dupliquer("Classe parente attendue : « Un membre est une personne. »."));
    }
    if (!parent && !chose) {
        size_t q = d + 1;
        while (q < a->n && mot_de_nom(a, q)) q++;
        char *p = a->j[d].type == J_CROCHETS ? grym_dupliquer(a->j[d].valeur) : cle(a, d, q);
        erreur(a, &a->j[d], grym_formater("Classe « %s » inconnue : une classe hérite d'une classe déjà déclarée.", p));
        free(p);
        free(nom);
        return NULL;
    }
    if (parent && parent->conserve != conserve) {
        erreur(a, &a->j[d], parent->conserve
            ? grym_formater("« %s » est une entité : écrivez « %s %s, %s, est %s %s. ».", parent->nom,
                            g == GENRE_FEMININ ? "Une" : "Un", nom, g == GENRE_FEMININ ? "conservée" : "conservé",
                            parent->genre == GENRE_FEMININ ? "une" : "un", parent->nom)
            : grym_formater("« %s » n'est pas une entité : une entité hérite d'une entité.", parent->nom));
        free(nom);
        return NULL;
    }
    if (parent && !tp->synthetique && gp != parent->genre) {
        erreur(a, tp, grym_formater("« %s » est %s : écrivez « %s %s ».", parent->nom,
                                    parent->genre == GENRE_FEMININ ? "féminin" : "masculin",
                                    parent->genre == GENRE_FEMININ ? "une" : "un", parent->nom));
        free(nom);
        return NULL;
    }
    /* Aptitudes adoptées : adjectifs accordés au nom qu'ils suivent (§ 13.7). */
    a->i = f;
    Genre gn = chose ? GENRE_FEMININ : parent->genre;
    Noeud *n = noeud_creer(P_CLASSE, tun->ligne, tun->colonne, tun->debut);
    n->texte = nom;
    n->texte2 = parent ? grym_dupliquer(parent->nom) : NULL;
    n->texte3 = pluriel ? grym_dupliquer(pluriel) : NULL;
    n->forme = (g == GENRE_FEMININ ? 2 : 1) | 16 | (conserve ? 32 : 0);
    for (;;) {
        Jeton *t = cour(a);
        if (!(t->type == J_CROCHETS || (t->type == J_MOT && !est_reserve(t)))) break;
        int feminin;
        Classe *ap = aptitude_de(a->portee, t->valeur, &feminin);
        char *probleme = NULL;
        if (!ap) probleme = grym_formater("Aptitude « %s » inconnue.", t->valeur);
        else if (!t->synthetique && (gn == GENRE_FEMININ) != feminin)
            probleme = grym_formater("Accord : « %s %s ».", chose ? "chose" : parent->nom,
                                     gn == GENRE_FEMININ ? ap->nom : ap->masculin);
        for (size_t q = 0; ap && q < n->nb_enfants && !probleme; q++)
            if (strcmp(n->enfants[q]->texte, ap->nom) == 0)
                probleme = grym_formater("Aptitude « %s » adoptée deux fois.", ap->nom);
        for (size_t q = 0; ap && conserve && q < ap->nb && !probleme; q++)
            if (!ap->types[q])
                probleme = grym_formater("L'aptitude « %s » a un champ sans type (« %s ») : une entité ne l'adopte "
                                         "pas.", ap->nom, ap->champs[q]);
        if (probleme) { noeud_liberer(n); return erreur(a, t, probleme); }
        Noeud *x = noeud_creer(N_TEXTE, t->ligne, t->colonne, t->debut);
        x->texte = grym_dupliquer(ap->nom);
        noeud_ajouter(n, x);
        avancer(a);
        if (cour(a)->type == J_VIRGULE || est_mot(cour(a), "et")) { avancer(a); continue; }
        break;
    }
    if (chose && !n->nb_enfants) {
        noeud_liberer(n);
        return erreur(a, cour(a), grym_dupliquer("Aptitude attendue : « Un document est une chose horodatée. »."));
    }
    /* Deux sources ne fournissent pas le même champ. */
    for (size_t q = 0; q < n->nb_enfants; q++) {
        Classe *ap = aptitude_de(a->portee, n->enfants[q]->texte, NULL);
        for (size_t k = 0; k < ap->nb; k++) {
            const Classe *origine = NULL;
            int deja = parent && classe_champ(a->portee, parent, ap->champs[k], NULL, &origine);
            for (size_t r = 0; r < q && !deja; r++) {
                Classe *autre = aptitude_de(a->portee, n->enfants[r]->texte, NULL);
                if (champ_propre(autre, ap->champs[k], NULL)) { deja = 1; origine = autre; }
            }
            if (deja) {
                erreur(a, &a->j[a->i - 1], grym_formater("« %s » : l'aptitude « %s » apporte un champ que « %s » a déjà.",
                                                         ap->champs[k], ap->nom, origine->nom));
                noeud_liberer(n);
                return NULL;
            }
        }
    }
    if (!fin_phrase(a, 0)) { noeud_liberer(n); return NULL; }
    Classe *c = ajouter_classe(a->portee, nom, g, n->texte2);   /* l'ajout déplace le tableau des classes */
    c->ligne = tun->ligne;
    c->conserve = conserve;
    c->pluriel = pluriel ? grym_dupliquer(pluriel) : NULL;
    c->nb_aptitudes = n->nb_enfants;
    c->aptitudes = n->nb_enfants ? grym_allouer(n->nb_enfants * sizeof *c->aptitudes) : NULL;
    for (size_t q = 0; q < n->nb_enfants; q++) c->aptitudes[q] = grym_dupliquer(n->enfants[q]->texte);
    free(a->a_completer);
    a->a_completer = grym_dupliquer(nom);   /* ses champs peuvent suivre, à la phrase suivante */
    return n;
}

/* « Un client a : un nom, un solde. » */
static Noeud *definition_classe(Analyse *a) {
    Jeton *tun = cour(a);
    Genre g = est_mot(tun, "une") ? GENRE_FEMININ : GENRE_MASCULIN;
    if (!premier_niveau(a, tun)) return NULL;
    avancer(a);
    if (est_mot(cour(a), "chose")) return definition_aptitude(a, tun);
    size_t d = a->i, k = d;
    if (a->j[k].type == J_CROCHETS) k++;
    else while (mot_de_nom(a, k) && !est_mot(&a->j[k], "a")) k++;
    if (k == d)
        return erreur(a, cour(a), grym_dupliquer("Nom de classe attendu : « Un client a : »."));
    size_t fin_nom = k;
    /* « Un cheval (chevaux), conservé, a : » : pluriel irrégulier, puis entité (§ 16.1) */
    const Jeton *tpluriel = NULL, *tconserve = NULL;
    if (a->j[k].type == J_PAR_OUV && (a->j[k + 1].type == J_MOT || a->j[k + 1].type == J_CROCHETS)
        && a->j[k + 2].type == J_PAR_FERM) {
        tpluriel = &a->j[k + 1];
        k += 3;
    }
    if (a->j[k].type == J_VIRGULE && (est_mot(&a->j[k + 1], "conservé") || est_mot(&a->j[k + 1], "conservée"))
        && a->j[k + 2].type == J_VIRGULE) {
        tconserve = &a->j[k + 1];
        k += 3;
    }
    int herite = est_mot(&a->j[k], "est");
    if (!herite && (!est_mot(&a->j[k], "a") || a->j[k + 1].type != J_DEUX_POINTS)) {
        a->i = k;
        attendre_mot(a, k, "a :", 3);
        attendre_mot(a, k, "est", 3);
        if (!tconserve && a->j[k].type == J_VIRGULE) attendre_mot(a, k, ", conservé,", 3);
        return erreur_inattendu(a, &a->j[k]);
    }
    if (article_de(&a->j[d]) != ART_AUCUN)
        return erreur(a, &a->j[d], grym_dupliquer("Un nom de classe ne commence pas par un article."));
    if (tpluriel && !tconserve)
        return erreur(a, tpluriel, grym_dupliquer("Seule une entité déclare son pluriel : « Un cheval (chevaux), conservé, a : »."));
    if (tconserve && !tconserve->synthetique && est_mot(tconserve, "conservée") != (g == GENRE_FEMININ))
        return erreur(a, tconserve, grym_formater("Accord : « %s ».", g == GENRE_FEMININ ? "conservée" : "conservé"));
    char *nom = a->j[d].type == J_CROCHETS ? grym_dupliquer(a->j[d].valeur) : cle(a, d, fin_nom);
    if (aptitude_de(a->portee, nom, NULL)) {
        erreur(a, &a->j[d], grym_formater("« %s » est une aptitude, pas une classe.", nom));
        free(nom);
        return NULL;
    }
    Classe *existante = classe_de(a->portee, nom);
    int complement = existante && !herite && a->a_completer && strcmp(a->a_completer, nom) == 0
                     && existante->nb == 0;
    if (existante && !complement) {
        erreur(a, &a->j[d], grym_formater("La classe « %s » existe déjà.", nom));
        free(nom);
        return NULL;
    }
    if (complement && (tconserve || tpluriel)) {
        erreur(a, tconserve ? tconserve : tpluriel, grym_formater(
            "« %s » est déjà déclaré : ses champs suivent avec « %s %s a : ».", nom, g == GENRE_FEMININ ? "Une" : "Un", nom));
        free(nom);
        return NULL;
    }
    if (herite) return heritage(a, tun, g, nom, k, tconserve != NULL, tpluriel ? tpluriel->valeur : NULL);
    if (complement && existante->genre != g && !tun->synthetique) {
        erreur(a, tun, grym_formater("« %s » est %s.", nom, existante->genre == GENRE_FEMININ ? "féminin" : "masculin"));
        free(nom);
        return NULL;
    }
    a->i = k + 2;
    Noeud *n = noeud_creer(P_CLASSE, tun->ligne, tun->colonne, tun->debut);
    n->texte = nom;
    int conserve = complement ? existante->conserve : tconserve != NULL;
    n->forme = (g == GENRE_FEMININ ? 2 : 1) | (complement ? 4 : 0) | (conserve ? 32 : 0);
    if (tpluriel) n->texte3 = grym_dupliquer(tpluriel->valeur);

    if (!lire_champs(a, n, complement ? existante : NULL, conserve, nom)) { noeud_liberer(n); return NULL; }
    /* La classe entre dans la portée ; un complément remplit la classe déclarée par « est ». */
    Classe *c = complement ? existante : ajouter_classe(a->portee, n->texte, g, NULL);
    c->conserve = conserve;
    if (tpluriel) c->pluriel = grym_dupliquer(tpluriel->valeur);
    classe_remplir(c, n);
    return n;
}

/* Bloc qui initialise un nouvel objet : « Le nom vaut « Dupont ». » (§ 13.2). */
static int bloc_initialisation(Analyse *a, Noeud *nv, const Jeton *tphrase) {
    Jeton *dp = cour(a);
    avancer(a);
    Jeton *premier = cour(a);
    if (premier->type == J_FIN || premier->ligne == dp->ligne || premier->retrait <= tphrase->retrait) {
        erreur(a, premier->type == J_FIN || premier->ligne == dp->ligne ? dp : premier, grym_dupliquer(
            "Bloc vide : après « : », initialisez les champs sur les lignes suivantes, indentées."));
        return 0;
    }
    Classe *cl = classe_de(a->portee, nv->texte);
    int c = premier->retrait;
    for (;;) {
        Jeton *u = cour(a);
        if (u->type == J_FIN || !premier_de_ligne(a, a->i) || u->retrait < c) break;
        if (u->retrait > c) {
            erreur(a, u, grym_dupliquer("Indentation inattendue : les champs s'alignent les uns sous les autres."));
            return 0;
        }
        Article art = article_de(u);
        if (art == ART_AUCUN) {
            erreur(a, u, grym_formater("Initialisation attendue : « Le %s vaut … ».", cl->nb ? cl->champs[0] : "champ"));
            return 0;
        }
        avancer(a);
        size_t d = a->i, k = d;
        if (a->j[k].type == J_CROCHETS) k++;
        else while (mot_de_nom(a, k)) k++;
        char *champ = a->j[d].type == J_CROCHETS ? grym_dupliquer(a->j[d].valeur) : cle(a, d, k);
        Genre gchamp = GENRE_LIBRE;
        if (!classe_champ(a->portee, cl, champ, &gchamp, NULL)) {
            erreur(a, &a->j[d], grym_formater("%s « %s » n'a pas de champ « %s ».",
                                              cl->genre == GENRE_FEMININ ? "Une" : "Un", cl->nom, champ));
            free(champ);
            return 0;
        }
        for (size_t r = 0; r < nv->nb_enfants; r++)
            if (strcmp(nv->enfants[r]->texte, champ) == 0) {
                erreur(a, &a->j[d], grym_formater("Champ « %s » déjà initialisé.", champ));
                free(champ);
                return 0;
            }
        if (!u->synthetique && (art == ART_LE || art == ART_LA) && genre_de(art) != gchamp) {
            erreur(a, u, grym_formater("« %s » est %s.", champ, gchamp == GENRE_FEMININ ? "féminin" : "masculin"));
            free(champ);
            return 0;
        }
        a->i = k;
        static const char *const VAUT[] = { "vaut" };
        if (!mots_fixes(a, VAUT, 1)) { free(champ); return 0; }
        Noeud *v = valeur(a);
        if (!v) { free(champ); return 0; }
        if (!verifier_type(a, champ, type_du_champ(a->portee, cl, champ), v) || !accorder_absent(a, v, gchamp)
            || !fin_phrase(a, 0)) { free(champ); noeud_liberer(v); return 0; }
        Noeud *init = noeud_creer(N_INIT, u->ligne, u->colonne, u->debut);
        init->texte = champ;
        init->article = art == ART_IMPLICITE ? ART_AUCUN : art;
        noeud_ajouter(init, v);
        init->fin = v->fin;
        noeud_ajouter(nv, init);
    }
    nv->forme = 1;
    return 1;
}

/* Mots qui commencent une construction et ne peuvent donc pas commencer le nom d'une action. */
static int mot_de_construction(const Jeton *t) {
    static const char *const M[] = { "tant", "répéter", "chaque", "sortir", "passer", "selon", "cas",
                                     "autrement", "afficher", "si", "sinon", "pour", "rendre", "enregistrer",
                                     "conserver", "supprimer", "rétablir" };
    for (size_t k = 0; k < sizeof M / sizeof *M; k++) if (est_mot(t, M[k])) return 1;
    return 0;
}

static Noeud *phrase(Analyse *a, int colonne) {
    Jeton *t = cour(a);
    attendre(a, A_DEBUT | (a->interactif ? A_VALEUR | A_BOOLEEN : 0) | (a->boucle ? A_BOUCLE : 0));

    if (t->type == J_REMARQUE) {
        Noeud *n = feuille(P_REMARQUE, t);
        avancer(a);
        return n;
    }
    if (est_mot(t, "afficher")) {
        if (a->formule == 1)
            return erreur(a, t, grym_dupliquer("Un calcul n'affiche rien : il rend une valeur. "
                                               "Pour afficher, écrivez une action."));
        return affichage(a);
    }
    if (est_mot(t, "conserver") || est_mot(t, "supprimer") || est_mot(t, "rétablir")) {
        /* « Conserver le client. », « Supprimer le client [définitivement]. », « Rétablir le client. » (§ 16.3, 16.12) */
        int conserver = est_mot(t, "conserver"), retablir = est_mot(t, "rétablir");
        if (a->formule == 1)
            return erreur(a, t, grym_formater("Un calcul ne %s pas : faites-le dans une action.",
                                              conserver ? "conserve" : retablir ? "rétablit" : "supprime"));
        avancer(a);
        Noeud *v = valeur(a);
        if (!v) return NULL;
        int definitif = 0;
        if (!conserver && !retablir && est_mot(cour(a), "définitivement")) { definitif = 1; avancer(a); }
        if (!fin_phrase(a, 0)) { noeud_liberer(v); return NULL; }
        Noeud *n = noeud_creer(conserver ? P_CONSERVER : P_SUPPRIMER, t->ligne, t->colonne, t->debut);
        n->entier = retablir ? 2 : definitif;   /* 0 : mise de côté ; 1 : définitive ; 2 : rétablir */
        noeud_ajouter(n, v);
        n->fin = v->fin;
        return n;
    }
    if (est_mot(t, "enregistrer")) {
        /* « Enregistrer … dans « chemin ». » (§ 15.2) */
        if (a->formule == 1)
            return erreur(a, t, grym_dupliquer("Un calcul n'écrit pas sur le disque : enregistrez dans une action."));
        avancer(a);
        Noeud *v = expression_avant(a, "dans", NULL);
        if (!v) return NULL;
        static const char *const DANS[] = { "dans" };
        if (!mots_fixes(a, DANS, 1)) { noeud_liberer(v); return NULL; }
        Noeud *chemin = valeur(a);
        if (!chemin) { noeud_liberer(v); return NULL; }
        if (!fin_phrase(a, 0)) { noeud_liberer(v); noeud_liberer(chemin); return NULL; }
        Noeud *n = noeud_creer(P_ENREGISTRER, t->ligne, t->colonne, t->debut);
        noeud_ajouter(n, v);
        noeud_ajouter(n, chemin);
        n->fin = chemin->fin;
        return n;
    }
    if (est_mot(t, "si")) return si(a, colonne, 0);
    if (est_mot(t, "tant") && est_mot(voir(a, 1), "que")) return tant_que(a, colonne);
    if (est_mot(t, "répéter")) return repeter(a, colonne);
    if (est_mot(t, "pour") && est_mot(voir(a, 1), "chaque")) return pour_chaque(a, colonne);
    if (est_mot(t, "sortir") || est_mot(t, "passer")) return sortie_de_boucle(a);
    if (est_mot(t, "selon")) return selon(a, colonne);
    if (est_mot(t, "cas") || est_mot(t, "autrement"))
        return erreur(a, t, grym_formater("« %s » hors d'un « Selon ».", est_mot(t, "cas") ? "Cas" : "Autrement"));
    if (est_mot(t, "pour")) return definition_action(a, colonne);
    if ((est_mot(t, "un") || est_mot(t, "une")) && !est_nouveau(voir(a, 1))) return definition_classe(a);
    if (est_mot(t, "rendre")) {
        if (a->formule != 1)
            return erreur(a, t, grym_dupliquer(a->formule == 2
                ? "Une action ne rend rien : pour rendre une valeur, écrivez un calcul."
                : "« Rendre » ne s'emploie que dans un calcul."));
        avancer(a);
        Noeud *v = valeur(a);
        if (!v) return NULL;
        if (!fin_phrase(a, 0)) { noeud_liberer(v); return NULL; }
        Noeud *n = noeud_creer(P_RENDRE, t->ligne, t->colonne, t->debut);
        noeud_ajouter(n, v);
        n->fin = v->fin;
        return n;
    }
    if (est_mot(t, "sinon"))
        return erreur(a, t, grym_dupliquer("« Sinon » sans « Si » correspondant."));
    if (est_mot(t, "remarque") && voir(a, 1)->type == J_DEUX_POINTS)
        return erreur(a, t, grym_dupliquer(
            "Une remarque doit commencer une ligne : passez à la ligne avant « Remarque : »."));

    Article art = article_de(t);
    if (art != ART_AUCUN) {
        size_t k = a->i + 1, marque = 0;
        while (a->j[k].type != J_POINT && a->j[k].type != J_FIN && a->j[k].type != J_REMARQUE
               && a->j[k].type != J_DEUX_POINTS
               && !est_mot(&a->j[k], "vaut") && !est_mot(&a->j[k], "devient")) {
            if (!marque && marque_parametre(a, k)) marque = k;
            k++;
        }
        if (marque && (est_mot(&a->j[k], "vaut") || a->j[k].type == J_DEUX_POINTS))
            return definition_calcul(a, marque, k, colonne);
        if (marque && est_mot(&a->j[k], "devient"))
            return erreur(a, &a->j[k], grym_dupliquer("Un calcul ne se modifie pas : il se définit avec « vaut »."));
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

    size_t fin_action;
    Symbole *action = action_en_tete(a, &fin_action);
    if (t->type == J_CROCHETS) {
        Symbole *s = visible(a, t->valeur);
        if (s && s->sorte == S_ACTION) { action = s; fin_action = a->i + 1; }
    }
    if (action) return appel_action(a, action, fin_action);

    if (a->interactif) return phrase_expression(a);
    char *x = texte_jeton(t);
    char *m = grym_formater("« %s » ne peut pas commencer une phrase : une phrase commence par "
                            "Le, La, L', Afficher, Si, Pour ou le nom d'une action.", x);
    free(x);
    return erreur(a, t, m);
}

/* Suite de phrases alignées sur une même colonne (§ 5.4). À la racine, les noms
 * restent déclarés ; dans un bloc, ils disparaissent à la fin du bloc. */
static Noeud *bloc(Analyse *a, int colonne, int racine) {
    Jeton *t0 = cour(a);
    Noeud *b = noeud_creer(N_BLOC, t0->ligne, t0->colonne, t0->debut);
    size_t sauve = a->portee->n;
    if (!racine) a->niveau++;
    for (;;) {
        attendre(a, A_DEBUT | (a->interactif ? A_VALEUR | A_BOOLEEN : 0) | (a->boucle ? A_BOUCLE : 0));
        Jeton *t = cour(a);
        if (t->type == J_FIN) break;
        if (t->type == J_REMARQUE && !racine && premier_de_ligne(a, a->i) && t->retrait < colonne) {
            /* une remarque moins indentée, suivie d'une phrase moins indentée aussi, appartient au bloc
             * englobant : le bloc s'arrête avant elle (sinon, elle resterait dans le bloc qui se ferme) */
            size_t k = a->i;
            while (a->j[k].type == J_REMARQUE) k++;
            if (a->j[k].type == J_FIN || (premier_de_ligne(a, k) && a->j[k].retrait < colonne)) break;
        }
        if (t->type != J_REMARQUE && premier_de_ligne(a, a->i)) {
            if (t->retrait < colonne) {
                if (!racine) break;
                noeud_liberer(b);
                return erreur(a, t, grym_dupliquer(
                    "Indentation incohérente : cette ligne est moins indentée que le début du programme."));
            }
            if (t->retrait > colonne) {
                noeud_liberer(b);
                return erreur(a, t, grym_dupliquer(
                    "Indentation inattendue : seul un bloc ouvert par « : » s'indente."));
            }
        }
        if (est_mot(t, "sinon") && !racine) break;
        Noeud *p = phrase(a, colonne);
        if (!p) { noeud_liberer(b); if (!racine) a->niveau--; return NULL; }
        p->ligne_fin = a->i > 0 ? a->j[a->i - 1].ligne_fin : p->ligne;
        int champs = 0;
        for (size_t q = 0; p->type == P_CLASSE && q < p->nb_enfants; q++) champs += p->enfants[q]->type == N_NOM;
        if (!(p->type == P_CLASSE && (p->forme & 16) && champs == 0)) {
            free(a->a_completer);
            a->a_completer = NULL;
        }
        if (p->type == P_CLASSE && (p->forme & 4) && b->nb_enfants) {
            /* « Un membre a : … » complète « Un membre est une personne. » : une seule déclaration */
            Noeud *h = b->enfants[b->nb_enfants - 1];
            for (size_t q = 0; q < p->nb_enfants; q++) noeud_ajouter(h, p->enfants[q]);
            p->nb_enfants = 0;
            h->ligne_fin = p->ligne_fin;
            h->forme |= 8;   /* champs écrits dans une seconde phrase */
            noeud_liberer(p);
            continue;
        }
        noeud_ajouter(b, p);
        b->fin = p->fin;
    }
    if (!racine) {
        portee_tronquer(a->portee, sauve);
        a->niveau--;
    }
    return b;
}

/* ---------------------------------------------------------------- */
/* Point d'entrée                                                   */
/* ---------------------------------------------------------------- */

/* Charte, art. 6 : si deux aptitudes d'une classe définissent la même formule, la classe tranche
 * avec sa propre version ; sinon, l'analyse échoue (§ 13.7). */
static void verifier_conflits(Analyse *a) {
    Portee *p = a->portee;
    for (size_t i = 0; i < p->nb_classes && !a->echec; i++) {
        const Classe *c = &p->classes[i];
        if (c->aptitude || c->nb_aptitudes < 2) continue;
        for (size_t s1 = 0; s1 < p->n && !a->echec; s1++) {
            const Symbole *v = &p->s[s1];
            if (v->sorte == S_VARIABLE || !v->classe) continue;
            size_t k1 = 0;
            while (k1 < c->nb_aptitudes && strcmp(c->aptitudes[k1], v->classe) != 0) k1++;
            if (k1 == c->nb_aptitudes) continue;
            for (size_t s2 = s1 + 1; s2 < p->n; s2++) {
                const Symbole *w = &p->s[s2];
                if (w->sorte == S_VARIABLE || !w->classe || strcmp(w->nom, v->nom) != 0) continue;
                size_t k2 = 0;
                while (k2 < c->nb_aptitudes && strcmp(c->aptitudes[k2], w->classe) != 0) k2++;
                if (k2 == c->nb_aptitudes) continue;
                int tranche = 0;
                for (size_t s3 = 0; s3 < p->n && !tranche; s3++)
                    tranche = p->s[s3].classe && strcmp(p->s[s3].nom, v->nom) == 0 && strcmp(p->s[s3].classe, c->nom) == 0;
                if (!tranche) {
                    erreur_a(a, c->ligne, 1, grym_formater(
                        "« %s » est défini par les aptitudes « %s » et « %s » de « %s » : définissez sa version "
                        "pour « %s » afin de trancher.", v->nom, v->classe, w->classe, c->nom, c->nom));
                    break;
                }
            }
        }
    }
}

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
    char **noms;         /* noms visibles à la fin de la source */
    int *sortes;
    size_t nb_noms;
} Capture;

static int analyser_interne(const char *source, size_t taille, Portee *portee, int interactif,
                            Programme *programme, Diagnostic *diag, Capture *capture, int compact) {
    programme->phrases = NULL;
    programme->nb = 0;
    programme->nb_locaux = 0;
    diag->message = NULL;
    diag->ligne = diag->colonne = 0;

    char *err = NULL;
    Lexeur *lx = compact ? lexeur_creer_compact(source, taille, &err) : lexeur_creer(source, taille, &err);
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

    if (compact) {
        /* La forme compacte est réécrite en jetons littéraires (§ 11). */
        Jeton *lj = NULL;
        size_t ln = 0;
        int ok = compact_vers_litteraire(j, n, &lj, &ln, diag);
        liberer_jetons(j, n);
        if (!ok) return 0;
        j = lj;
        n = ln;
    }

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
    a.formule = 0;
    a.barriere = 0;
    a.nb_locaux = 0;
    a.niveau = 0;
    a.noms_fin = NULL;
    a.sortes_fin = NULL;
    a.nb_noms_fin = 0;
    a.boucle = 0;
    a.nb_arrets = 0;
    a.a_completer = NULL;
    a.dont = NULL;
    a.corbeille = 0;

    /* Colonne de référence : celle de la première phrase (les remarques ne comptent pas). */
    int colonne = 1;
    for (size_t k = 0; k < n; k++)
        if (j[k].type != J_REMARQUE) { colonne = j[k].type == J_FIN ? 1 : j[k].retrait; break; }
    Noeud *racine = bloc(&a, colonne, 1);
    if (racine && !a.echec) verifier_conflits(&a);
    programme->nb_locaux = a.nb_locaux;
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
        capture->nb_noms = a.nb_noms_fin;
        capture->noms = a.noms_fin;
        capture->sortes = a.sortes_fin;
        a.noms_fin = NULL;
        a.sortes_fin = NULL;
        a.nb_noms_fin = 0;
    } else {
        for (size_t k = 0; k < a.att_nb; k++) free(a.att_mots[k]);
        free(a.att_mots);
    }
    liberer_noms_fin(&a);
    free(a.a_completer);
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
    return analyser_interne(source, taille, portee, interactif, programme, diag, NULL, 0);
}

int analyser_compact(const char *source, size_t taille, Portee *portee,
                     Programme *programme, Diagnostic *diag) {
    return analyser_interne(source, taille, portee, 0, programme, diag, NULL, 1);
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
    Capture c = { 0, 0, NULL, 0, NULL, NULL, 0 };
    if (analyser_interne(source, d, portee, 0, &prog, &diag, &c, 0)) programme_liberer(&prog);
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
            proposer(&r, "Tant que", pre, lp, 0);
            proposer(&r, "Répéter", pre, lp, 0);
            proposer(&r, "Pour chaque", pre, lp, 0);
            proposer(&r, "Selon", pre, lp, 0);
            proposer(&r, "Pour", pre, lp, 0);
            proposer(&r, "Remarque :", pre, lp, 0);
            for (size_t k = 0; k < c.nb_noms; k++)
                if (c.sortes[k] == S_ACTION) {
                    /* une action commence la phrase : « Relancer » */
                    char *x = grym_dupliquer(c.noms[k]);
                    if (x[0] >= 'a' && x[0] <= 'z') x[0] = (char)(x[0] - 32);
                    proposer(&r, x, pre, lp, 0);
                    free(x);
                }
        }
        if (m & (A_VALEUR | A_NOM))
            for (size_t k = 0; k < c.nb_noms; k++) {
                if (c.sortes[k] == S_ACTION) continue;
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
        if (m & A_BOUCLE) {
            proposer(&r, "Sortir de la boucle", pre, lp, 0);
            proposer(&r, "Passer au tour suivant", pre, lp, 0);
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
    free(c.sortes);
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

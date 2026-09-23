/* GrymoiR : imprimeurs de l'arbre, v0.2
 * Spécification : docs/grammaire.md (révision 1.26), § 11 et § 12.
 */
#include "imprimeur.h"
#include "date.h"
#include "decimal.h"
#include "texte.h"

#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------- */
/* Genres connus : pour régénérer articles et accords               */
/* ---------------------------------------------------------------- */

typedef enum { G_LIBRE, G_MASCULIN, G_FEMININ } Genre;

typedef struct {
    const char *nom;
    Genre genre;
} Entree;

typedef struct {
    const char *feminin, *masculin;
} Aptitude;

typedef struct {
    Chaine c;
    Entree *genres;
    size_t nb, cap;
    int compact;
    Aptitude *aptitudes;   /* formes des aptitudes déclarées, pour les accorder */
    Aptitude *pluriels;    /* pluriels irréguliers des entités (feminin : nom, masculin : pluriel) */
    size_t nb_pluriels;
    Aptitude *singuliers;  /* champs multiples (feminin : pluriel, masculin : singulier), § 16.13 */
    size_t nb_singuliers;
    size_t nb_aptitudes;
    char **a_liberer;      /* formes masculines déduites */
    size_t nb_a_liberer;
} Impression;

static char *masculin_deduit(const char *f) {
    size_t l = strlen(f);
    return l > 1 && f[l - 1] == 'e' ? grym_formater("%.*s", (int)(l - 1), f) : grym_dupliquer(f);
}

static const char *forme_masculine(const Impression *im, const char *feminin) {
    for (size_t k = 0; k < im->nb_aptitudes; k++)
        if (strcmp(im->aptitudes[k].feminin, feminin) == 0) return im->aptitudes[k].masculin;
    return feminin;
}

static void retenir(Impression *im, const char *nom, Genre g) {
    if (im->nb == im->cap) {
        im->cap = im->cap ? im->cap * 2 : 32;
        Entree *t = grym_allouer(im->cap * sizeof *t);
        if (im->nb) memcpy(t, im->genres, im->nb * sizeof *t);
        free(im->genres);
        im->genres = t;
    }
    im->genres[im->nb].nom = nom;
    im->genres[im->nb].genre = g;
    im->nb++;
}

static Genre genre_de_nom(const Impression *im, const char *nom) {
    for (size_t i = im->nb; i > 0; i--)
        if (strcmp(im->genres[i - 1].nom, nom) == 0) return im->genres[i - 1].genre;
    return G_LIBRE;
}

static Genre genre_article(Article a) {
    return a == ART_LE ? G_MASCULIN : a == ART_LA ? G_FEMININ : G_LIBRE;
}

/* ---------------------------------------------------------------- */
/* Petits outils                                                    */
/* ---------------------------------------------------------------- */

static void aj(Impression *im, const char *s) { chaine_ajouter(&im->c, s); }

static void retrait(Impression *im, int niveau) {
    for (int k = 0; k < niveau; k++) aj(im, "    ");
}

const char *article_ecrit(Article a, Genre g, const char *nom);

/* Commence par une voyelle (élision : l', d') ; le « h » est laissé de côté. */
static int voyelle(const char *s) {
    static const char *const V[] = { "a", "e", "i", "o", "u", "y", "à", "â", "ä", "é", "è", "ê", "ë",
                                     "î", "ï", "ô", "ö", "ù", "û", "ü", "œ" };
    for (size_t k = 0; k < sizeof V / sizeof *V; k++)
        if (strncmp(s, V[k], strlen(V[k])) == 0) return 1;
    return 0;
}

static void ecrire_nom(Impression *im, const char *nom, int crochets) {
    if (im->compact) {
        for (const char *p = nom; *p; p++) {
            char t[2] = { *p == ' ' ? '_' : *p, 0 };
            aj(im, t);
        }
        return;
    }
    if (crochets || nom_exige_crochets(nom)) {
        aj(im, "[");
        aj(im, nom);
        aj(im, "]");
    } else {
        aj(im, nom);
    }
}

/* Texte : « » si le contenu s'y prête (ces guillemets rognent les espaces), sinon " " ou “ ”. */
static void ecrire_texte(Impression *im, const char *t) {
    size_t l = strlen(t);
    int bords = l && (t[0] == ' ' || t[l - 1] == ' ');
    if (!bords && !strstr(t, "»")) { aj(im, "« "); aj(im, t); aj(im, " »"); }
    else if (!strchr(t, '"'))       { aj(im, "\""); aj(im, t); aj(im, "\""); }
    else                            { aj(im, "“"); aj(im, t); aj(im, "”"); }
}

static const char *symbole(char op) {
    switch (op) {
    case '+': return "+";
    case '-': return "−";
    case '*': return "×";
    case '/': return "÷";
    case '^': return "^";
    case '=': return "=";
    case '!': return "≠";
    case '<': return "<";
    case '>': return ">";
    case 'l': return "≤";
    case 'g': return "≥";
    default:  return "?";
    }
}

static const struct { char op; const char *m, *f, *complement; } RELATIONS[] = {
    { '=', "égal",                "égale",                  "à"  },
    { '!', "différent",           "différente",             "de" },
    { '<', "inférieur",           "inférieure",             "à"  },
    { '>', "supérieur",           "supérieure",             "à"  },
    { 'l', "inférieur ou égal",   "inférieure ou égale",    "à"  },
    { 'g', "supérieur ou égal",   "supérieure ou égale",    "à"  },
    { 'P', "positif",             "positive",               NULL },
    { 'N', "négatif",             "négative",               NULL },
    { '0', "nul",                 "nulle",                  NULL },
    { 'V', "vrai",                "vraie",                  NULL },
    { 'F', "faux",                "fausse",                 NULL },
    { 'A', "absent",              "absente",                NULL },
    { 'R', "présent",             "présente",               NULL },
};

static int relation(char op) {
    for (size_t k = 0; k < sizeof RELATIONS / sizeof *RELATIONS; k++)
        if (RELATIONS[k].op == op) return (int)k;
    return 0;
}

static const char *adjectif_compact(char op) {
    switch (op) {
    case 'P': return "_positif";
    case 'N': return "_négatif";
    case '0': return "_nul";
    case 'V': return "_vrai";
    case 'A': return "_absent";
    case 'R': return "_présent";
    default:  return "_faux";
    }
}

/* ---------------------------------------------------------------- */
/* Expressions                                                      */
/* ---------------------------------------------------------------- */

static void expression(Impression *im, const Noeud *n);

/* Un argument de calcul se lie plus fort que les opérateurs (§ 9.1) : parenthèses si besoin. */
static int argument_simple(const Noeud *n) {
    switch (n->type) {
    case N_OPERATION: return n->op == '^';
    case N_COMPARAISON: case N_LOGIQUE: return 0;
    default: return 1;
    }
}

/* « à » ou « de » suivi d'une expression, avec contraction et élision (§ 5.2). */
static void preposition(Impression *im, const char *prep, const Noeud *x) {
    int a = strcmp(prep, "à") == 0;
    if (x->type == N_FICHIER && !im->compact) {   /* « du fichier « a.jpg » » */
        aj(im, a ? "au fichier " : "du fichier ");
        expression(im, x->enfants[0]);
        return;
    }
    if (x->type == N_CHAMP && x->article != ART_AUCUN && !im->compact) {
        /* « du solde du client » : l'article du champ se contracte avec la préposition */
        if (x->article == ART_LE) aj(im, a ? "au " : "du ");
        else { aj(im, a ? "à " : "de "); aj(im, x->article == ART_LA ? "la " : "l'"); }
        ecrire_nom(im, x->texte, 0);
        aj(im, " ");
        preposition(im, "de", x->enfants[0]);
        return;
    }
    if (x->type == N_NOM && x->article != ART_AUCUN) {
        if (x->article == ART_LE) { aj(im, a ? "au " : "du "); ecrire_nom(im, x->texte, x->crochets); return; }
        aj(im, a ? "à " : "de ");
        aj(im, x->article == ART_LA ? "la " : "l'");
        ecrire_nom(im, x->texte, x->crochets);
        return;
    }
    Impression tmp = *im;
    tmp.c.d = NULL;
    tmp.c.n = tmp.c.cap = 0;
    expression(&tmp, x);
    char *t = chaine_rendre(&tmp.c);
    if (!a && voyelle(t)) aj(im, "d'");
    else { aj(im, prep); aj(im, " "); }
    aj(im, t);
    free(t);
}

static void nom_avec_article(Impression *im, const Noeud *n) {
    if (!im->compact) {
        if (n->article == ART_LE) aj(im, "le ");
        else if (n->article == ART_LA) aj(im, "la ");
        else if (n->article == ART_L) aj(im, "l'");
    }
    ecrire_nom(im, n->texte, n->crochets);
}

static void comparaison(Impression *im, const Noeud *n, int sans_sujet) {
    const Noeud *sujet = n->enfants[0];
    const Noeud *droite = n->nb_enfants > 1 ? n->enfants[1] : NULL;
    if (n->op == 'p') {   /* « baroque est parmi les genres » ; en compacte « genres _contient baroque » (§ 16.13) */
        if (im->compact) {
            if (n->negation) aj(im, "_non (");
            ecrire_nom(im, sujet->texte, 0);
            aj(im, " _contient ");
            expression(im, droite);
            if (n->negation) aj(im, ")");
        } else {
            expression(im, droite);
            aj(im, n->negation ? " n'est pas parmi les " : " est parmi les ");
            ecrire_nom(im, sujet->texte, 0);
        }
        return;
    }
    int adjectif = strchr("PN0VFAR", n->op) != NULL;
    if (im->compact) {
        if (n->forme == 2) { expression(im, droite); return; }            /* cas : une valeur */
        if ((n->forme == 3 || n->forme == 4) && !n->negation) {          /* « est valeur » : « = » */
            expression(im, sujet);
            aj(im, " = ");
            expression(im, droite);
            return;
        }
        if (n->negation) aj(im, "_non (");
        if (!sans_sujet) { expression(im, sujet); aj(im, " "); }
        if (adjectif) aj(im, adjectif_compact(n->op));
        else { aj(im, symbole(n->op)); aj(im, " "); expression(im, droite); }
        if (n->negation) aj(im, ")");
        return;
    }
    if (n->forme == 2) { expression(im, droite); return; }                /* cas : une valeur */
    if (n->forme == 4) {                                                   /* « dont brel est l'auteur » */
        expression(im, droite);
        aj(im, n->negation ? " n'est pas " : " est ");
        expression(im, sujet);
        return;
    }
    if (n->forme == 3) {                                                   /* « dont la licence est « A » » */
        expression(im, sujet);
        aj(im, n->negation ? " n'est pas " : " est ");
        expression(im, droite);
        return;
    }
    if (n->forme == 1 && !sans_sujet) {                                    /* écrit en symbole */
        expression(im, sujet);
        aj(im, " ");
        aj(im, symbole(n->op));
        aj(im, " ");
        expression(im, droite);
        return;
    }
    if (!sans_sujet) {
        expression(im, sujet);
        aj(im, n->negation ? " n'est pas " : " est ");
    }
    Genre g = sujet->type == N_NOM || sujet->type == N_CHAMP_DONT || sujet->type == N_CHAMP
            ? genre_de_nom(im, sujet->texte) : G_MASCULIN;
    int k = relation(n->op);
    aj(im, g == G_FEMININ ? RELATIONS[k].f : RELATIONS[k].m);
    if (RELATIONS[k].complement) {
        aj(im, " ");
        preposition(im, RELATIONS[k].complement, droite);
    }
}

static void expression(Impression *im, const Noeud *n) {
    switch (n->type) {
    case N_NOMBRE: {
        /* le style est normalisé (apostrophe), la décision de grouper appartient à l'auteur (§ 12) */
        Decimal d = dec_depuis_canonique(n->texte);
        char *s = dec_formater(&d);
        if (!n->forme) {
            char *w = s;
            for (const char *r = s; *r; r++) if (*r != '\'') *w++ = *r;
            *w = '\0';
        }
        aj(im, s);
        free(s);
        dec_liberer(&d);
        return;
    }
    case N_NOM:
        nom_avec_article(im, n);
        return;
    case N_TEXTE:
        ecrire_texte(im, n->texte);
        return;
    case N_BOOLEEN:
        aj(im, im->compact ? (strcmp(n->texte, "vrai") == 0 ? "_vrai" : "_faux") : n->texte);
        return;
    case N_NEGATION:
        aj(im, "−");
        expression(im, n->enfants[0]);
        return;
    case N_OPERATION:
        expression(im, n->enfants[0]);
        aj(im, " ");
        aj(im, symbole(n->op));
        aj(im, " ");
        expression(im, n->enfants[1]);
        return;
    case N_GROUPE:
        aj(im, "(");
        expression(im, n->enfants[0]);
        aj(im, ")");
        return;
    case N_LOGIQUE:
        expression(im, n->enfants[0]);
        aj(im, im->compact ? (n->op == 'e' ? " _et " : " _ou ") : (n->op == 'e' ? " et " : " ou "));
        expression(im, n->enfants[1]);
        return;
    case N_COMPARAISON:
        comparaison(im, n, n->enfants[0]->type == N_SUJET);
        return;
    case N_INTERVALLE:
        if (im->compact) {
            aj(im, "_de ");
            expression(im, n->enfants[0]);
            aj(im, " _à ");
            expression(im, n->enfants[1]);
        } else {
            preposition(im, "de", n->enfants[0]);
            aj(im, " ");
            preposition(im, "à", n->enfants[1]);
        }
        return;
    case N_APPEL:
        if (im->compact) {
            ecrire_nom(im, n->texte, 0);
            aj(im, "(");
            for (size_t k = 0; k < n->nb_enfants; k++) {
                if (k) aj(im, " ; ");
                const Noeud *x = n->enfants[k];
                expression(im, x->type == N_GROUPE ? x->enfants[0] : x);
            }
            aj(im, ")");
            return;
        }
        nom_avec_article(im, n);
        for (size_t k = 0; k < n->nb_enfants; k++) {
            aj(im, k ? " et " : " ");
            const Noeud *x = n->enfants[k];
            if (argument_simple(x)) {
                preposition(im, "de", x);
            } else {
                aj(im, "de (");
                expression(im, x);
                aj(im, ")");
            }
        }
        return;
    case N_SUJET:
        return;
    case N_DATE: {
        long j = 0;
        date_lire_iso(n->texte, &j);
        char *t = date_suisse(j);   /* forme canonique : jour et mois sur deux chiffres (§ 14.1) */
        aj(im, t);
        free(t);
        return;
    }
    case N_AUJOURDHUI:
        aj(im, im->compact ? "_aujourd'hui" : "aujourd'hui");
        return;
    case N_ABSENT:
        aj(im, im->compact ? "_absent" : n->forme == 2 ? "absente" : "absent");
        return;
    case N_CHAMP_DONT:
        if (!im->compact) {   /* l'article s'écrit toujours : « dont le solde … » */
            Article art = n->article;
            if (art == ART_AUCUN) art = voyelle(n->texte) ? ART_L : genre_de_nom(im, n->texte) == G_FEMININ ? ART_LA : ART_LE;
            aj(im, art == ART_LA ? "la " : art == ART_L ? "l'" : "le ");
        }
        ecrire_nom(im, n->texte, 0);
        return;
    case N_CHERCHER: {
        Genre g = genre_de_nom(im, n->texte);
        int fem = g == G_FEMININ;
        /* « de bach » (§ 16.10), « les interprètes de o » (§ 16.13) : l'objet en dernier enfant */
        int inverse = n->op == 'I' || n->op == 'M';
        const Noeud *objet = inverse ? n->enfants[n->nb_enfants - 1] : NULL;
        if (n->forme == 2 && n->op == 'M') {   /* « le nombre d'interprètes de o » */
            if (im->compact) {
                const char *sg = NULL;
                for (size_t k = 0; k < im->nb_singuliers && !sg; k++)
                    if (strcmp(im->singuliers[k].feminin, n->texte3) == 0) sg = im->singuliers[k].masculin;
                char *x = sg ? grym_dupliquer(sg) : singulier_regulier(n->texte3);
                aj(im, "_nombre_de ");
                ecrire_nom(im, x, 0);
                free(x);
                aj(im, " _de ");
                expression(im, objet);
            } else {
                aj(im, voyelle(n->texte3) ? "le nombre d'" : "le nombre de ");
                aj(im, n->texte3);
                aj(im, " ");
                preposition(im, "de", objet);
            }
        } else if (n->forme == 2) {   /* « le nombre de clients conservés », « le nombre d'œuvres de bach » */
            if (im->compact) {
                aj(im, "_nombre_de ");
                ecrire_nom(im, n->texte, 0);
                if (inverse) { aj(im, " _de "); expression(im, objet); }
                else aj(im, n->negation ? " _supprimé" : " _conservé");
            } else {
                const char *pl = NULL;
                for (size_t k = 0; k < im->nb_pluriels && !pl; k++)
                    if (strcmp(im->pluriels[k].feminin, n->texte) == 0) pl = im->pluriels[k].masculin;
                char *p = pl ? grym_dupliquer(pl) : grym_formater("%ss", n->texte);
                aj(im, voyelle(p) ? "le nombre d'" : "le nombre de ");
                aj(im, p);
                free(p);
                if (inverse) { aj(im, " "); preposition(im, "de", objet); }
                else if (n->negation) aj(im, fem ? " supprimées" : " supprimés");
                else aj(im, fem ? " conservées" : " conservés");
            }
        } else {               /* « le client conservé » ; la boucle écrit elle-même son début */
            if (n->forme == 1) {
                const char *art = voyelle(n->texte) ? "l'" : fem ? "la " : "le ";
                if (im->compact) aj(im, "_");
                aj(im, art);
                ecrire_nom(im, n->texte, 0);
            }
            if (n->forme == 1)
                aj(im, im->compact ? (n->negation ? " _supprimé" : " _conservé")
                       : n->negation ? (fem ? " supprimée" : " supprimé") : fem ? " conservée" : " conservé");
            if (n->forme == 0 && inverse) {   /* début de « Pour chaque œuvre de bach » */
                if (im->compact) { aj(im, " _de "); expression(im, objet); }
                else { aj(im, " "); preposition(im, "de", objet); }
            }
        }
        if (n->nb_enfants > (size_t)inverse) {
            aj(im, im->compact ? " _dont " : " dont ");
            expression(im, n->enfants[0]);
        }
        if (n->texte2) {
            aj(im, im->compact ? " _par " : ", par ");
            ecrire_nom(im, n->texte2, 0);
            if (n->entier) aj(im, im->compact ? " _décroissant" : " décroissant");
        }
        return;
    }
    case N_FICHIER:
        aj(im, im->compact ? "_fichier " : "le fichier ");
        expression(im, n->enfants[0]);
        return;
    case N_CHAMP:
        if (im->compact) {
            const Noeud *o = n->enfants[0];
            int simple = o->type == N_NOM || o->type == N_CHAMP || o->type == N_APPEL || o->type == N_GROUPE;
            if (!simple) aj(im, "(");
            expression(im, o);
            if (!simple) aj(im, ")");
            aj(im, ".");
            ecrire_nom(im, n->texte, 0);
            return;
        }
        if (n->article == ART_LE) aj(im, "le ");
        else if (n->article == ART_LA) aj(im, "la ");
        else if (n->article == ART_L) aj(im, "l'");
        ecrire_nom(im, n->texte, 0);
        aj(im, " ");
        preposition(im, "de", n->enfants[0]);
        return;
    case N_NOUVEAU: {
        if (im->compact) {
            aj(im, "_nouveau ");
            ecrire_nom(im, n->texte, 0);
            if (n->forme) aj(im, " _avec");
            return;
        }
        Genre g = genre_de_nom(im, n->texte);
        aj(im, g == G_FEMININ ? "une nouvelle " : voyelle(n->texte) ? "un nouvel " : "un nouveau ");
        ecrire_nom(im, n->texte, 0);
        return;
    }
    default:
        aj(im, "?");
        return;
    }
}

/* « (texte) », « (nombre entier), unique » ; en compacte « (nombre_entier) _unique ». */
static void type_de_champ(Impression *im, const Noeud *ch) {
    if (!ch->texte2) return;
    aj(im, " (");
    if (im->compact) ecrire_nom(im, ch->texte2, 0);
    else aj(im, ch->texte2);   /* « (vrai ou faux) » : un type, pas un nom, jamais entre crochets */
    aj(im, ")");
    if (ch->op == 'U') aj(im, im->compact ? " _unique" : ", unique");
    if (ch->entier & 1) aj(im, im->compact ? " _facultatif" : ch->forme == 2 ? ", facultative" : ", facultatif");
    if (ch->entier & 2)
        aj(im, im->compact ? " _disparaît_avec"
               : genre_de_nom(im, ch->texte2) == G_FEMININ ? ", et disparaît avec elle" : ", et disparaît avec lui");
    if (ch->nb_enfants) {   /* valeur de départ (§ 16.7) */
        aj(im, im->compact ? " _départ " : ", ");
        expression(im, ch->enfants[0]);
        if (!im->compact) aj(im, " au départ");
    }
}

/* Bloc qui initialise un nouvel objet, après la phrase qui le crée. */
static void initialisation(Impression *im, const Noeud *nv, int niveau) {
    for (size_t k = 0; k < nv->nb_enfants; k++) {
        const Noeud *init = nv->enfants[k];
        retrait(im, niveau + 1);
        if (im->compact) {
            ecrire_nom(im, init->texte, 0);
            aj(im, " << ");
            expression(im, init->enfants[0]);
            aj(im, "\n");
        } else {
            Genre g = genre_de_nom(im, init->texte);
            aj(im, article_ecrit(init->article, g, init->texte));
            ecrire_nom(im, init->texte, 0);
            aj(im, " vaut ");
            expression(im, init->enfants[0]);
            aj(im, ".\n");
        }
    }
    if (im->compact) { retrait(im, niveau); aj(im, "_fin\n"); }
}

static const Noeud *nouveau_en_bloc(const Noeud *v) {
    return v->type == N_NOUVEAU && v->forme ? v : NULL;
}

/* ---------------------------------------------------------------- */
/* Phrases                                                          */
/* ---------------------------------------------------------------- */

static void phrase(Impression *im, const Noeud *n, int niveau);
static void bloc(Impression *im, const Noeud *b, int niveau);

const char *article_ecrit(Article a, Genre g, const char *nom) {
    if (a == ART_LE) return "Le ";
    if (a == ART_LA) return "La ";
    if (a == ART_L) return "L'";
    if (voyelle(nom)) return "L'";
    return g == G_FEMININ ? "La " : "Le ";
}

/* Une phrase simple, écrite sur la ligne courante après « , » (forme courte). */
static void phrase_en_ligne(Impression *im, const Noeud *n) {
    Impression tmp = *im;
    tmp.c.d = NULL;
    tmp.c.n = tmp.c.cap = 0;
    phrase(&tmp, n, 0);
    char *t = chaine_rendre(&tmp.c);
    im->genres = tmp.genres;
    im->nb = tmp.nb;
    im->cap = tmp.cap;
    size_t l = strlen(t);
    if (l && t[l - 1] == '\n') t[l - 1] = '\0';
    if (t[0] >= 'A' && t[0] <= 'Z') t[0] = (char)(t[0] + 32);
    aj(im, t);
    free(t);
}

/* Corps d'une construction : « , phrase » (forme courte) ou « : » puis un bloc indenté. */
static void branche(Impression *im, const Noeud *corps, int court, int niveau) {
    if (im->compact) {
        aj(im, "\n");
        bloc(im, corps, niveau + 1);
        return;
    }
    if (court && corps->nb_enfants == 1) {
        aj(im, ", ");
        phrase_en_ligne(im, corps->enfants[0]);
        aj(im, "\n");
        return;
    }
    aj(im, " :\n");
    bloc(im, corps, niveau + 1);
}

static void fin_compacte(Impression *im, int niveau) {
    if (!im->compact) return;
    retrait(im, niveau);
    aj(im, "_fin\n");
}

static void parametres(Impression *im, const Noeud *liste, int de) {
    for (size_t k = 0; k < liste->nb_enfants; k++) {
        const Noeud *p = liste->enfants[k];
        retenir(im, p->texte, p->forme == 2 ? G_FEMININ : G_MASCULIN);
        if (im->compact) {
            if (k) aj(im, " ; ");
            aj(im, p->forme == 2 ? "_une " : "_un ");
        } else {
            aj(im, k ? " et " : " ");
            if (de) aj(im, "d'");
            aj(im, p->forme == 2 ? "une " : "un ");
        }
        ecrire_nom(im, p->texte, 0);
        if (p->texte2) { aj(im, im->compact ? "_" : " "); aj(im, p->texte2); }   /* une chose horodatée */
    }
}

static void si(Impression *im, const Noeud *n, int niveau, int sinon_si) {
    const Noeud *cond = n->enfants[0];
    if (im->compact) {
        aj(im, sinon_si ? "_sinon_si " : "_si ");
        expression(im, cond);
        aj(im, " _alors");
        branche(im, n->enfants[1], 0, niveau);
    } else {
        aj(im, sinon_si ? "Sinon si " : "Si ");
        expression(im, cond);
        branche(im, n->enfants[1], !(n->forme & 1), niveau);
    }
    if (n->nb_enfants > 2) {
        const Noeud *autre = n->enfants[2];
        retrait(im, niveau);
        if (autre->nb_enfants == 1 && autre->enfants[0]->type == P_SI && (autre->enfants[0]->forme & 2)) {
            si(im, autre->enfants[0], niveau, 1);
        } else if (im->compact) {
            aj(im, "_sinon");
            branche(im, autre, 0, niveau);
        } else {
            aj(im, "Sinon");
            branche(im, autre, !(n->forme & 4), niveau);
        }
    }
    if (!sinon_si) fin_compacte(im, niveau);
}

static void phrase(Impression *im, const Noeud *n, int niveau) {
    int c = im->compact;
    retrait(im, niveau);
    switch (n->type) {
    case P_REMARQUE:
        aj(im, c ? "#" : "Remarque :");
        if (n->texte[0]) { aj(im, " "); aj(im, n->texte); }
        aj(im, "\n");
        return;
    case P_CREATION:
    case P_MODIFICATION: {
        Genre g = n->type == P_CREATION ? genre_article(n->article) : genre_de_nom(im, n->texte);
        const Noeud *bloc_init = nouveau_en_bloc(n->enfants[0]);
        if (c) {
            if (n->type == P_CREATION)
                aj(im, n->article == ART_LA ? "_la " : n->article == ART_L ? "_l'" : "_le ");
            ecrire_nom(im, n->texte, 0);
            aj(im, " << ");
            expression(im, n->enfants[0]);
            aj(im, "\n");
        } else {
            aj(im, article_ecrit(n->article, g, n->texte));
            ecrire_nom(im, n->texte, n->crochets);
            aj(im, n->type == P_CREATION ? " vaut " : " devient ");
            expression(im, n->enfants[0]);
            aj(im, bloc_init ? " :\n" : ".\n");
        }
        if (bloc_init) initialisation(im, bloc_init, niveau);
        if (n->type == P_CREATION) retenir(im, n->texte, g);
        return;
    }
    case P_AFFICHAGE:
        aj(im, c ? "_afficher " : "Afficher ");
        for (size_t k = 0; k < n->nb_enfants; k++) {
            if (k) aj(im, c ? " ; " : " puis ");
            expression(im, n->enfants[k]);
        }
        aj(im, c ? "\n" : ".\n");
        return;
    case P_EXPRESSION:
        expression(im, n->enfants[0]);
        aj(im, "\n");
        return;
    case P_SI:
        si(im, n, niveau, 0);
        return;
    case P_CALCUL: {
        const Noeud *params = n->enfants[0], *corps = n->enfants[1];
        size_t sauve = im->nb;
        retenir(im, n->texte, genre_article(n->article));
        size_t apres_nom = im->nb;
        if (c) {
            aj(im, "_calcul ");
            aj(im, n->article == ART_LA ? "_la " : n->article == ART_L ? "_l'" : "_le ");
            ecrire_nom(im, n->texte, 0);
            aj(im, "(");
            parametres(im, params, 0);
            aj(im, ")");
            if (n->forme == 0) {
                aj(im, " << ");
                expression(im, corps);
                aj(im, "\n");
            } else {
                aj(im, "\n");
                bloc(im, corps, niveau + 1);
                fin_compacte(im, niveau);
            }
        } else {
            aj(im, article_ecrit(n->article, G_LIBRE, n->texte));
            ecrire_nom(im, n->texte, 0);
            parametres(im, params, 1);
            if (n->forme == 0) {
                aj(im, " vaut ");
                expression(im, corps);
                aj(im, ".\n");
            } else {
                aj(im, " :\n");
                bloc(im, corps, niveau + 1);
            }
        }
        im->nb = apres_nom;   /* les paramètres meurent avec la formule ; le nom reste */
        (void)sauve;
        return;
    }
    case P_ACTION: {
        size_t apres_nom = im->nb;
        if (c) {
            aj(im, "_action ");
            ecrire_nom(im, n->texte, 0);
            aj(im, "(");
            parametres(im, n->enfants[0], 0);
            aj(im, ")\n");
            bloc(im, n->enfants[1], niveau + 1);
            fin_compacte(im, niveau);
        } else {
            aj(im, "Pour ");
            ecrire_nom(im, n->texte, 0);
            parametres(im, n->enfants[0], 0);
            aj(im, " :\n");
            bloc(im, n->enfants[1], niveau + 1);
        }
        im->nb = apres_nom;
        return;
    }
    case P_RENDRE:
        aj(im, c ? "_rendre " : "Rendre ");
        expression(im, n->enfants[0]);
        aj(im, c ? "\n" : ".\n");
        return;
    case P_APPEL:
        if (c) {
            ecrire_nom(im, n->texte, 0);
            aj(im, "(");
            for (size_t k = 0; k < n->nb_enfants; k++) {
                if (k) aj(im, " ; ");
                expression(im, n->enfants[k]);
            }
            aj(im, ")\n");
        } else {
            char *nom = grym_dupliquer(n->texte);
            if (nom[0] >= 'a' && nom[0] <= 'z') nom[0] = (char)(nom[0] - 32);
            aj(im, nom);
            free(nom);
            for (size_t k = 0; k < n->nb_enfants; k++) {
                aj(im, k ? " et " : " ");
                const Noeud *x = n->enfants[k];
                int paren = x->type == N_LOGIQUE;
                if (paren) aj(im, "(");
                expression(im, x);
                if (paren) aj(im, ")");
            }
            aj(im, ".\n");
        }
        return;
    case P_TANT_QUE:
        aj(im, c ? "_tant_que " : "Tant que ");
        expression(im, n->enfants[0]);
        branche(im, n->enfants[1], !n->forme, niveau);
        fin_compacte(im, niveau);
        return;
    case P_REPETER:
        aj(im, c ? "_répéter " : "Répéter ");
        expression(im, n->enfants[0]);
        aj(im, c ? " _fois" : " fois");
        branche(im, n->enfants[1], !n->forme, niveau);
        fin_compacte(im, niveau);
        return;
    case P_POUR_CONSERVE: {
        size_t sauve = im->nb;
        int fem = genre_de_nom(im, n->texte) == G_FEMININ;
        aj(im, c ? "_pour_chaque " : "Pour chaque ");
        ecrire_nom(im, n->texte, 0);
        if (n->enfants[0]->op != 'I' && n->enfants[0]->op != 'M') {
            int sup = n->enfants[0]->negation;
            aj(im, c ? (sup ? " _supprimé" : " _conservé")
                   : sup ? (fem ? " supprimée" : " supprimé") : fem ? " conservée" : " conservé");
        }
        expression(im, n->enfants[0]);   /* « de … », « dont … » et « , par … » */
        retenir(im, n->texte, fem ? G_FEMININ : G_MASCULIN);
        const Noeud *corps = n->enfants[1];
        branche(im, corps, corps->ligne == n->ligne, niveau);
        fin_compacte(im, niveau);
        im->nb = sauve;
        return;
    }
    case P_POUR_CHAQUE: {
        size_t sauve = im->nb;
        if (c) {
            aj(im, "_pour_chaque ");
            ecrire_nom(im, n->texte, 0);
            aj(im, " _de ");
            expression(im, n->enfants[0]);
            aj(im, " _à ");
            expression(im, n->enfants[1]);
            if (n->forme & 1) { aj(im, " _pas "); expression(im, n->enfants[2]); }
        } else {
            aj(im, "Pour chaque ");
            ecrire_nom(im, n->texte, 0);
            aj(im, " ");
            if (n->forme & 2) { aj(im, "du "); expression(im, n->enfants[0]); }   /* contraction écrite (§ 12) */
            else preposition(im, "de", n->enfants[0]);
            aj(im, " ");
            if (n->forme & 4) { aj(im, "au "); expression(im, n->enfants[1]); }
            else preposition(im, "à", n->enfants[1]);
            if (n->forme & 1) { aj(im, " par pas "); preposition(im, "de", n->enfants[2]); }
        }
        retenir(im, n->texte, G_LIBRE);
        const Noeud *corps = n->enfants[n->nb_enfants - 1];
        branche(im, corps, corps->ligne == n->ligne, niveau);
        fin_compacte(im, niveau);
        im->nb = sauve;
        return;
    }
    case P_APTITUDE: {
        Aptitude *t = grym_allouer((im->nb_aptitudes + 1) * sizeof *t);
        if (im->nb_aptitudes) memcpy(t, im->aptitudes, im->nb_aptitudes * sizeof *t);
        free(im->aptitudes);
        im->aptitudes = t;
        char *masc = n->texte2 ? grym_dupliquer(n->texte2) : masculin_deduit(n->texte);
        im->a_liberer = realloc(im->a_liberer, (im->nb_a_liberer + 1) * sizeof *im->a_liberer);
        im->a_liberer[im->nb_a_liberer++] = masc;
        im->aptitudes[im->nb_aptitudes].feminin = n->texte;
        im->aptitudes[im->nb_aptitudes++].masculin = masc;
        aj(im, c ? "_aptitude " : "Une chose ");
        aj(im, n->texte);
        if (n->texte2) { aj(im, " ("); aj(im, n->texte2); aj(im, ")"); }
        aj(im, c ? "\n" : " a :\n");
        for (size_t k = 0; k < n->nb_enfants; k++) {
            const Noeud *ch = n->enfants[k];
            retenir(im, ch->texte, ch->forme == 2 ? G_FEMININ : G_MASCULIN);
            retrait(im, niveau + 1);
            aj(im, c ? (ch->forme == 2 ? "_une " : "_un ") : (ch->forme == 2 ? "une " : "un "));
            ecrire_nom(im, ch->texte, 0);
            type_de_champ(im, ch);
            aj(im, c ? "\n" : k + 1 < n->nb_enfants ? ",\n" : ".\n");
        }
        if (c) { retrait(im, niveau); aj(im, "_fin\n"); }
        return;
    }
    case P_CLASSE: {
        int fem = (n->forme & 3) == 2;
        retenir(im, n->texte, fem ? G_FEMININ : G_MASCULIN);
        if (n->texte3) {
            Aptitude *t = grym_allouer((im->nb_pluriels + 1) * sizeof *t);
            if (im->nb_pluriels) memcpy(t, im->pluriels, im->nb_pluriels * sizeof *t);
            free(im->pluriels);
            im->pluriels = t;
            im->pluriels[im->nb_pluriels].feminin = n->texte;
            im->pluriels[im->nb_pluriels++].masculin = n->texte3;
        }
        int herite = (n->forme & 16) != 0;
        int fem_base = !n->texte2 || genre_de_nom(im, n->texte2) == G_FEMININ;   /* « chose » est féminin */
        size_t nb_champs = 0, nb_apt = 0;
        for (size_t k = 0; k < n->nb_enfants; k++) {
            if (n->enfants[k]->type == N_TEXTE) nb_apt++;
            else nb_champs++;
        }
        int conserve = (n->forme & 32) != 0;
        if (c) {
            aj(im, fem ? "_classe _une " : "_classe _un ");
            ecrire_nom(im, n->texte, 0);
            if (n->texte3) { aj(im, " ("); ecrire_nom(im, n->texte3, 0); aj(im, ")"); }
            if (conserve) aj(im, " _conservé");
            if (herite) {
                aj(im, fem_base ? " _est _une " : " _est _un ");
                ecrire_nom(im, n->texte2 ? n->texte2 : "chose", 0);
                size_t q = 0;
                for (size_t k = 0; k < n->nb_enfants; k++) {
                    if (n->enfants[k]->type != N_TEXTE) continue;
                    aj(im, q++ ? " ; " : " _adopte ");
                    aj(im, n->enfants[k]->texte);
                }
            }
            aj(im, "\n");
        } else {
            if (herite) {
                /* « Un membre est une personne horodatée. », puis ses champs dans une seconde phrase */
                aj(im, fem ? "Une " : "Un ");
                ecrire_nom(im, n->texte, 0);
                if (n->texte3) { aj(im, " ("); aj(im, n->texte3); aj(im, ")"); }
                if (conserve) aj(im, fem ? ", conservée," : ", conservé,");
                aj(im, fem_base ? " est une " : " est un ");
                ecrire_nom(im, n->texte2 ? n->texte2 : "chose", 0);
                size_t q = 0;
                for (size_t k = 0; k < n->nb_enfants; k++) {
                    if (n->enfants[k]->type != N_TEXTE) continue;
                    aj(im, q == 0 ? " " : q + 1 == nb_apt ? " et " : ", ");
                    q++;
                    const char *f = n->enfants[k]->texte;
                    aj(im, fem_base ? f : forme_masculine(im, f));
                }
                aj(im, ".\n");
                if (!nb_champs) return;
                retrait(im, niveau);
            }
            aj(im, fem ? "Une " : "Un ");
            ecrire_nom(im, n->texte, 0);
            if (!herite && n->texte3) { aj(im, " ("); aj(im, n->texte3); aj(im, ")"); }
            if (!herite && conserve) aj(im, fem ? ", conservée," : ", conservé,");
            aj(im, " a :\n");
        }
        size_t vus = 0;
        for (size_t k = 0; k < n->nb_enfants; k++) {
            const Noeud *ch = n->enfants[k];
            if (ch->type == N_TEXTE) continue;
            vus++;
            retrait(im, niveau + 1);
            if (ch->forme == 3) {   /* « des interprètes (personne) », « des travaux (travail) (tâche) » (§ 16.13) */
                if (ch->texte3) {   /* singulier irrégulier : retenu pour « _nombre_de travail _de o » */
                    Aptitude *t = grym_allouer((im->nb_singuliers + 1) * sizeof *t);
                    if (im->nb_singuliers) memcpy(t, im->singuliers, im->nb_singuliers * sizeof *t);
                    free(im->singuliers);
                    im->singuliers = t;
                    im->singuliers[im->nb_singuliers].feminin = ch->texte;
                    im->singuliers[im->nb_singuliers++].masculin = ch->texte3;
                }
                aj(im, c ? "_des " : "des ");
                ecrire_nom(im, ch->texte, 0);
                if (ch->texte3) { aj(im, " ("); ecrire_nom(im, ch->texte3, 0); aj(im, ")"); }
                type_de_champ(im, ch);
                aj(im, c ? "\n" : vus < nb_champs ? ",\n" : ".\n");
                continue;
            }
            retenir(im, ch->texte, ch->forme == 2 ? G_FEMININ : G_MASCULIN);
            aj(im, c ? (ch->forme == 2 ? "_une " : "_un ") : (ch->forme == 2 ? "une " : "un "));
            ecrire_nom(im, ch->texte, 0);
            type_de_champ(im, ch);
            aj(im, c ? "\n" : vus < nb_champs ? ",\n" : ".\n");
        }
        if (c) { retrait(im, niveau); aj(im, "_fin\n"); }
        return;
    }
    case P_GAGNER: {   /* « Les genres de o gagnent baroque. » ; « o.genres _gagne baroque » (§ 16.13) */
        if (c) {
            Noeud champ = *n;
            champ.type = N_CHAMP;
            champ.nb_enfants = 1;
            expression(im, &champ);
            aj(im, n->forme ? " _perd " : " _gagne ");
            expression(im, n->enfants[1]);
            aj(im, "\n");
        } else {
            aj(im, "Les ");
            ecrire_nom(im, n->texte, 0);
            aj(im, " ");
            preposition(im, "de", n->enfants[0]);
            aj(im, n->forme ? " perdent " : " gagnent ");
            expression(im, n->enfants[1]);
            aj(im, ".\n");
        }
        return;
    }
    case P_MODIF_CHAMP: {
        const Noeud *bloc_init = nouveau_en_bloc(n->enfants[1]);
        if (c) {
            Noeud champ = *n;
            champ.type = N_CHAMP;
            champ.nb_enfants = 1;
            expression(im, &champ);
            aj(im, " << ");
            expression(im, n->enfants[1]);
            aj(im, "\n");
        } else {
            Genre g = genre_de_nom(im, n->texte);
            aj(im, article_ecrit(n->article, g, n->texte));
            ecrire_nom(im, n->texte, 0);
            aj(im, " ");
            preposition(im, "de", n->enfants[0]);
            aj(im, " devient ");
            expression(im, n->enfants[1]);
            aj(im, bloc_init ? " :\n" : ".\n");
        }
        if (bloc_init) initialisation(im, bloc_init, niveau);
        return;
    }
    case P_SORTIR:
        aj(im, c ? "_sortir\n" : "Sortir de la boucle.\n");
        return;
    case P_CONSERVER:
    case P_SUPPRIMER:
        if (c) aj(im, n->type == P_CONSERVER ? "_conserver " : n->entier == 2 ? "_rétablir " : "_supprimer ");
        else aj(im, n->type == P_CONSERVER ? "Conserver " : n->entier == 2 ? "Rétablir " : "Supprimer ");
        expression(im, n->enfants[0]);
        if (n->type == P_SUPPRIMER && n->entier == 1) aj(im, c ? " _définitivement" : " définitivement");
        aj(im, c ? "\n" : ".\n");
        return;
    case P_ENREGISTRER:
        aj(im, c ? "_enregistrer " : "Enregistrer ");
        expression(im, n->enfants[0]);
        aj(im, c ? " _dans " : " dans ");
        expression(im, n->enfants[1]);
        aj(im, c ? "\n" : ".\n");
        return;
    case P_PASSER:
        aj(im, c ? "_passer\n" : "Passer au tour suivant.\n");
        return;
    case P_SELON:
        aj(im, c ? "_selon " : "Selon ");
        expression(im, n->enfants[0]);
        aj(im, c ? "\n" : " :\n");
        for (size_t k = 1; k < n->nb_enfants; k++) {
            const Noeud *cas = n->enfants[k];
            if (cas->type != N_CAS) { phrase(im, cas, niveau + 1); continue; }
            retrait(im, niveau + 1);
            const Noeud *corps = cas->enfants[cas->nb_enfants - 1];
            if (cas->forme == 1) {
                aj(im, c ? "_autrement" : "Autrement");
            } else {
                aj(im, c ? "_cas " : "Cas ");
                for (size_t q = 0; q + 1 < cas->nb_enfants; q++) {
                    if (q) aj(im, c ? " _ou " : " ou ");
                    expression(im, cas->enfants[q]);
                }
            }
            branche(im, corps, corps->ligne == cas->ligne, niveau + 1);
        }
        fin_compacte(im, niveau);
        return;
    default:
        aj(im, "?\n");
        return;
    }
}

/* Phrases d'un bloc ; une ligne vide de la source est conservée (une seule). */
static void bloc(Impression *im, const Noeud *b, int niveau) {
    size_t sauve = im->nb;
    for (size_t k = 0; k < b->nb_enfants; k++) {
        const Noeud *p = b->enfants[k];
        if (k > 0 && p->ligne > b->enfants[k - 1]->ligne_fin + 1) aj(im, "\n");
        phrase(im, p, niveau);
    }
    im->nb = sauve;
}

static char *imprimer(const Programme *p, int compact) {
    Impression im;
    memset(&im, 0, sizeof im);
    im.compact = compact;
    for (size_t k = 0; k < p->nb; k++) {
        if (k > 0 && p->phrases[k]->ligne > p->phrases[k - 1]->ligne_fin + 1) aj(&im, "\n");
        phrase(&im, p->phrases[k], 0);
    }
    free(im.genres);
    free(im.aptitudes);
    free(im.pluriels);
    free(im.singuliers);
    for (size_t k = 0; k < im.nb_a_liberer; k++) free(im.a_liberer[k]);
    free(im.a_liberer);
    return chaine_rendre(&im.c);
}

char *imprimer_litteraire(const Programme *p) { return imprimer(p, 0); }
char *imprimer_compact(const Programme *p)    { return imprimer(p, 1); }

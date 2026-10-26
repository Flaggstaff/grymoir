/* GrymoiR : imprimeurs de l'arbre, v0.2
 * Spécification : docs/grammaire.md (révision 1.37), § 11 et § 12.
 */
#include "imprimeur.h"
#include "date.h"
#include "decimal.h"
#include "texte.h"
#include "types.h"

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
    const Noeud **parties;   /* les parties du programme (2.56), pour reconnaître leurs éléments dépliés dans un écran */
    size_t nb_parties;
    Aptitude *aptitudes;   /* formes des aptitudes déclarées, pour les accorder */
    Aptitude *pluriels;    /* pluriels irréguliers des entités (feminin : nom, masculin : pluriel) */
    size_t nb_pluriels;
    Aptitude *singuliers;  /* champs multiples (feminin : pluriel, masculin : singulier), § 16.13 */
    size_t nb_singuliers;
    size_t nb_aptitudes;
    char **a_liberer;      /* formes masculines déduites */
    size_t nb_a_liberer;
    int devine;            /* l'article du champ qu'on écrit a été deviné : son objet peut l'être aussi (« du c ») */
    int champ_nu;          /* on écrit l'objet d'un champ resté sans article : il reste nu aussi (« longueur de n ») */
    int garder_groupe;     /* le membre gauche d'une opération : ses parenthèses restent (« de (… dont …) − … ») */
    int sans_marque;       /* l'objet d'un champ compact (`c.nom`) : pas de « _nu » ni d'article devant lui (2.35) */
    int sans_marque_objet; /* « o.genres _gagne … » : pas de marque devant l'objet d'un champ en tête de phrase (2.36) */
    char **variables;      /* les variables créées avec un article (« L'effectif vaut 12. ») et leur genre (§ 11.4) */
    Genre *genres_var;
    size_t nb_var;
} Impression;

/* Le genre d'une variable créée avec un article, ou G_LIBRE : un paramètre, un nom écrit nu, reste nu. */
static Genre genre_de_variable(const Impression *im, const char *nom) {
    for (size_t k = im->nb_var; k-- > 0;) if (strcmp(im->variables[k], nom) == 0) return im->genres_var[k];
    return G_LIBRE;
}


/* Initiale d'une phrase : capitale (majuscule 1) ou minuscule (0), lettres accentuées du latin et « œ » comprises
 * (« Étiqueter o. », « si x, étiqueter o. »). */
static void initiale(char *s, int majuscule) {
    unsigned char *u = (unsigned char *)s;
    if (majuscule) {
        if (u[0] >= 'a' && u[0] <= 'z') u[0] = (unsigned char)(u[0] - 32);
        else if (u[0] == 0xC3 && u[1] >= 0xA0 && u[1] <= 0xBE && u[1] != 0xB7) u[1] = (unsigned char)(u[1] - 0x20);
        else if (u[0] == 0xC5 && u[1] == 0x93) u[1] = 0x92;
    } else {
        if (u[0] >= 'A' && u[0] <= 'Z') u[0] = (unsigned char)(u[0] + 32);
        else if (u[0] == 0xC3 && u[1] >= 0x80 && u[1] <= 0x9E && u[1] != 0x97) u[1] = (unsigned char)(u[1] + 0x20);
        else if (u[0] == 0xC5 && u[1] == 0x92) u[1] = 0x93;
    }
}

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

/* Une expression qui finit par une condition « dont » (une recherche, ou une opération dont le dernier membre en est
 * une) : un opérateur écrit après elle appartiendrait à la condition. */
static int finit_par_dont(const Noeud *n) {
    if (!n) return 0;
    if (n->type == N_CHERCHER) return n->nb_enfants > 0 && n->enfants[0] && n->enfants[0]->type != N_NOM;
    if (n->type == N_OPERATION) return finit_par_dont(n->enfants[1]);
    return 0;
}

/* Les mots en « h » aspiré les plus courants : « la hauteur », « le haut », pas d'élision (2.33). */
static int h_aspire(const char *s) {
    static const char *const H[] = { "haut", "hauteur", "hasard", "hâte", "hausse", "héros", "honte", "hors", "hall",
                                     "hameau", "hanche", "handicap", "hangar", "hareng", "haricot", "harpe", "hache",
                                     "haie", "haine", "hamac", "hamster", "harcèlement", "hiérarchie", "hibou", "hockey",
                                     "homard", "hongrois", "hollandais", "hublot", "huit", "hurlement", "hutte", "houx" };
    for (size_t k = 0; k < sizeof H / sizeof *H; k++) {
        const size_t l = strlen(H[k]);
        if (strncmp(s, H[k], l) == 0 && (s[l] == 0 || s[l] == ' ' || s[l] == 's')) return 1;
    }
    return 0;
}

/* Commence par une voyelle (élision : l', d') ; le « h » est laissé de côté. */
static int voyelle(const char *s) {
    static const char *const V[] = { "a", "e", "i", "o", "u", "y", "à", "â", "ä", "é", "è", "ê", "ë",
                                     "î", "ï", "ô", "ö", "ù", "û", "ü", "œ" };
    for (size_t k = 0; k < sizeof V / sizeof *V; k++)
        if (strncmp(s, V[k], strlen(V[k])) == 0) return 1;
    return s[0] == 'h' && !h_aspire(s);   /* « l'heure », « l'hôtel » : le « h » muet s'élide (2.33) */
}

/* L'article d'un champ en forme littéraire. La forme compacte n'en écrit pas (`_écran.compositeur_choisi.portrait`) :
 * traduite, elle le retrouve par le genre du champ, retenu de sa déclaration (« le portrait », « la photo », « l'âge »).
 * Genre inconnu : aucun article, comme écrit. */
static Article article_du_champ(const Impression *im, const Noeud *n) {
    if (n->article != ART_AUCUN || im->compact || !n->article_a_deviner) return n->article;
    /* les propriétés du langage (§ 4.5, § 14, § 15) : leur genre ne vient d'aucune déclaration (2.33) */
    static const char *const FEMININES[] = { "longueur", "taille", "partie entière", "année", "valeur absolue", "heure", "durée" };
    static const char *const MASCULINES[] = { "jour", "mois", "format", "nom", "nombre de jours", "nombre de mois" };
    for (size_t k = 0; k < sizeof FEMININES / sizeof *FEMININES; k++)
        if (strcmp(n->texte, FEMININES[k]) == 0 && genre_de_nom(im, n->texte) == G_LIBRE) return voyelle(n->texte) ? ART_L : ART_LA;
    for (size_t k = 0; k < sizeof MASCULINES / sizeof *MASCULINES; k++)
        if (strcmp(n->texte, MASCULINES[k]) == 0 && genre_de_nom(im, n->texte) == G_LIBRE) return ART_LE;
    const Genre g = genre_de_nom(im, n->texte);
    if (g == G_LIBRE) return ART_AUCUN;
    return voyelle(n->texte) ? ART_L : g == G_FEMININ ? ART_LA : ART_LE;
}

static void ecrire_nom_tel(Impression *im, const char *nom, int crochets);

/* Un nom s'écrit avec ses sigles en capitales (§ 2.4) : « numéro RCC ». */
/* un nom d'écran avec ses sigles en capitales (§ 2.4) : « l'écran des SMS » */
static void aj_sigles(Impression *im, const char *t) {
    char *x = texte_avec_sigles(t);
    aj(im, x);
    free(x);
}

static void ecrire_nom(Impression *im, const char *nom, int crochets) {
    char *s = texte_avec_sigles(nom);
    ecrire_nom_tel(im, s, crochets);
    free(s);
}

static void ecrire_nom_tel(Impression *im, const char *nom, int crochets) {
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

/* Texte : « » si le contenu s'y prête (ces guillemets rognent les espaces), sinon " " ou “ ”.
 * Un texte vide s'écrit « » : une seule espace, que les guillemets rognent. */
static void ecrire_texte(Impression *im, const char *t) {
    size_t l = strlen(t);
    if (l == 0) { aj(im, "« »"); return; }
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
    case N_COLLAGE: return 0;   /* « de ("Rappel " suivi du numéro) » : sans parenthèses, « suivi de » s'appliquerait à l'appel (2.35) */
    default: return 1;
    }
}

/* « à » ou « de » suivi d'une expression, avec contraction et élision (§ 5.2). */
static void preposition(Impression *im, const char *prep, const Noeud *x) {
    int a = strcmp(prep, "à") == 0;
    /* « du réglage conservé », pas « de (le réglage conservé) » : les parenthèses de la forme compacte
     * (`(_le réglage _conservé).logo`) ne servent plus en forme littéraire autour d'un objet cherché (2.33) */
    if (!im->compact && x->type == N_GROUPE && x->nb_enfants == 1 && !(im->garder_groupe && finit_par_dont(x->enfants[0]))
        && (x->enfants[0]->type == N_CHERCHER || x->enfants[0]->type == N_AUJOURDHUI || x->enfants[0]->type == N_MAINTENANT
            || x->enfants[0]->type == N_NOMBRE || x->enfants[0]->type == N_TEXTE))
        x = x->enfants[0];   /* « de 2,5 », « d'aujourd'hui » : un atome n'a pas besoin de ses parenthèses compactes */
    if (x->type == N_FICHIER && !im->compact) {   /* « du fichier « a.jpg » » */
        aj(im, a ? "au fichier " : "du fichier ");
        expression(im, x->enfants[0]);
        return;
    }
    if (x->type == N_CHAMP && article_du_champ(im, x) != ART_AUCUN && !im->compact) {
        /* « du solde du client » : l'article du champ se contracte avec la préposition */
        const Article art = article_du_champ(im, x);
        if (art == ART_LE) aj(im, a ? "au " : "du ");
        else { aj(im, a ? "à " : "de "); aj(im, art == ART_LA ? "la " : "l'"); }
        ecrire_nom(im, x->texte, 0);
        aj(im, " ");
        const int avant = im->devine;
        im->devine = x->article_a_deviner;
        preposition(im, "de", x->enfants[0]);
        im->devine = avant;
        return;
    }
    if (x->type == N_NOM && x->article == ART_AUCUN && x->article_a_deviner && !im->compact && !im->champ_nu
        && (genre_de_variable(im, x->texte) != G_LIBRE || (im->devine && genre_de_nom(im, x->texte) != G_LIBRE))) {
        /* « du c », « de l'œuvre » : `o.titre` traduit (forme compacte) ; une variable créée avec un article, ou l'objet
         * d'un champ dont l'article a été retrouvé */
        const Genre gv = genre_de_variable(im, x->texte) != G_LIBRE ? genre_de_variable(im, x->texte) : genre_de_nom(im, x->texte);
        if (voyelle(x->texte)) aj(im, a ? "à l'" : "de l'");
        else if (gv == G_FEMININ) aj(im, a ? "à la " : "de la ");
        else aj(im, a ? "au " : "du ");
        ecrire_nom(im, x->texte, x->crochets);
        return;
    }
    if (x->type == N_NOM && x->article != ART_AUCUN) {
        if (x->article == ART_LES) { aj(im, a ? "aux " : "des "); ecrire_nom(im, x->texte, x->crochets); return; }
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
    if (!im->compact && strncmp(t, "le ", 3) == 0) {          /* « du membre conservé », « du nombre de … » */
        aj(im, a ? "au " : "du ");
        aj(im, t + 3);
    } else if (!im->compact && strncmp(t, "les ", 4) == 0) {  /* « des œuvres de bach » */
        aj(im, a ? "aux " : "des ");
        aj(im, t + 4);
    } else {
        if (!a && voyelle(t)) aj(im, "d'");
        else { aj(im, prep); aj(im, " "); }
        aj(im, t);
    }
    free(t);
}

/* « de » suivi d'une expression entière, la préposition se contractant avec son premier terme : « du prix × 2 ». */
static void de_expression(Impression *im, const Noeud *x) {
    if (x->type == N_OPERATION && !im->compact) {
        const int avant = im->garder_groupe;
        im->garder_groupe = 1;
        de_expression(im, x->enfants[0]);
        im->garder_groupe = avant;
        aj(im, " ");
        aj(im, symbole(x->op));
        aj(im, " ");
        expression(im, x->enfants[1]);
        return;
    }
    preposition(im, "de", x);
}

/* « comme une personne » (§ 13.8) ; « _comme personne ». */
static void comme(Impression *im, const Noeud *n) {
    if (!n->texte2) return;
    if (im->compact) aj(im, " _comme ");
    else aj(im, genre_de_nom(im, n->texte2) == G_FEMININ ? " comme une " : " comme un ");
    ecrire_nom(im, n->texte2, 0);
}

/* Une borne de zone (§ 22.6), de sa forme canonique (« -2.5 ») à l'écrit (« −2,5 »). */
static char *borne_ecrite(const char *canon) {
    const int neg = canon[0] == '-';
    Decimal d = dec_depuis_canonique(neg ? canon + 1 : canon);
    char *s = dec_formater(&d);
    dec_liberer(&d);
    char *w = s;
    for (const char *r = s; *r; r++) if (*r != '\'') *w++ = *r;
    *w = '\0';
    if (!neg) return s;
    char *t = grym_formater("−%s", s);
    free(s);
    return t;
}

static void nom_avec_article(Impression *im, const Noeud *n) {
    if (!im->compact && n->article == ART_AUCUN && n->article_a_deviner && !im->champ_nu) {   /* venu de la forme compacte (§ 11.4) */
        if (strcmp(n->texte, "nouveau") == 0) aj(im, "le ");
        else if (strcmp(n->texte, "nouvelle") == 0) aj(im, "la ");
        else if (n->type == N_APPEL && n->genre_appel && (n->genre_appel != 3 || voyelle(n->texte)))   /* un calcul : son genre, retenu à l'analyse */
            aj(im, voyelle(n->texte) ? "l'" : n->genre_appel == 2 ? "la " : "le ");
        else {
            const Genre g = genre_de_variable(im, n->texte);
            if (g != G_LIBRE) aj(im, voyelle(n->texte) ? "l'" : g == G_FEMININ ? "la " : "le ");
        }
    }
    if (!im->compact) {
        if (n->article == ART_LES) aj(im, "les ");
        else if (n->article == ART_LE) aj(im, "le ");
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
    if (n->op == 'm') {   /* « m est parmi les élus » ; « élus _contient m » (§ 13.9) */
        if (im->compact) {
            if (n->negation) aj(im, "_non (");
            expression(im, droite);
            aj(im, " _contient ");
            expression(im, sujet);
            if (n->negation) aj(im, ")");
        } else {
            expression(im, sujet);
            aj(im, n->negation ? " n'est pas parmi " : " est parmi ");
            expression(im, droite);
        }
        return;
    }
    if (n->op == 'k') {   /* « correspond au code du membre » ; « _correspond » (§ 16.1) */
        expression(im, sujet);
        if (im->compact) { aj(im, " _correspond "); expression(im, droite); return; }
        aj(im, " correspond ");
        preposition(im, "à", droite);
        return;
    }
    if (n->op == 'c' || n->op == 'd' || n->op == 'f') {   /* « contient », « commence par », « finit par » (§ 4.5) */
        if (im->compact) {
            if (n->negation) aj(im, "_non (");
            expression(im, sujet);
            aj(im, n->op == 'c' ? " _inclut " : n->op == 'd' ? " _commence " : " _finit ");
            expression(im, droite);
            if (n->negation) aj(im, ")");
            return;
        }
        expression(im, sujet);
        if (n->negation) aj(im, n->op == 'c' ? " ne contient pas " : n->op == 'd' ? " ne commence pas par " : " ne finit pas par ");
        else aj(im, n->op == 'c' ? " contient " : n->op == 'd' ? " commence par " : " finit par ");
        expression(im, droite);
        return;
    }
    int adjectif = strchr("PN0VFAR", n->op) != NULL;
    if (im->compact) {
        if (n->forme == 2) { expression(im, droite); return; }            /* cas : une valeur */
        if ((n->forme == 3 || n->forme == 4) && !n->negation) {          /* « est valeur » : « _est » (2.35), « = » avant */
            expression(im, sujet);
            aj(im, n->forme == 3 ? " _est " : " = ");
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
    /* 2.35 : la forme compacte dit « _est » ; un « = » compact reste un « = » (avant, devant une constante d'un « dont »,
     * il devenait « est », et le texte de l'auteur changeait) */
    if (n->forme == 1 && !sans_sujet) {                        /* écrit en symbole */
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
            ? genre_de_nom(im, sujet->texte)
            : sujet->type == N_COLONNE ? G_FEMININ   /* « la colonne « Année » de la ligne est présente » (§ 15.5) */
                                       : G_MASCULIN;
    if (g == G_LIBRE && n->genre_appel == 2) g = G_FEMININ;   /* « L'ordonnance ouverte » : l'article ne dit pas le genre, l'accord écrit si */
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
        if (im->compact && !n->crochets && n->article != ART_LES && !im->sans_marque) {
            /* 2.35 : l'article écrit, quand la traduction ne le retrouverait pas : « _nu d » (écrit sans article, mais
             * une variable connue en reçoit un), « _le relevé » (écrit avec, mais un paramètre n'en reçoit pas) */
            const int defaut = genre_de_variable(im, n->texte) != G_LIBRE;
            if (n->article == ART_AUCUN && !n->article_a_deviner && defaut) aj(im, "_nu ");
            else if (n->article == ART_LE && !defaut) aj(im, "_le ");
            else if (n->article == ART_LA && !defaut) aj(im, "_la ");
            else if (n->article == ART_L && !defaut) aj(im, "_l' ");
        }
        nom_avec_article(im, n);
        return;
    case N_TEXTE:
        if (strcmp(n->texte, "\n") == 0) { aj(im, im->compact ? "_saut_de_ligne" : "un saut de ligne"); return; }   /* 2.27 */
        ecrire_texte(im, n->texte);
        return;
    case N_ENVOI: {   /* « l'envoi à « … » avec : … et de : / du tableau : », « la lecture de « … » avec : » (2.42, 2.43) */
        if (im->compact) {   /* « _envoi « … » _avec », une valeur par ligne, « _et_du_tableau », « _fin » */
            aj(im, n->forme ? "_lecture " : "_envoi ");
            expression(im, n->enfants[0]);
            size_t debut = im->c.n;
            while (debut > 0 && im->c.d[debut - 1] != '\n') debut--;
            size_t r = 0;
            while (debut + r < im->c.n && im->c.d[debut + r] == ' ') r++;
            const size_t nq = (size_t)n->entier;
            if (!nq && n->op != 'o' && n->op != 't') return;
            aj(im, nq ? " _avec" : n->op == 'o' ? " _de" : " _du_tableau");
            size_t k = 1;
            for (size_t q = 0; q < nq; q++, k += 2) {
                aj(im, "\n");
                for (size_t e = 0; e < r + 4; e++) aj(im, " ");
                const char *cle = n->enfants[k]->texte;
                if (cle[0] == '\x01') { aj(im, "_entête "); cle++; }   /* 2.60 */
                ecrire_texte(im, cle);
                aj(im, " : ");
                expression(im, n->enfants[k + 1]);
            }
            if (nq && (n->op == 'o' || n->op == 't')) {
                aj(im, "\n");
                for (size_t e = 0; e < r; e++) aj(im, " ");
                aj(im, n->op == 'o' ? "_et_de" : "_et_du_tableau");
            }
            while (k < n->nb_enfants) {
                aj(im, "\n");
                for (size_t e = 0; e < r + 4; e++) aj(im, " ");
                if (n->op == 'o') {
                    ecrire_texte(im, n->enfants[k]->texte);
                    aj(im, " : ");
                    expression(im, n->enfants[k + 1]);
                    k += 2;
                } else {
                    expression(im, n->enfants[k]);
                    k++;
                }
            }
            aj(im, "\n");
            for (size_t e = 0; e < r; e++) aj(im, " ");
            aj(im, "_fin");
            return;
        }
        aj(im, n->forme ? "la lecture " : "l'envoi ");
        preposition(im, n->forme ? "de" : "à", n->enfants[0]);   /* « au service », « du service », « à « https://… » » */
        size_t debut = im->c.n;   /* le retrait de la ligne en cours */
        while (debut > 0 && im->c.d[debut - 1] != '\n') debut--;
        size_t r = 0;
        while (debut + r < im->c.n && im->c.d[debut + r] == ' ') r++;
        const size_t nq = (size_t)n->entier;
        size_t k = 1;
        if (nq) {
            aj(im, " avec :");
            for (size_t q = 0; q < nq; q++, k += 2) {
                aj(im, q ? ",\n" : "\n");
                for (size_t e = 0; e < r + 4; e++) aj(im, " ");
                const char *cle = n->enfants[k]->texte;
                if (cle[0] == '\x01') { aj(im, "l'en-tête "); cle++; }   /* « l'en-tête « Authorization » : … » (2.60) */
                ecrire_texte(im, cle);
                aj(im, " : ");
                expression(im, n->enfants[k + 1]);
            }
        }
        if (n->op == 'o' || n->op == 't') {
            if (nq) {
                aj(im, ",\n");
                for (size_t e = 0; e < r; e++) aj(im, " ");
                aj(im, n->op == 'o' ? "et de :" : "et du tableau :");
            } else aj(im, n->op == 'o' ? " de :" : " du tableau :");
            for (int premier = 1; k < n->nb_enfants; premier = 0) {
                aj(im, premier ? "\n" : ",\n");
                for (size_t e = 0; e < r + 4; e++) aj(im, " ");
                if (n->op == 'o') {
                    ecrire_texte(im, n->enfants[k]->texte);
                    aj(im, " : ");
                    expression(im, n->enfants[k + 1]);
                    k += 2;
                } else {
                    expression(im, n->enfants[k]);
                    k++;
                }
            }
        }
        return;
    }
    case N_XML:   /* « le XML de facture » (2.46) ; « _xml facture » ; avec ses valeurs (2.63), comme un appel */
        if (im->compact) {
            aj(im, "_xml ");
            ecrire_nom(im, n->texte + 7, 0);
            if (n->nb_enfants) {
                aj(im, "(");
                for (size_t k = 0; k < n->nb_enfants; k++) {
                    if (k) aj(im, " ; ");
                    const Noeud *x = n->enfants[k];
                    expression(im, x->type == N_GROUPE ? x->enfants[0] : x);
                }
                aj(im, ")");
            }
            return;
        }
        aj(im, "le XML de ");
        ecrire_nom(im, n->texte + 7, 0);
        for (size_t k = 0; k < n->nb_enfants; k++) {
            aj(im, k ? " et " : " ");
            const Noeud *x = n->enfants[k];
            const Noeud *xi = x->type == N_GROUPE && x->nb_enfants == 1 ? x->enfants[0] : x;
            const int appel_suivi = k + 1 < n->nb_enfants && xi->type == N_APPEL && xi->nb_enfants > 0;
            if (argument_simple(x) && !appel_suivi) {
                preposition(im, "de", x);
            } else {
                aj(im, "de (");
                expression(im, xi);
                aj(im, ")");
            }
        }
        return;
    case N_JSON_LISTE:   /* « le nombre d'éléments du champ « a » de r » ; « _nombre_éléments « a » r » (2.60) */
        if (im->compact) { aj(im, "_nombre_éléments "); ecrire_texte(im, n->texte); aj(im, " "); expression(im, n->enfants[0]); return; }
        aj(im, "le nombre d'éléments du champ ");
        ecrire_texte(im, n->texte);
        aj(im, " ");
        de_expression(im, n->enfants[0]);
        return;
    case N_CHAMP_JSON:   /* « le champ « a.b » de r » (2.42) ; « _champ « a.b » r » */
        if (im->compact) { aj(im, "_champ "); ecrire_texte(im, n->texte); aj(im, " "); expression(im, n->enfants[0]); return; }
        aj(im, "le champ ");
        ecrire_texte(im, n->texte);
        aj(im, " ");
        de_expression(im, n->enfants[0]);
        return;
    case N_CODE_QR:   /* « le code QR suisse de t » ; « _code_qr_suisse t » (2.27) */
        if (im->compact) { aj(im, n->forme ? "_code_qr_suisse " : "_code_qr "); expression(im, n->enfants[0]); return; }
        aj(im, n->forme ? "le code QR suisse " : "le code QR ");
        de_expression(im, n->enfants[0]);   /* « du contenu », « de « … » » */
        return;
    case N_BOOLEEN:
        aj(im, im->compact ? (strcmp(n->texte, "vrai") == 0 ? "_vrai" : "_faux") : n->texte);
        return;
    case N_NEGATION:
        aj(im, "−");
        expression(im, n->enfants[0]);
        return;
    case N_OPERATION:
        if (!im->compact && finit_par_dont(n->enfants[0])) {
            /* « (la somme … dont …) − (…) » : sans ses parenthèses, le « dont » prendrait l'opération pour lui (2.33) */
            aj(im, "(");
            expression(im, n->enfants[0]);
            aj(im, ")");
        } else {
            expression(im, n->enfants[0]);
        }
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
            comme(im, n);
            return;
        }
        nom_avec_article(im, n);
        for (size_t k = 0; k < n->nb_enfants; k++) {
            aj(im, k ? " et " : " ");
            const Noeud *x = n->enfants[k];
            /* un appel avec ses arguments, suivi d'un autre argument : « de (le dû de la relance) et de … », sans quoi
             * le « et de » suivant irait à lui (2.33) */
            const Noeud *xi = x->type == N_GROUPE && x->nb_enfants == 1 ? x->enfants[0] : x;
            const int appel_suivi = k + 1 < n->nb_enfants && xi->type == N_APPEL && xi->nb_enfants > 0;
            if (argument_simple(x) && !appel_suivi) {
                preposition(im, "de", x);
            } else {
                aj(im, "de (");
                expression(im, x->type == N_GROUPE && x->nb_enfants == 1 ? x->enfants[0] : x);   /* une seule paire de parenthèses */
                aj(im, ")");
            }
        }
        comme(im, n);
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
    case N_HEURE:
        aj(im, n->texte[0] == '0' ? n->texte + 1 : n->texte);   /* « 8:05 » comme écrit d'habitude ; « 0:30 » */
        return;
    case N_MAINTENANT:
        aj(im, im->compact ? "_maintenant" : "maintenant");
        return;
    case N_AUJOURDHUI:
        aj(im, im->compact ? "_aujourd'hui" : "aujourd'hui");
        return;
    case N_DUREE:   /* « 3 mois », « 3 _mois » (§ 14.2) */
        expression(im, n->enfants[0]);
        aj(im, im->compact ? " _" : " ");
        aj(im, n->texte);
        return;
    case N_CONVERSION: {   /* « le nom en majuscules » ; « nom _en_majuscules » (§ 4.5) */
        const Noeud *x = n->enfants[0];
        int simple = x->type != N_OPERATION && x->type != N_NEGATION && x->type != N_COLLAGE && x->type != N_CADRE
                     && x->type != N_COMPARAISON && x->type != N_LOGIQUE && x->type != N_ARRONDI;
        if (!simple) aj(im, "(");
        expression(im, x);
        if (!simple) aj(im, ")");
        if (n->entier == 10) {
            aj(im, im->compact ? " _en_IBAN" : " en IBAN");   /* un sigle, en capitales (2.29) */
        } else if (im->compact) {
            aj(im, " _en_");
            ecrire_nom(im, n->texte2, 0);
        } else {
            aj(im, " en ");
            aj(im, n->texte2);
        }
        return;
    }
    case N_ECART:   /* « le nombre de mois entre d1 et d2 » ; « _écart_mois(d1 ; d2) » (§ 14.6) */
        if (im->compact) {
            aj(im, n->op == 'j' ? "_écart_jours(" : n->op == 's' ? "_écart_semaines(" : n->op == 'm' ? "_écart_mois("
                   : n->op == 'n' ? "_écart_minutes(" : n->op == 'h' ? "_écart_heures(" : n->op == 'o' ? "_écart_ouvrables("
                   : n->texte && !strcmp(n->texte, "ans") ? "_écart_ans(" : "_écart_années(");   /* « ans » gardé (2.51) */
            expression(im, n->enfants[0]);
            aj(im, " ; ");
            expression(im, n->enfants[1]);
            aj(im, ")");
            return;
        }
        aj(im, voyelle(n->texte) || n->op == 'h' ? "le nombre d'" : "le nombre de ");
        aj(im, n->texte);
        aj(im, " entre ");
        expression(im, n->enfants[0]);
        aj(im, " et ");
        if (argument_simple(n->enfants[1])) expression(im, n->enfants[1]);
        else { aj(im, "("); expression(im, n->enfants[1]); aj(im, ")"); }
        return;
    case N_HASARD:   /* « un nombre au hasard de 1 à 6 » ; « _hasard(1 ; 6) », « _hasard_parmi(faces) » (§ 3.6) */
        if (im->compact) {
            aj(im, n->forme ? "_hasard_parmi(" : "_hasard(");
            for (size_t k = 0; k < n->nb_enfants; k++) { if (k) aj(im, " ; "); expression(im, n->enfants[k]); }
            aj(im, ")");
            return;
        }
        if (n->forme) { aj(im, "un élément au hasard parmi "); expression(im, n->enfants[0]); return; }
        aj(im, "un nombre au hasard ");
        preposition(im, "de", n->enfants[0]);
        aj(im, " ");
        if (argument_simple(n->enfants[1])) preposition(im, "à", n->enfants[1]);
        else { aj(im, "à ("); expression(im, n->enfants[1]); aj(im, ")"); }
        return;
    case N_TAILLE:   /* « le nombre de couleurs » ; « _nombre_de couleurs » (§ 13.9) */
        if (im->compact) { aj(im, "_nombre_de "); ecrire_nom(im, n->enfants[0]->texte, 0); return; }
        aj(im, voyelle(n->enfants[0]->texte) ? "le nombre d'" : "le nombre de ");
        ecrire_nom(im, n->enfants[0]->texte, 0);
        return;
    case N_JOINDRE:   /* « les couleurs jointes par « , » » ; « couleurs _jointes « , » » */
        expression(im, n->enfants[0]);
        aj(im, im->compact ? " _jointes " : n->forme ? " joints par " : " jointes par ");
        expression(im, n->enfants[1]);
        return;
    case N_ENSEMBLE:   /* hors d'une création : ne s'écrit pas seul */
        aj(im, "?");
        return;
    case N_COLONNE:   /* « la colonne « Nom » de la ligne » ; « _colonne(« Nom » ; ligne) » (§ 15.5) */
        if (im->compact) {
            aj(im, "_colonne(");
            expression(im, n->enfants[0]);
            aj(im, " ; ");
            expression(im, n->enfants[1]);
            aj(im, ")");
            return;
        }
        aj(im, "la colonne ");
        expression(im, n->enfants[0]);
        aj(im, " ");
        preposition(im, "de", n->enfants[1]);
        return;
    case N_RESTE:   /* « le reste de 17 par 5 » ; « _reste(17 ; 5) » (§ 3.5) */
        if (im->compact) {
            aj(im, "_reste(");
            for (size_t k = 0; k < 2; k++) {
                if (k) aj(im, " ; ");
                const Noeud *x = n->enfants[k];
                expression(im, x->type == N_GROUPE ? x->enfants[0] : x);
            }
            aj(im, ")");
            return;
        }
        aj(im, "le reste ");
        de_expression(im, n->enfants[0]);
        if (argument_simple(n->enfants[1])) {
            aj(im, " par ");
            expression(im, n->enfants[1]);
        } else {
            aj(im, " par (");
            expression(im, n->enfants[1]);
            aj(im, ")");
        }
        return;
    case N_ARRONDI: {   /* « l'arrondi de x à 0,05 vers le bas » ; « _arrondi_bas(x ; 0,05) » (§ 3.4) */
        const char *mode = n->op == 'b' ? "vers le bas" : n->op == 'h' ? "vers le haut" : n->op == 'e' ? "au pair" : NULL;
        if (im->compact) {
            aj(im, n->op == 'b' ? "_arrondi_bas(" : n->op == 'h' ? "_arrondi_haut(" : n->op == 'e' ? "_arrondi_pair(" : "_arrondi(");
            for (size_t k = 0; k < 2; k++) {
                if (k) aj(im, " ; ");
                const Noeud *x = n->enfants[k];
                expression(im, x->type == N_GROUPE ? x->enfants[0] : x);
            }
            aj(im, ")");
            return;
        }
        aj(im, n->forme ? "l'arrondi " : "arrondi ");
        de_expression(im, n->enfants[0]);
        aj(im, " ");
        if (argument_simple(n->enfants[1])) {
            preposition(im, "à", n->enfants[1]);
        } else {
            aj(im, "à (");
            expression(im, n->enfants[1]);
            aj(im, ")");
        }
        if (mode) { aj(im, " "); aj(im, mode); }
        return;
    }
    case N_ABSENT:
        aj(im, im->compact ? (n->forme == 2 ? "_absente" : "_absent") : n->forme == 2 ? "absente" : "absent");   /* 2.36 : l'accord passe */
        return;
    case N_CHAMP_DONT: {
        /* un chemin (§ 16.4) : texte2, les liens de l'entité examinée vers le champ, séparés par U+001C */
        char *liens[8];
        size_t nl = 0;
        char *copie = n->texte2 ? grym_dupliquer(n->texte2) : NULL;
        for (char *s1 = copie, *s2; s1 && nl < 8; s1 = s2) {
            s2 = strchr(s1, '\x1c');
            if (s2) *s2++ = '\0';
            liens[nl++] = s1;
        }
        if (im->compact) {   /* « membre.catégorie.nom » */
            for (size_t k = 0; k < nl; k++) { ecrire_nom(im, liens[k], 0); aj(im, "."); }
            ecrire_nom(im, n->texte, 0);
            free(copie);
            return;
        }
        Article art = n->article;   /* l'article s'écrit toujours : « dont le solde … » */
        if (art == ART_AUCUN) art = voyelle(n->texte) ? ART_L : genre_de_nom(im, n->texte) == G_FEMININ ? ART_LA : ART_LE;
        aj(im, art == ART_LA ? "la " : art == ART_L ? "l'" : "le ");
        ecrire_nom(im, n->texte, 0);
        for (size_t k = nl; k-- > 0;) {   /* « de l'écriture », « de la catégorie du membre » */
            aj(im, voyelle(liens[k]) ? " de l'" : genre_de_nom(im, liens[k]) == G_FEMININ ? " de la " : " du ");
            ecrire_nom(im, liens[k], 0);
        }
        free(copie);
        return;
    }
    case N_DOCUMENT: {   /* « le document de quittance » ; « _document de_quittance » (2.25) */
        const char *nd = strncmp(n->texte, "document ", 9) == 0 ? n->texte + 9 : n->texte;
        if (im->compact) { aj(im, "_document "); ecrire_nom(im, nd, 0); }
        else { aj(im, "le document "); aj(im, nd); }
        return;
    }
    case N_ECRAN:   /* « l'écran » ; « _écran » (§ 22.2) */
        aj(im, im->compact ? "_écran" : "l'écran");
        return;
    case N_CHERCHER: {
        Genre g = genre_de_nom(im, n->texte);
        int fem = g == G_FEMININ;
        /* « de bach » (§ 16.10), « les interprètes de o » (§ 16.13) : l'objet en dernier enfant */
        int inverse = n->op == 'I' || n->op == 'M';
        const Noeud *objet = inverse ? n->enfants[n->nb_enfants - 1] : NULL;
        if (n->forme >= 3 && n->forme <= 6) {   /* « la somme des montants des cotisations conservées », « … des lignes du
                                                   compte » ; « la moyenne des soldes », « le plus grand solde » (§ 16.4) */
            static const char *const C[] = { "_somme_de ", "_moyenne_de ", "_plus_grand_de ", "_plus_petit_de " };
            if (im->compact) {
                aj(im, C[n->forme - 3]);
                ecrire_nom(im, n->texte2, 0);
                aj(im, " _de ");
                ecrire_nom(im, n->texte, 0);
                if (inverse) { aj(im, " _de "); expression(im, objet); }
                else aj(im, n->negation ? " _supprimé" : " _conservé");
            } else {
                const char *esp = strchr(n->texte2, ' ');
                const size_t l = esp ? (size_t)(esp - n->texte2) : strlen(n->texte2);
                const char der = l ? n->texte2[l - 1] : 0;
                char *pc = n->forme >= 5 || der == 's' || der == 'x' || der == 'z'
                         ? grym_dupliquer(n->texte2) : grym_formater("%.*ss%s", (int)l, n->texte2, esp ? esp : "");
                if (n->forme >= 5) {
                    int f2 = genre_de_nom(im, n->texte2) == G_FEMININ;
                    aj(im, n->forme == 5 ? (f2 ? "la plus grande " : "le plus grand ") : (f2 ? "la plus petite " : "le plus petit "));
                } else {
                    aj(im, n->forme == 3 ? "la somme des " : "la moyenne des ");
                }
                aj(im, pc);
                free(pc);
                const char *pl = NULL;
                for (size_t k = 0; k < im->nb_pluriels && !pl; k++)
                    if (strcmp(im->pluriels[k].feminin, n->texte) == 0) pl = im->pluriels[k].masculin;
                char *p = pl ? grym_dupliquer(pl) : grym_formater("%ss", n->texte);
                aj(im, " des ");
                aj(im, p);
                free(p);
                if (inverse) { aj(im, " "); preposition(im, "de", objet); }
                else if (n->negation) aj(im, fem ? " supprimées" : " supprimés");
                else aj(im, fem ? " conservées" : " conservés");
            }
        } else if (n->forme == 2 && n->op == 'M') {   /* « le nombre d'interprètes de o » */
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
            if (n->forme == 1 || n->forme == 7) {
                const char *art = voyelle(n->texte) ? "l'" : fem ? "la " : "le ";
                if (im->compact) aj(im, "_");
                aj(im, art);
                ecrire_nom(im, n->texte, 0);
                aj(im, im->compact ? (n->negation ? " _supprimé" : " _conservé")
                       : n->negation ? (fem ? " supprimée" : " supprimé") : fem ? " conservée" : " conservé");
            }
            if (n->forme == 7) {   /* « au plus grand solde », « à la plus petite cotisation » (§ 16.4) */
                if (im->compact) aj(im, n->entier ? " _au_plus_grand " : " _au_plus_petit ");
                else {
                    int f2 = genre_de_nom(im, n->texte2) == G_FEMININ;
                    aj(im, n->entier ? (f2 ? " à la plus grande " : " au plus grand ") : (f2 ? " à la plus petite " : " au plus petit "));
                }
                ecrire_nom(im, n->texte2, 0);
            }
            if (n->forme == 0 && inverse) {   /* début de « Pour chaque œuvre de bach » */
                if (im->compact) { aj(im, " _de "); expression(im, objet); }
                else { aj(im, " "); preposition(im, "de", objet); }
            }
        }
        if (n->nb_enfants > (size_t)inverse) {
            aj(im, im->compact ? " _dont " : " dont ");
            expression(im, n->enfants[0]);
        }
        if (n->texte2 && n->forme < 3) {   /* formes 3 à 7 : texte2 est le champ de l'agrégat, pas un tri */
            aj(im, im->compact ? " _par " : ", par ");
            ecrire_nom(im, n->texte2, 0);
            if (n->entier & 1) aj(im, im->compact ? " _décroissant" : " décroissant");
        }
        return;
    }
    case N_CADRE:   /* « le nom sur 20 à droite » ; « nom _sur 20 _droite » (§ 4.2) */
        expression(im, n->enfants[0]);
        aj(im, im->compact ? " _sur " : " sur ");
        expression(im, n->enfants[1]);
        if (n->forme) aj(im, n->forme == 2 ? (im->compact ? " _droite" : " à droite")
                                           : (im->compact ? " _gauche" : " à gauche"));
        return;
    case N_COLLAGE:   /* « a suivi de b » ; « a _suivi b » (§ 4.4) */
        expression(im, n->enfants[0]);
        if (im->compact) {
            aj(im, " _suivi ");
            expression(im, n->enfants[1]);
        } else {
            aj(im, " suivi ");
            preposition(im, "de", n->enfants[1]);
        }
        return;
    case N_ELISION:   /* « de x », « que x » ; « _de x », « _que x » (§ 4.4) */
        aj(im, n->op == 'q' ? (im->compact ? "_que " : "que ") : (im->compact ? "_de " : "de "));
        {   /* « puis de nom » : cette tournure ne contracte pas (« du nom » ne se relit pas) ; le nom y reste nu (§ 11.4) */
            const int nu = im->champ_nu;
            im->champ_nu = n->op != 'q';
            expression(im, n->enfants[0]);
            im->champ_nu = nu;
        }
        return;
    case N_MOTIF:   /* « le motif de l'échec » ; « _motif » (§ 18) */
        aj(im, im->compact ? "_motif" : "le motif de l'échec");
        return;
    case N_REPONSE: {   /* « la réponse en nombre à « Âge ? » » ; « _réponse (nombre) « Âge ? » » (§ 17) */
        int typé = strcmp(n->texte2, "texte") != 0;
        aj(im, im->compact ? "_réponse " : "la réponse ");
        if (typé) {
            aj(im, im->compact ? "(" : "en ");
            if (im->compact) {
                for (const char *p = n->texte2; *p; p++) { char c2[2] = { *p == ' ' ? '_' : *p, 0 }; aj(im, c2); }
                aj(im, ") ");
            } else {
                aj(im, n->texte2);
                aj(im, " ");
            }
        }
        if (!im->compact) aj(im, "à ");
        expression(im, n->enfants[0]);
        return;
    }
    case N_FICHIER:
        aj(im, im->compact ? "_fichier " : "le fichier ");
        expression(im, n->enfants[0]);
        return;
    case N_CHAMP:
        if (im->compact) {
            const Noeud *o = n->enfants[0];
            int simple = o->type == N_NOM || o->type == N_CHAMP || o->type == N_APPEL || o->type == N_GROUPE || o->type == N_ECRAN;
            if (im->sans_marque_objet) {
                /* rien : en tête d'une phrase « _gagne », l'objet reste tel quel */
            } else if (o->type == N_NOM && !o->crochets && genre_de_variable(im, o->texte) == G_LIBRE
                       && genre_de_nom(im, o->texte) == G_LIBRE && n->enfants[0] == o) {
                /* « _le patient.nom » (2.36) : l'article écrit d'un objet que la traduction laisserait nu */
                if (o->article == ART_LE) aj(im, "_le ");
                else if (o->article == ART_LA) aj(im, "_la ");
                else if (o->article == ART_L) aj(im, "_l' ");
            } else if (o->type == N_NOM && !o->crochets && o->article == ART_AUCUN && !o->article_a_deviner
                       && (genre_de_variable(im, o->texte) != G_LIBRE || genre_de_nom(im, o->texte) != G_LIBRE)) {
                aj(im, "_nu ");   /* « l'année de date », écrit sans article : « _nu date.année » (2.36) */
            }
            if (!simple) aj(im, "(");
            const int avant = im->sans_marque;
            im->sans_marque = o->type == N_NOM;   /* seulement l'objet lui-même : ce qu'il contient garde ses marques */
            expression(im, o);
            im->sans_marque = avant;
            if (!simple) aj(im, ")");
            aj(im, ".");
            ecrire_nom(im, n->texte, 0);
            return;
        }
        {
            const Article art = article_du_champ(im, n);
            if (art == ART_LE) aj(im, "le ");
            else if (art == ART_LA) aj(im, "la ");
            else if (art == ART_L) aj(im, "l'");
        }
        ecrire_nom(im, n->texte, 0);
        aj(im, " ");
        {
            const int avant = im->devine, nu = im->champ_nu;
            im->devine = article_du_champ(im, n) != ART_AUCUN && n->article_a_deviner;
            im->champ_nu = article_du_champ(im, n) == ART_AUCUN;   /* un champ nu garde son objet nu */
            preposition(im, "de", n->enfants[0]);
            im->devine = avant;
            im->champ_nu = nu;
        }
        return;
    case N_NOUVEAU: {
        if (im->compact) {
            aj(im, "_nouveau ");
            ecrire_nom(im, n->texte, 0);
            if (n->op == 'S') aj(im, " _saisi");
            if (n->forme) aj(im, " _avec");
            return;
        }
        Genre g = genre_de_nom(im, n->texte);
        aj(im, g == G_FEMININ ? "une nouvelle " : voyelle(n->texte) ? "un nouvel " : "un nouveau ");
        ecrire_nom(im, n->texte, 0);
        if (n->op == 'S') aj(im, g == G_FEMININ ? " saisie" : " saisi");
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
    if (ch->texte3 && ch->forme != 3) {   /* valeurs permises (§ 16.15) : « (« a », « b » ou « c ») » ; « (« a » ; « b » ; « c ») » */
        size_t nb = 1;
        for (const char *p = ch->texte3; *p; p++) nb += *p == '\x1e';
        size_t k = 0;
        for (const char *p = ch->texte3; ; k++) {
            const char *q = strchr(p, '\x1e');
            char *v = q ? grym_formater("%.*s", (int)(q - p), p) : grym_dupliquer(p);
            if (k) aj(im, im->compact ? " ; " : k + 1 == nb ? " ou " : ", ");
            ecrire_texte(im, v);
            free(v);
            if (ch->couleurs && k < strlen(ch->couleurs) && ch->couleurs[k] != '-') {   /* « « payée » en vert » ; « _vert » */
                static const char *const NOMS[] = {"bleu", "vert", "turquoise", "violet", "gris", "rouge", "orange"};
                const char *l = strchr("bvtpgro", ch->couleurs[k]);
                if (l) { aj(im, im->compact ? " _" : " en "); aj(im, NOMS[l - "bvtpgro"]); }
            }
            if (!q) break;
            p = q + 1;
        }
    } else if (im->compact) ecrire_nom(im, ch->texte2, 0);
    else aj(im, ch->texte2);   /* « (vrai ou faux) » : un type, pas un nom, jamais entre crochets */
    aj(im, ")");
    if (ch->op == 'U') aj(im, im->compact ? " _unique" : ", unique");
    if (ch->entier & 1) aj(im, im->compact ? " _facultatif" : ch->forme == 2 ? ", facultative" : ", facultatif");
    if (ch->transcrit_de) {   /* « , transcrite du mémo » ; « _transcrit mémo » (§ 15.8) */
        if (im->compact) { aj(im, " _transcrit "); ecrire_nom(im, ch->transcrit_de, 0); }
        else {
            aj(im, ch->forme == 2 ? ", transcrite " : ", transcrit ");
            const int fem = genre_de_nom(im, ch->transcrit_de) == G_FEMININ;
            const int voy = voyelle(ch->transcrit_de);
            aj(im, voy ? "de l'" : fem ? "de la " : "du ");
            aj(im, ch->transcrit_de);
        }
    }
    if (ch->entier & ABANDONNE) aj(im, im->compact ? " _abandonné" : ch->forme == 2 ? ", abandonnée" : ", abandonné");   /* 2.49 */
    if (ch->entier & 2)
        aj(im, im->compact ? " _disparaît_avec"
               : genre_de_nom(im, ch->texte2) == G_FEMININ ? ", et disparaît avec elle" : ", et disparaît avec lui");
    if (ch->nb_enfants) {   /* valeur de départ (§ 16.7) */
        aj(im, im->compact ? " _départ " : ", ");
        expression(im, ch->enfants[0]);
        if (!im->compact) aj(im, " au départ");
    }
    if (ch->ancien) {   /* « , anciennement « client » » ; « _anciennement « client » » (§ 16.7) */
        aj(im, im->compact ? " _anciennement " : ", anciennement ");
        ecrire_texte(im, ch->ancien);
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
    initiale(t, 0);
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
            if (de == 1 || (de == 2 && k == 0)) aj(im, "d'");   /* 2 : seul le premier suit le « de » du nom de l'action */
            aj(im, p->forme == 2 ? "une " : "un ");
        }
        ecrire_nom(im, p->texte, 0);
        if (p->texte2) { aj(im, im->compact ? "_" : " "); aj(im, p->texte2); }   /* une chose horodatée */
    }
}

/* La règle du guide pour une forme compacte sans marque (analyseur.c, phrase_courte_possible) : une branche d'une seule
 * phrase sans bloc, « Afficher » ou « Rendre », revient en forme courte. */
static int courte_par_defaut(const Noeud *corps) {
    if (!corps || corps->nb_enfants != 1) return 0;
    const Noeud *p = corps->enfants[0];
    for (size_t k = 0; k < p->nb_enfants; k++)
        if (p->enfants[k] && p->enfants[k]->type == N_BLOC) return 0;
    return p->type == P_AFFICHAGE || p->type == P_RENDRE;
}

/* « _court », « _bloc » (2.35) : la forme littéraire d'une branche, dite seulement quand la règle du guide se tromperait. */
static void marque_branche(Impression *im, const Noeud *corps, int en_bloc) {
    const int defaut_court = courte_par_defaut(corps);
    if (!en_bloc && !defaut_court) aj(im, " _court");
    else if (en_bloc && defaut_court) aj(im, " _bloc");
}

static void si(Impression *im, const Noeud *n, int niveau, int sinon_si) {
    const Noeud *cond = n->enfants[0];
    if (im->compact) {
        aj(im, sinon_si ? "_sinon_si " : "_si ");
        expression(im, cond);
        aj(im, " _alors");
        marque_branche(im, n->enfants[1], n->forme & 1);
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
            marque_branche(im, autre, n->forme & 4);
            branche(im, autre, 0, niveau);
        } else {
            aj(im, "Sinon");
            branche(im, autre, !(n->forme & 4), niveau);
        }
    }
    if (!sinon_si) fin_compacte(im, niveau);
}


/* « , actif si … », « , visible si … » : les états d'un élément d'écran, ses derniers enfants (§ 22.6). */
/* La place et la taille d'un élément en position libre (§ 22.9) : « , à 96 de la gauche et 16 du haut, large de 240,
 * haut de 80 » ; « _place 96 16 _largeur 240 _hauteur 80 ». */
static void place_ecran(Impression *im, const Noeud *el, int feminin) {
    char *x;
    if (el->place_dite & 1) {
        x = im->compact ? grym_formater(" _place %d %d", el->place[0], el->place[1])
                        : grym_formater(", à %d de la gauche et %d du haut", el->place[0], el->place[1]);
        aj(im, x);
        free(x);
    }
    if (el->place_dite & 8) {   /* 2.34 */
        x = grym_formater(im->compact ? " _largeur_caractères %d" : ", large de %d caractères", el->place[2]);
        aj(im, x);
        free(x);
    }
    if (el->place_dite & 2) {
        x = grym_formater(im->compact ? " _largeur %d" : ", large de %d", el->place[2]);
        aj(im, x);
        free(x);
    }
    if (el->place_dite & 4) {
        x = im->compact ? grym_formater(" _hauteur %d", el->place[3])
                        : grym_formater(", %s de %d", feminin ? "haute" : "haut", el->place[3]);
        aj(im, x);
        free(x);
    }
}

/* Une colonne choisie écrite sans article (venue de la forme compacte, « titre », « date de naissance de compositeur ») :
 * refaite depuis son chemin (« compositeur ␟ date de naissance », de l'objet vers le champ) avec les genres retenus,
 * « la date de naissance du compositeur ». NULL si elle a déjà son article, ou si un genre manque. */
static char *colonne_avec_articles(Impression *im, const char *ecrit, const char *chemin, const char *fin) {
    if (strncmp(ecrit, "le ", 3) == 0 || strncmp(ecrit, "la ", 3) == 0 || strncmp(ecrit, "l'", 2) == 0
        || strncmp(ecrit, "les ", 4) == 0)
        return NULL;
    char *segs[16];
    size_t ns = 0;
    for (const char *p = chemin; p < fin && ns < 16;) {
        const char *f2 = memchr(p, '\x1f', (size_t)(fin - p));
        if (!f2) f2 = fin;
        segs[ns++] = grym_formater("%.*s", (int)(f2 - p), p);
        p = f2 < fin ? f2 + 1 : fin;
    }
    Chaine c = {0};
    int ok = ns > 0;
    for (size_t k = ns; ok && k-- > 0;) {   /* du champ montré vers l'objet de la liste */
        const Genre g = genre_de_nom(im, segs[k]);
        if (g == G_LIBRE) { ok = 0; break; }
        const int v = voyelle(segs[k]);
        if (k + 1 == ns) chaine_ajouter(&c, v ? "l'" : g == G_FEMININ ? "la " : "le ");
        else chaine_ajouter(&c, v ? " de l'" : g == G_FEMININ ? " de la " : " du ");
        chaine_ajouter(&c, segs[k]);
    }
    for (size_t k = 0; k < ns; k++) free(segs[k]);
    char *r = chaine_rendre(&c);
    if (!ok) { free(r); return NULL; }
    return r;
}

static void etats_ecran(Impression *im, const Noeud *el, int feminin) {
    place_ecran(im, el, feminin);
    if (el->type == N_TEXTE_ECRAN && (el->entier & 2048)) aj(im, im->compact ? " _capitales" : ", en petites capitales");   /* 2.20 */
    if (el->type == N_BOUTON && (el->entier & 256)) aj(im, im->compact ? " _principal" : ", principal");   /* 2.14 */
    if (el->type == N_BOUTON && (el->entier & 512)) aj(im, im->compact ? " _tuile" : ", en tuile");     /* 2.15 */
    if (el->type == N_BOUTON && (el->entier & 1024)) aj(im, im->compact ? " _côté_titre" : ", à côté du titre");   /* 2.17 */
    if (el->type == N_BOUTON && el->indication) { aj(im, im->compact ? " _description " : ", avec la description "); ecrire_texte(im, el->indication); }
    if (el->type == N_BOUTON && el->texte2) { aj(im, im->compact ? " _image " : ", avec l'image "); ecrire_texte(im, el->texte2); }
    for (size_t q = 0; q < 2 * el->nb_enfants; q++) {   /* « actif si » avant « visible si », quel que soit l'ordre écrit */
        const Noeud *x = el->enfants[q % el->nb_enfants];
        if (x->type != N_SI_ECRAN || x->forme != (q < el->nb_enfants ? 1 : 2)) continue;
        if (im->compact) aj(im, x->forme == 1 ? " _actif_si " : " _visible_si ");
        else aj(im, x->forme == 1 ? (feminin ? ", active si " : ", actif si ") : ", visible si ");
        expression(im, x->enfants[0]);
    }
}

static void phrase(Impression *im, const Noeud *n, int niveau) {
    int c = im->compact;
    retrait(im, niveau);
    switch (n->type) {
    case P_XML:   /* un élément d'un XML déclaré (2.46) ; la virgule du dernier devient un point (P_ACTION 'X') */
        if (c) {   /* « _attribut « n » v », « _contenu v », « _élément « n » [v | _enfants … _fin] » */
            aj(im, n->op == 'a' ? "_attribut " : n->op == 'c' ? "_contenu " : "_élément ");
            if (n->op != 'c') { ecrire_texte(im, n->texte); if (n->nb_enfants) aj(im, " "); }
            if (n->op == 'b') { aj(im, "_enfants\n"); bloc(im, n->enfants[0], niveau + 1); fin_compacte(im, niveau); return; }
            if (n->nb_enfants) expression(im, n->enfants[0]);
            aj(im, "\n");
            return;
        }
        if (n->op == 'a') { aj(im, "l'attribut "); ecrire_texte(im, n->texte); aj(im, " : "); expression(im, n->enfants[0]); aj(im, ",\n"); return; }
        if (n->op == 'c') { aj(im, "le contenu : "); expression(im, n->enfants[0]); aj(im, ",\n"); return; }
        aj(im, "l'élément ");
        ecrire_texte(im, n->texte);
        if (n->op == 'b') { aj(im, " :\n"); bloc(im, n->enfants[0], niveau + 1); return; }
        if (n->nb_enfants) { aj(im, " : "); expression(im, n->enfants[0]); }
        aj(im, ",\n");
        return;
    case P_REMARQUE:
        aj(im, c ? "#" : "Remarque :");
        if (n->texte[0]) { aj(im, " "); aj(im, n->texte); }
        aj(im, "\n");
        return;
    case P_CREATION:
    case P_MODIFICATION:
        if (n->forme == 9 && n->nb_enfants && n->enfants[0]->type == N_APPEL) {
            /* « Quittancer 120, qui donne la q. » ; « quittancer(120) _qui_donne q » (2.57) : l'appel, puis la variable */
            Noeud appel_seul = *n->enfants[0];
            appel_seul.type = P_APPEL;
            const size_t debut = im->c.n;
            phrase(im, &appel_seul, 0);
            while (im->c.n > debut && (im->c.d[im->c.n - 1] == '\n' || im->c.d[im->c.n - 1] == '.')) im->c.n--;
            im->c.d[im->c.n] = '\0';
            if (c) {
                aj(im, " _qui_donne ");
                aj(im, n->article == ART_LE ? "_le " : n->article == ART_LA ? "_la " : n->article == ART_L ? "_l' " : "");
                ecrire_nom(im, n->texte, 0);
                aj(im, "\n");
            }
            else {
                aj(im, ", qui donne ");
                if (n->article != ART_AUCUN) {   /* « la q », en minuscule au milieu de la phrase */
                    char *art = grym_dupliquer(article_ecrit(n->article, genre_de_nom(im, n->texte), n->texte));
                    if (art[0] >= 'A' && art[0] <= 'Z') art[0] = (char)(art[0] + 32);
                    aj(im, art);
                    free(art);
                }
                ecrire_nom(im, n->texte, 0);
                aj(im, ".\n");
            }
            return;
        }
        /* fallthrough */
    {
        if (n->type == P_CREATION && (n->article == ART_LE || n->article == ART_LA || n->article == ART_L)) {
            /* « L'effectif vaut 12. » : une variable avec un article, que la forme compacte perd (§ 11.4) */
            im->variables = realloc(im->variables, (im->nb_var + 1) * sizeof *im->variables);
            im->genres_var = realloc(im->genres_var, (im->nb_var + 1) * sizeof *im->genres_var);
            im->variables[im->nb_var] = n->texte;
            im->genres_var[im->nb_var++] = n->article == ART_LA ? G_FEMININ : n->article == ART_LE ? G_MASCULIN
                                          : genre_article(n->article) != G_LIBRE ? genre_article(n->article) : G_MASCULIN;
        }
        if (n->article == ART_LES) {   /* ensembles (§ 13.9) */
            const Noeud *v = n->enfants[0];
            int ajout = v->type == N_AJOUT;
            aj(im, c ? "_les " : "Les ");
            ecrire_nom(im, n->texte, 0);
            if (ajout) aj(im, c ? (v->op == 'g' ? " _gagnent " : " _perdent ") : (v->op == 'g' ? " gagnent " : " perdent "));
            else if (n->type == P_CREATION) aj(im, c ? " << " : " valent ");
            else aj(im, c ? " _deviennent " : " deviennent ");
            if (ajout) expression(im, v->enfants[1]);
            else if (v->type == N_ENSEMBLE && v->nb_enfants == 0) aj(im, c ? "_ensemble_vide" : "un ensemble vide");
            else if (v->type == N_ENSEMBLE) {
                for (size_t k = 0; k < v->nb_enfants; k++) {
                    if (k) aj(im, c ? " ; " : k + 1 == v->nb_enfants ? " et " : ", ");
                    expression(im, v->enfants[k]);
                }
            } else expression(im, v);
            aj(im, c ? "\n" : ".\n");
            if (n->type == P_CREATION) retenir(im, n->texte, G_LIBRE);
            return;
        }
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
    case P_EFFACER:
        aj(im, c ? "_effacer\n" : "Effacer l'écran.\n");
        return;
    case P_ECRAN: {   /* « L'écran des compositeurs montre : … » ; « _écran des_compositeurs … _fin » (§ 22.1) */
        for (size_t k = 0; k < n->nb_enfants; k++)   /* une condition peut citer une zone écrite plus bas (§ 22.6) */
            if (n->enfants[k]->type == N_NOM)
                retenir(im, n->enfants[k]->texte, n->enfants[k]->forme == 2 ? G_FEMININ : G_MASCULIN);
        const int doc = n->op == 'D';   /* « Le document de quittance montre : » ; « _document de_quittance » (2.25) */
        const char *nom_e = doc && strncmp(n->texte, "document ", 9) == 0 ? n->texte + 9 : n->texte;
        if (n->op == 'P') {   /* « La partie « en-tête » montre : » ; « _partie « en-tête » … _fin » (2.56) */
            aj(im, c ? "_partie " : "La partie ");
            ecrire_texte(im, n->texte + strlen("partie "));
            aj(im, c ? "\n" : " montre :\n");
        } else if (c) {
            aj(im, doc ? "_document " : "_écran ");
            ecrire_nom(im, nom_e, 0);
            if (n->texte2) { aj(im, " "); ecrire_texte(im, n->texte2); }
            if (n->titre_calcule) { aj(im, " _titre "); expression(im, n->titre_calcule); }   /* 2.33 */
            if (n->taille_titre) aj(im, n->taille_titre > 0 ? " _en_grand" : " _en_petit");   /* § 22.5 */
            aj(im, "\n");
        } else {
            aj(im, doc ? "Le document " : "L'écran ");
            aj_sigles(im, nom_e);
            if (n->texte2 || n->taille_titre || n->titre_calcule) {   /* « , « Recherche » en grand, » (§ 22.1, § 22.5) */
                aj(im, ",");
                if (n->texte2) { aj(im, " "); ecrire_texte(im, n->texte2); }
                if (n->titre_calcule) { aj(im, " "); expression(im, n->titre_calcule); }   /* « (…) », un titre calculé (2.33) */
                if (n->taille_titre) aj(im, n->taille_titre > 0 ? " en grand" : " en petit");
                aj(im, ",");
            }
            aj(im, " montre :\n");
        }
        size_t dernier = 0;   /* le dernier élément véritable finit la phrase par un point */
        for (size_t k = 0; k < n->nb_enfants; k++) if (n->enfants[k]->type != N_DISPOSITION) dernier = k;
        int profondeur = 0;
        for (size_t k = 0; k < n->nb_enfants; k++) {
            const Noeud *el = n->enfants[k];
            /* un élément venu d'une partie (2.56) : sa place dans la source est dans la déclaration de la partie ; on écrit
             * « la partie « … » » une fois, et on passe tout ce qu'elle a déplié, blocs qu'elle a ouverts compris */
            const Noeud *part = NULL;
            if (n->op != 'P')
                for (size_t q = 0; q < im->nb_parties && !part; q++)
                    if (el->debut >= im->parties[q]->debut && el->debut < im->parties[q]->fin) part = im->parties[q];
            if (part) {
                int ouverts = 0;
                size_t kk = k;
                for (;;) {
                    const Noeud *x = n->enfants[kk];
                    if (x->type == N_DISPOSITION) ouverts += x->forme == 0 ? -1 : 1;
                    if (kk + 1 >= n->nb_enfants) break;
                    const Noeud *y = n->enfants[kk + 1];
                    const int dedans = y->debut >= part->debut && y->debut < part->fin;
                    if (!dedans && !(y->type == N_DISPOSITION && y->forme == 0 && ouverts > 0)) break;
                    kk++;
                }
                retrait(im, niveau + 1 + profondeur);
                aj(im, c ? "_partie " : "la partie ");
                ecrire_texte(im, part->texte + strlen("partie "));
                aj(im, c ? "\n" : kk < dernier ? ",\n" : ".\n");
                k = kk;
                continue;
            }
            if (el->type == N_DISPOSITION) {   /* « côte à côte : », « l'un sous l'autre : » ; fin : « _fin » en compact */
                if (el->forme == 0) {
                    profondeur--;
                    if (c) { retrait(im, niveau + 1 + profondeur); aj(im, "_fin\n"); }
                    continue;
                }
                retrait(im, niveau + 1 + profondeur);
                /* le nom du bloc, sa place s'il est dans un bloc libre (§ 22.9), puis « : » (en compact : rien) */
                if (el->forme == 3 || el->forme == 7) {   /* « le groupe « Adresse » : » (§ 22.7) ; « le menu « Membres » : » (§ 22.12) */
                    aj(im, el->forme == 7 ? (c ? "_menu " : "le menu ") : (c ? "_groupe " : "le groupe "));
                    ecrire_texte(im, el->texte);
                } else if (el->forme == 4)   /* « en position libre : » ; « _position_libre » (§ 22.9) */
                    aj(im, c ? "_position_libre" : "en position libre");
                else if (el->forme == 5)   /* « les onglets : » ; « _onglets » (§ 22.10) */
                    aj(im, c ? "_onglets" : "les onglets");
                else if (el->forme == 6) {   /* « l'onglet « Identité » : » ; « _onglet « Identité » » */
                    aj(im, c ? "_onglet " : "l'onglet ");
                    ecrire_texte(im, el->texte);
                }
                else
                    aj(im, el->forme == 1 ? (c ? "_côte_à_côte" : "côte à côte") : (c ? "_l'un_sous_l'autre" : "l'un sous l'autre"));
                place_ecran(im, el, 0);
                if (el->forme == 3 && (el->entier & 1)) aj(im, c ? " _inversé" : ", en couleur inversée");   /* 2.18 */
                aj(im, c ? "\n" : " :\n");
                profondeur++;
                continue;
            }
            retrait(im, niveau + 1 + profondeur);
            if (el->type == N_BOUTON) {
                aj(im, c ? "_bouton " : "un bouton ");
                ecrire_texte(im, el->texte);
                etats_ecran(im, el, 0);
            } else if (el->type == N_IMAGE_ECRAN) {   /* « l'image « plan.png » » ; « _image « plan.png » » (§ 22.8) */
                if (el->texte) {
                    aj(im, c ? "_image " : "l'image ");
                    ecrire_texte(im, el->texte);
                } else {   /* « l'image du portrait du compositeur choisi de l'écran » ; « _image_de … » (§ 22.8) */
                    const Noeud *src = el->enfants[0]->enfants[0];
                    if (c) { aj(im, "_image_de "); expression(im, src); }
                    else { aj(im, "l'image "); de_expression(im, src); }
                }
                etats_ecran(im, el, 1);
            } else if (el->type == N_TEXTE_ECRAN) {
                const Noeud *calcul = NULL;   /* le texte calculé (2.24) : « le texte » suivi d'une valeur */
                for (size_t q = 0; q < el->nb_enfants; q++)
                    if (el->enfants[q]->type == N_SI_ECRAN && el->enfants[q]->forme == 4 && el->enfants[q]->nb_enfants)
                        calcul = el->enfants[q]->enfants[0];
                if (calcul && (el->entier & 4096)) {   /* « la section paiement de … » ; « _section_paiement … » (2.28) */
                    if (c) { aj(im, "_section_paiement "); expression(im, calcul); }
                    else { aj(im, "la section paiement "); de_expression(im, calcul); }
                } else if (calcul) { aj(im, c ? "_texte_calculé " : "le texte "); expression(im, calcul); }
                else { aj(im, c ? "_texte " : "le texte "); ecrire_texte(im, el->texte); }
                etats_ecran(im, el, 0);
            } else if (el->type == N_NOM) {   /* zone : « un pays (texte), « Suisse » au départ, facultatif » */
                int fz = el->forme == 2;
                retenir(im, el->texte, fz ? G_FEMININ : G_MASCULIN);
                aj(im, (el->entier & 16) ? (c ? "_des " : "des ") : c ? (fz ? "_une " : "_un ") : (fz ? "une " : "un "));
                ecrire_nom(im, el->texte, 0);
                aj(im, " (");
                if (el->texte3) {   /* valeurs permises (§ 22.7) : « (« a », « b » ou « c ») » ; « (« a » ; « b ») » */
                    size_t nb = 1;
                    for (const char *p = el->texte3; *p; p++) nb += *p == '\x1e';
                    size_t q = 0;
                    for (const char *p = el->texte3;; q++) {
                        const char *f2 = strchr(p, '\x1e');
                        char *v = f2 ? grym_formater("%.*s", (int)(f2 - p), p) : grym_dupliquer(p);
                        if (q) aj(im, c ? " ; " : q + 1 == nb ? " ou " : ", ");
                        ecrire_texte(im, v);
                        free(v);
                        if (!f2) break;
                        p = f2 + 1;
                    }
                } else if (c) ecrire_nom(im, el->texte2, 0); else aj(im, el->texte2);
                if (el->titre_calcule && el->titre_calcule->nb_enfants) {   /* « (patient dont l'archivage est absent) » (2.48) */
                    aj(im, c ? " _dont " : " dont ");
                    expression(im, el->titre_calcule->enfants[0]);
                }
                aj(im, ")");
                if (el->nb_enfants && el->enfants[0]->type != N_SI_ECRAN) {
                    aj(im, c ? " _départ " : ", ");
                    expression(im, el->enfants[0]);
                    if (!c) aj(im, " au départ");
                }
                if (el->entier & 1) aj(im, c ? " _facultatif" : fz ? ", facultative" : ", facultatif");
                if (el->entier & 2) aj(im, c ? " _plusieurs_lignes" : ", sur plusieurs lignes");
                if (el->entier & 4) aj(im, c ? " _lecture_seule" : ", en lecture seule");
                if (el->entier & 8) aj(im, c ? " _interrupteur" : ", en interrupteur");   /* § 22.6 */
                if (el->entier & 32) aj(im, c ? " _recherche" : ", avec recherche");   /* § 22.6 */
                if (el->entier & 64) aj(im, c ? " _masqué" : fz ? ", masquée" : ", masqué");   /* § 22.6 */
                if (el->entier & 4096) aj(im, c ? " _choix_dossier" : ", avec le choix d'un dossier");   /* 2.32 */
                if (el->transcrit_de) {   /* « , transcrite du mémo » ; « _transcrit mémo » (§ 15.8) */
                    if (c) { aj(im, " _transcrit "); ecrire_nom(im, el->transcrit_de, 0); }
                    else {
                        aj(im, fz ? ", transcrite " : ", transcrit ");
                        aj(im, voyelle(el->transcrit_de) ? "de l'" : genre_de_nom(im, el->transcrit_de) == G_FEMININ ? "de la " : "du ");
                        aj(im, el->transcrit_de);
                    }
                }
                if (el->borne_min || el->borne_max) {   /* « , de 1 à 99 », « , au plus 30 caractères » ; « _min 1 _max 99 » */
                    const int car = type_textuel(el->texte2);
                    char *a1 = el->borne_min ? borne_ecrite(el->borne_min) : NULL, *a2 = el->borne_max ? borne_ecrite(el->borne_max) : NULL;
                    if (c) {
                        if (a1) { aj(im, " _min "); aj(im, a1); }
                        if (a2) { aj(im, " _max "); aj(im, a2); }
                    } else if (a1 && a2 && strcmp(el->borne_min, el->borne_max) == 0 && car) {
                        aj(im, ", de "); aj(im, a1); aj(im, " caractères");
                    } else if (a1 && a2) {
                        aj(im, ", de "); aj(im, a1); aj(im, " à "); aj(im, a2); if (car) aj(im, " caractères");
                    } else {
                        if (a1) { aj(im, ", au moins "); aj(im, a1); if (car) aj(im, " caractères"); }
                        if (a2) { aj(im, ", au plus "); aj(im, a2); if (car) aj(im, " caractères"); }
                    }
                    free(a1);
                    free(a2);
                }
                if (el->unite) {   /* « , avec l'unité « CHF » devant » ; « _unité « CHF » _devant » */
                    aj(im, c ? " _unité " : ", avec l'unité ");
                    ecrire_texte(im, el->unite);
                    if (el->entier & 128) aj(im, c ? " _devant" : " devant");
                }
                if (el->indication) {   /* « , avec l'indication « … » » ; « _indication « … » » */
                    aj(im, c ? " _indication " : ", avec l'indication ");
                    ecrire_texte(im, el->indication);
                }
                etats_ecran(im, el, fz);
            } else if (c) {
                aj(im, el->graphique ? "_graphique " : "_liste ");
                ecrire_nom(im, el->texte, 0);
                aj(im, el->negation ? " _supprimé" : " _conservé");
                expression(im, el);   /* « _dont … » et « _par … » */
                if (el->texte3 && el->graphique) {   /* « _par membre.catégorie _somme montant _secteurs » (§ 22.11) */
                    for (const char *q = el->texte3; *q;) {
                        const char *fin = strchr(q, '\x1e');
                        if (!fin) fin = q + strlen(q);
                        const char *chemin = memchr(q, '\x1d', (size_t)(fin - q));
                        chemin = chemin ? chemin + 1 : q;
                        aj(im, q == el->texte3 ? " _par " : " _somme ");
                        if (q == el->texte3 && (el->graphique & 56))   /* « _par _mois échéance » (§ 22.11) */
                            aj(im, (el->graphique & 8) ? "_mois " : (el->graphique & 16) ? "_semaine " : "_année ");
                        for (const char *p = chemin; p < fin;) {
                            const char *f2 = memchr(p, '\x1f', (size_t)(fin - p));
                            if (!f2) f2 = fin;
                            char *seg = grym_formater("%.*s", (int)(f2 - p), p);
                            if (p != chemin) aj(im, ".");
                            ecrire_nom(im, seg, 0);
                            free(seg);
                            p = f2 < fin ? f2 + 1 : fin;
                        }
                        q = *fin ? fin + 1 : fin;
                    }
                    if (el->graphique & 2) aj(im, " _secteurs");
                } else if (el->texte3) {   /* « _avec titre ; compositeur.naissance » : les chemins, séparés par « ; » */
                    aj(im, " _avec ");
                    for (const char *q = el->texte3; *q;) {
                        const char *fin = strchr(q, '\x1e');
                        if (!fin) fin = q + strlen(q);
                        const char *chemin = memchr(q, '\x1d', (size_t)(fin - q));
                        chemin = chemin ? chemin + 1 : q;
                        if (q != el->texte3) aj(im, " ; ");
                        for (const char *p = chemin; p < fin;) {
                            const char *f2 = memchr(p, '\x1f', (size_t)(fin - p));
                            if (!f2) f2 = fin;
                            char *seg = grym_formater("%.*s", (int)(f2 - p), p);
                            if (p != chemin) aj(im, ".");
                            ecrire_nom(im, seg, 0);
                            free(seg);
                            p = f2 < fin ? f2 + 1 : fin;
                        }
                        q = *fin ? fin + 1 : fin;
                    }
                }
                if (!el->graphique && (el->entier & 2)) aj(im, " _cartes");   /* 2.19 */
                place_ecran(im, el, 1);
            } else {
                int fem = genre_de_nom(im, el->texte) == G_FEMININ;
                /* « la partition choisie de l'écran » : ce champ implicite prend le genre de l'entité (§ 22.2) */
                char *choisi = grym_formater("%s %s", el->texte, fem ? "choisie" : "choisi");
                retenir(im, choisi, fem ? G_FEMININ : G_MASCULIN);
                if (im->nb_a_liberer % 16 == 0) {
                    char **t = grym_allouer((im->nb_a_liberer + 16) * sizeof *t);
                    if (im->nb_a_liberer) memcpy(t, im->a_liberer, im->nb_a_liberer * sizeof *t);
                    free(im->a_liberer);
                    im->a_liberer = t;
                }
                im->a_liberer[im->nb_a_liberer++] = choisi;
                const char *pl = NULL;
                for (size_t q = 0; q < im->nb_pluriels && !pl; q++)
                    if (strcmp(im->pluriels[q].feminin, el->texte) == 0) pl = im->pluriels[q].masculin;
                char *x = pl ? grym_dupliquer(pl) : grym_formater("%ss", el->texte);
                aj(im, el->graphique ? "le graphique des " : "la liste des ");
                aj(im, x);
                free(x);
                aj(im, el->negation ? (fem ? " supprimées" : " supprimés") : fem ? " conservées" : " conservés");
                expression(im, el);   /* « dont … » et « , par … » */
                if (el->texte3 && el->graphique) {   /* « , par catégorie du membre, avec la somme du montant, en secteurs » */
                    for (const char *q = el->texte3; *q;) {
                        const char *fin = strchr(q, '\x1e');
                        if (!fin) fin = q + strlen(q);
                        const char *sep = memchr(q, '\x1d', (size_t)(fin - q));
                        char *ecrit = grym_formater("%.*s", (int)((sep ? sep : fin) - q), q);
                        char *refait = sep ? colonne_avec_articles(im, ecrit, sep + 1, fin) : NULL;
                        const char *e2 = refait ? refait : ecrit;
                        const char *periode = (el->graphique & 8) ? "mois" : (el->graphique & 16) ? "semaine"
                                            : (el->graphique & 32) ? "année" : NULL;
                        if (q == el->texte3 && periode && (refait || (strncmp(e2, periode, strlen(periode))
                                                                      || e2[strlen(periode)] != ' '))) {
                            /* « , par mois de l'échéance » (§ 22.11) : la période, puis le champ avec son article
                             * contracté, comme la somme ; oubliée ici, elle disparaissait au formatage */
                            aj(im, ", par ");
                            aj(im, periode);
                            if (!strncmp(e2, "le ", 3)) { aj(im, " du "); aj(im, e2 + 3); }
                            else { aj(im, " de "); aj(im, e2); }   /* « de l'échéance », « de la date de fin » */
                        } else if (q == el->texte3) {   /* la clé s'écrit sans article, comme on dit « par état » */
                            const char *sans = e2;
                            if (!strncmp(sans, "le ", 3) || !strncmp(sans, "la ", 3)) sans += 3;
                            else if (!strncmp(sans, "l'", 2)) sans += 2;
                            aj(im, ", par ");
                            aj(im, sans);
                        } else {   /* « de » contracté avec l'article de la valeur : « du montant », « de la durée » */
                            aj(im, ", avec la somme ");
                            if (!strncmp(e2, "le ", 3)) { aj(im, "du "); aj(im, e2 + 3); }
                            else if (!strncmp(e2, "les ", 4)) { aj(im, "des "); aj(im, e2 + 4); }
                            else { aj(im, "de "); aj(im, e2); }
                        }
                        free(refait);
                        free(ecrit);
                        q = *fin ? fin + 1 : fin;
                    }
                    if (el->graphique & 2) aj(im, ", en secteurs");
                } else if (el->texte3) {   /* « , avec le titre, le compositeur et l'année » : tels qu'écrits */
                    aj(im, ", avec ");
                    size_t nb = 1, i = 0;
                    for (const char *q = el->texte3; *q; q++) nb += *q == '\x1e';
                    for (const char *q = el->texte3; *q; i++) {
                        const char *fin = strchr(q, '\x1e');
                        if (!fin) fin = q + strlen(q);
                        const char *sep = memchr(q, '\x1d', (size_t)(fin - q));
                        if (i) aj(im, i + 1 == nb ? " et " : ", ");
                        char *ecrit = grym_formater("%.*s", (int)((sep ? sep : fin) - q), q);
                        char *refait = sep ? colonne_avec_articles(im, ecrit, sep + 1, fin) : NULL;
                        char *sigles = texte_avec_sigles(refait ? refait : ecrit);   /* « le numéro GLN », pas « gln » */
                        aj(im, sigles);
                        free(sigles);
                        free(refait);
                        free(ecrit);
                        q = *fin ? fin + 1 : fin;
                    }
                }
                if (!el->graphique && (el->entier & 2)) aj(im, ", en cartes");   /* 2.19 */
                place_ecran(im, el, 1);   /* la place d'une liste vient après ses colonnes (§ 22.9) */
            }
            aj(im, c ? "\n" : k < dernier ? ",\n" : ".\n");
        }
        fin_compacte(im, niveau);
        return;
    }
    case P_QUAND: {   /* « Quand on clique sur « B » dans l'écran X : » (§ 22.2) */
        size_t apres_nom = im->nb;
        if (n->forme >= 4) {   /* « Quand on ouvre l'écran X : », « Quand on ferme … » */
            if (c) { aj(im, n->forme == 4 ? "_quand _ouvre " : "_quand _ferme "); ecrire_nom(im, n->texte3, 0); aj(im, "\n"); }
            else { aj(im, n->forme == 4 ? "Quand on ouvre l'écran " : "Quand on ferme l'écran "); aj_sigles(im, n->texte3); aj(im, " :\n"); }
        } else if (c) {
            aj(im, n->forme == 1 ? "_quand _clique " : n->forme == 3 ? "_quand _change " : "_quand _choisit ");
            if (n->forme == 1) ecrire_texte(im, n->enfants[2]->texte);
            else if (n->forme == 3) ecrire_nom(im, n->enfants[2]->texte, 0);
            else parametres(im, n->enfants[0], 0);
            aj(im, " _dans ");
            ecrire_nom(im, n->texte3, 0);
            aj(im, "\n");
        } else {
            aj(im, n->forme == 1 ? "Quand on clique sur " : n->forme == 3 ? "Quand on change " : "Quand on choisit");
            if (n->forme == 1) ecrire_texte(im, n->enfants[2]->texte);
            else if (n->forme == 3) {   /* « le pays », « la ville » : l'article selon le genre retenu */
                const char *z = n->enfants[2]->texte;
                aj(im, voyelle(z) ? "l'" : genre_de_nom(im, z) == G_FEMININ ? "la " : "le ");
                aj(im, z);
            }
            else parametres(im, n->enfants[0], 0);
            aj(im, " dans l'écran ");
            aj_sigles(im, n->texte3);
            aj(im, " :\n");
        }
        bloc(im, n->enfants[1], niveau + 1);
        fin_compacte(im, niveau);
        im->nb = apres_nom;
        return;
    }
    case P_OUVRIR:
        if (n->forme == 1) {   /* « Ouvrir la fiche de c. » ; « _ouvrir _fiche c » */
            if (c) { aj(im, "_ouvrir _fiche "); expression(im, n->enfants[0]); aj(im, "\n"); }
            else { aj(im, "Ouvrir la fiche "); preposition(im, "de", n->enfants[0]); aj(im, ".\n"); }
            return;
        }
        if (n->forme == 3) {   /* « Ouvrir le justificatif du frais. » ; « _ouvrir_fichier frais.justificatif » (2.26) */
            if (c) { aj(im, "_ouvrir_fichier "); expression(im, n->enfants[0]); aj(im, "\n"); }
            else { aj(im, "Ouvrir "); expression(im, n->enfants[0]); aj(im, ".\n"); }
            return;
        }
        if (n->forme == 2) {   /* « Ouvrir le document de quittance. » ; « _ouvrir _document de_quittance » (2.25) */
            const char *nd = strncmp(n->texte, "document ", 9) == 0 ? n->texte + 9 : n->texte;
            if (c) { aj(im, "_ouvrir _document "); ecrire_nom(im, nd, 0); aj(im, "\n"); }
            else { aj(im, "Ouvrir le document "); aj(im, nd); aj(im, ".\n"); }
            return;
        }
        if (c) { aj(im, "_ouvrir "); ecrire_nom(im, n->texte, 0); aj(im, "\n"); }
        else { aj(im, "Ouvrir l'écran "); aj_sigles(im, n->texte); aj(im, ".\n"); }
        return;
    case P_FERMER:
        aj(im, c ? "_fermer\n" : "Fermer l'écran.\n");
        return;
    case P_UTILISER:   /* « Utiliser « données ». » ; « _utiliser « données » » (§ 21) : jamais ses déclarations */
        aj(im, c ? "_utiliser " : "Utiliser ");
        if (n->forme == 1) aj(im, c ? "_bibliothèque " : "la bibliothèque ");   /* 2.61, § 21.9 */
        ecrire_texte(im, n->texte);
        if (n->forme == 1) { aj(im, c ? " " : ", version "); aj(im, n->texte2); }
        aj(im, c ? "\n" : ".\n");
        return;
    case P_INTERNE:   /* « Ce fichier est interne à sa bibliothèque. » ; « _interne » (2.62, § 21.9) */
        aj(im, c ? "_interne\n" : "Ce fichier est interne à sa bibliothèque.\n");
        return;
    case P_BIBLIOTHEQUE:   /* « Cette bibliothèque est « X », version 2. » ; « _bibliothèque « X » 2 » (2.61, § 21.9) */
        aj(im, c ? "_bibliothèque " : "Cette bibliothèque est ");
        ecrire_texte(im, n->texte);
        aj(im, c ? " " : ", version ");
        aj(im, n->texte2);
        aj(im, c ? "\n" : ".\n");
        return;
    case P_FICHE:   /* « Afficher la fiche de p. » ; « _fiche p » (§ 20) */
        if (c) {
            aj(im, "_fiche ");
            expression(im, n->enfants[0]);
            aj(im, "\n");
        } else {
            aj(im, "Afficher la fiche ");
            preposition(im, "de", n->enfants[0]);
            aj(im, ".\n");
        }
        return;
    case P_RESAISIR:   /* « Saisir à nouveau p. » ; « _saisir p » (§ 19) */
        aj(im, c ? "_saisir " : "Saisir à nouveau ");
        expression(im, n->enfants[0]);
        aj(im, c ? "\n" : ".\n");
        return;
    case P_ESSAYER:   /* « Essayer : … En cas d'échec : … » ; « _essayer … _échec … _fin » (§ 18) */
        aj(im, c ? "_essayer" : "Essayer");
        branche(im, n->enfants[0], 0, niveau);
        retrait(im, niveau);
        aj(im, c ? "_échec" : "En cas d'échec");
        branche(im, n->enfants[1], !(n->forme & 1), niveau);
        fin_compacte(im, niveau);
        return;
    case P_STYLE:   /* « Les nombres s'affichent à la française. » (§ 4.1) */
        aj(im, c ? "_style " : "Les nombres s'affichent ");
        aj(im, n->entier == 0 ? (c ? "_suisse" : "à la suisse") : n->entier == 1 ? (c ? "_française" : "à la française")
                                                                                : (c ? "_sans_séparateur" : "sans séparateur"));
        aj(im, c ? "\n" : ".\n");
        return;
    case P_REGLE: {   /* « Chaque écriture conservée dont … vérifie : cond, sinon … . » ; une ligne en compacte (§ 16.14) */
        const Noeud *boucle = n->enfants[0]->enfants[1]->enfants[0];
        const Noeud *cherche = boucle->enfants[0];
        const Noeud *si = boucle->enfants[1]->enfants[0];
        const Noeud *refus = si->enfants[2]->enfants[0];
        const int fem = genre_de_nom(im, n->texte) == G_FEMININ;
        aj(im, c ? "_chaque " : "Chaque ");
        ecrire_nom(im, n->texte, 0);
        aj(im, c ? " _conservé" : fem ? " conservée" : " conservé");
        if (cherche->nb_enfants) {
            aj(im, c ? " _dont " : " dont ");
            expression(im, cherche->enfants[0]);
        }
        aj(im, c ? " _vérifie " : " vérifie :\n");
        if (!c) retrait(im, niveau + 1);
        expression(im, si->enfants[0]);
        aj(im, c ? " _sinon " : ",\n");
        if (!c) { retrait(im, niveau + 1); aj(im, "sinon "); }
        for (size_t k = 0; k < refus->nb_enfants; k++) {
            if (k) aj(im, c ? " ; " : " puis ");
            expression(im, refus->enfants[k]);
        }
        aj(im, c ? "\n" : ".\n");
        return;
    }
    case P_REFUSER:   /* « Refuser « … ». » ; « _refuser « … » » (§ 18.1) */
        aj(im, c ? "_refuser " : "Refuser ");
        for (size_t k = 0; k < n->nb_enfants; k++) {
            if (k) aj(im, c ? " ; " : " puis ");
            expression(im, n->enfants[k]);
        }
        aj(im, c ? "\n" : ".\n");
        return;
    case P_APPARENCE: {   /* « Les écrans ont la couleur verte et le logo « … ». » ; « _écrans _couleur _verte _logo « … » » */
        static const char *const C[] = { "bleue", "verte", "turquoise", "violette", "grise" };
        aj(im, c ? "_écrans" : "Les écrans ont");
        /* couleur, fond, police, logo (2.13) : des virgules entre eux, « et » devant le dernier */
        const char *valeurs[5] = { n->texte2, n->texte3, apparence_reglage(n, 0), n->texte, apparence_reglage(n, 1) };
        const Noeud *calcules[5] = { NULL, NULL, NULL, apparence_expression(n, 'L'), apparence_expression(n, 'S') };   /* 2.33 */
        const int nommee = n->entier >= 0 && n->entier < 5;
        int total = nommee || valeurs[0];
        for (int r = 1; r < 5; r++) total += valeurs[r] != NULL || calcules[r] != NULL;
        static const char *const LIT[] = { "la couleur ", "le fond ", "la police ", "le logo ", "la signature " };
        static const char *const CMP[] = { " _couleur ", " _fond ", " _police ", " _logo ", " _signature " };
        int faits = 0;
        for (int r = 0; r < 5; r++) {
            if (r == 0 ? !(nommee || valeurs[0]) : !valeurs[r] && !calcules[r]) continue;
            if (c) aj(im, CMP[r]);
            else {
                aj(im, faits == 0 ? " " : faits + 1 == total ? " et " : ", ");
                aj(im, LIT[r]);
            }
            if (r == 0 && nommee) { if (c) aj(im, "_"); aj(im, C[n->entier]); }
            else if (calcules[r]) expression(im, calcules[r]);   /* « (le logo du cabinet conservé) » */
            else ecrire_texte(im, valeurs[r]);
            faits++;
        }
        aj(im, c ? "\n" : ".\n");
        return;
    }
    case P_AFFICHAGE:
        aj(im, c ? "_afficher " : "Afficher ");
        for (size_t k = 0; k < n->nb_enfants; k++) {
            if (k) aj(im, c ? " ; " : " puis ");
            expression(im, n->enfants[k]);
        }
        if (n->forme) aj(im, c ? " _sans_ligne" : ", sans passer à la ligne");
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
        if (n->op == 'X' && c) {   /* « _xml facture « invoice:request » » … « _fin » (2.46) */
            aj(im, "_xml ");
            ecrire_nom(im, n->texte + 7, 0);
            if (n->enfants[0]->nb_enfants) {   /* « _xml quittance(_une chose_tarifée) » (2.63) */
                aj(im, "(");
                parametres(im, n->enfants[0], 0);
                aj(im, ")");
            }
            aj(im, " ");
            ecrire_texte(im, n->texte3);
            aj(im, "\n");
            bloc(im, n->enfants[1], niveau + 1);
            fin_compacte(im, niveau);
            im->nb = apres_nom;
            return;
        }
        if (n->op == 'X') {   /* « Le XML de facture, « invoice:request », montre : » (2.46) */
            aj(im, "Le XML de ");
            ecrire_nom(im, n->texte + 7, 0);   /* sans « xml de » */
            parametres(im, n->enfants[0], 1);   /* « d'une chose tarifée » (2.63) */
            aj(im, ", ");
            ecrire_texte(im, n->texte3);
            aj(im, ", montre :\n");
            bloc(im, n->enfants[1], niveau + 1);
            if (im->c.n >= 2 && im->c.d[im->c.n - 2] == ',' && im->c.d[im->c.n - 1] == '\n') im->c.d[im->c.n - 2] = '.';
            im->nb = apres_nom;
            return;
        }
        if (c) {
            aj(im, "_action ");
            ecrire_nom(im, n->texte, 0);
            aj(im, "(");
            parametres(im, n->enfants[0], 0);
            aj(im, ")");
            if (n->texte3) {   /* 2.55 ; « _ou » pour chaque autre écran (2.58) */
                aj(im, " _dans_écran ");
                char *noms = grym_dupliquer(n->texte3);
                for (char *p = noms, *suite; p; p = suite) {
                    suite = strchr(p, '\x1f');
                    if (suite) *suite++ = '\0';
                    if (p != noms) aj(im, " _ou ");
                    ecrire_nom(im, p, 0);
                }
                free(noms);
            }
            aj(im, "\n");
            bloc(im, n->enfants[1], niveau + 1);
            fin_compacte(im, niveau);
        } else {
            aj(im, "Pour ");
            const size_t l = strlen(n->texte);
            const int finit_de = l > 3 && strcmp(n->texte + l - 3, " de") == 0 && n->enfants[0]->nb_enfants > 0;
            if (finit_de) {   /* « Pour acquitter le solde d'un relevé : », pas « … de un relevé » */
                char *court = grym_formater("%.*s", (int)(l - 3), n->texte);
                ecrire_nom(im, court, 0);
                free(court);
            } else {
                ecrire_nom(im, n->texte, 0);
            }
            parametres(im, n->enfants[0], finit_de ? 2 : 0);
            if (n->texte3) {   /* 2.55 ; « ou l'écran … » pour chaque autre écran (2.58) */
                char *noms = grym_dupliquer(n->texte3);
                for (char *p = noms, *suite; p; p = suite) {
                    suite = strchr(p, '\x1f');
                    if (suite) *suite++ = '\0';
                    aj(im, p == noms ? ", dans l'écran " : " ou l'écran ");
                    aj_sigles(im, p);
                }
                free(noms);
            }
            aj(im, " :\n");
            bloc(im, n->enfants[1], niveau + 1);
        }
        im->nb = apres_nom;
        return;
    }
    case P_EXEMPLE:   /* « Exemple : le carré de 7 vaut 49. » ; « _exemple carré(7) << 49 » (§ 9.6) */
        aj(im, c ? "_exemple " : "Exemple : ");
        expression(im, n->enfants[0]);
        aj(im, c ? " << " : " vaut ");
        expression(im, n->enfants[1]);
        aj(im, c ? "\n" : ".\n");
        return;
    case P_RENDRE:
        aj(im, c ? "_rendre " : "Rendre ");
        expression(im, n->enfants[0]);
        aj(im, c ? "\n" : ".\n");
        return;
    case P_APPEL:
        if (n->forme == 9) {   /* « Faire comme un clic sur « B ». » ; « _comme_clic « B » » (2.36) */
            aj(im, c ? "_comme_clic " : "Faire comme un clic sur ");
            ecrire_texte(im, n->indication);
            aj(im, c ? "\n" : ".\n");
            return;
        }
        if (c) {
            ecrire_nom(im, n->texte, 0);
            aj(im, "(");
            for (size_t k = 0; k < n->nb_enfants; k++) {
                if (k) aj(im, " ; ");
                expression(im, n->enfants[k]);
            }
            aj(im, ")");
            comme(im, n);
            aj(im, "\n");
        } else {
            const size_t l = strlen(n->texte);
            const int finit_de = l > 3 && strcmp(n->texte + l - 3, " de") == 0 && n->nb_enfants > 0
                                 && n->enfants[0]->type != N_LOGIQUE;
            char *nom = finit_de ? grym_formater("%.*s", (int)(l - 3), n->texte) : grym_dupliquer(n->texte);
            initiale(nom, 1);
            aj(im, nom);
            free(nom);
            for (size_t k = 0; k < n->nb_enfants; k++) {
                if (k == 0 && finit_de) {   /* « Acquitter le solde du relevé. » : le « de » se contracte (§ 5.2) */
                    aj(im, " ");
                    preposition(im, "de", n->enfants[0]);
                    continue;
                }
                aj(im, k ? " et " : " ");
                const Noeud *x = n->enfants[k];
                int paren = x->type == N_LOGIQUE;
                if (paren) aj(im, "(");
                expression(im, x);
                if (paren) aj(im, ")");
            }
            comme(im, n);
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
    case P_POUR_TABLEAU: {   /* « Pour chaque ligne du tableau « f.csv » : » ; « _pour_chaque ligne _tableau « f.csv » » */
        size_t sauve = im->nb;
        aj(im, c ? "_pour_chaque " : "Pour chaque ");
        ecrire_nom(im, n->texte, 0);
        if (n->op == 'p' && n->enfants[0]->type == N_JSON_LISTE) {   /* « du champ « collection » de r » (2.60) */
            const Noeud *src = n->enfants[0];
            aj(im, c ? " _champ " : " du champ ");
            ecrire_texte(im, src->texte);
            aj(im, " ");
            if (c) expression(im, src->enfants[0]);
            else de_expression(im, src->enfants[0]);
        } else {
            if (n->op == 'p') aj(im, c ? " _parmi " : " parmi ");   /* « parmi les couleurs » (§ 13.9) */
            else aj(im, c ? " _tableau " : " du tableau ");
            expression(im, n->enfants[0]);
        }
        retenir(im, n->texte, G_FEMININ);
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
        int hors = conserve && (n->forme & 64) != 0;   /* « , hors des fiches, » (§ 22.3) */
        if (c) {
            aj(im, fem ? "_classe _une " : "_classe _un ");
            ecrire_nom(im, n->texte, 0);
            if (n->texte3) { aj(im, " ("); ecrire_nom(im, n->texte3, 0); aj(im, ")"); }
            if (conserve) aj(im, " _conservé");
            if (hors) aj(im, " _hors_des_fiches");
            if (n->ancien) { aj(im, " _anciennement "); ecrire_texte(im, n->ancien); }
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
                if (hors) aj(im, " hors des fiches,");
                if (n->ancien) { aj(im, " anciennement "); ecrire_texte(im, n->ancien); aj(im, ","); }
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
            if (!herite && hors) aj(im, " hors des fiches,");
            if (!herite && n->ancien) { aj(im, " anciennement "); ecrire_texte(im, n->ancien); aj(im, ","); }
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
            const int avant = im->sans_marque_objet;
            im->sans_marque_objet = 1;   /* « o.genres _gagne … » : la phrase commence par l'objet, sans marque (2.36) */
            expression(im, &champ);
            im->sans_marque_objet = avant;
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
            const int avant = im->devine;
            im->devine = n->enfants[0]->article_a_deviner;   /* `f.montant << …` traduit : « Le montant de la f » */
            preposition(im, "de", n->enfants[0]);
            im->devine = avant;
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
    case P_EXPORTER: {   /* « Exporter les œuvres conservées …, avec …, dans « m.csv ». » (§ 15.4) */
        const Noeud *el = n->enfants[0];
        int fem = genre_de_nom(im, el->texte) == G_FEMININ;
        if (c) {
            aj(im, "_exporter ");
            ecrire_nom(im, el->texte, 0);
            aj(im, el->negation ? " _supprimé" : " _conservé");
        } else {
            const char *pl = NULL;
            for (size_t q = 0; q < im->nb_pluriels && !pl; q++)
                if (strcmp(im->pluriels[q].feminin, el->texte) == 0) pl = im->pluriels[q].masculin;
            char *x = pl ? grym_dupliquer(pl) : grym_formater("%ss", el->texte);
            aj(im, "Exporter les ");
            aj(im, x);
            free(x);
            aj(im, el->negation ? (fem ? " supprimées" : " supprimés") : fem ? " conservées" : " conservés");
        }
        expression(im, el);   /* « dont … » et « , par … » */
        if (el->texte3) {
            aj(im, c ? " _avec " : ", avec ");
            size_t nb = 1, i = 0;
            for (const char *q = el->texte3; *q; q++) nb += *q == '\x1e';
            for (const char *q = el->texte3; *q; i++) {
                const char *fin = strchr(q, '\x1e');
                if (!fin) fin = q + strlen(q);
                const char *sep = memchr(q, '\x1d', (size_t)(fin - q));
                if (c) {   /* « titre ; compositeur.nom » */
                    if (i) aj(im, " ; ");
                    const char *chemin = sep ? sep + 1 : q;
                    for (const char *p = chemin; p < fin;) {
                        const char *f2 = memchr(p, '\x1f', (size_t)(fin - p));
                        if (!f2) f2 = fin;
                        char *seg = grym_formater("%.*s", (int)(f2 - p), p);
                        if (p != chemin) aj(im, ".");
                        ecrire_nom(im, seg, 0);
                        free(seg);
                        p = f2 < fin ? f2 + 1 : fin;
                    }
                } else {
                    if (i) aj(im, i + 1 == nb ? " et " : ", ");
                    char *ecrit = grym_formater("%.*s", (int)((sep ? sep : fin) - q), q);
                    aj(im, ecrit);
                    free(ecrit);
                }
                q = *fin ? fin + 1 : fin;
            }
            if (!c) aj(im, ",");
        }
        int virgule = el->texte3 != NULL;   /* les colonnes finissent déjà par une virgule */
        if (n->texte) {   /* « , avec le titre « … » » ; « _titre « … » » (§ 15.6) */
            aj(im, c ? " _titre " : virgule ? " avec le titre " : ", avec le titre ");
            virgule = 1;
            ecrire_texte(im, n->texte);
            if (!c) aj(im, ",");
        }
        if (n->texte2) {   /* « , avec le total de la cotisation et du montant » ; « _total cotisation ; montant » */
            aj(im, c ? " _total " : virgule ? " avec le total " : ", avec le total ");
            size_t i = 0;
            for (const char *q = n->texte2; *q; i++) {
                const char *fin = strchr(q, '\x1e');
                if (!fin) fin = q + strlen(q);
                const char *sep = memchr(q, '\x1d', (size_t)(fin - q));
                if (c) {
                    if (i) aj(im, " ; ");
                    const char *chemin = sep ? sep + 1 : q;
                    for (const char *p = chemin; p < fin;) {
                        const char *f2 = memchr(p, '\x1f', (size_t)(fin - p));
                        if (!f2) f2 = fin;
                        char *seg = grym_formater("%.*s", (int)(f2 - p), p);
                        if (p != chemin) aj(im, ".");
                        ecrire_nom(im, seg, 0);
                        free(seg);
                        p = f2 < fin ? f2 + 1 : fin;
                    }
                } else {
                    if (i) aj(im, " et ");
                    char *e2 = grym_formater("%.*s", (int)((sep ? sep : fin) - q), q);
                    if (!strncmp(e2, "le ", 3)) { aj(im, "du "); aj(im, e2 + 3); }
                    else if (!strncmp(e2, "les ", 4)) { aj(im, "des "); aj(im, e2 + 4); }
                    else { aj(im, "de "); aj(im, e2); }
                    free(e2);
                }
                q = *fin ? fin + 1 : fin;
            }
            if (!c) aj(im, ",");
        }
        aj(im, c ? " _dans " : " dans ");
        expression(im, n->enfants[1]);
        aj(im, c ? "\n" : ".\n");
        return;
    }
    case P_SAUVEGARDER:
    case P_RESTAURER:
        if (n->type == P_SAUVEGARDER) aj(im, c ? "_sauvegarder _dans " : "Sauvegarder la base dans ");
        else aj(im, c ? "_restaurer _depuis " : "Restaurer la base depuis ");
        expression(im, n->enfants[0]);
        if (n->type == P_SAUVEGARDER && n->entier) aj(im, c ? " _une_par_jour_passé" : ", une par jour passé");   /* 2.53 */
        if (n->forme) {
            aj(im, c ? " _mot_de_passe " : " avec le mot de passe ");
            expression(im, n->enfants[1]);
        }
        aj(im, c ? "\n" : ".\n");
        return;
    case P_BASE_CHIFFREE:   /* 2.37 */
        aj(im, c ? "_base_chiffrée\n" : "La base est chiffrée.\n");
        return;
    case P_CHANGER_MDP:     /* 2.37 */
        aj(im, c ? "_changer_mot_de_passe " : "Changer le mot de passe de la base pour ");
        expression(im, n->enfants[0]);
        aj(im, c ? " _ancien " : ", l'ancien étant ");
        expression(im, n->enfants[1]);
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

static void apprendre(Impression *im, Noeud *const *phrases, size_t nb, const Noeud *sauf);

static char *imprimer(const Programme *p, int compact) {
    Impression im;
    memset(&im, 0, sizeof im);
    im.compact = compact;
    /* Les fichiers utilisés (§ 21) : leurs déclarations ne s'impriment pas, mais leurs genres et leurs aptitudes
     * valent ici (« une nouvelle catégorie », « est présente ») ; sinon, formater un fichier les écrirait au masculin. */
    for (size_t k = 0; k < p->nb; k++)
        if (p->phrases[k]->type == P_UTILISER) apprendre(&im, p->phrases[k]->enfants, p->phrases[k]->nb_enfants, NULL);
    if (im.c.d) { im.c.n = 0; im.c.d[0] = '\0'; }
    im.parties = grym_allouer((p->nb ? p->nb : 1) * sizeof *im.parties);   /* 2.56 */
    for (size_t k = 0; k < p->nb; k++)
        if (p->phrases[k]->type == P_ECRAN && p->phrases[k]->op == 'P') im.parties[im.nb_parties++] = p->phrases[k];
    for (size_t k = 0; k < p->nb; k++) {
        if (k > 0 && p->phrases[k]->ligne > p->phrases[k - 1]->ligne_fin + 1) aj(&im, "\n");
        phrase(&im, p->phrases[k], 0);
    }
    free(im.genres);
    free(im.variables);   /* les noms appartiennent à l'arbre */
    free(im.genres_var);
    free(im.aptitudes);
    free(im.pluriels);
    free(im.singuliers);
    free(im.parties);
    for (size_t k = 0; k < im.nb_a_liberer; k++) free(im.a_liberer[k]);
    free(im.a_liberer);
    return chaine_rendre(&im.c);
}

/* Imprime, sans les garder, les déclarations du contexte : l'impression apprend ainsi genres et aptitudes. */
static void apprendre(Impression *im, Noeud *const *phrases, size_t nb, const Noeud *sauf) {
    for (size_t k = 0; k < nb; k++) {
        const Noeud *ph = phrases[k];
        if (ph == sauf) continue;
        if (ph->type == P_UTILISER) apprendre(im, ph->enfants, ph->nb_enfants, sauf);
        else if (ph->type == P_CLASSE || ph->type == P_APTITUDE || ph->type == P_CALCUL) phrase(im, ph, 0);
    }
}

char *imprimer_phrase(const Programme *contexte, const Noeud *n, int compact) {
    Impression im;
    memset(&im, 0, sizeof im);
    im.compact = compact;
    if (contexte) apprendre(&im, contexte->phrases, contexte->nb, n);
    if (im.c.d) { im.c.n = 0; im.c.d[0] = '\0'; }
    phrase(&im, n, 0);
    free(im.genres);
    free(im.variables);   /* les noms appartiennent à l'arbre */
    free(im.genres_var);
    free(im.aptitudes);
    free(im.pluriels);
    free(im.singuliers);
    for (size_t k = 0; k < im.nb_a_liberer; k++) free(im.a_liberer[k]);
    free(im.a_liberer);
    return chaine_rendre(&im.c);
}

char *imprimer_litteraire(const Programme *p) { return imprimer(p, 0); }
char *imprimer_compact(const Programme *p)    { return imprimer(p, 1); }

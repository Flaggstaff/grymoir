/* GrymoiR : lexeur de la forme littéraire, v0.1
 * Spécification : docs/grammaire.md (révision 1.24), § 1.
 */
#include "lexeur.h"
#include "date.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Lexeur {
    uint32_t *cp;      /* source décodée et normalisée */
    size_t n;
    size_t pos;
    int ligne;
    int colonne;
    int debut_ligne;   /* vrai si seuls des blancs précèdent pos sur la ligne */
    int termine;
    int compact;       /* forme compacte (§ 11.1) */
};

/* ---------------------------------------------------------------- */
/* Mémoire et texte                                                 */
/* ---------------------------------------------------------------- */

static void memoire_epuisee(void) {
    fputs("GrymoiR : mémoire épuisée\n", stderr);
    exit(EXIT_FAILURE);
}

static void *allouer(size_t taille) {
    void *p = malloc(taille ? taille : 1);
    if (!p) memoire_epuisee();
    return p;
}

typedef struct { char *d; size_t n, cap; } Tampon;

static void tampon_octet(Tampon *t, unsigned char c) {
    if (t->n + 2 > t->cap) {
        size_t cap = t->cap ? t->cap * 2 : 32;
        char *d = realloc(t->d, cap);
        if (!d) memoire_epuisee();
        t->d = d;
        t->cap = cap;
    }
    t->d[t->n++] = (char)c;
    t->d[t->n] = '\0';
}

static void tampon_cp(Tampon *t, uint32_t c) {
    if (c < 0x80) {
        tampon_octet(t, (unsigned char)c);
    } else if (c < 0x800) {
        tampon_octet(t, (unsigned char)(0xC0 | (c >> 6)));
        tampon_octet(t, (unsigned char)(0x80 | (c & 0x3F)));
    } else if (c < 0x10000) {
        tampon_octet(t, (unsigned char)(0xE0 | (c >> 12)));
        tampon_octet(t, (unsigned char)(0x80 | ((c >> 6) & 0x3F)));
        tampon_octet(t, (unsigned char)(0x80 | (c & 0x3F)));
    } else {
        tampon_octet(t, (unsigned char)(0xF0 | (c >> 18)));
        tampon_octet(t, (unsigned char)(0x80 | ((c >> 12) & 0x3F)));
        tampon_octet(t, (unsigned char)(0x80 | ((c >> 6) & 0x3F)));
        tampon_octet(t, (unsigned char)(0x80 | (c & 0x3F)));
    }
}

static char *tampon_rendre(Tampon *t) {
    if (!t->d) {
        char *s = allouer(1);
        s[0] = '\0';
        return s;
    }
    return t->d;
}

static char *formater(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n < 0) n = 0;
    char *s = allouer((size_t)n + 1);
    va_start(ap, fmt);
    vsnprintf(s, (size_t)n + 1, fmt, ap);
    va_end(ap);
    return s;
}

/* ---------------------------------------------------------------- */
/* Décodage UTF-8 (§ 1 de la charte : sources en UTF-8)             */
/* ---------------------------------------------------------------- */

static int decoder_utf8(const unsigned char *s, size_t n,
                        uint32_t **sortie, size_t *nsortie, char **erreur) {
    uint32_t *cp = allouer((n + 1) * sizeof *cp);
    size_t i = 0, k = 0;
    int ligne = 1, col = 1;

    /* Marque d'ordre des octets (Bloc-notes de Windows) : ignorée. */
    if (n >= 3 && s[0] == 0xEF && s[1] == 0xBB && s[2] == 0xBF) i = 3;

    while (i < n) {
        unsigned char c = s[i];
        uint32_t v, min;
        size_t len;

        if (c < 0x80)                { v = c;        len = 1; min = 0; }
        else if ((c & 0xE0) == 0xC0) { v = c & 0x1F; len = 2; min = 0x80; }
        else if ((c & 0xF0) == 0xE0) { v = c & 0x0F; len = 3; min = 0x800; }
        else if ((c & 0xF8) == 0xF0) { v = c & 0x07; len = 4; min = 0x10000; }
        else goto invalide;

        if (i + len > n) goto invalide;
        for (size_t j = 1; j < len; j++) {
            if ((s[i + j] & 0xC0) != 0x80) goto invalide;
            v = (v << 6) | (s[i + j] & 0x3F);
        }
        if (v < min || v > 0x10FFFF || (v >= 0xD800 && v <= 0xDFFF)) goto invalide;
        i += len;

        /* Fins de ligne : CRLF (Windows) et CR seul deviennent LF. */
        if (v == '\r') {
            if (i < n && s[i] == '\n') i++;
            v = '\n';
        }
        cp[k++] = v;
        if (v == '\n') { ligne++; col = 1; } else col++;
        continue;

    invalide:
        free(cp);
        *erreur = formater("Encodage invalide (ligne %d, colonne %d) : "
                           "le fichier doit être en UTF-8.", ligne, col);
        return 0;
    }
    *sortie = cp;
    *nsortie = k;
    return 1;
}

/* ---------------------------------------------------------------- */
/* Normalisation NFC, partielle : lettres françaises uniquement     */
/* (lettre de base + accent combinant → lettre précomposée)         */
/* ---------------------------------------------------------------- */

static const struct { uint32_t base, marque, compose; } COMPOSITIONS[] = {
    /* accent grave U+0300 */
    {'a',0x300,0xE0},{'e',0x300,0xE8},{'i',0x300,0xEC},{'o',0x300,0xF2},{'u',0x300,0xF9},
    {'A',0x300,0xC0},{'E',0x300,0xC8},{'I',0x300,0xCC},{'O',0x300,0xD2},{'U',0x300,0xD9},
    /* accent aigu U+0301 */
    {'a',0x301,0xE1},{'e',0x301,0xE9},{'i',0x301,0xED},{'o',0x301,0xF3},{'u',0x301,0xFA},{'y',0x301,0xFD},
    {'A',0x301,0xC1},{'E',0x301,0xC9},{'I',0x301,0xCD},{'O',0x301,0xD3},{'U',0x301,0xDA},{'Y',0x301,0xDD},
    /* accent circonflexe U+0302 */
    {'a',0x302,0xE2},{'e',0x302,0xEA},{'i',0x302,0xEE},{'o',0x302,0xF4},{'u',0x302,0xFB},
    {'A',0x302,0xC2},{'E',0x302,0xCA},{'I',0x302,0xCE},{'O',0x302,0xD4},{'U',0x302,0xDB},
    /* tréma U+0308 */
    {'a',0x308,0xE4},{'e',0x308,0xEB},{'i',0x308,0xEF},{'o',0x308,0xF6},{'u',0x308,0xFC},{'y',0x308,0xFF},
    {'A',0x308,0xC4},{'E',0x308,0xCB},{'I',0x308,0xCF},{'O',0x308,0xD6},{'U',0x308,0xDC},{'Y',0x308,0x178},
    /* cédille U+0327 */
    {'c',0x327,0xE7},{'C',0x327,0xC7},
};

static uint32_t composer(uint32_t base, uint32_t marque) {
    for (size_t i = 0; i < sizeof COMPOSITIONS / sizeof COMPOSITIONS[0]; i++)
        if (COMPOSITIONS[i].base == base && COMPOSITIONS[i].marque == marque)
            return COMPOSITIONS[i].compose;
    return 0;
}

static size_t normaliser(uint32_t *cp, size_t n) {
    size_t k = 0;
    for (size_t i = 0; i < n; i++) {
        if (k > 0) {
            uint32_t r = composer(cp[k - 1], cp[i]);
            if (r) { cp[k - 1] = r; continue; }
        }
        cp[k++] = cp[i];
    }
    return k;
}

/* ---------------------------------------------------------------- */
/* Classes de caractères                                            */
/* ---------------------------------------------------------------- */

static int est_lettre(uint32_t c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
        || (c >= 0xC0 && c <= 0xFF && c != 0xD7 && c != 0xF7)
        || c == 0x152 || c == 0x153 || c == 0x178;
}

static int est_chiffre(uint32_t c) { return c >= '0' && c <= '9'; }

static int est_insecable(uint32_t c) { return c == 0xA0 || c == 0x202F; }

static int est_blanc(uint32_t c) {
    return c == ' ' || c == '\t' || c == '\n' || c == 0x2009 || est_insecable(c);
}

static int est_apostrophe(uint32_t c) { return c == '\'' || c == 0x2019; }

/* Casse ignorée (charte, art. 4). */
static uint32_t minuscule(uint32_t c) {
    if (c >= 'A' && c <= 'Z') return c + 32;
    if (c >= 0xC0 && c <= 0xDE && c != 0xD7) return c + 32;
    if (c == 0x152) return 0x153;
    if (c == 0x178) return 0xFF;
    return c;
}

/* ---------------------------------------------------------------- */
/* Lexeur                                                           */
/* ---------------------------------------------------------------- */

Lexeur *lexeur_creer(const char *source, size_t taille, char **erreur) {
    uint32_t *cp;
    size_t n;
    if (!decoder_utf8((const unsigned char *)source, taille, &cp, &n, erreur))
        return NULL;
    Lexeur *lx = allouer(sizeof *lx);
    lx->cp = cp;
    lx->n = normaliser(cp, n);
    lx->pos = 0;
    lx->ligne = 1;
    lx->colonne = 1;
    lx->debut_ligne = 1;
    lx->termine = 0;
    lx->compact = 0;
    return lx;
}

Lexeur *lexeur_creer_compact(const char *source, size_t taille, char **erreur) {
    Lexeur *lx = lexeur_creer(source, taille, erreur);
    if (lx) lx->compact = 1;
    return lx;
}

void lexeur_detruire(Lexeur *lx) {
    if (!lx) return;
    free(lx->cp);
    free(lx);
}

void jeton_liberer(Jeton *j) {
    free(j->valeur);
    j->valeur = NULL;
}

const char *type_jeton_nom(TypeJeton t) {
    static const char *NOMS[] = {
        "FIN", "MOT", "ÉLISION", "NOMBRE", "TEXTE", "PLUS", "MOINS", "FOIS",
        "DIVISE", "PUISSANCE", "PAR_OUV", "PAR_FERM", "POINT", "VIRGULE",
        "DEUX_POINTS", "REMARQUE", "ÉGAL", "DIFFÉRENT", "INFÉRIEUR", "SUPÉRIEUR",
        "INFÉRIEUR_OU_ÉGAL", "SUPÉRIEUR_OU_ÉGAL", "CROCHETS", "DATE", "ARTICLE_IMPLICITE", "MOT_CLÉ",
        "AFFECTE", "POINT_VIRGULE", "ERREUR"
    };
    return (t >= J_FIN && t <= J_ERREUR) ? NOMS[t] : "?";
}

static uint32_t voir(const Lexeur *lx, size_t decalage) {
    size_t p = lx->pos + decalage;
    return p < lx->n ? lx->cp[p] : 0;
}

static int reste(const Lexeur *lx, size_t decalage) {
    return lx->pos + decalage < lx->n;
}

static void avancer(Lexeur *lx) {
    if (lx->pos >= lx->n) return;
    if (lx->cp[lx->pos] == '\n') {
        lx->ligne++;
        lx->colonne = 1;
        lx->debut_ligne = 1;
    } else {
        lx->colonne++;
    }
    lx->pos++;
}

static char *extrait(const Lexeur *lx, size_t debut, size_t fin) {
    Tampon t = {0};
    for (size_t i = debut; i < fin && i < lx->n; i++) tampon_cp(&t, lx->cp[i]);
    return tampon_rendre(&t);
}

static Jeton faire(const Lexeur *lx, TypeJeton type, size_t debut,
                   int ligne, int col, char *valeur) {
    Jeton j;
    j.type = type;
    j.debut = debut;
    j.longueur = lx->pos - debut;
    j.ligne = ligne;
    j.colonne = col;
    j.valeur = valeur;
    j.retrait = col;
    j.synthetique = 0;
    j.ligne_fin = ligne;
    return j;
}

static Jeton echec(Lexeur *lx, size_t debut, int ligne, int col, char *message) {
    lx->termine = 1;
    return faire(lx, J_ERREUR, debut, ligne, col, message);
}

static Jeton simple(Lexeur *lx, TypeJeton type, size_t debut, int ligne, int col) {
    avancer(lx);
    return faire(lx, type, debut, ligne, col, NULL);
}

/* ---------- Mots, élisions, remarques ---------- */

static Jeton lire_mot(Lexeur *lx, size_t debut, int ligne, int col, int en_debut_ligne) {
    Tampon t = {0};
    while (reste(lx, 0) && (est_lettre(voir(lx, 0)) || est_chiffre(voir(lx, 0)))) {
        tampon_cp(&t, minuscule(voir(lx, 0)));
        avancer(lx);
    }
    char *mot = tampon_rendre(&t);

    /* Élision : l'apostrophe suit une lettre (§ 1.2). */
    if (reste(lx, 0) && est_apostrophe(voir(lx, 0))) {
        avancer(lx);
        return faire(lx, J_ELISION, debut, ligne, col, mot);
    }

    /* Commentaire : ligne qui commence par « Remarque : » (§ 1.7). */
    if (en_debut_ligne && strcmp(mot, "remarque") == 0) {
        size_t p = lx->pos;
        while (p < lx->n && lx->cp[p] != '\n' && est_blanc(lx->cp[p])) p++;
        if (p < lx->n && lx->cp[p] == ':') {
            free(mot);
            while (lx->pos <= p) avancer(lx);
            while (reste(lx, 0) && voir(lx, 0) != '\n' && est_blanc(voir(lx, 0))) avancer(lx);
            size_t d = lx->pos, f = lx->pos;
            while (f < lx->n && lx->cp[f] != '\n') f++;
            size_t fin_ligne = f;
            while (f > d && est_blanc(lx->cp[f - 1])) f--;
            char *contenu = extrait(lx, d, f);
            while (lx->pos < fin_ligne) avancer(lx);
            return faire(lx, J_REMARQUE, debut, ligne, col, contenu);
        }
    }
    return faire(lx, J_MOT, debut, ligne, col, mot);
}

/* ---------- Nombres (§ 1.2) ---------- */

static Jeton lire_nombre(Lexeur *lx, size_t debut, int ligne, int col) {
    Tampon t = {0};
    int groupe = 0, separateur_vu = 0, groupes_faux = 0;

    /* Partie entière, avec séparateurs de milliers facultatifs. */
    while (reste(lx, 0)) {
        uint32_t c = voir(lx, 0);
        if (est_chiffre(c)) {
            tampon_octet(&t, (unsigned char)c);
            groupe++;
            avancer(lx);
        } else if ((est_apostrophe(c) || est_insecable(c))
                   && reste(lx, 1) && est_chiffre(voir(lx, 1))) {
            if (separateur_vu ? groupe != 3 : (groupe < 1 || groupe > 3)) groupes_faux = 1;
            separateur_vu = 1;
            groupe = 0;
            avancer(lx);
        } else {
            break;
        }
    }
    if (separateur_vu && groupe != 3) groupes_faux = 1;
    if (groupes_faux) {
        free(t.d);
        char *texte = extrait(lx, debut, lx->pos);
        char *m = formater("Séparateur de milliers mal placé dans « %s » : "
                           "les groupes comptent trois chiffres (1'000, 12'345).", texte);
        free(texte);
        return echec(lx, debut, ligne, col, m);
    }

    /* Partie décimale : virgule suivie d'un chiffre collé. */
    if (voir(lx, 0) == ',' && reste(lx, 1) && est_chiffre(voir(lx, 1))) {
        avancer(lx);
        tampon_octet(&t, '.');
        while (reste(lx, 0) && est_chiffre(voir(lx, 0))) {
            tampon_octet(&t, (unsigned char)voir(lx, 0));
            avancer(lx);
        }
        if (voir(lx, 0) == ',' && reste(lx, 1) && est_chiffre(voir(lx, 1))) {
            free(t.d);
            avancer(lx);
            while (reste(lx, 0) && est_chiffre(voir(lx, 0))) avancer(lx);
            char *texte = extrait(lx, debut, lx->pos);
            char *m = formater("Nombre mal formé « %s » : un nombre n'a qu'une virgule décimale.", texte);
            free(texte);
            return echec(lx, debut, ligne, col, m);
        }
    } else if (voir(lx, 0) == '.' && reste(lx, 1) && est_chiffre(voir(lx, 1))) {
        /* Date « 21.09.2026 » (§ 14.1) : trois groupes de chiffres séparés par des points. */
        size_t k = 1, g2 = 0, g3 = 0;
        while (reste(lx, k) && est_chiffre(voir(lx, k))) { k++; g2++; }
        if (reste(lx, k + 1) && voir(lx, k) == '.' && est_chiffre(voir(lx, k + 1))) {
            size_t q = k + 1;
            while (reste(lx, q) && est_chiffre(voir(lx, q))) { q++; g3++; }
            int colle = reste(lx, q) && est_lettre(voir(lx, q));
            size_t g1 = strlen(t.d ? t.d : "");
            int jour = atoi(t.d ? t.d : "0");
            free(t.d);
            size_t fin = lx->pos + q;
            if (separateur_vu || g1 > 2 || g2 > 2 || g3 != 4 || colle) {
                while (lx->pos < fin) avancer(lx);
                char *texte = extrait(lx, debut, lx->pos);
                char *m = formater("Date mal formée « %s » : écrivez jour.mois.année, "
                                   "l'année sur quatre chiffres (21.09.2026).", texte);
                free(texte);
                return echec(lx, debut, ligne, col, m);
            }
            avancer(lx);
            int mois = 0, annee = 0;
            while (est_chiffre(voir(lx, 0))) { mois = mois * 10 + (int)(voir(lx, 0) - '0'); avancer(lx); }
            avancer(lx);
            while (lx->pos < fin) { annee = annee * 10 + (int)(voir(lx, 0) - '0'); avancer(lx); }
            char *probleme = NULL;
            if (!date_verifier(annee, mois, jour, &probleme)) {
                char *m = formater("%s", probleme);
                free(probleme);
                return echec(lx, debut, ligne, col, m);
            }
            return faire(lx, J_DATE, debut, ligne, col, date_iso(date_jours(annee, mois, jour)));
        }
        /* Point décimal à l'anglaise : erreur avec correction. */
        free(t.d);
        avancer(lx);
        while (reste(lx, 0) && est_chiffre(voir(lx, 0))) avancer(lx);
        char *texte = extrait(lx, debut, lx->pos);
        char *correction = extrait(lx, debut, lx->pos);
        for (char *p = correction; *p; p++) if (*p == '.') *p = ',';
        int court = strlen(texte) <= 5;   /* « 21.09 » : peut-être une date sans année */
        char *m = formater("« %s » : en GrymoiR, la virgule sert de séparateur décimal. "
                           "Écrivez « %s »%s", texte, correction,
                           court ? ", ou, pour une date, ajoutez l'année : 21.09.2026." : ".");
        free(texte);
        free(correction);
        return echec(lx, debut, ligne, col, m);
    }

    /* Nombre collé à un mot : « 3kg ». */
    if (reste(lx, 0) && est_lettre(voir(lx, 0))) {
        free(t.d);
        while (reste(lx, 0) && (est_lettre(voir(lx, 0)) || est_chiffre(voir(lx, 0)))) avancer(lx);
        char *texte = extrait(lx, debut, lx->pos);
        char *m = formater("« %s » : un nombre ne peut pas être collé à un mot. "
                           "Séparez-les par une espace.", texte);
        free(texte);
        return echec(lx, debut, ligne, col, m);
    }

    return faire(lx, J_NOMBRE, debut, ligne, col, tampon_rendre(&t));
}

/* ---------- Texte (§ 1.5) ---------- */

static Jeton lire_texte(Lexeur *lx, size_t debut, int ligne, int col,
                        uint32_t fermant, int rogner) {
    avancer(lx); /* guillemet ouvrant */
    size_t d = lx->pos;
    while (reste(lx, 0) && voir(lx, 0) != fermant && voir(lx, 0) != '\n') avancer(lx);
    if (!reste(lx, 0) || voir(lx, 0) == '\n') {
        char *m = formater("Texte non fermé : il manque le guillemet fermant %s "
                           "avant la fin de la ligne.",
                           fermant == 0xBB ? "»" : fermant == 0x201D ? "”" : "\"");
        return echec(lx, debut, ligne, col, m);
    }
    size_t f = lx->pos;
    avancer(lx); /* guillemet fermant */
    if (rogner) {
        while (d < f && est_blanc(lx->cp[d])) d++;
        while (f > d && est_blanc(lx->cp[f - 1])) f--;
    }
    return faire(lx, J_TEXTE, debut, ligne, col, extrait(lx, d, f));
}

/* ---------- Noms entre crochets (§ 2.2) ---------- */

/* [frais de port et d'emballage] : un nom qui peut contenir des mots réservés.
 * La valeur du jeton est la clé du nom, écrite comme l'analyseur l'écrit. */
static Jeton lire_crochets(Lexeur *lx, size_t debut, int ligne, int col) {
    avancer(lx); /* [ */
    Tampon t = {0};
    int mots = 0, apres_elision = 0;
    for (;;) {
        while (reste(lx, 0) && voir(lx, 0) != '\n' && est_blanc(voir(lx, 0))) avancer(lx);
        if (!reste(lx, 0) || voir(lx, 0) == '\n') {
            free(t.d);
            return echec(lx, debut, ligne, col, formater(
                "Crochet fermant ] manquant avant la fin de la ligne."));
        }
        uint32_t c = voir(lx, 0);
        if (c == ']') {
            avancer(lx);
            break;
        }
        if (!est_lettre(c)) {
            free(t.d);
            char *car = extrait(lx, lx->pos, lx->pos + 1);
            char *m = formater("« %s » ne peut pas faire partie d'un nom entre crochets.", car);
            free(car);
            return echec(lx, debut, ligne, col, m);
        }
        if (mots && !apres_elision) tampon_octet(&t, ' ');
        while (reste(lx, 0) && (est_lettre(voir(lx, 0)) || est_chiffre(voir(lx, 0)))) {
            tampon_cp(&t, minuscule(voir(lx, 0)));
            avancer(lx);
        }
        apres_elision = reste(lx, 0) && est_apostrophe(voir(lx, 0));
        if (apres_elision) {
            tampon_octet(&t, '\'');
            avancer(lx);
        }
        mots++;
    }
    if (!mots || apres_elision) {
        free(t.d);
        return echec(lx, debut, ligne, col, formater(mots
            ? "Un nom entre crochets ne peut pas finir par une élision."
            : "Nom vide entre crochets."));
    }
    return faire(lx, J_CROCHETS, debut, ligne, col, tampon_rendre(&t));
}

static Jeton double_(Lexeur *lx, TypeJeton type, size_t debut, int ligne, int col) {
    avancer(lx);
    return simple(lx, type, debut, ligne, col);
}

/* Vrai si la ligne ne contient plus que des blancs à partir de la position courante. */
static int ligne_blanche(const Lexeur *lx) {
    for (size_t p = lx->pos; p < lx->n && lx->cp[p] != '\n'; p++)
        if (!est_blanc(lx->cp[p])) return 0;
    return 1;
}

/* ---------- Forme compacte (§ 11.1) ---------- */

/* « _sinon_si », « _l' » : mot-clé ; « prix_de_l'article » : nom. */
static Jeton lire_compact(Lexeur *lx, size_t debut, int ligne, int col) {
    int cle = voir(lx, 0) == '_';
    if (cle) avancer(lx);
    if (!reste(lx, 0) || !est_lettre(voir(lx, 0))) {
        return echec(lx, debut, ligne, col, formater(cle
            ? "« _ » seul : un mot-clé s'écrit « _si », « _fin »…"
            : "Nom attendu."));
    }
    Tampon t = {0};
    for (;;) {
        uint32_t c = voir(lx, 0);
        if (est_lettre(c) || est_chiffre(c)) {
            tampon_cp(&t, minuscule(c));
            avancer(lx);
        } else if (c == '_' && reste(lx, 1) && est_lettre(voir(lx, 1))) {
            tampon_octet(&t, (unsigned char)(cle ? '_' : ' '));
            avancer(lx);
        } else if (est_apostrophe(c)) {
            tampon_octet(&t, '\'');
            avancer(lx);
            if (cle && t.n == 8 && memcmp(t.d, "aujourd'", 8) == 0 && voir(lx, 0) == 'h'
                && voir(lx, 1) == 'u' && voir(lx, 2) == 'i') {
                for (int q = 0; q < 3; q++) { tampon_octet(&t, (unsigned char)voir(lx, 0)); avancer(lx); }
                break;                                        /* « _aujourd'hui » */
            }
            if (cle) break;                                   /* « _l' » */
            if (!reste(lx, 0) || !est_lettre(voir(lx, 0))) {
                free(t.d);
                return echec(lx, debut, ligne, col, formater("Une élision est suivie d'une lettre : « prix_de_l'article »."));
            }
        } else {
            break;
        }
    }
    Jeton j = faire(lx, cle ? J_MOT_CLE : J_CROCHETS, debut, ligne, col, tampon_rendre(&t));
    j.synthetique = !cle;
    return j;
}

/* ---------- Aiguillage ---------- */

Jeton lexeur_suivant(Lexeur *lx) {
    if (!lx->termine)
        while (reste(lx, 0) && est_blanc(voir(lx, 0))) {
            /* L'indentation délimite les blocs (§ 5) : pas de tabulation en début de ligne. */
            if (voir(lx, 0) == '\t' && lx->debut_ligne && !ligne_blanche(lx))
                return echec(lx, lx->pos, lx->ligne, lx->colonne, formater(
                    "Tabulation en début de ligne : indentez avec des espaces."));
            avancer(lx);
        }

    size_t debut = lx->pos;
    int ligne = lx->ligne, col = lx->colonne;

    if (lx->termine || !reste(lx, 0)) {
        lx->termine = 1;
        return faire(lx, J_FIN, debut, ligne, col, NULL);
    }

    int en_debut_ligne = lx->debut_ligne;
    lx->debut_ligne = 0;
    uint32_t c = voir(lx, 0);

    if (lx->compact) {
        if (est_lettre(c) || c == '_') return lire_compact(lx, debut, ligne, col);
        if (c == '<' && voir(lx, 1) == '<') return double_(lx, J_AFFECTE, debut, ligne, col);
        if (c == ';') return simple(lx, J_POINT_VIRGULE, debut, ligne, col);
        if (c == '[' || c == ']')
            return echec(lx, debut, ligne, col, formater(
                "Pas de crochets en forme compacte : écrivez le nom avec des soulignés (frais_de_port)."));
        if (c == '#') {
            if (!en_debut_ligne)
                return echec(lx, debut, ligne, col, formater(
                    "Une remarque commence une ligne : passez à la ligne avant « # »."));
            avancer(lx);
            while (reste(lx, 0) && voir(lx, 0) != '\n' && est_blanc(voir(lx, 0))) avancer(lx);
            size_t d = lx->pos, f = lx->pos;
            while (f < lx->n && lx->cp[f] != '\n') f++;
            size_t fin_ligne = f;
            while (f > d && est_blanc(lx->cp[f - 1])) f--;
            char *contenu = extrait(lx, d, f);
            while (lx->pos < fin_ligne) avancer(lx);
            return faire(lx, J_REMARQUE, debut, ligne, col, contenu);
        }
    }

    if (est_lettre(c)) return lire_mot(lx, debut, ligne, col, en_debut_ligne);
    if (est_chiffre(c)) return lire_nombre(lx, debut, ligne, col);

    switch (c) {
    case '+':    return simple(lx, J_PLUS, debut, ligne, col);
    case '-':
    case 0x2013: /* tiret demi-cadratin, inséré par la correction automatique de Word */
    case 0x2212: return simple(lx, J_MOINS, debut, ligne, col);
    case '*':
    case 0xD7:   return simple(lx, J_FOIS, debut, ligne, col);
    case '/':
    case 0xF7:   return simple(lx, J_DIVISE, debut, ligne, col);
    case '^':    return simple(lx, J_PUISSANCE, debut, ligne, col);
    case '(':    return simple(lx, J_PAR_OUV, debut, ligne, col);
    case ')':    return simple(lx, J_PAR_FERM, debut, ligne, col);
    case '.':    return simple(lx, J_POINT, debut, ligne, col);
    case ',':    return simple(lx, J_VIRGULE, debut, ligne, col);
    case ':':    return simple(lx, J_DEUX_POINTS, debut, ligne, col);
    case '=':    return simple(lx, J_EGAL, debut, ligne, col);
    case 0x2260: return simple(lx, J_DIFFERENT, debut, ligne, col);
    case 0x2264: return simple(lx, J_INF_EGAL, debut, ligne, col);
    case 0x2265: return simple(lx, J_SUP_EGAL, debut, ligne, col);
    case '<':
        if (voir(lx, 1) == '=') return double_(lx, J_INF_EGAL, debut, ligne, col);
        if (voir(lx, 1) == '>') return double_(lx, J_DIFFERENT, debut, ligne, col);
        return simple(lx, J_INFERIEUR, debut, ligne, col);
    case '>':
        if (voir(lx, 1) == '=') return double_(lx, J_SUP_EGAL, debut, ligne, col);
        return simple(lx, J_SUPERIEUR, debut, ligne, col);
    case '[':    return lire_crochets(lx, debut, ligne, col);
    case ']':
        return echec(lx, debut, ligne, col, formater("Crochet fermant ] sans crochet ouvrant [."));
    case 0xAB:   return lire_texte(lx, debut, ligne, col, 0xBB, 1);
    case '"':    return lire_texte(lx, debut, ligne, col, '"', 0);

    case 0x201C: return lire_texte(lx, debut, ligne, col, 0x201D, 0);
    case 0x201D:
        return echec(lx, debut, ligne, col, formater(
            "Guillemet fermant ” sans guillemet ouvrant “."));
    case 0x201E:
        return echec(lx, debut, ligne, col, formater(
            "Guillemet „ non reconnu : utilisez « », “ ” ou \" \"."));
    case 0xBB:
        return echec(lx, debut, ligne, col, formater(
            "Guillemet fermant » sans guillemet ouvrant."));
    case 0x2014:
        return echec(lx, debut, ligne, col, formater(
            "Tiret cadratin non reconnu : pour soustraire, utilisez -, − ou –."));
    case '\'': case 0x2019:
        return echec(lx, debut, ligne, col, formater(
            "Apostrophe inattendue : l'élision suit une lettre (l'addition), "
            "le séparateur de milliers suit un chiffre (1'000)."));
    default:
        break;
    }

    if (c < 0x20 || (c >= 0x300 && c <= 0x36F) || c == 0x7F)
        return echec(lx, debut, ligne, col,
                     formater("Caractère inattendu U+%04X.", (unsigned)c));
    char *car = extrait(lx, lx->pos, lx->pos + 1);
    char *m = formater("Caractère inattendu « %s ».", car);
    free(car);
    return echec(lx, debut, ligne, col, m);
}

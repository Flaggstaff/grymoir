/* GrymoiR : utilitaires de texte UTF-8 partagés. */
#include "texte.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void *grym_allouer(size_t taille) {
    void *p = malloc(taille ? taille : 1);
    if (!p) {
        fputs("GrymoiR : mémoire épuisée\n", stderr);
        exit(EXIT_FAILURE);
    }
    return p;
}

char *grym_dupliquer(const char *s) {
    size_t n = strlen(s);
    char *d = grym_allouer(n + 1);
    memcpy(d, s, n + 1);
    return d;
}

char *grym_formater(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n < 0) n = 0;
    char *s = grym_allouer((size_t)n + 1);
    va_start(ap, fmt);
    vsnprintf(s, (size_t)n + 1, fmt, ap);
    va_end(ap);
    return s;
}

void chaine_ajouter(Chaine *c, const char *s) {
    size_t n = strlen(s);
    if (c->n + n + 1 > c->cap) {
        size_t cap = c->cap ? c->cap : 64;
        while (c->n + n + 1 > cap) cap *= 2;
        char *d = realloc(c->d, cap);
        if (!d) {
            fputs("GrymoiR : mémoire épuisée\n", stderr);
            exit(EXIT_FAILURE);
        }
        c->d = d;
        c->cap = cap;
    }
    memcpy(c->d + c->n, s, n + 1);
    c->n += n;
}

char *chaine_rendre(Chaine *c) {
    if (!c->d) return grym_dupliquer("");
    return c->d;
}

/* Décodage minimal : la source a déjà été validée par le lexeur. */
static size_t decoder(const char *s, uint32_t *sortie, size_t max) {
    const unsigned char *p = (const unsigned char *)s;
    size_t k = 0;
    while (*p && k < max) {
        uint32_t v;
        int len;
        if (*p < 0x80)                { v = *p;        len = 1; }
        else if ((*p & 0xE0) == 0xC0) { v = *p & 0x1F; len = 2; }
        else if ((*p & 0xF0) == 0xE0) { v = *p & 0x0F; len = 3; }
        else                          { v = *p & 0x07; len = 4; }
        p++;
        for (int i = 1; i < len && *p; i++, p++) v = (v << 6) | (*p & 0x3F);
        sortie[k++] = v;
    }
    return k;
}

size_t longueur_utf8(const char *s) {
    size_t n = 0;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++)
        if ((*p & 0xC0) != 0x80) n++;
    return n;
}

size_t distance_edition(const char *a, const char *b) {
    enum { MAX = 128 };
    uint32_t x[MAX], y[MAX];
    size_t m = decoder(a, x, MAX), n = decoder(b, y, MAX);
    size_t ligne[MAX + 1];
    for (size_t j = 0; j <= n; j++) ligne[j] = j;
    for (size_t i = 1; i <= m; i++) {
        size_t diag = ligne[0];
        ligne[0] = i;
        for (size_t j = 1; j <= n; j++) {
            size_t haut = ligne[j];
            size_t cout = x[i - 1] == y[j - 1] ? 0 : 1;
            size_t v = diag + cout;
            if (haut + 1 < v) v = haut + 1;
            if (ligne[j - 1] + 1 < v) v = ligne[j - 1] + 1;
            ligne[j] = v;
            diag = haut;
        }
    }
    return ligne[n];
}

/* ---------------------------------------------------------------- */
/* Casse et accents (grammaire, § 4.5)                              */
/* ---------------------------------------------------------------- */

/* Minuscule, majuscule et lettre de base ; même partage que l'ordre du dictionnaire des recherches (base.c). */
static const struct { const char *min, *maj; char base; } LETTRES[] = {
    { "à", "À", 'a' }, { "â", "Â", 'a' }, { "ä", "Ä", 'a' }, { "á", "Á", 'a' }, { "ç", "Ç", 'c' },
    { "é", "É", 'e' }, { "è", "È", 'e' }, { "ê", "Ê", 'e' }, { "ë", "Ë", 'e' }, { "î", "Î", 'i' },
    { "ï", "Ï", 'i' }, { "í", "Í", 'i' }, { "ô", "Ô", 'o' }, { "ö", "Ö", 'o' }, { "ó", "Ó", 'o' },
    { "ù", "Ù", 'u' }, { "û", "Û", 'u' }, { "ü", "Ü", 'u' }, { "ú", "Ú", 'u' }, { "ÿ", "Ÿ", 'y' },
    { "ñ", "Ñ", 'n' }, { "œ", "Œ", 0 }, { "æ", "Æ", 0 }
};
#define NB_LETTRES (sizeof LETTRES / sizeof *LETTRES)

/* sens : 0 minuscules, 1 majuscules, 2 plié */
static char *transformer(const char *s, int sens) {
    Chaine c = {0};
    char un[2] = { 0, 0 };
    for (const char *p = s; *p; ) {
        size_t k;
        for (k = 0; k < NB_LETTRES; k++) {
            const char *autre = sens == 1 ? LETTRES[k].min : LETTRES[k].maj;   /* la forme à remplacer */
            size_t lm = strlen(LETTRES[k].min), lM = strlen(LETTRES[k].maj);
            if (sens == 2) {
                const char *forme = strncmp(p, LETTRES[k].min, lm) == 0 ? LETTRES[k].min
                                  : strncmp(p, LETTRES[k].maj, lM) == 0 ? LETTRES[k].maj : NULL;
                if (!forme) continue;
                if (LETTRES[k].base) { un[0] = LETTRES[k].base; chaine_ajouter(&c, un); }
                else chaine_ajouter(&c, LETTRES[k].min);   /* œ, æ : sans lettre de base, minuscule */
                p += strlen(forme);
                break;
            }
            if (strncmp(p, autre, strlen(autre)) == 0) {
                chaine_ajouter(&c, sens == 1 ? LETTRES[k].maj : LETTRES[k].min);
                p += strlen(autre);
                break;
            }
        }
        if (k < NB_LETTRES) continue;
        un[0] = *p;
        if (sens != 1 && *p >= 'A' && *p <= 'Z') un[0] = (char)(*p + 32);
        if (sens == 1 && *p >= 'a' && *p <= 'z') un[0] = (char)(*p - 32);
        chaine_ajouter(&c, un);
        p++;
    }
    return chaine_rendre(&c);
}

char *texte_plier(const char *s) { return transformer(s, 2); }
char *texte_majuscules(const char *s) { return transformer(s, 1); }
char *texte_minuscules(const char *s) { return transformer(s, 0); }

int texte_contient(const char *texte, const char *partie, char sens) {
    char *t = texte_plier(texte), *q = texte_plier(partie);
    size_t lt = strlen(t), lq = strlen(q);
    int r = 0;
    if (lq <= lt) {
        if (sens == 'd') r = memcmp(t, q, lq) == 0;
        else if (sens == 'f') r = memcmp(t + lt - lq, q, lq) == 0;
        else r = strstr(t, q) != NULL;
    }
    free(t);
    free(q);
    return r;
}

/* ---------------------------------------------------------------- */
/* Sigles (grammaire, § 2.4)                                        */
/* ---------------------------------------------------------------- */

/* Le registre de ce processus : chaque mot vu, en minuscules, avec la façon dont on l'a écrit. Un mot écrit au moins
 * une fois tout en capitales, et jamais autrement, est un sigle : le libellé et la forme canonique lui rendent ses
 * capitales. Les noms restent insensibles à la casse : le registre ne change que l'affichage. */
typedef struct { char *mot; unsigned char vu; } MotVu;   /* vu : 1 en capitales, 2 autrement */
static MotVu *mots;
static size_t nb_mots, cap_mots;   /* table à adressage ouvert, cap_mots puissance de deux */

static size_t hacher(const char *m) {
    size_t h = 1469598103934665603u;
    for (; *m; m++) h = (h ^ (unsigned char)*m) * 1099511628211u;
    return h;
}

static MotVu *case_de(const char *mot) {
    size_t i = hacher(mot) & (cap_mots - 1);
    while (mots[i].mot && strcmp(mots[i].mot, mot) != 0) i = (i + 1) & (cap_mots - 1);
    return &mots[i];
}

static void noter(const char *mot, unsigned char vu) {
    if ((nb_mots + 1) * 2 > cap_mots) {
        MotVu *ancien = mots;
        size_t ancienne = cap_mots;
        cap_mots = cap_mots ? cap_mots * 2 : 256;
        mots = grym_allouer(cap_mots * sizeof *mots);
        memset(mots, 0, cap_mots * sizeof *mots);
        for (size_t k = 0; k < ancienne; k++) if (ancien[k].mot) *case_de(ancien[k].mot) = ancien[k];
        free(ancien);
    }
    MotVu *c = case_de(mot);
    if (!c->mot) { c->mot = grym_dupliquer(mot); nb_mots++; }
    c->vu |= vu;
}

void sigle_retenir(const char *mot) { noter(mot, 1); }
void sigle_dementir(const char *mot) { noter(mot, 2); }

static int est_sigle(const char *mot) {
    if (!cap_mots) return 0;
    const MotVu *c = case_de(mot);
    return c->mot && c->vu == 1;
}

void sigles_oublier(void) {
    for (size_t k = 0; k < cap_mots; k++) free(mots[k].mot);
    free(mots);
    mots = NULL;
    nb_mots = cap_mots = 0;
}

static int separe_les_mots(unsigned char c) { return c == ' ' || c == '\'' || c == '_' || c == '-'; }

char *texte_avec_sigles(const char *nom) {
    Chaine c = {0};
    for (const char *p = nom; *p; ) {
        if (separe_les_mots((unsigned char)*p)) {
            char t[2] = { *p, 0 };
            chaine_ajouter(&c, t);
            p++;
            continue;
        }
        size_t l = 0;
        while (p[l] && !separe_les_mots((unsigned char)p[l])) l++;
        char *mot = grym_allouer(l + 1);
        memcpy(mot, p, l);
        mot[l] = 0;
        if (est_sigle(mot)) {
            char *m = texte_majuscules(mot);
            chaine_ajouter(&c, m);
            free(m);
        } else {
            chaine_ajouter(&c, mot);
        }
        free(mot);
        p += l;
    }
    return chaine_rendre(&c);
}

/* Les sigles, triés : le bytecode ne dépend pas de l'ordre de la table. */
static int comparer_textes(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

char *texte_sigles(void) {
    char **l = grym_allouer((nb_mots ? nb_mots : 1) * sizeof *l);
    size_t n = 0;
    for (size_t k = 0; k < cap_mots; k++) if (mots[k].mot && mots[k].vu == 1) l[n++] = mots[k].mot;
    qsort(l, n, sizeof *l, comparer_textes);
    Chaine c = {0};
    for (size_t k = 0; k < n; k++) {
        if (k) chaine_ajouter(&c, " ");
        chaine_ajouter(&c, l[k]);
    }
    free(l);
    return chaine_rendre(&c);
}

/* La langue d'un texte (grammaire 2.86) : ses mots outils, comptés dans sept langues ; la plus présente l'emporte si elle a
 * deux mots au moins et un de plus que la suivante. Les mots sont pliés (sans accents ni casse) et coupés aux apostrophes :
 * « c'est » donne « c » et « est ». Un mot propre à plusieurs langues (« de », « la ») compte pour chacune. NULL : indécis
 * (trop court, des indicatifs, des chiffres). Codes ISO 639-1. */
const char *langue_reconnue(const char *texte) {
    static const char *const CODES[] = { "fr", "en", "de", "es", "it", "nl", "pt" };
    static const char *const MOTS[] = {
        /* fr */ " le la les un une des et est je tu il elle nous vous ils elles du que qui pas ne au aux pour avec sur ce cette"
                 " mais ou suis sont c j qu l d n bonjour merci oui non tres bien ici ",
        /* en */ " the and is are you i he she we they of to in that it for on with this be not have at was your what"
                 " hello thanks yes no my me over here there will ",
        /* de */ " der die das und ist ich du er sie wir ihr nicht ein eine zu mit auf fur von den dem ja nein bitte danke"
                 " hier auch sind bin habe ",
        /* es */ " el la los las y es de que en un una por con para no se lo como pero esta muy hola gracias si estoy ",
        /* it */ " il lo la gli le e di che non un una per con sono ma come questo della ciao grazie si ho sei ",
        /* nl */ " de het een en is van ik je niet dat die op te met voor zijn wat hallo dank ja nee ",
        /* pt */ " o a os as e de que nao um uma para com por em do da sao mas ola obrigado sim estou ",
    };
    enum { NL = 7 };
    int compte[NL] = { 0 };
    char *p = texte_plier(texte ? texte : "");
    for (char *q = p; *q;) {
        while (*q && !((*q >= 'a' && *q <= 'z') || (unsigned char)*q >= 0x80)) q++;
        char *d = q;
        while ((*q >= 'a' && *q <= 'z') || (unsigned char)*q >= 0x80) q++;
        if (q == d) continue;
        char mot[40];
        const size_t n = (size_t)(q - d);
        if (n >= sizeof mot - 2) continue;
        mot[0] = ' ';
        memcpy(mot + 1, d, n);
        mot[n + 1] = ' ';
        mot[n + 2] = '\0';
        for (int k = 0; k < NL; k++) if (strstr(MOTS[k], mot)) compte[k]++;
    }
    free(p);
    int premiere = -1, seconde = 0;
    for (int k = 0; k < NL; k++) {
        if (premiere < 0 || compte[k] > compte[premiere]) {
            if (premiere >= 0) seconde = compte[premiere] > seconde ? compte[premiere] : seconde;
            premiere = k;
        } else if (compte[k] > seconde) seconde = compte[k];
    }
    if (premiere < 0 || compte[premiere] < 2 || compte[premiere] <= seconde) return NULL;
    return CODES[premiere];
}

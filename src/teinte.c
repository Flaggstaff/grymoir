/* GrymoiR : les couleurs libres des écrans (grammaire, § 22.5). Voir teinte.h. */
#include "teinte.h"
#include "texte.h"

#include <stdio.h>
#include <string.h>

/* Les jetons du thème que GrymoiR fixe (src/atelier/theme/grymoir-jetons.json), ceux dont la dérivation dépend. */
#define FOND_CLAIR "#F5EFE6"
#define TEXTE_CLAIR "#2B221B"
#define TEXTE2_CLAIR "#675A4D"
#define BORDURE_CLAIR "#86776A"
#define DANGER_CLAIR "#B42328"
#define FOND_SOMBRE "#1B1613"

static int hex(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

int teinte_lire(const char *t, int rgb[3]) {
    if (!t || t[0] != '#' || strlen(t) != 7) return 0;
    for (int k = 0; k < 3; k++) {
        const int a = hex(t[1 + 2 * k]), b = hex(t[2 + 2 * k]);
        if (a < 0 || b < 0) return 0;
        rgb[k] = a * 16 + b;
    }
    return 1;
}

/* x^(2/5), x dans [0, 1] : la racine cinquième de x², par Newton (sans libm, que tous les liens n'ont pas). */
static double puissance_deux_cinquiemes(double x) {
    if (x <= 0) return 0;
    const double c = x * x;
    double y = x < 0.5 ? 0.5 : x;
    for (int k = 0; k < 60; k++) {
        const double y4 = y * y * y * y;
        y -= (y4 * y - c) / (5 * y4);
    }
    return y;
}

static double luminance(const int rgb[3]) {
    double l[3];
    for (int k = 0; k < 3; k++) {
        const double x = rgb[k] / 255.0;
        if (x <= 0.04045) l[k] = x / 12.92;
        else {
            const double b = (x + 0.055) / 1.055;
            l[k] = b * b * puissance_deux_cinquiemes(b);   /* b^2.4 = b² · b^0.4 */
        }
    }
    return 0.2126 * l[0] + 0.7152 * l[1] + 0.0722 * l[2];
}

double teinte_contraste(const char *a, const char *b) {
    int x[3], y[3];
    if (!teinte_lire(a, x) || !teinte_lire(b, y)) return 0;
    const double la = luminance(x), lb = luminance(y);
    return ((la > lb ? la : lb) + 0.05) / ((la > lb ? lb : la) + 0.05);
}

/* a mêlé à b, dans la proportion t de b ; en « #RRGGBB » dans r (8 octets). */
static void melange(const char *a, const char *b, double t, char r[8]) {
    int x[3] = { 0, 0, 0 }, y[3] = { 0, 0, 0 };
    teinte_lire(a, x);
    teinte_lire(b, y);
    int z[3];
    for (int k = 0; k < 3; k++) {
        const double v = x[k] + (y[k] - x[k]) * t;
        z[k] = (int)(v + 0.5);
    }
    snprintf(r, 8, "#%02X%02X%02X", z[0], z[1], z[2]);
}

static void poser(TeinteTheme *t, const char *nom, const char *clair, const char *sombre) {
    for (size_t k = 0; k < t->n; k++)
        if (strcmp(t->noms[k], nom) == 0) {
            snprintf(t->clair[k], 8, "%s", clair);
            snprintf(t->sombre[k], 8, "%s", sombre ? sombre : "");
            return;
        }
    if (t->n == TEINTE_JETONS_MAX) return;
    snprintf(t->noms[t->n], sizeof t->noms[t->n], "%s", nom);
    snprintf(t->clair[t->n], 8, "%s", clair);
    snprintf(t->sombre[t->n], 8, "%s", sombre ? sombre : "");
    t->n++;
}

/* Un rapport sous son seuil : le message, avec le rapport mesuré, arrondi au dixième vers le bas (on ne flatte pas
 * une couleur qui échoue). */
static int seuil(const char *a, const char *b, double minimum, const char *quoi, char **erreur) {
    const double r = teinte_contraste(a, b);
    if (r >= minimum) return 1;
    /* au dixième, vers le bas (on ne flatte pas une couleur qui échoue), avec la virgule décimale */
    const long dixiemes = (long)(r * 10), seuil10 = (long)(minimum * 10 + 0.5);
    *erreur = grym_formater("%s : contraste de %ld,%ld:1, il faut au moins %ld,%ld:1 (WCAG 2.1).", quoi,
                            dixiemes / 10, dixiemes % 10, seuil10 / 10, seuil10 % 10);
    return 0;
}

int teinte_theme(const char *couleur, const char *fond, TeinteTheme *t, char **erreur) {
    int rgb[3];
    memset(t, 0, sizeof *t);
    *erreur = NULL;
    if (couleur && !teinte_lire(couleur, rgb)) {
        *erreur = grym_formater("Couleur « %s » mal écrite : # et six chiffres hexadécimaux, « #46433B ».", couleur);
        return 0;
    }
    if (fond && !teinte_lire(fond, rgb)) {
        *erreur = grym_formater("Fond « %s » mal écrit : # et six chiffres hexadécimaux, « #E8E2D6 ».", fond);
        return 0;
    }
    const char *g = fond ? fond : FOND_CLAIR;   /* le fond clair effectif */
    if (fond) {
        char surface[8], alt[8], survol[8], sep[8], desact[8], doux[8];
        melange(fond, "#FFFFFF", 0.55, surface);
        melange(fond, TEXTE_CLAIR, 0.06, alt);
        melange(fond, "#FFFFFF", 0.30, survol);
        melange(fond, TEXTE_CLAIR, 0.12, sep);
        melange(fond, TEXTE_CLAIR, 0.04, desact);
        melange(fond, DANGER_CLAIR, 0.08, doux);
        char quoi[96];
        snprintf(quoi, sizeof quoi, "Le texte secondaire sur le fond « %s »", fond);
        if (!seuil(TEXTE2_CLAIR, alt, 4.5, quoi, erreur)) return 0;
        snprintf(quoi, sizeof quoi, "Le bord des zones sur le fond « %s »", fond);
        if (!seuil(BORDURE_CLAIR, fond, 3.0, quoi, erreur)) return 0;
        t->clair_impose = 1;
        poser(t, "fond", fond, NULL);
        poser(t, "surface", surface, NULL);
        poser(t, "surface-alt", alt, NULL);
        poser(t, "survol", survol, NULL);
        poser(t, "separateur", sep, NULL);
        poser(t, "fond-desactive", desact, NULL);
        poser(t, "danger-doux", doux, NULL);
    }
    if (couleur) {
        char quoi[96];
        snprintf(quoi, sizeof quoi, "La couleur « %s » sur le fond « %s »", couleur, g);
        if (!seuil(couleur, g, 4.5, quoi, erreur)) return 0;
        const int blanc = teinte_contraste("#FFFFFF", couleur) >= 4.5;
        const char *sur = blanc ? "#FFFFFF" : FOND_SOMBRE;
        snprintf(quoi, sizeof quoi, "Le texte des boutons sur la couleur « %s »", couleur);
        if (!seuil(sur, couleur, 4.5, quoi, erreur)) return 0;
        /* survol et enfoncé s'éloignent du texte des boutons : plus sombres sous du blanc, plus clairs sinon */
        char survol[8], enfonce[8], doux[8];
        melange(couleur, blanc ? "#000000" : "#FFFFFF", 0.12, survol);
        melange(couleur, blanc ? "#000000" : "#FFFFFF", 0.24, enfonce);
        melange(couleur, g, 0.84, doux);
        snprintf(quoi, sizeof quoi, "Le texte sur la teinte douce de « %s »", couleur);
        if (!seuil(TEXTE_CLAIR, doux, 4.5, quoi, erreur)) return 0;
        /* en sombre : la couleur éclaircie jusqu'à se lire sur le fond sombre */
        char s_accent[8] = "", s_survol[8] = "", s_enfonce[8] = "", s_doux[8] = "";
        if (!t->clair_impose) {
            double k = 0;
            for (; k <= 1.0001; k += 0.05) {
                melange(couleur, "#FFFFFF", k, s_accent);
                if (teinte_contraste(s_accent, FOND_SOMBRE) >= 4.5) break;
            }
            melange(s_accent, "#FFFFFF", 0.15, s_survol);
            melange(s_accent, "#FFFFFF", 0.30, s_enfonce);
            melange(couleur, FOND_SOMBRE, 0.78, s_doux);
        }
        const int sombre = !t->clair_impose;
        poser(t, "accent", couleur, sombre ? s_accent : NULL);
        poser(t, "accent-survol", survol, sombre ? s_survol : NULL);
        poser(t, "accent-enfonce", enfonce, sombre ? s_enfonce : NULL);
        poser(t, "sur-accent", sur, sombre ? FOND_SOMBRE : NULL);
        poser(t, "accent-doux", doux, sombre ? s_doux : NULL);
    }
    return 1;
}

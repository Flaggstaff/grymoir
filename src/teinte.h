/* GrymoiR : les couleurs libres des écrans (grammaire, § 22.5).
 *
 * « Les écrans ont la couleur « #46433B » et le fond « #E8E2D6 ». » : une couleur d'accent et un fond, écrits en
 * hexadécimal. GrymoiR en dérive les autres jetons du thème (survol, enfoncé, doux, surfaces) et vérifie les
 * contrastes (WCAG 2.1) : une couleur illisible est refusée dès l'analyse, le rapport mesuré dans le message.
 * Le même calcul sert l'analyseur (le refus), la machine (les jetons envoyés à l'interface), l'atelier et le
 * navigateur (qui les appliquent).
 */
#ifndef GRYM_TEINTE_H
#define GRYM_TEINTE_H

#include <stddef.h>

/* « #46433B » ou « #46433b » → 1 et rgb ; sinon 0. Exactement six chiffres hexadécimaux après « # ». */
int teinte_lire(const char *texte, int rgb[3]);

/* Rapport de contraste WCAG 2.1 entre deux couleurs « #RRGGBB », de 1 à 21 ; 0 si l'une est illisible. */
double teinte_contraste(const char *a, const char *b);

/* Les jetons dérivés : un nom, sa valeur en clair, sa valeur en sombre (NULL : le mode sombre garde le thème).
 * Les noms sont ceux de src/atelier/theme/grymoir-jetons.json. */
#define TEINTE_JETONS_MAX 16
typedef struct {
    size_t n;
    char noms[TEINTE_JETONS_MAX][24];
    char clair[TEINTE_JETONS_MAX][8];
    char sombre[TEINTE_JETONS_MAX][8];   /* chaîne vide : pas de valeur sombre */
    int clair_impose;                     /* un fond est donné : les écrans restent clairs, quel que soit le système */
} TeinteTheme;

/* couleur, fond : « #RRGGBB » ou NULL. Rend 1 et remplit *t ; 0 et *erreur (à libérer) si une couleur est
 * illisible ou si un contraste n'atteint pas son seuil. */
int teinte_theme(const char *couleur, const char *fond, TeinteTheme *t, char **erreur);

#endif

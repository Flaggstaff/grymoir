/* GrymoiR : utilitaires de texte UTF-8 partagés (analyseur, outils). */
#ifndef GRYM_TEXTE_H
#define GRYM_TEXTE_H

#include <stddef.h>

typedef struct { char *d; size_t n, cap; } Chaine;

void chaine_ajouter(Chaine *c, const char *s);
char *chaine_rendre(Chaine *c);          /* jamais NULL ; la chaîne appartient à l'appelant */

void *grym_allouer(size_t taille);       /* quitte le programme si la mémoire manque */
char *grym_dupliquer(const char *s);
char *grym_formater(const char *fmt, ...);

/* Les sortes d'échec (grammaire, § 18.2) : la sorte se pose là où le motif s'écrit, jamais d'après ses mots. grym_echec
 * écrit le motif comme grym_formater et retient sa sorte (une constante : « lecture », « rang »…) ; une sorte NULL
 * n'en retient aucune. grym_echec_sorte la retrouve, pour ce motif-là, tel qu'il a été écrit : un motif recopié ou
 * changé n'a plus de sorte (NULL). Le registre ne garde que les derniers motifs : la machine lit la sorte à l'échec. */
char *grym_echec(const char *sorte, const char *fmt, ...);
char *grym_echec_poser(const char *sorte, char *motif);   /* un motif déjà écrit ; le rend tel quel */
const char *grym_echec_sorte(const char *motif);

/* Distance d'édition de Levenshtein, calculée sur les points de code. */
size_t distance_edition(const char *a, const char *b);
size_t longueur_utf8(const char *s);    /* en points de code */

/* Textes pour l'utilisateur (grammaire, § 4.5) : lettres latines accentuées du français et des langues voisines. */
char *texte_plier(const char *s);       /* sans accents ni casse : « Élodie » → « elodie » ; pour comparer */
char *texte_majuscules(const char *s);  /* « élodie » → « ÉLODIE » */
char *texte_minuscules(const char *s);  /* « ÉLODIE » → « élodie » */
/* 'c' contient, 'd' commence par, 'f' finit par, sans tenir compte des accents ni de la casse. */
int texte_contient(const char *texte, const char *partie, char sens);
/* La langue d'un texte, par ses mots outils (2.86) : « fr », « en », « de », « es », « it », « nl », « pt », ou NULL. */
const char *langue_reconnue(const char *texte);

/* Sigles (grammaire, § 2.4) : un mot écrit tout en capitales, deux lettres au moins, et jamais autrement (« numéro RCC »).
 * L'identité ne change pas, le registre retient le mot en minuscules ; l'affichage lui rend ses capitales. */
void sigle_retenir(const char *mot);       /* un mot écrit tout en capitales, donné en minuscules ; le lexeur l'appelle */
void sigle_dementir(const char *mot);      /* le même mot écrit autrement : ce n'est plus un sigle */
char *texte_avec_sigles(const char *nom);  /* « numéro rcc » → « numéro RCC » */
char *texte_sigles(void);                  /* les sigles retenus, en minuscules, séparés par une espace */
void sigles_oublier(void);                 /* l'analyse d'un programme repart de zéro */

#endif

/* GrymoiR : dates du calendrier grégorien, années 1 à 9999
 * Spécification : docs/grammaire.md (révision 1.20), § 14.
 */
#ifndef GRYM_DATE_H
#define GRYM_DATE_H

/* Une date est un nombre de jours depuis le 1er janvier 1970 (négatif avant). */
#define DATE_MIN (-719162L)   /* 01.01.0001 */
#define DATE_MAX (2932896L)   /* 31.12.9999 */

long date_jours(int annee, int mois, int jour);          /* date supposée valide */
void date_civile(long jours, int *annee, int *mois, int *jour);

/* 1 si la date existe ; sinon 0 et *message (à libérer) : « Le 31 février 2026 n'existe pas. » */
int date_verifier(int annee, int mois, int jour, char **message);

/* « 2026-09-21 » → jours ; 0 si le texte n'est pas une date ISO valide. */
int date_lire_iso(const char *texte, long *jours);

char *date_iso(long jours);       /* « 2026-09-21 », à libérer */
char *date_suisse(long jours);    /* « 21.09.2026 », à libérer */

/* Date du jour selon l'horloge locale. */
long date_aujourdhui(void);

#endif

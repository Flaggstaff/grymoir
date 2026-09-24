/* GrymoiR : dates du calendrier grégorien, années 1 à 9999
 * Spécification : docs/grammaire.md (révision 1.37), § 14.
 *
 * Conversions jours <-> date civile : algorithmes « days_from_civil » et « civil_from_days »
 * de Howard Hinnant, « chrono-Compatible Low-Level Date Algorithms »
 * (https://howardhinnant.github.io/date_algorithms.html), calendrier grégorien proleptique.
 */
#include "date.h"
#include "texte.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

long date_jours(int annee, int mois, int jour) {
    long y = (long)annee - (mois <= 2);
    long era = (y >= 0 ? y : y - 399) / 400;
    long yoe = y - era * 400;                                            /* [0, 399] */
    long doy = (153L * (mois + (mois > 2 ? -3 : 9)) + 2) / 5 + jour - 1; /* [0, 365] */
    long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;                    /* [0, 146096] */
    return era * 146097 + doe - 719468;
}

void date_civile(long jours, int *annee, int *mois, int *jour) {
    long z = jours + 719468;
    long era = (z >= 0 ? z : z - 146096) / 146097;
    long doe = z - era * 146097;
    long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    long y = yoe + era * 400;
    long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    long mp = (5 * doy + 2) / 153;
    long d = doy - (153 * mp + 2) / 5 + 1;
    long m = mp < 10 ? mp + 3 : mp - 9;
    *annee = (int)(y + (m <= 2));
    *mois = (int)m;
    *jour = (int)d;
}

static int bissextile(int a) {
    return (a % 4 == 0 && a % 100 != 0) || a % 400 == 0;
}

static int jours_du_mois(int a, int m) {
    static const int J[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    return m == 2 && bissextile(a) ? 29 : J[m - 1];
}

static const char *const MOIS[] = { "janvier", "février", "mars", "avril", "mai", "juin", "juillet",
                                    "août", "septembre", "octobre", "novembre", "décembre" };

int date_verifier(int annee, int mois, int jour, char **message) {
    if (annee < 1 || annee > 9999) {
        *message = grym_formater("Année %d hors du calendrier : de 1 à 9999.", annee);
        return 0;
    }
    if (mois < 1 || mois > 12) {
        *message = grym_formater("Mois %d impossible : de 1 à 12.", mois);
        return 0;
    }
    if (jour < 1 || jour > jours_du_mois(annee, mois)) {
        *message = jour < 1 ? grym_formater("Jour %d impossible.", jour)
                            : grym_formater("Le %d %s %d n'existe pas.", jour, MOIS[mois - 1], annee);
        return 0;
    }
    return 1;
}

int date_lire_iso(const char *t, long *jours) {
    int a, m, j;
    char fin;
    if (strlen(t) != 10 || t[4] != '-' || t[7] != '-') return 0;
    if (sscanf(t, "%4d-%2d-%2d%c", &a, &m, &j, &fin) != 3) return 0;
    char *msg = NULL;
    if (!date_verifier(a, m, j, &msg)) { free(msg); return 0; }
    *jours = date_jours(a, m, j);
    return 1;
}

char *date_iso(long jours) {
    int a, m, j;
    date_civile(jours, &a, &m, &j);
    return grym_formater("%04d-%02d-%02d", a, m, j);
}

char *date_suisse(long jours) {
    int a, m, j;
    date_civile(jours, &a, &m, &j);
    return grym_formater("%02d.%02d.%04d", j, m, a);
}

long date_aujourdhui(void) {
    time_t t = time(NULL);
    struct tm *l = localtime(&t);
    return date_jours(l->tm_year + 1900, l->tm_mon + 1, l->tm_mday);
}

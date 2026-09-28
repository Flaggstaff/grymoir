/* GrymoiR : le guide « Premiers pas » (docs/guide.md) ne ment pas.
 *
 * Chaque chapitre « ## N. … » a son programme complet, docs/guide/chapitre-NN.grym, ou, s'il tient en plusieurs
 * fichiers, docs/guide/chapitre-NN/association.grym et ceux qu'il utilise (§ 21). Pour chacun :
 *   1. le programme s'analyse, se compile et s'exécute (base en mémoire ; un programme qui ouvre des écrans
 *      est seulement compilé, la console n'en montre pas) ;
 *   2. il est en forme canonique (grammaire, § 12), fichiers utilisés compris : le guide montre ce que
 *      « grym formater » écrirait ;
 *   3. chaque bloc ```grymoir du chapitre est un extrait de son programme (ou d'un fichier qu'il utilise) : ses lignes s'y suivent, telles
 *      quelles, au retrait près ;
 *   4. chaque bloc ```sortie du chapitre apparaît dans ce que le programme affiche, dans l'ordre.
 * Un exemple faux dans le guide fait donc échouer les essais. Lancer depuis la racine du dépôt. */
#include "analyseur.h"
#include "compilateur.h"
#include "imprimeur.h"
#include "texte.h"
#include "vm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#endif

static int total = 0, echecs = 0;

static void echec(const char *chapitre, const char *quoi, const char *detail) {
    echecs++;
    printf("ÉCHEC (%s) : %s\n", chapitre, quoi);
    if (detail) printf("%s\n", detail);
}

static char *lire(const char *chemin, size_t *taille) {
    FILE *f = fopen(chemin, "rb");
    if (!f) return NULL;
    Chaine c = { 0 };
    char tampon[4096];
    size_t n;
    while ((n = fread(tampon, 1, sizeof tampon - 1, f)) > 0) {
        tampon[n] = '\0';
        chaine_ajouter(&c, tampon);
    }
    fclose(f);
    char *r = chaine_rendre(&c);
    if (!r) r = grym_dupliquer("");
    if (taille) *taille = strlen(r);
    return r;
}

/* Les lignes d'un texte, sans leur retrait ni les lignes vides. */
static char **lignes(const char *t, size_t *nb) {
    char **r = NULL;
    size_t n = 0;
    const char *p = t;
    while (*p) {
        const char *fin = strchr(p, '\n');
        size_t l = fin ? (size_t)(fin - p) : strlen(p);
        size_t d = 0;
        while (d < l && p[d] == ' ') d++;
        size_t e = l;
        while (e > d && (p[e - 1] == ' ' || p[e - 1] == '\r')) e--;
        if (e > d) {
            char **x = grym_allouer((n + 1) * sizeof *x);
            if (n) memcpy(x, r, n * sizeof *x);
            free(r);
            r = x;
            r[n++] = grym_formater("%.*s", (int)(e - d), p + d);
        }
        p = fin ? fin + 1 : p + l;
    }
    *nb = n;
    return r;
}

static void lignes_liberer(char **l, size_t n) {
    for (size_t k = 0; k < n; k++) free(l[k]);
    free(l);
}

/* Les lignes de l'extrait se suivent-elles dans le programme ? */
static int extrait_de(const char *extrait, const char *programme) {
    size_t ne, np;
    char **e = lignes(extrait, &ne), **p = lignes(programme, &np);
    int trouve = ne == 0;
    for (size_t k = 0; !trouve && k + ne <= np; k++) {
        size_t q = 0;
        while (q < ne && strcmp(e[q], p[k + q]) == 0) q++;
        trouve = q == ne;
    }
    lignes_liberer(e, ne);
    lignes_liberer(p, np);
    return trouve;
}

/* Le texte des fichiers qu'un programme utilise (« Utiliser « x ». » en tête, § 21), mis bout à bout, pour les
 * extraits ; chacun doit aussi être en forme canonique. Rend un message d'échec ou NULL. */
static char *fichiers_utilises(const char *dossier, const char *texte, Chaine *reunis) {
    const char *p = texte;
    while ((p = strstr(p, "Utiliser « ")) != NULL) {
        p += strlen("Utiliser « ");
        const char *f = strstr(p, " »");
        if (!f) break;
        char *chemin = grym_formater("%s%.*s.grym", dossier, (int)(f - p), p);
        size_t t;
        char *src = lire(chemin, &t);
        if (!src) { char *m = grym_formater("%s introuvable", chemin); free(chemin); return m; }
        Portee *portee = portee_creer();
        portee_fichier(portee, chemin);
        Programme pr;
        Diagnostic d;
        char *probleme = NULL;
        if (!analyser(src, t, portee, 0, &pr, &d)) {
            probleme = grym_formater("%s:%d:%d : %s", chemin, d.ligne, d.colonne, d.message);
            diagnostic_liberer(&d);
        } else {
            char *canon = imprimer_litteraire(&pr);
            if (strcmp(canon, src) != 0)
                probleme = grym_formater("%s n'est pas en forme canonique : lancez « grym formater » dessus.", chemin);
            free(canon);
            programme_liberer(&pr);
        }
        portee_detruire(portee);
        chaine_ajouter(reunis, "\n");
        chaine_ajouter(reunis, src);
        if (!probleme) probleme = fichiers_utilises(dossier, src, reunis);
        free(src);
        free(chemin);
        if (probleme) return probleme;
        p = f;
    }
    return NULL;
}

/* Analyse, formate, compile et exécute ; *sortie reçoit ce qui s'affiche. Rend un message d'échec ou NULL. */
static char *executer(const char *chemin, const char *source, size_t taille, char **sortie) {
    *sortie = grym_dupliquer("");
    Portee *portee = portee_creer();
    portee_fichier(portee, chemin);
    Programme p;
    Diagnostic d;
    if (!analyser(source, taille, portee, 0, &p, &d)) {
        char *m = grym_formater("%s:%d:%d : %s", chemin, d.ligne, d.colonne, d.message);
        diagnostic_liberer(&d);
        portee_detruire(portee);
        return m;
    }
    char *canon = imprimer_litteraire(&p);
    char *probleme = NULL;
    if (strcmp(canon, source) != 0)
        probleme = grym_formater("%s n'est pas en forme canonique : lancez « grym formater » dessus.", chemin);
    free(canon);
    Module *b = probleme ? NULL : compiler(&p, &d);
    if (!probleme && !b) {
        probleme = grym_formater("%s : compilation : %s", chemin, d.message);
        diagnostic_liberer(&d);
    }
    if (b) {
        Machine *m = machine_creer();
        char *dossier = grym_dupliquer(chemin);
        char *barre = strrchr(dossier, '/');
        if (barre) barre[1] = '\0';
        machine_dossier(m, dossier);
        Chaine s = { 0 };
        if (!machine_executer(m, b, &s, &d)) {
            /* Un programme à écrans ne s'exécute pas en console : compilé, il a fait sa part. */
            if (!strstr(d.message, "écrans"))
                probleme = grym_formater("%s:%d:%d : %s", chemin, d.ligne, d.colonne, d.message);
            diagnostic_liberer(&d);
        }
        free(*sortie);
        *sortie = chaine_rendre(&s);
        if (!*sortie) *sortie = grym_dupliquer("");
        machine_detruire(m);
        free(dossier);
        module_detruire(b);
    }
    programme_liberer(&p);
    portee_detruire(portee);
    return probleme;
}

int main(void) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    size_t taille;
    char *guide = lire("docs/guide.md", &taille);
    if (!guide) {
        printf("ÉCHEC : docs/guide.md introuvable (lancez depuis la racine du dépôt).\n");
        return 1;
    }
    /* Le guide, chapitre par chapitre : « ## N. » ouvre le chapitre N. */
    int chapitre = 0;
    char *programme = NULL, *sortie = NULL, nom[64] = "";
    const char *reste_sortie = NULL;   /* les blocs ```sortie se cherchent dans l'ordre */
    const char *p = guide;
    while (*p) {
        const char *fin = strchr(p, '\n');
        size_t l = fin ? (size_t)(fin - p) : strlen(p);
        int n;
        if (l > 3 && strncmp(p, "## ", 3) == 0 && sscanf(p + 3, "%d.", &n) == 1) {
            chapitre = n;
            free(programme);
            free(sortie);
            programme = sortie = NULL;
            snprintf(nom, sizeof nom, "docs/guide/chapitre-%02d.grym", n);
            size_t t;
            programme = lire(nom, &t);
            if (!programme) {   /* un chapitre en plusieurs fichiers : son dossier */
                snprintf(nom, sizeof nom, "docs/guide/chapitre-%02d/association.grym", n);
                programme = lire(nom, &t);
            }
            total++;
            if (!programme) {
                echec(nom, "programme du chapitre introuvable", NULL);
            } else {
                char *probleme = executer(nom, programme, t, &sortie);
                if (probleme) { echec(nom, "le programme ne passe pas", probleme); free(probleme); }
                char dossier[64];
                snprintf(dossier, sizeof dossier, "%s", nom);
                char *barre = strrchr(dossier, '/');
                if (barre) barre[1] = '\0';
                Chaine reunis = { 0 };
                chaine_ajouter(&reunis, programme);
                probleme = fichiers_utilises(dossier, programme, &reunis);
                if (probleme) { echec(nom, "un fichier utilisé ne passe pas", probleme); free(probleme); }
                free(programme);
                programme = chaine_rendre(&reunis);
            }
            reste_sortie = sortie;
        } else if (chapitre && (strncmp(p, "```grymoir", 10) == 0 || strncmp(p, "```sortie", 9) == 0)) {
            const int code = p[3] == 'g';
            const char *debut = fin ? fin + 1 : p + l;
            const char *f = strstr(debut, "\n```");
            if (!f) { echec(nom, "bloc de code non refermé", NULL); break; }
            char *bloc = grym_formater("%.*s", (int)(f - debut + 1), debut);
            total++;
            if (code && programme && !extrait_de(bloc, programme)) {
                echec(nom, "cet extrait du guide n'est pas dans le programme du chapitre :", bloc);
            } else if (!code && reste_sortie) {
                const char *t = strstr(reste_sortie, bloc);
                if (!t) echec(nom, "cette sortie du guide n'est pas ce que le programme affiche :", bloc);
                else reste_sortie = t + strlen(bloc);
            }
            free(bloc);
            fin = strchr(f + 1, '\n');
            if (!fin) break;
        }
        p = fin ? fin + 1 : p + l;
    }
    free(programme);
    free(sortie);
    free(guide);
    printf("%d/%d tests réussis\n", total - echecs, total);
    return echecs ? 1 : 0;
}

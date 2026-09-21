/* Tests de l'arithmétique décimale et de l'exécution, GrymoiR v0.1. */
#include "analyseur.h"
#include "evaluateur.h"
#include "texte.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#endif

static int total = 0, echecs = 0;

/* Exécute une source ; renvoie la sortie sans le dernier saut de ligne,
 * ou « ERREUR l:c message ». */
static char *executer_source(Portee *portee, Environnement *env, const char *src, int interactif) {
    Programme p;
    Diagnostic d;
    if (!analyser(src, strlen(src), portee, interactif, &p, &d)) {
        char *m = grym_formater("ERREUR %d:%d %s", d.ligne, d.colonne, d.message);
        diagnostic_liberer(&d);
        return m;
    }
    Chaine s = {0};
    int ok = executer(&p, env, &s, &d);
    programme_liberer(&p);
    char *r = chaine_rendre(&s);
    if (!ok) {
        free(r);
        char *m = grym_formater("ERREUR %d:%d %s", d.ligne, d.colonne, d.message);
        diagnostic_liberer(&d);
        return m;
    }
    size_t l = strlen(r);
    if (l && r[l - 1] == '\n') r[l - 1] = '\0';
    return r;
}

static void signaler(int l, const char *src, const char *attendu, const char *obtenu) {
    echecs++;
    printf("ÉCHEC (test ligne %d)\n  source  : %s\n  attendu : %s\n  obtenu  : %s\n",
           l, src, attendu, obtenu);
}

/* Programme complet (mode fichier). Un attendu qui commence par « ~ » est un fragment. */
static void verifier(int l, const char *src, const char *attendu, int interactif) {
    total++;
    Portee *p = portee_creer();
    Environnement *e = env_creer();
    char *r = executer_source(p, e, src, interactif);
    int ok = attendu[0] == '~' ? strstr(r, attendu + 1) != NULL : strcmp(r, attendu) == 0;
    if (!ok) signaler(l, src, attendu, r);
    free(r);
    env_detruire(e);
    portee_detruire(p);
}

#define CALC(expr, att)  verifier(__LINE__, expr, att, 1)
#define PROG(src, att)   verifier(__LINE__, src, att, 0)

int main(void) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    /* --- Décimal exact (§ 3.2) --- */
    CALC("1,1 + 2,2", "3,3");
    CALC("0,1 + 0,2", "0,3");
    CALC("12,50 × 3", "37,50");
    CALC("100 − 100,00", "0,00");
    CALC("0,5 × 0,5", "0,25");
    CALC("1 − 3", "−2");
    CALC("−0", "0");
    CALC("1'234'567,891", "1'234'567,891");
    CALC("−1'000", "−1'000");
    CALC("999 + 1", "1'000");
    CALC("123456", "123'456");
    CALC("0,001", "0,001");
    CALC("99999999999999999999 + 1", "100'000'000'000'000'000'000");

    /* --- Division --- */
    CALC("1 ÷ 3", "0,3333333333333333333333333333");
    CALC("2 ÷ 3", "0,6666666666666666666666666667");
    CALC("10 ÷ 3", "3,333333333333333333333333333");
    CALC("1 ÷ 7 × 7", "1,0000000000000000000000000003");   /* ÷ arrondit, × reste exact */
    CALC("1 ÷ 8", "0,125");
    CALC("10 ÷ 4", "2,5");
    CALC("6 ÷ 3", "2");
    CALC("1,0 ÷ 2", "0,5");
    CALC("12,50 ÷ 5", "2,50");
    CALC("1 ÷ 1024", "0,0009765625");
    CALC("−7 ÷ 2", "−3,5");
    CALC("100 ÷ 0,5", "200");
    CALC("0 ÷ 5", "0");
    CALC("1 ÷ 0", "ERREUR 1:3 Division par zéro.");
    CALC("1 ÷ (2 − 2)", "ERREUR 1:3 Division par zéro.");

    /* --- Puissance (§ 3.1) --- */
    CALC("2 ^ 10", "1'024");
    CALC("2 ^ −1", "0,5");
    CALC("2 ^ −2", "0,25");
    CALC("3 ^ −1", "0,3333333333333333333333333333");
    CALC("−2 ^ 2", "−4");
    CALC("(−2) ^ 2", "4");
    CALC("(−2) ^ 3", "−8");
    CALC("2 ^ 3 ^ 2", "512");
    CALC("2 ^ 2,0", "4");
    CALC("1,5 ^ 2", "2,25");
    CALC("0 ^ 0", "1");
    CALC("1 ^ 1000000000000", "1");
    CALC("(−1) ^ 1000000000001", "−1");
    CALC("0 ^ −1", "ERREUR 1:3 Division par zéro.");
    CALC("2 ^ 0,5", "~Exposant non entier");
    CALC("10 ^ 2000", "~Nombre trop grand");
    CALC("2 ^ 1000000000000", "~Nombre trop grand");

    /* --- Programmes --- */
    PROG("Remarque : premier programme GrymoiR\n"
         "Le prix unitaire vaut 12,50.\n"
         "La quantité vaut 3.\n"
         "Le total vaut prix unitaire × quantité.\n"
         "Le total devient total + 1'000.\n"
         "Afficher « Total à payer : » puis le total.",
         "Total à payer : 1'037,50");
    PROG("Le x vaut 1.\nLe x devient x + 1.\nLe x devient x × 10.\nAfficher x.", "20");
    PROG("Afficher « a ».\nAfficher 1 puis 2 puis « b ».", "a\n1 2 b");
    PROG("Afficher “Bonjour”.", "Bonjour");
    PROG("Le taux vaut 7,7.\nLe montant vaut 250.\nAfficher le montant × le taux ÷ 100.",
         "19,25");
    PROG("Le x vaut 5.\nAfficher x.\nLe y vaut x ÷ 0.\nAfficher y.", "ERREUR 3:13 Division par zéro.");

    /* --- Boucle interactive : une saisie ratée n'a aucun effet (§ 3.3) --- */
    {
        total++;
        Portee *p = portee_creer();
        Environnement *e = env_creer();
        const char *saisies[] = { "Le a vaut 1.", "Le a devient 5. Le b vaut 1 ÷ 0.", "a", "b" };
        const char *attendus[] = { "", "~Division par zéro", "1", "~« b » inconnu" };
        /* Comme la boucle de grym : sauvegarde, puis restauration en cas d'échec. */
        for (int i = 0; i < 4; i++) {
            Portee *sp = portee_cloner(p);
            Environnement *se = env_cloner(e);
            char *r = executer_source(p, e, saisies[i], 1);
            int echec = strncmp(r, "ERREUR", 6) == 0;
            int ok = attendus[i][0] == '~' ? strstr(r, attendus[i] + 1) != NULL
                                            : strcmp(r, attendus[i]) == 0;
            if (echec) {
                portee_detruire(p); env_detruire(e);
                p = sp; e = se;
            } else {
                portee_detruire(sp); env_detruire(se);
            }
            if (!ok) { signaler(__LINE__, saisies[i], attendus[i], r); free(r); break; }
            free(r);
        }
        /* La saisie 2 échoue sur b : « a devient 5 » est annulé aussi, a vaut toujours 1. */
        env_detruire(e);
        portee_detruire(p);
    }

    printf("%d/%d tests réussis\n", total - echecs, total);
    return echecs ? EXIT_FAILURE : EXIT_SUCCESS;
}

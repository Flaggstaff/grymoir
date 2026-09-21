/* Tests de l'arithmétique décimale, du bytecode et de la machine virtuelle, GrymoiR v0.2. */
#include "analyseur.h"
#include "bytecode.h"
#include "compilateur.h"
#include "vm.h"
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
static char *executer_source(Portee *portee, Machine *m, const char *src, int interactif) {
    Programme p;
    Diagnostic d;
    if (!analyser(src, strlen(src), portee, interactif, &p, &d)) {
        char *msg = grym_formater("ERREUR %d:%d %s", d.ligne, d.colonne, d.message);
        diagnostic_liberer(&d);
        return msg;
    }
    Chaine s = {0};
    Bloc *b = compiler(&p, &d);
    int ok = b && machine_executer(m, b, &s, &d);
    bloc_detruire(b);
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
    Machine *m = machine_creer();
    char *r = executer_source(p, m, src, interactif);
    int ok = attendu[0] == '~' ? strstr(r, attendu + 1) != NULL : strcmp(r, attendu) == 0;
    if (!ok) signaler(l, src, attendu, r);
    free(r);
    machine_detruire(m);
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

    /* --- Conditions (grammaire, § 5) --- */
    PROG("Le x vaut 5.\nSi x > 3, afficher « grand ». Sinon, afficher « petit ».", "grand");
    PROG("Le x vaut 2.\nSi x > 3, afficher « grand ». Sinon, afficher « petit ».", "petit");
    PROG("Afficher 1,0 = 1 puis 2 > 3 puis 0 est nul puis −1 est négatif puis 0 est positif.",
         "vrai faux vrai vrai faux");
    PROG("Afficher 3 ≤ 3 puis 3 < 3 puis 3 ≥ 4 puis 3 ≠ 3,00 puis 2 n'est pas inférieur à 1.",
         "vrai faux faux faux vrai");
    PROG("Afficher vrai et faux puis vrai ou faux puis (faux ou faux) et vrai.",
         "faux vrai faux");
    PROG("Le t vaut vrai.\nLe u vaut vrai.\nAfficher t = u puis t ≠ u.", "vrai faux");
    PROG("Le total vaut 120.\nLe rabais vaut 0.\n"
         "Si le total est supérieur à 100 :\n    Le rabais devient 10.\n"
         "Sinon si le total est supérieur ou égal à 50 :\n    Le rabais devient 5.\n"
         "Afficher le rabais.", "10");
    PROG("Le total vaut 70.\nLe rabais vaut 0.\n"
         "Si le total est supérieur à 100 :\n    Le rabais devient 10.\n"
         "Sinon si le total est supérieur ou égal à 50 :\n    Le rabais devient 5.\n"
         "Afficher le rabais.", "5");
    PROG("Le total vaut 10.\nLe rabais vaut 0.\n"
         "Si le total est supérieur à 100 :\n    Le rabais devient 10.\n"
         "Sinon si le total est supérieur ou égal à 50 :\n    Le rabais devient 5.\n"
         "Afficher le rabais.", "0");
    PROG("Le x vaut 5.\nSi x > 0 :\n  Si x > 3 :\n    Afficher 1.\n  Sinon :\n    Afficher 2.\n  Afficher 3.\n"
         "Afficher 4.", "1\n3\n4");
    /* court-circuit : le second membre n'est pas calculé */
    PROG("Le x vaut 0.\nSi x ≠ 0 et 1 ÷ x > 1, afficher 1. Sinon, afficher 2.", "2");
    PROG("Le x vaut 0.\nSi x = 0 ou 1 ÷ x > 1, afficher 3.", "3");
    PROG("Le t vaut 2 > 1.\nSi le t, afficher « oui ».\nAfficher le t est faux.", "oui\nfaux");
    PROG("Le [frais et port] vaut 7,50.\nAfficher [frais et port] × 2.", "15,00");
    /* erreurs d'exécution */
    PROG("Le x vaut 3.\nSi x, afficher 1.", "ERREUR 2:4 Condition ni vraie ni fausse : la valeur est un nombre.");
    PROG("Le t vaut vrai.\nAfficher t + 1.", "~ADDITION impossible : un des opérandes est un booléen.");
    PROG("Le t vaut vrai.\nLe u vaut faux.\nAfficher t < u.", "~Seuls deux nombres se comparent par ordre");
    PROG("Le t vaut vrai.\nAfficher 1 = t.", "~Comparaison impossible entre un nombre et un booléen.");
    PROG("Le t vaut 3.\nAfficher t et vrai.", "~Condition ni vraie ni fausse");

    /* --- Boucle interactive : une saisie ratée n'a aucun effet (§ 3.3, docs/vm.md § 6) --- */
    {
        total++;
        Portee *p = portee_creer();
        Machine *m = machine_creer();
        const char *saisies[] = { "Le a vaut 1.", "Le a devient 5. Le b vaut 1 ÷ 0.", "a", "b",
                                  "Le a devient a + 1. Le a devient a × 10. Le a devient a ÷ 0.", "a",
                                  "Si vrai :\n    Le a devient 7.\n    Le a devient a ÷ 0.", "a" };
        const char *attendus[] = { "", "~Division par zéro", "1", "~« b » inconnu",
                                   "~Division par zéro", "1", "~Division par zéro", "1" };
        /* Comme la boucle de grym : la machine annule ses écritures, la portée est restaurée. */
        for (int i = 0; i < 8; i++) {
            Portee *sp = portee_cloner(p);
            char *r = executer_source(p, m, saisies[i], 1);
            int echec = strncmp(r, "ERREUR", 6) == 0;
            int ok = attendus[i][0] == '~' ? strstr(r, attendus[i] + 1) != NULL
                                            : strcmp(r, attendus[i]) == 0;
            if (echec) { portee_detruire(p); p = sp; } else portee_detruire(sp);
            if (!ok) { signaler(__LINE__, saisies[i], attendus[i], r); free(r); break; }
            free(r);
        }
        machine_detruire(m);
        portee_detruire(p);
    }

    /* --- Désassemblage (docs/vm.md, § 9) --- */
    {
        total++;
        const char *src =
            "Le prix unitaire vaut 12,50.\n"
            "La quantité vaut 3.\n"
            "Le total vaut prix unitaire × quantité.\n"
            "Afficher « Total : » puis −total.";
        const char *attendu =
            "   1  0000  CONSTANTE         0     ; 12,50\n"
            "      0003  ÉCRIRE            0     ; prix unitaire\n"
            "   2  0006  CONSTANTE         1     ; 3\n"
            "      0009  ÉCRIRE            1     ; quantité\n"
            "   3  0012  LIRE              0     ; prix unitaire\n"
            "      0015  LIRE              1     ; quantité\n"
            "      0018  MULTIPLICATION\n"
            "      0019  ÉCRIRE            2     ; total\n"
            "   4  0022  CONSTANTE         2     ; « Total : »\n"
            "      0025  LIRE              2     ; total\n"
            "      0028  NÉGATION\n"
            "      0029  AFFICHER          2\n"
            "      0032  RETOUR\n";
        Portee *p = portee_creer();
        Programme prog;
        Diagnostic d;
        char *r = NULL;
        if (analyser(src, strlen(src), p, 0, &prog, &d)) {
            Bloc *b = compiler(&prog, &d);
            r = bloc_desassembler(b);
            bloc_detruire(b);
            programme_liberer(&prog);
        } else {
            r = grym_dupliquer(d.message);
            diagnostic_liberer(&d);
        }
        if (strcmp(r, attendu) != 0) signaler(__LINE__, src, attendu, r);
        free(r);
        portee_detruire(p);
    }

    /* --- Fichier .grymb : aller-retour et fichiers corrompus (docs/vm.md, § 4 et 8) --- */
    {
        const char *src = "Le x vaut 2 ^ 10.\nAfficher « x = » puis x ÷ 4.";
        Portee *p = portee_creer();
        Programme prog;
        Diagnostic d;
        analyser(src, strlen(src), p, 0, &prog, &d);
        Bloc *b = compiler(&prog, &d);
        programme_liberer(&prog);
        portee_detruire(p);
        size_t taille;
        unsigned char *octets = bloc_serialiser(b, &taille);

        /* aller-retour : même désassemblage, même résultat */
        total++;
        char *err = NULL;
        Bloc *relu = bloc_lire(octets, taille, &err);
        char *d1 = bloc_desassembler(b), *d2 = relu ? bloc_desassembler(relu) : grym_dupliquer(err);
        if (strcmp(d1, d2) != 0) signaler(__LINE__, "aller-retour .grymb", d1, d2);
        free(d1); free(d2); free(err);
        total++;
        if (relu) {
            Machine *m = machine_creer();
            Chaine s = {0};
            machine_executer(m, relu, &s, &d);
            char *r = chaine_rendre(&s);
            if (strcmp(r, "x = 256\n") != 0) signaler(__LINE__, "exécution du .grymb relu", "x = 256", r);
            free(r);
            machine_detruire(m);
            bloc_detruire(relu);
        } else {
            signaler(__LINE__, "exécution du .grymb relu", "x = 256", "fichier illisible");
        }

        /* corruptions : chaque cas doit être refusé avec un message, sans planter */
        struct { const char *nom; size_t pos; int octet; size_t coupe; const char *fragment; } cas[] = {
            { "en-tête",           0, 'X', 0, "en-tête" },
            { "version",           4, 9,   0, "version" },
            { "fichier tronqué",   0, -1,  7, "tronqué" },
            { "octet en trop",     0, -1,  (size_t)-1, "en trop" },
        };
        for (size_t k = 0; k < sizeof cas / sizeof *cas; k++) {
            total++;
            size_t t = cas[k].coupe == (size_t)-1 ? taille + 1 : cas[k].coupe ? cas[k].coupe : taille;
            unsigned char *copie = grym_allouer(t);
            memcpy(copie, octets, t < taille ? t : taille);
            if (t > taille) copie[taille] = 0;
            if (cas[k].octet >= 0) copie[cas[k].pos] = (unsigned char)cas[k].octet;
            char *e = NULL;
            Bloc *x = bloc_lire(copie, t, &e);
            if (x || !e || !strstr(e, cas[k].fragment))
                signaler(__LINE__, cas[k].nom, cas[k].fragment, e ? e : "(accepté)");
            bloc_detruire(x);
            free(e);
            free(copie);
        }
        free(octets);

        /* code invalide : vérification avant exécution */
        struct { const char *nom; uint8_t code[16]; size_t n; const char *fragment; } codes[] = {
            { "code inconnu",       { 99 }, 1, "inconnu" },
            { "pile vide",          { I_ADDITION, I_RETOUR }, 2, "pile insuffisante" },
            { "constante absente",  { I_CONSTANTE, 7, 0, I_RETOUR }, 4, "inexistante" },
            { "sans RETOUR",        { I_CONSTANTE, 0, 0, I_AFFICHER, 1, 0 }, 6, "RETOUR" },
            { "pile non vide",      { I_CONSTANTE, 0, 0, I_RETOUR }, 4, "non vide" },
            { "saut hors code",     { I_SAUTER, 9, 0, 0, 0, I_RETOUR }, 6, "ne commence pas une instruction" },
            { "saut au milieu",     { I_SAUTER, 1, 0, 0, 0, I_RETOUR }, 6, "ne commence pas une instruction" },
            { "sortie sans RETOUR", { I_CONSTANTE, 0, 0, I_SAUTER_SI_FAUX, 11, 0, 0, 0 }, 8, "RETOUR" },
            { "pile incohérente",   { I_CONSTANTE, 0, 0, I_SAUTER_SI_FAUX, 11, 0, 0, 0,
                                      I_CONSTANTE, 0, 0, I_AFFICHER, 1, 0, I_RETOUR }, 15, "Bytecode invalide" },
            { "opérande tronqué",   { I_CONSTANTE, 0 }, 2, "tronqué" },
        };
        for (size_t k = 0; k < sizeof codes / sizeof *codes; k++) {
            total++;
            Bloc *x = bloc_creer();
            bloc_constante(x, C_NOMBRE, "1");
            x->code = grym_allouer(codes[k].n);
            memcpy(x->code, codes[k].code, codes[k].n);
            x->taille_code = x->cap_code = codes[k].n;
            Machine *m = machine_creer();
            Chaine s = {0};
            Diagnostic dd;
            int ok = machine_executer(m, x, &s, &dd);
            if (ok || !dd.message || !strstr(dd.message, codes[k].fragment))
                signaler(__LINE__, codes[k].nom, codes[k].fragment, dd.message ? dd.message : "(accepté)");
            if (!ok) diagnostic_liberer(&dd);
            free(s.d);
            machine_detruire(m);
            bloc_detruire(x);
        }
        bloc_detruire(b);
    }

    printf("%d/%d tests réussis\n", total - echecs, total);
    return echecs ? EXIT_FAILURE : EXIT_SUCCESS;
}

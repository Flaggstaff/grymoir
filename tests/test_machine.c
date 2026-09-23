/* Tests de l'arithmétique décimale, du bytecode et de la machine virtuelle, GrymoiR v0.2. */
#include "analyseur.h"
#include "bytecode.h"
#include "compilateur.h"
#include "vm.h"
#include "date.h"
#include "sqlite3.h"
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
    Module *b = compiler(&p, &d);
    int ok = b && machine_executer(m, b, &s, &d);
    module_detruire(b);
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

static void signaler(int l, const char *src, const char *attendu, const char *obtenu);

/* Réponses scriptées (grammaire, § 17) : la question et ce qui attend vont dans la sortie,
 * comme à l'écran, puis la ligne suivante du script est rendue. */
typedef struct { const char *const *lignes; size_t n, i; } Script;

static char *reponses(void *contexte, Chaine *sortie, const char *question) {
    Script *sc = contexte;
    chaine_ajouter(sortie, question);
    chaine_ajouter(sortie, " ");
    if (sc->i == sc->n) return NULL;
    return grym_dupliquer(sc->lignes[sc->i++]);
}

static void verifier_saisie(int l, const char *src, const char *const *lignes, size_t n, const char *attendu) {
    total++;
    Portee *p = portee_creer();
    Machine *m = machine_creer();
    Script sc = { lignes, n, 0 };
    machine_lecteur(m, reponses, &sc);
    char *r = executer_source(p, m, src, 0);
    int ok = attendu[0] == '~' ? strstr(r, attendu + 1) != NULL : strcmp(r, attendu) == 0;
    if (!ok) signaler(l, src, attendu, r);
    free(r);
    machine_detruire(m);
    portee_detruire(p);
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

/* Fichier d'essai, créé dans le dossier courant et retiré à la fin. */
static void creer_fichier(const char *nom, const void *octets, size_t n) {
    FILE *f = fopen(nom, "wb");
    if (f) { fwrite(octets, 1, n, f); fclose(f); }
}

static int fichier_existe(const char *nom) {
    FILE *f = fopen(nom, "rb");
    if (f) fclose(f);
    return f != NULL;
}

#define APT_M "Une chose horodatée a : une date.\nUne chose numérotée a : un numéro.\nUne personne a : un nom.\n"

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
    PROG("Le t vaut vrai.\nLe u vaut faux.\nAfficher t < u.", "~Seuls deux nombres ou deux dates se comparent par ordre");
    PROG("Le t vaut vrai.\nAfficher 1 = t.", "~Comparaison impossible entre un nombre et un booléen.");
    PROG("Le t vaut 3.\nAfficher t et vrai.", "~Condition ni vraie ni fausse");

    /* --- Formules (grammaire, § 9 ; docs/vm.md, § 3) --- */
    PROG("Le carré d'un nombre vaut nombre × nombre.\nAfficher le carré de 7 puis le carré de −1,5.", "49 2,25");
    PROG("La moyenne d'un premier nombre et d'un second nombre vaut (premier nombre + second nombre) ÷ 2.\n"
         "Afficher la moyenne de 4 et de 6 puis la moyenne de 1 et de 2.", "5 1,5");
    PROG("La valeur absolue d'un nombre :\n    Si nombre est négatif, rendre −nombre.\n    Rendre nombre.\n"
         "Afficher la valeur absolue de −5 puis la valeur absolue de 3.", "5 3");
    PROG("La factorielle d'un nombre :\n    Si nombre ≤ 1, rendre 1.\n    Rendre nombre × la factorielle de (nombre − 1).\n"
         "Afficher la factorielle de 20.", "2'432'902'008'176'640'000");
    PROG("Le carré d'un nombre vaut nombre × nombre.\nLa somme des carrés d'un a et d'un b vaut carré de a + carré de b.\n"
         "Afficher la somme des carrés de 3 et de 4.", "25");
    PROG("Le double d'un nombre :\n    Le résultat vaut nombre × 2.\n    Le résultat devient résultat + 0.\n"
         "    Rendre résultat.\nAfficher le double de 21.", "42");
    PROG("Le total vaut 0.\nPour ajouter un montant :\n    Le total devient total + montant.\n"
         "Ajouter 5.\nAjouter 2,50.\nAfficher le total.", "7,50");
    PROG("Pour saluer :\n    Afficher « Bonjour ».\nSaluer.\nSaluer.", "Bonjour\nBonjour");
    PROG("Pour payer un montant et une remise :\n    Afficher montant − remise.\nPayer 10 et 2.", "8");
    PROG("Le compteur vaut 0.\nPour compter un nombre :\n    Si nombre > 0 :\n        Le compteur devient compteur + 1.\n"
         "        Compter nombre − 1.\nCompter 50.\nAfficher le compteur.", "50");
    PROG("La boucle d'un nombre vaut la boucle de nombre + 1.\nAfficher la boucle de 1.",
         "~Trop d'appels imbriqués : plus de 1000.");
    PROG("L'inverse d'un nombre vaut 1 ÷ nombre.\nAfficher 1.\nAfficher l'inverse de 0.", "ERREUR 1:30 Division par zéro.");
    /* une action qui échoue n'écrit rien (charte, art. 7) */
    PROG("Le total vaut 1.\nPour casser :\n    Le total devient 99.\n    Le total devient total ÷ 0.\nCasser.",
         "ERREUR 4:28 Division par zéro.");

    /* --- Boucles et Selon (grammaire, § 10) --- */
    PROG("Le x vaut 3.\nTant que x > 0 :\n    Afficher x.\n    Le x devient x − 1.", "3\n2\n1");
    PROG("Répéter 3 fois, afficher « Bip ».", "Bip\nBip\nBip");
    PROG("Répéter 0 fois, afficher « jamais ».\nAfficher « fin ».", "fin");
    PROG("Répéter 2,00 fois, afficher 1.", "1\n1");
    PROG("Répéter 2,5 fois, afficher 1.",
         "ERREUR 1:9 Nombre de tours invalide : un entier positif ou nul est attendu, pas 2,5.");
    PROG("Répéter −1 fois, afficher 1.", "~pas −1");
    PROG("Pour chaque mois de 1 à 4, afficher mois.", "1\n2\n3\n4");
    PROG("Pour chaque n de 3 à 1, afficher n.", "3\n2\n1");
    PROG("Pour chaque n de 1 à 1, afficher n.", "1");
    PROG("Pour chaque n de 1 à 10 par pas de 4, afficher n.", "1\n5\n9");
    PROG("Pour chaque n de 10 à 1 par pas de −4, afficher n.", "10\n6\n2");
    PROG("Pour chaque n de 1 à 3 par pas de −1, afficher n.\nAfficher « rien ».", "rien");
    PROG("Le total vaut 0.\nPour chaque t de 0 à 1 par pas de 0,1, le total devient total + 1.\nAfficher le total.", "11");
    PROG("Pour chaque t de 0 à 0,5 par pas de 0,25, afficher t.", "0\n0,25\n0,50");
    PROG("Pour chaque n de 1 à 3 par pas de 0, afficher n.", "ERREUR 1:1 Pas nul : la boucle ne finirait jamais.");
    PROG("Pour chaque i de 1 à 5 :\n    Si i = 2, passer au tour suivant.\n    Si i = 4, sortir de la boucle.\n    Afficher i.",
         "1\n3");
    PROG("Le x vaut 0.\nTant que vrai :\n    Le x devient x + 1.\n    Si x = 3, sortir de la boucle.\nAfficher x.", "3");
    PROG("Le x vaut 0.\nTant que x < 5 :\n    Le x devient x + 1.\n    Si x < 3, passer au tour suivant.\n    Afficher x.",
         "3\n4\n5");
    PROG("Le n vaut 3.\nRépéter n fois :\n    Le n devient n + 1.\n    Si n > 10, sortir de la boucle.\nAfficher n.", "6");
    /* boucles imbriquées : Sortir ne quitte que la plus proche */
    PROG("Pour chaque i de 1 à 3 :\n    Pour chaque j de 1 à 3 :\n        Si j > i, sortir de la boucle.\n"
         "        Afficher i × 10 + j.", "11\n21\n22\n31\n32\n33");
    PROG("La somme d'un n :\n    Le total vaut 0.\n    Pour chaque i de 1 à n, le total devient total + i.\n"
         "    Rendre total.\nAfficher la somme de 100.", "5'050");
    PROG("Le x vaut 7.\nSelon x :\n    Cas 1 ou 2 :\n        Afficher « un ou deux ».\n    Cas de 3 à 9 :\n"
         "        Afficher « chiffre ».\n    Autrement :\n        Afficher « autre ».", "chiffre");
    PROG("Le x vaut 10.\nSelon x :\n    Cas négatif, afficher « négatif ».\n    Cas supérieur ou égal à 10, afficher « grand ».\n"
         "    Cas 10, afficher « jamais atteint ».", "grand");
    PROG("Le x vaut 2.\nSelon x :\n    Cas 1, afficher 1.\nAfficher « fin ».", "fin");
    PROG("Le t vaut vrai.\nSelon le t :\n    Cas vrai, afficher « oui ».\n    Cas faux, afficher « non ».", "oui");
    PROG("Le carré d'un n vaut n × n.\nSelon le carré de 3 :\n    Cas 9, afficher « neuf ».", "neuf");
    PROG("Le x vaut 1.\nSelon x :\n    Cas vrai, afficher 1.", "~Comparaison impossible entre un nombre et un booléen.");
    /* interruption (Ctrl+C) : l'exécution s'arrête au premier saut arrière */
    {
        grym_interruption = 1;
        PROG("Le x vaut 0.\nTant que vrai, le x devient x + 1.", "~Interrompu (Ctrl+C).");
        grym_interruption = 0;
    }

    /* --- Objets (grammaire, § 13) --- */
    PROG("Un client a :\n    un nom,\n    un solde.\nLe c vaut un nouveau client :\n    Le nom vaut « Dupont ».\n"
         "    Le solde vaut 100.\nLe solde du c devient solde du c − 30.\nAfficher le nom du c puis le solde du c puis c.",
         "Dupont 70 un client");
    PROG("Un client a : un solde.\nPour créditer un client et un montant :\n    Le solde du client devient solde du client + montant.\n"
         "Le solde initial d'un client vaut solde du client × 2.\nLe c vaut un nouveau client :\n    Le solde vaut 5.\n"
         "Créditer c et 10.\nAfficher solde du c puis solde initial de c.", "15 30");
    PROG("Un client a : un nom.\nLe a vaut un nouveau client.\nLe b vaut a.\nLe c vaut un nouveau client.\n"
         "Afficher a = b puis a = c puis a ≠ c.", "vrai faux vrai");
    PROG("Un client a : un nom.\nLe b vaut un nouveau client.\nLe nom du b devient « x ».\nLe a vaut b.\n"
         "Le nom du a devient « y ».\nAfficher nom du b.", "y");
    PROG("Un client a : un solde.\nLe c vaut un nouveau client.\nAfficher le solde du c.",
         "ERREUR 3:10 Le champ « solde » n'a pas de valeur.");
    PROG("Un client a : un nom.\nUne facture a : un montant.\nLe c vaut un nouveau client.\nAfficher montant du c.",
         "ERREUR 4:10 Un client n'a pas de champ « montant ».");
    PROG("Un client a : un nom.\nLe x vaut 3.\nAfficher le nom du x.",
         "ERREUR 3:10 « nom » : la valeur n'est pas un objet, c'est un nombre.");
    PROG("Un nœud a : un suivant, une valeur.\nLe premier vaut un nouveau nœud :\n    La valeur vaut 1.\n"
         "Le courant vaut premier.\nPour chaque i de 2 à 5 :\n    Le suivant du courant devient un nouveau nœud :\n"
         "        La valeur vaut i.\n    Le courant devient suivant du courant.\n"
         "Afficher valeur du suivant du suivant du premier.", "3");

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

    /* --- Héritage --- */
    PROG("Une personne a : un nom.\nUn membre est une personne.\nUn membre a : une licence.\n"
         "Le m vaut un nouveau membre :\n    Le nom vaut « Ana ».\n    La licence vaut 42.\n"
         "Le nom du m devient « Anna ».\nAfficher nom du m puis licence du m puis m.", "Anna 42 un membre");
    PROG("Un objet a : un nom.\nUn outil est un objet. Un outil a : un usage.\nUn marteau est un outil. Un marteau a : un poids.\n"
         "Le h vaut un nouveau marteau :\n    Le nom vaut « M ».\n    L'usage vaut « clouer ».\n    Le poids vaut 2.\n"
         "Afficher nom du h puis usage du h puis poids du h.", "M clouer 2");
    PROG("Une personne a : un nom.\nUn membre est une personne.\nUn invité est une personne.\n"
         "Pour saluer une personne :\n    Afficher « Bonjour » puis nom de personne.\n"
         "Le m vaut un nouveau membre :\n    Le nom vaut « Ana ».\nLe i vaut un nouvel invité :\n    Le nom vaut « Bo ».\n"
         "Saluer m.\nSaluer i.", "Bonjour Ana\nBonjour Bo");
    PROG("Une personne a : un nom.\nUne ville a : un nom.\nUn membre est une personne. Un membre a : une ville.\n"
         "Le m vaut un nouveau membre.\nLa ville du m devient une nouvelle ville :\n    Le nom vaut « Bulle ».\n"
         "Afficher nom de la ville du m.", "Bulle");

    /* --- Méthodes : la version la plus précise, selon la classe réelle --- */
    PROG("Une personne a : un nom.\nUn membre est une personne. Un membre a : une licence.\nUn invité est une personne.\n"
         "Pour saluer une personne :\n    Afficher « Bonjour » puis nom de la personne.\n"
         "Pour saluer un membre :\n    Afficher « Salut » puis nom du membre puis licence du membre.\n"
         "La description d'une personne vaut « personne ».\nLa description d'un invité vaut « invité ».\n"
         "Le m vaut un nouveau membre :\n    Le nom vaut « Ana ».\n    La licence vaut 7.\n"
         "Le i vaut un nouvel invité :\n    Le nom vaut « Bo ».\nSaluer m.\nSaluer i.\n"
         "Afficher la description de m puis la description de i.",
         "Salut Ana 7\nBonjour Bo\npersonne invité");
    PROG("Un objet a : un nom.\nUn outil est un objet.\nUn marteau est un outil.\n"
         "Le poids d'un objet vaut 1.\nLe poids d'un outil vaut 2.\nLe h vaut un nouveau marteau.\nAfficher poids de h.", "2");
    PROG("Une personne a : un nom.\nPour saluer une personne :\n    Afficher 1.\nSaluer 3.",
         "ERREUR 4:1 « saluer » choisit sa version selon la classe de son premier argument : "
         "celui-ci n'est pas un objet, c'est un nombre.");
    PROG("Une personne a : un nom.\nUne ville a : un nom.\nPour saluer une personne :\n    Afficher 1.\n"
         "La v vaut une nouvelle ville.\nSaluer v.", "ERREUR 6:1 Aucune version de « saluer » pour une ville.");

    /* --- Aptitudes : champs apportés, versions, ordre de choix --- */
    PROG(APT_M "Un membre est une personne horodatée.\nUn membre a : une licence.\n"
         "Pour dater une chose horodatée :\n    La date de la chose devient « lundi ».\n"
         "Le m vaut un nouveau membre :\n    Le nom vaut « Ana ».\n    La licence vaut 3.\n"
         "Dater m.\nAfficher nom du m puis date du m puis licence du m.", "Ana lundi 3");
    PROG(APT_M "Un document est une chose horodatée et numérotée.\n"
         "Le d vaut un nouveau document :\n    La date vaut 1.\n    Le numéro vaut 2.\nAfficher date du d puis numéro du d puis d.",
         "1 2 un document");
    /* la classe l'emporte sur ses aptitudes, qui l'emportent sur la classe parente */
    PROG(APT_M "Un employé est une personne horodatée.\nUn cadre est un employé.\n"
         "La sorte d'une personne vaut « personne ».\nLa sorte d'une chose horodatée vaut « horodatée ».\n"
         "La sorte d'un cadre vaut « cadre ».\n"
         "Le e vaut un nouvel employé.\nLe c vaut un nouveau cadre.\nLa p vaut une nouvelle personne.\n"
         "Afficher sorte de e puis sorte de c puis sorte de p.", "horodatée cadre personne");
    PROG(APT_M "Un membre est une personne horodatée et numérotée.\n"
         "Pour décrire une chose horodatée :\n    Afficher 1.\nPour décrire une chose numérotée :\n    Afficher 2.\n"
         "Pour décrire un membre :\n    Afficher 3.\nLe m vaut un nouveau membre.\nDécrire m.", "3");

    /* --- Dates (§ 14) --- */
    PROG("La facture vaut 21.09.2026.\nL'échéance vaut facture + 30.\n"
         "Afficher échéance puis échéance − facture puis facture − 5 puis 1 + facture.",
         "21.10.2026 30 16.09.2026 22.09.2026");
    PROG("Afficher 29.02.2024 + 365 puis 28.02.2023 + 1 puis 31.12.2026 + 1 puis 01.03.2024 − 1.",
         "28.02.2025 01.03.2023 01.01.2027 29.02.2024");
    PROG("Afficher 01.01.2027 − 01.01.2026 puis 01.01.2025 − 01.01.2024 puis 01.01.2026 − 21.09.2026.",
         "365 366 −263");
    PROG("Afficher 21.09.2026 + 3,00 puis 21.09.2026 < 22.09.2026 puis 21.09.2026 = 21.09.2026 puis 1.3.2026.",
         "24.09.2026 vrai vrai 01.03.2026");
    PROG("Pour chaque j de 30.12.2026 à 02.01.2027, afficher j.", "30.12.2026\n31.12.2026\n01.01.2027\n02.01.2027");
    PROG("Pour chaque j de 03.01.2026 à 01.01.2026, afficher j.", "03.01.2026\n02.01.2026\n01.01.2026");
    PROG("Pour chaque j de 01.01.2026 à 15.01.2026 par pas de 7, afficher j.", "01.01.2026\n08.01.2026\n15.01.2026");
    PROG("Le jour vaut 14.07.2026.\nSelon jour :\n    Cas de 01.07.2026 à 31.08.2026, afficher « été ».\n"
         "    Autrement, afficher « autre ».", "été");
    PROG("Afficher aujourd'hui = aujourd'hui puis aujourd'hui > 01.01.2026.", "vrai vrai");
    PROG("Afficher 21.09.2026 + 21.09.2026.", "ERREUR 1:21 On n'additionne pas deux dates.");
    PROG("Afficher 21.09.2026 + 0,5.", "ERREUR 1:21 Une date se décale d'un nombre entier de jours.");
    PROG("Afficher 31.12.9999 + 1.", "ERREUR 1:21 Date hors du calendrier : du 01.01.0001 au 31.12.9999.");
    PROG("Afficher 01.01.0001 − 1.", "ERREUR 1:21 Date hors du calendrier : du 01.01.0001 au 31.12.9999.");
    PROG("Afficher 21.09.2026 + 99999999.", "ERREUR 1:21 Une date se décale d'un nombre entier de jours.");
    PROG("Afficher 3 − 21.09.2026.", "ERREUR 1:12 On ne soustrait pas une date d'un nombre.");
    PROG("Afficher 21.09.2026 + « a ».", "ERREUR 1:21 ADDITION impossible entre une date et un texte.");
    PROG("Afficher 21.09.2026 < 3.", "ERREUR 1:21 Comparaison impossible entre une date et un nombre.");
    PROG("Afficher −21.09.2026.", "ERREUR 1:10 Opposé impossible : la valeur est une date.");
    PROG("Répéter 21.09.2026 fois, afficher 1.",
         "~Nombre de tours invalide : un entier positif ou nul est attendu, pas une date.");
    {
        total++;
        char *attendu = date_suisse(date_aujourdhui());
        Portee *p = portee_creer();
        Machine *m = machine_creer();
        char *r = executer_source(p, m, "Afficher aujourd'hui.", 0);
        if (strcmp(r, attendu) != 0) signaler(__LINE__, "Afficher aujourd'hui.", attendu, r);
        free(r);
        free(attendu);
        machine_detruire(m);
        portee_detruire(p);
    }

    /* --- Fichiers (§ 15) --- */
    {
        static const unsigned char PNG[] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A, 0, 0, 0, 13 };
        static const unsigned char JPEG[] = { 0xFF, 0xD8, 0xFF, 0xE0, 0, 16 };
        static const unsigned char WEBP[] = { 'R', 'I', 'F', 'F', 4, 0, 0, 0, 'W', 'E', 'B', 'P', 'V', 'P' };
        static const unsigned char FAUX_WEBP[] = { 'R', 'I', 'F', 'F', 4, 0, 0, 0, 'W', 'A', 'V', 'E' };
        creer_fichier("_essai.png", PNG, sizeof PNG);
        creer_fichier("_essai.jpg", JPEG, sizeof JPEG);
        creer_fichier("_essai.gif", "GIF89a....", 10);
        creer_fichier("_essai.webp", WEBP, sizeof WEBP);
        creer_fichier("_essai.wav", FAUX_WEBP, sizeof FAUX_WEBP);
        creer_fichier("_essai.txt", "bonjour", 7);
        creer_fichier("_essai1.bin", "x", 1);
        static char gros[2345678];
        memset(gros, 'x', sizeof gros);
        creer_fichier("_essai_gros.bin", gros, sizeof gros);
        creer_fichier("_essai_1000.bin", gros, 1000);
        creer_fichier("_essai_1050.bin", gros, 1050);
        creer_fichier("_essai_999.bin", gros, 999);
        remove("_essai_copie.png");
        remove("_essai_copie2.png");
        remove("_essai_rate.png");

        PROG("La photo vaut le fichier « _essai.png ».\n"
             "Afficher photo puis taille de la photo puis format de la photo puis nom de fichier de la photo.",
             "une image PNG de 12 octets 12 PNG _essai.png");
        PROG("Afficher format du fichier « _essai.jpg » puis format du fichier « _essai.gif » "
             "puis format du fichier « _essai.webp » puis format du fichier « _essai.wav ».",
             "JPEG GIF WebP inconnu");
        PROG("Afficher le fichier « _essai.txt » puis le fichier « _essai1.bin ».",
             "un fichier de 7 octets un fichier de 1 octet");
        PROG("Afficher le fichier « _essai_999.bin ».\nAfficher le fichier « _essai_1000.bin ».\n"
             "Afficher le fichier « _essai_1050.bin ».\nAfficher le fichier « _essai_gros.bin ».",
             "un fichier de 999 octets\nun fichier de 1 Ko\nun fichier de 1,1 Ko\nun fichier de 2,3 Mo");
        PROG("Afficher le fichier « _essai.png » = le fichier « _essai.png » puis "
             "le fichier « _essai.png » = le fichier « _essai.jpg ».", "vrai faux");
        PROG("Le chemin vaut « _essai.txt ».\nAfficher taille du fichier (chemin).", "7");
        PROG("Afficher le fichier « _absent.png ».", "ERREUR 1:10 Fichier « _absent.png » introuvable ou illisible.");
        PROG("Afficher le fichier (3).", "ERREUR 1:10 Le chemin d'un fichier est un texte, pas un nombre.");
        PROG("La photo vaut le fichier « _essai.png ».\nAfficher poids de la photo.", "ERREUR 2:10 « poids de la photo » inconnu.");
        PROG("Une personne a : un nom, une photo.\nLa p vaut une nouvelle personne :\n"
             "    La photo vaut le fichier « _essai.jpg ».\nAfficher photo de la p puis format de la photo de la p.",
             "une image JPEG de 6 octets JPEG");
        /* écriture différée : rien n'est écrit si l'exécution échoue */
        PROG("Enregistrer le fichier « _essai.png » dans « _essai_rate.png ».\nAfficher 1 ÷ 0.",
             "ERREUR 2:12 Division par zéro.");
        total++;
        if (fichier_existe("_essai_rate.png")) signaler(__LINE__, "écriture annulée", "absent", "présent");
        PROG("Enregistrer le fichier « _essai.png » dans « _essai_copie.png ».\nAfficher « écrit ».", "écrit");
        PROG("Afficher le fichier « _essai_copie.png » = le fichier « _essai.png ».", "vrai");
        PROG("Enregistrer le fichier « _essai.txt » dans « _essai_copie.png ».",
             "ERREUR 1:1 « _essai_copie.png » existe déjà : il n'est jamais écrasé.");
        PROG("Enregistrer le fichier « _essai.txt » dans « _essai_copie2.png ».\n"
             "Enregistrer le fichier « _essai.txt » dans « _essai_copie2.png ».",
             "ERREUR 2:1 « _essai_copie2.png » est déjà enregistré par cette exécution.");
        total++;
        if (fichier_existe("_essai_copie2.png")) signaler(__LINE__, "écriture annulée", "absent", "présent");
        PROG("Enregistrer 3 dans « _essai_x ».", "ERREUR 1:1 Seul un fichier s'enregistre : la valeur est un nombre.");
        PROG("La photo vaut le fichier « _essai.png ».\nAfficher photo < photo.",
             "~Seuls deux nombres ou deux dates se comparent par ordre");

        const char *essais[] = { "_essai.png", "_essai.jpg", "_essai.gif", "_essai.webp", "_essai.wav", "_essai.txt",
                                 "_essai1.bin", "_essai_gros.bin", "_essai_1000.bin", "_essai_1050.bin",
                                 "_essai_999.bin", "_essai_copie.png" };
        for (size_t k = 0; k < sizeof essais / sizeof *essais; k++) remove(essais[k]);
    }

    /* --- Entités : typage strict avant toute écriture (§ 16.2) --- */
    PROG("Un client, conservé, a : un nom (texte), un âge (nombre entier), un solde (nombre), un actif (vrai ou faux), "
         "une inscription (date).\nLe c vaut un nouveau client :\n    Le nom vaut « Ana ».\n    L'âge vaut 30.\n"
         "    Le solde vaut 12,50.\n    L'actif vaut vrai.\n    L'inscription vaut 21.09.2026.\n"
         "Afficher nom du c puis âge du c puis solde du c puis actif du c puis inscription du c.",
         "Ana 30 12,50 vrai 21.09.2026");
    PROG("Un client, conservé, a : un âge (nombre entier).\nLe v vaut 2,5.\nLe c vaut un nouveau client :\n    L'âge vaut v.",
         "ERREUR 4:5 Le champ « âge » attend un nombre entier, pas 2,5.");
    PROG("Un client, conservé, a : un nom (texte).\nLe c vaut un nouveau client.\nLe x vaut 3.\nLe nom du c devient x.",
         "ERREUR 4:1 Le champ « nom » attend un texte, pas 3.");
    PROG("Un client, conservé, a : un parrain (client).\nUne personne a : un nom.\nLe p vaut une nouvelle personne.\n"
         "Le c vaut un nouveau client.\nLe parrain du c devient p.",
         "ERREUR 5:1 Le champ « parrain » attend un client, pas une personne.");
    PROG("Un client, conservé, a : un parrain (client).\nUn membre, conservé, est un client.\n"
         "Le m vaut un nouveau membre.\nLe c vaut un nouveau client.\nLe parrain du c devient m.\nAfficher parrain du c.",
         "un membre");
    PROG("Une chose datée a : une date (date).\nUn document, conservé, est une chose datée.\nLe x vaut « lundi ».\n"
         "Le d vaut un nouveau document.\nLa date du d devient x.",
         "ERREUR 5:1 Le champ « date » attend une date, pas un texte.");
    PROG("Un client, conservé, a : un nom (texte).\nUne personne a : un nom.\nLe p vaut une nouvelle personne.\n"
         "Le nom du p devient 3.\nAfficher nom du p.", "3");
    {
        static const unsigned char PNG[] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
        creer_fichier("_essai_img.png", PNG, sizeof PNG);
        creer_fichier("_essai_doc.pdf", "%PDF-1.7", 8);
        PROG("Un client, conservé, a : une photo (image), un contrat (fichier).\nLe c vaut un nouveau client :\n"
             "    La photo vaut le fichier « _essai_img.png ».\n    Le contrat vaut le fichier « _essai_doc.pdf ».\n"
             "Afficher photo du c puis contrat du c.", "une image PNG de 8 octets un fichier de 8 octets");
        PROG("Un client, conservé, a : une photo (image).\nLe c vaut un nouveau client :\n"
             "    La photo vaut le fichier « _essai_doc.pdf ».",
             "ERREUR 3:5 « _essai_doc.pdf » n'est pas une image (PNG, JPEG, GIF ou WebP).");
        remove("_essai_img.png");
        remove("_essai_doc.pdf");
    }

    /* --- La base : conserver, modifier, supprimer (§ 16.3), base en mémoire --- */
#define CLIENT "Un client, conservé, a : un nom (texte), un parrain (client), une licence (texte), unique.\n"
#define ANA "Le a vaut un nouveau client :\n    Le nom vaut « Ana ».\n    La licence vaut « A-1 ».\n    Le parrain vaut a.\n"
    PROG(CLIENT ANA "Afficher « avant ».", "ERREUR 5:21 « a » inconnu.");   /* un objet ne se désigne qu'une fois créé */
    PROG(CLIENT "Le a vaut un nouveau client :\n    Le nom vaut « Ana ».\n    La licence vaut « A-1 ».\n"
         "Le parrain du a devient a.\nConserver a.\nLe nom du a devient « Anna ».\nAfficher nom du a puis nom du parrain du a.",
         "Anna Anna");
    PROG("Une personne a : un nom.\nLa p vaut une nouvelle personne.\nConserver p.",
         "ERREUR 3:1 « personne » n'est pas une entité : ses objets ne se conservent pas. "
         "Déclarez « Une personne, conservée, a : ».");
    PROG("Conserver 3.", "ERREUR 1:1 Seul un objet se conserve : la valeur est un nombre.");
    PROG(CLIENT "Le a vaut un nouveau client :\n    Le nom vaut « Ana ».\nConserver a.",
         "ERREUR 4:1 Le champ « parrain » n'a pas de valeur : un client incomplet ne se conserve pas.");
    PROG(CLIENT "Le b vaut un nouveau client :\n    Le nom vaut « Bo ».\n    La licence vaut « B ».\n"
         "Le parrain du b devient b.\nLe a vaut un nouveau client :\n    Le nom vaut « Ana ».\n    La licence vaut « A ».\n"
         "    Le parrain vaut b.\nConserver a.",
         "ERREUR 10:1 Le champ « parrain » désigne un client qui n'est pas conservé : conservez-le d'abord.");
    PROG(CLIENT "Le a vaut un nouveau client :\n    Le nom vaut « Ana ».\n    La licence vaut « A ».\n"
         "Le parrain du a devient a.\nConserver a.\nConserver a.",
         "ERREUR 7:1 Un client déjà conservé ne se conserve pas deux fois.");
    PROG(CLIENT "Le a vaut un nouveau client :\n    Le nom vaut « Ana ».\n    La licence vaut « A ».\n"
         "Le parrain du a devient a.\nConserver a.\nLe b vaut un nouveau client :\n    Le nom vaut « Bo ».\n"
         "    La licence vaut « A ».\n    Le parrain vaut a.\nConserver b.",
         "ERREUR 11:1 « licence » est unique : un autre client conservé a déjà « A ».");
    PROG(CLIENT "Le a vaut un nouveau client :\n    Le nom vaut « Ana ».\n    La licence vaut « A ».\n"
         "Le parrain du a devient a.\nConserver a.\nLe b vaut un nouveau client :\n    Le nom vaut « Bo ».\n"
         "    La licence vaut « B ».\n    Le parrain vaut a.\nConserver b.\nLa licence du b devient « A ».",
         "ERREUR 12:1 « licence » est unique : un autre client conservé a déjà « A ».");
    PROG(CLIENT "Le a vaut un nouveau client :\n    Le nom vaut « Ana ».\n    La licence vaut « A ».\n"
         "Le parrain du a devient a.\nConserver a.\nLe b vaut un nouveau client :\n    Le nom vaut « Bo ».\n"
         "    La licence vaut « B ».\n    Le parrain vaut a.\nConserver b.\nSupprimer a définitivement.",
         "ERREUR 12:1 Ce client est encore désigné par le champ « parrain » d'un client.");
    /* la suppression simple ne casse rien : toujours permise, même désigné (§ 16.12) */
    PROG(CLIENT "Le a vaut un nouveau client :\n    Le nom vaut « Ana ».\n    La licence vaut « A ».\n"
         "Le parrain du a devient a.\nConserver a.\nLe b vaut un nouveau client :\n    Le nom vaut « Bo ».\n"
         "    La licence vaut « B ».\n    Le parrain vaut a.\nConserver b.\nSupprimer a.\n"
         "Afficher le nombre de clients conservés puis nom du parrain du b.", "1 Ana");
    PROG(CLIENT "Le a vaut un nouveau client :\n    Le nom vaut « Ana ».\n    La licence vaut « A ».\n"
         "Le parrain du a devient a.\nConserver a.\nSupprimer a définitivement.\nSupprimer a.",
         "ERREUR 8:1 Un client qui n'est pas conservé ne se supprime pas.");
    PROG(CLIENT "Le a vaut un nouveau client :\n    Le nom vaut « Ana ».\n    La licence vaut « A ».\n"
         "Le parrain du a devient a.\nConserver a.\nSupprimer a.\nSupprimer a.",
         "ERREUR 8:1 Ce client est déjà supprimé : « Supprimer … définitivement » l'efface de la base.");
    PROG(CLIENT "Le a vaut un nouveau client :\n    Le nom vaut « Ana ».\n    La licence vaut « A ».\n"
         "Le parrain du a devient a.\nConserver a.\nSupprimer a définitivement.\nConserver a.\nAfficher nom du a.", "Ana");
    PROG("Une adhésion, conservée, a : un numéro (nombre entier).\nLa x vaut une nouvelle adhésion :\n"
         "    Le numéro vaut 99999999999999999999.\nConserver x.",
         "ERREUR 4:1 Le champ « numéro » est trop grand pour la base : un nombre entier y tient entre "
         "−9'223'372'036'854'775'808 et 9'223'372'036'854'775'807.");
    PROG("Une facture, conservée, a : un montant (nombre).\nUn avoir, conservé, est une facture.\n"
         "Un contrat, conservé, a : une facture (facture).\nL'a vaut un nouvel avoir :\n    Le montant vaut −5.\n"
         "Conserver a.\nLe c vaut un nouveau contrat :\n    La facture vaut a.\nConserver c.\nSupprimer a définitivement.",
         "ERREUR 10:1 Cet avoir est encore désigné par le champ « facture » d'un contrat.");
    /* une saisie ratée rend à l'objet son état « non conservé » */
    {
        total++;
        Portee *p = portee_creer();
        Machine *m = machine_creer();
        const char *saisies[] = { "Un client, conservé, a : un nom (texte).",
                                  "Le c vaut un nouveau client :\n    Le nom vaut « Ana ».",
                                  "Conserver c. Afficher 1 ÷ 0.", "Conserver c. Afficher « conservé »." };
        const char *attendus[] = { "", "", "~Division par zéro", "conservé" };
        for (int i = 0; i < 4; i++) {
            Portee *sp = portee_cloner(p);
            char *r = executer_source(p, m, saisies[i], 1);
            int echec = strncmp(r, "ERREUR", 6) == 0;
            int ok = attendus[i][0] == '~' ? strstr(r, attendus[i] + 1) != NULL : strcmp(r, attendus[i]) == 0;
            if (echec) { portee_detruire(p); p = sp; } else portee_detruire(sp);
            if (!ok) { signaler(__LINE__, saisies[i], attendus[i], r); free(r); break; }
            free(r);
        }
        machine_detruire(m);
        portee_detruire(p);
    }
    /* --- Retrouver (§ 16.4), base en mémoire --- */
#define RC "Un client, conservé, a : un nom (texte), un solde (nombre), un actif (vrai ou faux), un parrain (client), " \
           "une inscription (date), un rang (nombre entier).\n" \
           "Pour créer un nom et un solde et un actif et une inscription et un rang :\n" \
           "    Le c vaut un nouveau client :\n        Le nom vaut nom.\n        Le solde vaut solde.\n" \
           "        L'actif vaut actif.\n        L'inscription vaut inscription.\n        Le rang vaut rang.\n" \
           "    Le parrain du c devient c.\n    Conserver c.\nLe oui vaut vrai.\nLe non vaut faux.\n" \
           "Créer « Zoé » et −12,50 et oui et 01.03.2026 et 3.\nCréer « émile » et 10 et oui et 15.01.2025 et 1.\n" \
           "Créer « Ana » et 9 et non et 21.09.2026 et 10.\nCréer « Bob » et −3 et oui et 01.01.2026 et 2.\n" \
           "Créer « Élodie » et 100,000000000000000001 et oui et 31.12.2025 et 20.\n"
    PROG(RC "Pour chaque client conservé, par nom, afficher nom du client.", "Ana\nBob\nÉlodie\némile\nZoé");
    PROG(RC "Pour chaque client conservé, afficher nom du client.", "Zoé\némile\nAna\nBob\nÉlodie");
    PROG(RC "Pour chaque client conservé, par solde décroissant, afficher solde du client.",
         "100,000000000000000001\n10\n9\n−3\n−12,50");
    PROG(RC "Pour chaque client conservé dont le solde est négatif et l'actif est vrai, par solde décroissant, "
         "afficher nom du client.", "Bob\nZoé");
    PROG(RC "Afficher le nombre de clients conservés dont le solde > 9 puis le nombre de clients conservés dont le solde ≥ 9 "
         "puis le nombre de clients conservés dont le solde = 10,00 puis le nombre de clients conservés.", "2 3 1 5");
    PROG(RC "Afficher le nombre de clients conservés dont l'inscription < 01.01.2026 puis "
         "le nombre de clients conservés dont le rang ≥ 2,5 puis le nombre de clients conservés dont l'actif est faux.",
         "2 3 1");
    PROG(RC "Pour chaque client conservé dont le nom > « b », par nom, afficher nom du client.", "Bob\nÉlodie\némile\nZoé");
    PROG(RC "Le a vaut le client conservé dont le nom est « Ana ».\nAfficher solde du a puis parrain du a = a puis "
         "a = le client conservé dont le rang est 10.", "9 vrai vrai");
    PROG(RC "Le a vaut le client conservé dont le nom est « Ana ».\nLe solde du a devient 1000.\n"
         "Afficher le nombre de clients conservés dont le solde ≥ 1000.", "1");
    PROG(RC "Afficher le client conservé dont le solde > 5.",
         "ERREUR 18:10 3 clients conservés répondent à cette condition : « le client conservé dont … » en attend un seul.");
    PROG(RC "Afficher le client conservé dont le nom est « Personne ».",
         "ERREUR 18:10 Aucun client conservé ne répond à cette condition.");
    PROG(RC "Le a vaut le client conservé dont le nom est « Ana ».\n"
         "Afficher le nombre de clients conservés dont le parrain est a puis le nombre de clients conservés dont le parrain n'est pas a.",
         "1 4");
    PROG(RC "Le x vaut « 9 ».\nAfficher le nombre de clients conservés dont le solde = x.",
         "ERREUR 19:10 Le champ « solde » se compare à un nombre.");
    /* la liste d'une boucle est figée à son début ; sortir, passer */
    PROG(RC "Pour chaque client conservé, par nom :\n    Si nom du client = « Bob », passer au tour suivant.\n"
         "    Si nom du client = « émile », sortir de la boucle.\n    Créer « N » et 0 et oui et 01.01.2026 et 0.\n"
         "    Afficher nom du client.\nAfficher le nombre de clients conservés.", "Ana\nÉlodie\n7");
    PROG(RC "Le compte vaut 0.\nPour chaque client conservé :\n    Le d vaut un nouveau client :\n"
         "        Le nom vaut « X ».\n        Le solde vaut 0.\n        L'actif vaut vrai.\n        L'inscription vaut 01.01.2026.\n"
         "        Le rang vaut 0.\n    Le parrain du d devient d.\n    Conserver d.\n    Le compte devient compte + 1.\n"
         "Afficher compte puis le nombre de clients conservés.", "5 10");
    /* héritage : un membre est un client */
    PROG("Un client, conservé, a : un nom (texte).\nUn membre, conservé, est un client. Un membre a : une cotisation (nombre).\n"
         "Le c vaut un nouveau client :\n    Le nom vaut « C ».\nConserver c.\nLe m vaut un nouveau membre :\n"
         "    Le nom vaut « M ».\n    La cotisation vaut 5.\nConserver m.\n"
         "Pour chaque client conservé, par nom, afficher nom du client puis client.\n"
         "Pour chaque membre conservé dont le nom est « M », afficher cotisation du membre.",
         "C un client\nM un membre\n5");
    /* --- Champs facultatifs (§ 16.9) --- */
#define FAC "Un compositeur, conservé, a : un nom (texte), unique, un maître (compositeur), facultatif.\n" \
            "Une partition, conservée, a : un titre (texte), un arrangeur (compositeur), facultatif, " \
            "une édition (date), facultative, un tirage (nombre entier), facultatif.\n"
    PROG(FAC "La p vaut une nouvelle partition :\n    Le titre vaut « A ».\nConserver p.\n"
         "Afficher arrangeur de p puis édition de p puis arrangeur de p est absent puis tirage de p est présent.",
         "absent absent vrai faux");
    PROG(FAC "La p vaut une nouvelle partition :\n    Le titre vaut « A ».\nAfficher tirage de p + 1.",
         "ERREUR 5:22 Le champ « tirage » est absent : vérifiez-le d'abord avec « est présent ».");
    PROG(FAC "La p vaut une nouvelle partition :\n    Le titre vaut « A ».\nAfficher nom de l'arrangeur de p.",
         "ERREUR 5:10 Le champ « arrangeur » est absent : vérifiez-le d'abord avec « est présent ».");
    PROG(FAC "La p vaut une nouvelle partition :\n    Le titre vaut « A ».\nAfficher édition de p = 01.01.2026.",
         "~Le champ « édition » est absent");
    PROG(FAC "Le c vaut un nouveau compositeur.\nLe nom du c devient absent.",
         "ERREUR 4:1 Le champ « nom » n'est pas facultatif : il ne devient pas absent.");
    /* deux objets neufs qui se désignent l'un l'autre : possible grâce au champ facultatif */
    PROG(FAC "Le a vaut un nouveau compositeur :\n    Le nom vaut « A ».\nConserver a.\n"
         "Le b vaut un nouveau compositeur :\n    Le nom vaut « B ».\n    Le maître vaut a.\nConserver b.\n"
         "Le maître du a devient b.\nAfficher nom du maître du maître du a.", "A");
    PROG(FAC "Pour créer un titre et une édition :\n    La p vaut une nouvelle partition :\n        Le titre vaut titre.\n"
         "    Si édition est présente, l'édition de p devient édition.\n    Conserver p.\n"
         "Créer « C » et 01.01.2000.\nCréer « A » et absente.\nCréer « B » et 01.01.1990.\n"
         "Pour chaque partition conservée, par édition, afficher titre de la partition.\n"
         "Pour chaque partition conservée, par édition décroissant, afficher titre de la partition.\n"
         "Afficher le nombre de partitions conservées dont l'édition est absente puis "
         "le nombre de partitions conservées dont l'édition < 01.01.2020.",
         "B\nC\nA\nC\nB\nA\n1 2");
    {   /* migration : un champ facultatif s'ajoute à une entité qui a déjà des objets */
        remove("_essai_fac.grymd");
        const char *etapes[][2] = {
            { "Un livre, conservé, a : un titre (texte).\nLe l vaut un nouveau livre :\n    Le titre vaut « X ».\nConserver l.\n", "" },
            { "Un livre, conservé, a : un titre (texte), un auteur (texte), facultatif, un parrain (livre), facultatif.\n"
              "Afficher auteur du livre conservé dont le titre est « X » puis le nombre de livres conservés dont l'auteur est absent.\n",
              "absent 1" },
            { "Un livre, conservé, a : un titre (texte), facultatif, un auteur (texte), facultatif, un parrain (livre), facultatif.\n",
              "~« titre » ne peut pas devenir facultatif" },
        };
        for (int i = 0; i < 3; i++) {
            total++;
            Portee *p = portee_creer();
            Machine *m = machine_creer();
            machine_base(m, "_essai_fac.grymd");
            char *r = executer_source(p, m, etapes[i][0], 0);
            const char *att = etapes[i][1];
            int ok = att[0] == '~' ? strstr(r, att + 1) != NULL : strcmp(r, att) == 0;
            if (!ok) signaler(__LINE__, etapes[i][0], att, r);
            free(r);
            machine_detruire(m);
            portee_detruire(p);
        }
        remove("_essai_fac.grymd");
    }

    /* --- Relations inverses (§ 16.10) --- */
#define INV "Une personne, conservée, a : un nom (texte), unique.\n" \
            "Un membre, conservé, est une personne.\n" \
            "Une chanson, conservée, a : un titre (texte), un compositeur (personne), un auteur (personne), facultatif.\n" \
            "Une œuvre, conservée, a : un titre (texte), un compositeur (personne).\n" \
            "Pour ajouter un titre et une musique et une parole :\n    La c vaut une nouvelle chanson :\n" \
            "        Le titre vaut titre.\n        Le compositeur vaut musique.\n" \
            "    Si parole est présente, l'auteur de c devient parole.\n    Conserver c.\n" \
            "La brel vaut une nouvelle personne :\n    Le nom vaut « Brel ».\nConserver brel.\n" \
            "Le rauber vaut un nouveau membre :\n    Le nom vaut « Rauber ».\nConserver rauber.\n" \
            "Ajouter « Ne me quitte pas » et brel et brel.\nAjouter « La Valse » et brel et absent.\n" \
            "Ajouter « Madeleine » et rauber et brel.\n" \
            "La b vaut une nouvelle œuvre :\n    Le titre vaut « Sonate ».\n    Le compositeur vaut rauber.\nConserver b.\n"
    PROG(INV "Pour chaque œuvre de rauber, afficher titre de l'œuvre.\nAfficher le nombre d'œuvres de brel.", "Sonate\n0");
    PROG(INV "Pour chaque chanson conservée dont brel est l'auteur, par titre, afficher titre de la chanson.\n"
         "Afficher le nombre de chansons conservées dont brel est le compositeur et rauber n'est pas l'auteur.",
         "Madeleine\nNe me quitte pas\n1");
    PROG(INV "Afficher le nombre de chansons de brel.",
         "~Plusieurs champs d'une chanson peuvent désigner une personne : « compositeur » et « auteur ».");
    PROG(INV "Pour chaque œuvre de 3, afficher 1.",
         "~« de … » désigne un objet : un nombre, un texte ou une date n'a pas de liens.");
    PROG(INV "La x vaut une nouvelle chanson :\n    Le titre vaut « X ».\n    Le compositeur vaut brel.\n"
         "Afficher le nombre d'œuvres de l'auteur de x.", "~Le champ « auteur » est absent");
    PROG(INV "Pour chaque œuvre de b, afficher 1.", "~Aucun champ d'une œuvre ne peut désigner une œuvre");
    PROG(INV "Pour chaque œuvre de rauber dont le titre = « Sonate », par titre décroissant, afficher titre de l'œuvre.",
         "Sonate");

    /* --- Corbeille et cascade (§ 16.12) --- */
#define SD "Une partition, conservée, a : un titre (texte), une cote (texte), unique.\n" \
           "Un pupitre, conservé, a : une partition (partition), et disparaît avec elle, un instrument (texte).\n" \
           "Une note, conservée, a : une partition (partition), un texte (texte).\n" \
           "Pour créer un titre et une cote :\n    La p vaut une nouvelle partition :\n        Le titre vaut titre.\n" \
           "        La cote vaut cote.\n    Conserver p.\n    Le v vaut un nouveau pupitre :\n        La partition vaut p.\n" \
           "        L'instrument vaut « violon ».\n    Conserver v.\n" \
           "Créer « Offrande » et « A-1 ».\nCréer « Sonate » et « A-2 ».\n" \
           "La o vaut la partition conservée dont la cote est « A-1 ».\n" \
           "La s vaut la partition conservée dont la cote est « A-2 ».\n"
    PROG(SD "Supprimer o.\nAfficher le nombre de partitions conservées puis le nombre de partitions supprimées puis "
         "le nombre de pupitres conservés puis le nombre de pupitres supprimés puis titre de o.\n"
         "Pour chaque partition supprimée, afficher titre de la partition.\nRétablir o.\n"
         "Afficher le nombre de partitions conservées puis le nombre de pupitres conservés.",
         "1 1 1 1 Offrande\nOffrande\n2 2");
    PROG(SD "La n vaut une nouvelle note :\n    La partition vaut s.\n    Le texte vaut « x ».\nConserver n.\n"
         "Supprimer s.\nAfficher texte de n puis titre de la partition de n.\nSupprimer n.\nSupprimer s définitivement.",
         "~Cette partition est encore désignée par le champ « partition » d'une note supprimée : supprimez-la "
         "définitivement d'abord.");
    PROG(SD "Supprimer s.\nLa m vaut une nouvelle note :\n    La partition vaut s.\n    Le texte vaut « y ».\nConserver m.",
         "~Le champ « partition » désignerait une partition supprimée : rétablissez-la d'abord.");
    PROG(SD "Supprimer s.\nLa q vaut une nouvelle partition :\n    Le titre vaut « Z ».\n    La cote vaut « A-2 ».\nConserver q.",
         "~« cote » est unique : « A-2 » appartient à une partition supprimée. Rétablissez-la, ou supprimez-la définitivement.");
    PROG(SD "Rétablir s.", "~Cette partition n'est pas supprimée.");
    PROG(SD "Supprimer s.\nLe pu vaut le pupitre supprimé dont l'instrument est « violon ».\nRétablir pu.",
         "~Ce pupitre a disparu avec un autre objet : rétablissez celui-là, et il reviendra avec lui.");
    PROG(SD "Supprimer s.\nSupprimer o définitivement.\nAfficher le nombre de partitions supprimées puis "
         "le nombre de pupitres conservés puis le nombre de pupitres supprimés.\nConserver o.\n"
         "Afficher le nombre de partitions conservées puis titre de o.",
         "1 0 1\n1 Offrande");
    {   /* une base d'avant la corbeille reçoit ses colonnes, et ses objets restent conservés */
        remove("_essai_corbeille.grymd");
        sqlite3 *db = NULL;
        sqlite3_open("_essai_corbeille.grymd", &db);
        sqlite3_exec(db, "CREATE TABLE grym_objet (id INTEGER PRIMARY KEY AUTOINCREMENT, classe TEXT NOT NULL);"
                         "CREATE TABLE grym_schema (entite TEXT PRIMARY KEY, definition TEXT NOT NULL);"
                         "INSERT INTO grym_objet (classe) VALUES ('livre');"
                         "CREATE TABLE \"e livre\" (id INTEGER PRIMARY KEY REFERENCES grym_objet(id) ON DELETE CASCADE,"
                         " \"c titre\" TEXT NOT NULL);"
                         "INSERT INTO \"e livre\" VALUES (1, 'X');"
                         "INSERT INTO grym_schema VALUES ('livre', 'parent=;titre:texte');", NULL, NULL, NULL);
        sqlite3_close(db);
        total++;
        Portee *p = portee_creer();
        Machine *m = machine_creer();
        machine_base(m, "_essai_corbeille.grymd");
        char *r = executer_source(p, m, "Un livre, conservé, a : un titre (texte).\n"
                                        "Afficher le nombre de livres conservés puis le nombre de livres supprimés.\n"
                                        "Supprimer le livre conservé dont le titre est « X ».\n"
                                        "Afficher le nombre de livres supprimés.", 0);
        if (strcmp(r, "1 0\n1") != 0) signaler(__LINE__, "base d'avant la corbeille", "1 0\n1", r);
        free(r);
        machine_detruire(m);
        portee_detruire(p);
        remove("_essai_corbeille.grymd");
    }

    /* --- Relire, dans une autre exécution, ce qu'une première a conservé --- */
    {
        remove("_essai_relire.grymd");
        const char *decl = "Un client, conservé, a : un nom (texte), un parrain (client), une photo (fichier).\n"
                           "Un membre, conservé, est un client. Un membre a : une cotisation (nombre).\n";
        char *ecrire = grym_formater("%s"
            "Le a vaut un nouveau client :\n    Le nom vaut « Ana ».\n    La photo vaut le fichier « _essai_relire.txt ».\n"
            "Le parrain du a devient a.\nConserver a.\n"
            "Le m vaut un nouveau membre :\n    Le nom vaut « Mo ».\n    Le parrain vaut a.\n"
            "    La photo vaut le fichier « _essai_relire.txt ».\n    La cotisation vaut 12,50.\nConserver m.\n", decl);
        char *lire = grym_formater("%s"
            "Pour chaque client conservé, par nom :\n"
            "    Afficher nom du client puis client puis nom du parrain du client puis photo du client.\n"
            "Le m vaut le membre conservé dont le nom est « Mo ».\nAfficher cotisation du m puis parrain du m = "
            "le client conservé dont le nom est « Ana ».\nLa cotisation du m devient 20.\n", decl);
        char *relire = grym_formater("%sAfficher cotisation du membre conservé dont le nom est « Mo ».\n"
            "Supprimer le membre conservé dont le nom est « Mo ».\nAfficher le nombre de clients conservés.\n", decl);
        creer_fichier("_essai_relire.txt", "bonjour", 7);
        const char *sources[] = { ecrire, lire, relire };
        const char *attendus[] = { "", "Ana un client Ana un fichier de 7 octets\nMo un membre Ana un fichier de 7 octets\n12,50 vrai",
                                   "20\n1" };
        for (int i = 0; i < 3; i++) {
            total++;
            Portee *p = portee_creer();
            Machine *m = machine_creer();
            machine_base(m, "_essai_relire.grymd");
            char *r = executer_source(p, m, sources[i], 0);
            if (strcmp(r, attendus[i]) != 0) signaler(__LINE__, sources[i], attendus[i], r);
            free(r);
            machine_detruire(m);
            portee_detruire(p);
        }
        free(ecrire);
        free(lire);
        free(relire);
        remove("_essai_relire.txt");
        remove("_essai_relire.grymd");
    }
    /* --- Migrations (§ 16.7) : une même base, des déclarations qui évoluent --- */
    {
        remove("_essai_mig.grymd");
        const char *etapes[][2] = {
            { "Un client, conservé, a : un nom (texte), unique, un âge (nombre entier).\n"
              "Le c vaut un nouveau client :\n    Le nom vaut « Ana ».\n    L'âge vaut 30.\nConserver c.\n", "" },
            { "Un client, conservé, a : un nom (texte), unique, un âge (nombre entier), un pays (texte).\n",
              "~« pays » est nouveau, et 1 client est déjà conservé : donnez-lui une valeur de départ" },
            { "Un client, conservé, a : un nom (texte), unique, un âge (nombre entier), un pays (texte), « Suisse » au départ, "
              "un actif (vrai ou faux), vrai au départ.\nPour chaque client conservé, afficher pays du client puis actif du client.\n",
              "Suisse vrai" },
            { "Un client, conservé, a : un nom (texte), unique, un âge (nombre), un pays (texte), un actif (vrai ou faux).\n"
              "Le a vaut le client conservé dont le nom est « Ana ».\nL'âge du a devient 30,5.\n"
              "Afficher le nombre de clients conservés dont l'âge > 30,25.\n", "1" },
            { "Un client, conservé, a : un nom (texte), unique, un âge (nombre), un pays (texte).\n",
              "~« actif » a disparu de « client » : 1 valeur conservée serait perdue." },
            { "Un client, conservé, a : un nom (nombre), un âge (nombre), un pays (texte), un actif (vrai ou faux).\n",
              "~« nom » ne peut pas passer de « texte » à « nombre »" },
            { "Un client, conservé, a : un nom (texte), unique, un âge (nombre), un pays (texte), un actif (vrai ou faux), "
              "un parrain (client).\n", "~un lien n'a pas de valeur de départ" },
            { "Un client, conservé, a : un nom (texte), unique, un âge (nombre), un pays (texte), unique, un actif (vrai ou faux).\n"
              "Le d vaut un nouveau client :\n    Le nom vaut « Bo ».\n    L'âge vaut 1.\n    Le pays vaut « Suisse ».\n"
              "    L'actif vaut faux.\nConserver d.\n", "~« pays » est unique : un autre client conservé a déjà « Suisse »." },
            { "Un client, conservé, a : un nom (texte), unique, un âge (nombre), un pays (texte), un actif (vrai ou faux).\n"
              "Afficher âge du client conservé dont le nom est « Ana » puis le nombre de clients conservés.\n", "30,5 1" },
        };
        for (int i = 0; i < 9; i++) {
            total++;
            Portee *p = portee_creer();
            Machine *m = machine_creer();
            machine_base(m, "_essai_mig.grymd");
            char *r = executer_source(p, m, etapes[i][0], 0);
            const char *att = etapes[i][1];
            int ok = att[0] == '~' ? strstr(r, att + 1) != NULL : strcmp(r, att) == 0;
            if (!ok) signaler(__LINE__, etapes[i][0], att, r);
            free(r);
            machine_detruire(m);
            portee_detruire(p);
        }
        total++;
        sqlite3 *db = NULL;
        sqlite3_stmt *st = NULL;
        char lu[100] = "";
        if (sqlite3_open("_essai_mig.grymd", &db) == SQLITE_OK
            && sqlite3_prepare_v2(db, "PRAGMA integrity_check", -1, &st, NULL) == SQLITE_OK && sqlite3_step(st) == SQLITE_ROW)
            snprintf(lu, sizeof lu, "%s", (const char *)sqlite3_column_text(st, 0));
        sqlite3_finalize(st);
        sqlite3_close(db);
        if (strcmp(lu, "ok") != 0) signaler(__LINE__, "intégrité après migrations", "ok", lu);
        remove("_essai_mig.grymd");
    }
    /* --- Message d'annulation (§ 3.3) --- */
    {
        total++;
        Portee *p = portee_creer();
        Machine *m = machine_creer();
        char *r = executer_source(p, m, "Un client, conservé, a : un nom (texte).\nAfficher 1 ÷ 0.", 0);
        char *a = machine_annulation(m, 0), *b = machine_annulation(m, 1);
        if (!a || strcmp(a, "Exécution annulée : rien n'a été conservé dans la base.") != 0
            || strcmp(b, "Saisie annulée : aucun nom n'a changé, rien n'a été conservé dans la base.") != 0)
            signaler(__LINE__, "annulation", "Exécution annulée : rien n'a été conservé dans la base.", a ? a : "(rien)");
        free(r); free(a); free(b);
        machine_detruire(m);
        portee_detruire(p);
        p = portee_creer();
        m = machine_creer();
        r = executer_source(p, m, "Afficher 1 ÷ 0.", 0);
        a = machine_annulation(m, 0);
        total++;
        if (a) signaler(__LINE__, "annulation sans base", "(rien)", a);
        free(r); free(a);
        machine_detruire(m);
        portee_detruire(p);
    }

    /* --- La base dans un fichier : ce qui est validé y reste, le reste n'y entre pas --- */
    {
        remove("_essai.grymd");
        const char *prog = "Un client, conservé, a : un nom (texte), un solde (nombre), une date (date), un actif (vrai ou faux).\n"
                           "Le a vaut un nouveau client :\n    Le nom vaut « Thérèse ».\n    Le solde vaut 0,1.\n"
                           "    La date vaut 21.09.2026.\n    L'actif vaut vrai.\nConserver a.\n";
        const char *rate = "Un client, conservé, a : un nom (texte), un solde (nombre), une date (date), un actif (vrai ou faux).\n"
                           "Le b vaut un nouveau client :\n    Le nom vaut « Bo ».\n    Le solde vaut 1.\n"
                           "    La date vaut 01.01.2026.\n    L'actif vaut faux.\nConserver b.\nAfficher 1 ÷ 0.\n";
        const char *autre = "Un client, conservé, a : un nom (texte).\n";
        const char *sources[] = { prog, rate, autre };
        const char *attendus[] = { "", "~Division par zéro", "~« solde » a disparu de « client » : 1 valeur conservée serait perdue." };
        for (int i = 0; i < 3; i++) {
            total++;
            Portee *p = portee_creer();
            Machine *m = machine_creer();
            machine_base(m, "_essai.grymd");
            char *r = executer_source(p, m, sources[i], 0);
            int ok = attendus[i][0] == '~' ? strstr(r, attendus[i] + 1) != NULL : strcmp(r, attendus[i]) == 0;
            if (!ok) signaler(__LINE__, sources[i], attendus[i], r);
            free(r);
            machine_detruire(m);
            portee_detruire(p);
        }
        total++;
        sqlite3 *db = NULL;
        sqlite3_stmt *st = NULL;
        char lu[200] = "";
        if (sqlite3_open("_essai.grymd", &db) == SQLITE_OK
            && sqlite3_prepare_v2(db, "SELECT (SELECT count(*) FROM grym_objet) || ' ' || \"c nom\" || ' ' || \"c solde\" "
                                      "|| ' ' || \"c date\" || ' ' || \"c actif\" FROM \"e client\"", -1, &st, NULL) == SQLITE_OK
            && sqlite3_step(st) == SQLITE_ROW)
            snprintf(lu, sizeof lu, "%s", (const char *)sqlite3_column_text(st, 0));
        sqlite3_finalize(st);
        sqlite3_close(db);
        if (strcmp(lu, "1 Thérèse 0.1 2026-09-21 1") != 0) signaler(__LINE__, "contenu de la base", "1 Thérèse 0.1 2026-09-21 1", lu);
        remove("_essai.grymd");
    }

    /* --- Ramasse-miettes : cycles et objets abandonnés --- */
    {
        total++;
        const char *src =
            "Un maillon a : un autre.\nLe dernier vaut un nouveau maillon.\n"
            "Répéter 50000 fois :\n    Le a vaut un nouveau maillon.\n    Le b vaut un nouveau maillon.\n"
            "    L'autre du a devient b.\n    L'autre du b devient a.\n    Le dernier devient a.\n";
        Portee *p = portee_creer();
        Machine *m = machine_creer();
        char *r = executer_source(p, m, src, 0);
        size_t vivants = machine_objets_vivants(m);
        if (strcmp(r, "") != 0 || vivants != 2) {
            char *o = grym_formater("sortie « %s », %lu objets vivants", r, (unsigned long)vivants);
            signaler(__LINE__, "ramasse-miettes", "2 objets vivants (le dernier maillon et son vis-à-vis)", o);
            free(o);
        }
        free(r);
        machine_detruire(m);
        portee_detruire(p);
    }
    /* un champ modifié par une saisie ratée retrouve sa valeur */
    {
        total++;
        Portee *p = portee_creer();
        Machine *m = machine_creer();
        const char *saisies[] = { "Un client a : un solde.", "Le c vaut un nouveau client :\n    Le solde vaut 100.",
                                  "Le solde du c devient 5. Afficher 1 ÷ 0.", "solde du c" };
        const char *attendus[] = { "", "", "~Division par zéro", "100" };
        for (int i = 0; i < 4; i++) {
            Portee *sp = portee_cloner(p);
            char *r = executer_source(p, m, saisies[i], 1);
            int echec = strncmp(r, "ERREUR", 6) == 0;
            int ok = attendus[i][0] == '~' ? strstr(r, attendus[i] + 1) != NULL : strcmp(r, attendus[i]) == 0;
            if (echec) { portee_detruire(p); p = sp; } else portee_detruire(sp);
            if (!ok) { signaler(__LINE__, saisies[i], attendus[i], r); free(r); break; }
            free(r);
        }
        machine_detruire(m);
        portee_detruire(p);
    }

    /* --- Une nouvelle version, ajoutée dans une saisie ultérieure --- */
    {
        total++;
        Portee *p = portee_creer();
        Machine *m = machine_creer();
        const char *saisies[] = { "Une personne a : un nom.\nUn membre est une personne.",
                                  "La sorte d'une personne vaut « personne ».", "Le m vaut un nouveau membre.",
                                  "sorte de m", "La sorte d'un membre vaut « membre ».", "sorte de m" };
        const char *attendus[] = { "", "", "", "personne", "", "membre" };
        for (int i = 0; i < 6; i++) {
            Portee *sp = portee_cloner(p);
            char *r = executer_source(p, m, saisies[i], 1);
            if (strncmp(r, "ERREUR", 6) == 0) { portee_detruire(p); p = sp; } else portee_detruire(sp);
            if (strcmp(r, attendus[i]) != 0) { signaler(__LINE__, saisies[i], attendus[i], r); free(r); break; }
            free(r);
        }
        machine_detruire(m);
        portee_detruire(p);
    }

    /* --- Formules dans la boucle interactive --- */
    {
        total++;
        Portee *p = portee_creer();
        Machine *m = machine_creer();
        const char *saisies[] = {
            "Le carré d'un nombre vaut nombre × nombre.",
            "carré de 9",
            "Le cube d'un nombre vaut nombre × carré de nombre. Afficher 1 ÷ 0.",
            "cube de 2",
            "Le total vaut 1.",
            "Pour doubler :\n    Le total devient total × 2.",
            "Doubler.",
            "total",
            "Doubler. Le total devient total ÷ 0.",
            "total"
        };
        const char *attendus[] = { "", "81", "~Division par zéro", "~« cube de » inconnu", "", "", "", "2",
                                   "~Division par zéro", "2" };
        for (int i = 0; i < 10; i++) {
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

    /* --- Module à plusieurs blocs : désassemblage et fichier --- */
    {
        total++;
        const char *src = "Le carré d'un nombre vaut nombre × nombre.\nAfficher le carré de 3.";
        const char *attendu =
            "Programme\n"
            "   2  0000  CONSTANTE         0     ; 3\n"
            "      0003  APPELER           0     ; carré (1 argument, rend une valeur)\n"
            "      0008  AFFICHER          1\n"
            "      0011  RETOUR\n"
            "\n"
            "Calcul « carré » : 1 paramètre, 1 case locale\n"
            "   1  0000  LIRE_LOCAL        0     ; paramètre 1\n"
            "      0003  LIRE_LOCAL        0     ; paramètre 1\n"
            "      0006  MULTIPLICATION\n"
            "      0007  RENDRE\n";
        Portee *p = portee_creer();
        Programme prog;
        Diagnostic d;
        analyser(src, strlen(src), p, 0, &prog, &d);
        Module *b = compiler(&prog, &d);
        programme_liberer(&prog);
        portee_detruire(p);
        char *r = module_desassembler(b);
        if (strcmp(r, attendu) != 0) signaler(__LINE__, src, attendu, r);
        free(r);
        /* aller-retour et exécution du fichier relu */
        total++;
        size_t taille;
        unsigned char *octets = module_serialiser(b, &taille);
        char *err = NULL;
        Module *relu = module_lire(octets, taille, &err);
        Machine *m = machine_creer();
        Chaine s = {0};
        if (!relu || !machine_executer(m, relu, &s, &d)) signaler(__LINE__, "relecture", "9", err ? err : d.message);
        else if (strcmp(s.d, "9\n") != 0) signaler(__LINE__, "relecture", "9", s.d);
        free(s.d);
        free(err);
        machine_detruire(m);
        module_detruire(relu);
        free(octets);
        module_detruire(b);

        /* un fichier au format 1 (un seul bloc) reste lisible */
        total++;
        const unsigned char v1[] = { 'G','R','Y','M', 1,0, 0,0,0,0, 0,0,0,0, 1,0,0,0, I_RETOUR, 0,0,0,0 };
        Module *ancien = module_lire(v1, sizeof v1, &err);
        if (!ancien) signaler(__LINE__, "format 1", "lisible", err);
        free(err);
        err = NULL;
        module_detruire(ancien);

        /* appel d'une formule absente (fichier fabriqué) */
        total++;
        Bloc *x = bloc_creer();
        long k = bloc_nom(x, "fantôme");
        bloc_emettre_appel(x, (uint16_t)k, 0, 0, 1, 1);
        bloc_emettre(x, I_RETOUR, 0, 1, 1);
        Module *mx = module_creer();
        module_ajouter(mx, x);
        Machine *m2 = machine_creer();
        Chaine s2 = {0};
        if (machine_executer(m2, mx, &s2, &d) || !strstr(d.message, "Formule « fantôme » inconnue"))
            signaler(__LINE__, "formule absente", "Formule « fantôme » inconnue", d.message ? d.message : "(acceptée)");
        else diagnostic_liberer(&d);
        free(s2.d);
        machine_detruire(m2);
        module_detruire(mx);
    }

    /* --- Désassemblage (docs/vm.md, § 12) --- */
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
            Module *b = compiler(&prog, &d);
            r = module_desassembler(b);
            module_detruire(b);
            programme_liberer(&prog);
        } else {
            r = grym_dupliquer(d.message);
            diagnostic_liberer(&d);
        }
        if (strcmp(r, attendu) != 0) signaler(__LINE__, src, attendu, r);
        free(r);
        portee_detruire(p);
    }

    /* --- Fichier .grymb : aller-retour et fichiers corrompus (docs/vm.md, § 4 et 9) --- */
    {
        const char *src = "Le x vaut 2 ^ 10.\nAfficher « x = » puis x ÷ 4.";
        Portee *p = portee_creer();
        Programme prog;
        Diagnostic d;
        analyser(src, strlen(src), p, 0, &prog, &d);
        Module *b = compiler(&prog, &d);
        programme_liberer(&prog);
        portee_detruire(p);
        size_t taille;
        unsigned char *octets = module_serialiser(b, &taille);

        /* aller-retour : même désassemblage, même résultat */
        total++;
        char *err = NULL;
        Module *relu = module_lire(octets, taille, &err);
        char *d1 = module_desassembler(b), *d2 = relu ? module_desassembler(relu) : grym_dupliquer(err);
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
            module_detruire(relu);
        } else {
            signaler(__LINE__, "exécution du .grymb relu", "x = 256", "fichier illisible");
        }

        /* corruptions : chaque cas doit être refusé avec un message, sans planter */
        struct { const char *nom; size_t pos; int octet; size_t coupe; const char *fragment; } cas[] = {
            { "en-tête",           0, 'X', 0, "en-tête" },
            { "version",           4, 99,  0, "version" },
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
            Module *x = module_lire(copie, t, &e);
            if (x || !e || !strstr(e, cas[k].fragment))
                signaler(__LINE__, cas[k].nom, cas[k].fragment, e ? e : "(accepté)");
            module_detruire(x);
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
            Module *mx = module_creer();
            module_ajouter(mx, x);
            Machine *m = machine_creer();
            Chaine s = {0};
            Diagnostic dd;
            int ok = machine_executer(m, mx, &s, &dd);
            if (ok || !dd.message || !strstr(dd.message, codes[k].fragment))
                signaler(__LINE__, codes[k].nom, codes[k].fragment, dd.message ? dd.message : "(accepté)");
            if (!ok) diagnostic_liberer(&dd);
            free(s.d);
            machine_detruire(m);
            module_detruire(mx);
        }
        module_detruire(b);
    }

    /* --- Plusieurs vers plusieurs (§ 16.13) --- */
#define MUL "Une personne, conservée, a : un nom (texte), unique.\n" \
            "Un genre, conservé, a : un nom (texte).\n" \
            "Une œuvre, conservée, a : un titre (texte), un compositeur (personne), des interprètes (personne), " \
            "des genres (genre), des travaux (travail) (genre).\n" \
            "Pour créer un nom :\n    La p vaut une nouvelle personne :\n        Le nom vaut nom.\n    Conserver p.\n" \
            "Pour classer un nom :\n    Le g vaut un nouveau genre :\n        Le nom vaut nom.\n    Conserver g.\n" \
            "Créer « Bach ».\nCréer « Callas ».\nClasser « baroque ».\nClasser « sacré ».\n" \
            "La bach vaut la personne conservée dont le nom est « Bach ».\n" \
            "La callas vaut la personne conservée dont le nom est « Callas ».\n" \
            "Le baroque vaut le genre conservé dont le nom est « baroque ».\n" \
            "Le sacré vaut le genre conservé dont le nom est « sacré ».\n" \
            "L'o vaut une nouvelle œuvre :\n    Le titre vaut « Messe ».\n    Le compositeur vaut bach.\nConserver o.\n" \
            "Les genres de o gagnent baroque.\nLes genres de o gagnent sacré.\nLes genres de o gagnent baroque.\n" \
            "Les interprètes de o gagnent callas.\n"
    PROG(MUL "Pour chaque genre de o, par nom décroissant, afficher nom du genre.\nAfficher le nombre de genres de o.",
         "sacré\nbaroque\n2");
    PROG(MUL "Pour chaque interprète de o, afficher nom de l'interprète.\nAfficher le nombre d'interprètes de o.",
         "Callas\n1");
    PROG(MUL "Pour chaque œuvre de sacré, afficher titre de l'œuvre.",
         "~Plusieurs champs relient une œuvre à un genre : « genres » et « travaux ». "
         "Précisez avec « dont … est parmi les genres » ou « dont … est parmi les travaux ».");
    PROG(MUL "Afficher le nombre d'œuvres de callas.",
         "~Plusieurs champs relient une œuvre à une personne : « compositeur » et « interprètes ». "
         "Précisez avec « dont … est le compositeur » ou « dont … est parmi les interprètes ».");
    PROG(MUL "Afficher le nombre de travaux de o.\nPour chaque travail de o, afficher 1.", "0");
    PROG(MUL "Pour chaque œuvre conservée dont sacré est parmi les genres, afficher titre de l'œuvre.", "Messe");
    PROG(MUL "Les travaux de o gagnent sacré.\nPour chaque travail de o, afficher nom du travail.\n"
         "Afficher le nombre de genres de o.", "sacré\n2");
    PROG(MUL "Afficher le nombre d'œuvres conservées dont callas est parmi les interprètes puis "
         "le nombre d'œuvres conservées dont bach est parmi les interprètes puis "
         "le nombre d'œuvres conservées dont sacré n'est pas parmi les genres puis "
         "le nombre d'œuvres conservées dont bach est le compositeur et baroque est parmi les genres.", "1 0 0 1");
    PROG(MUL "Les genres de o perdent sacré.\nLes genres de o perdent sacré.\nAfficher le nombre de genres de o.", "1");
    /* corbeille : un genre supprimé disparaît des lectures et ne se gagne pas */
    PROG(MUL "Supprimer sacré.\nAfficher le nombre de genres de o.\nRétablir sacré.\nAfficher le nombre de genres de o.",
         "1\n2");
    PROG(MUL "Supprimer sacré.\nLes genres de o gagnent sacré.",
         "~Le champ « genres » gagnerait un genre supprimé : rétablissez-le d'abord.");
    /* effacer : une œuvre emporte ses liaisons ; un genre encore gagné ne s'efface pas */
    PROG(MUL "Supprimer baroque définitivement.",
         "~Ce genre est encore désigné par le champ « genres » d'une œuvre.");
    PROG(MUL "Supprimer o définitivement.\nSupprimer baroque définitivement.\nAfficher le nombre de genres conservés.", "1");
    PROG(MUL "Le g vaut un nouveau genre :\n    Le nom vaut « x ».\nLes genres de o gagnent g.",
         "~Le champ « genres » gagnerait un genre qui n'est pas conservé : conservez-le d'abord.");
    PROG(MUL "La n vaut une nouvelle œuvre :\n    Le titre vaut « N ».\n    Le compositeur vaut bach.\n"
         "Les genres de n gagnent baroque.",
         "~Une œuvre qui n'est pas conservée ne gagne rien : ses « genres » vivent dans la base.");
    PROG(MUL "Les genres de o gagnent bach.", "~Le champ « genres » attend un genre, pas une personne.");
    PROG(MUL "Les genres de o gagnent 3.", "~Le champ « genres » attend un genre, pas un nombre.");
    PROG(MUL "Afficher le genres de o.", "~« genres » est un champ multiple, pas une valeur");
    /* un champ multiple qui porte le nom d'une entité : repli sur la relation inverse quand l'objet ne l'a pas */
    PROG("Un genre, conservé, a : un nom (texte).\nUne œuvre, conservée, a : un titre (texte), un genre (genre).\n"
         "Une personne, conservée, a : un nom (texte), des œuvres (œuvre).\n"
         "Le g vaut un nouveau genre :\n    Le nom vaut « b ».\nConserver g.\n"
         "L'o vaut une nouvelle œuvre :\n    Le titre vaut « O ».\n    Le genre vaut g.\nConserver o.\n"
         "La p vaut une nouvelle personne :\n    Le nom vaut « P ».\nConserver p.\n"
         "Pour chaque œuvre de g, afficher titre de l'œuvre.\nAfficher le nombre d'œuvres de p.\n"
         "Les œuvres de p gagnent o.\nPour chaque œuvre de p, afficher titre de l'œuvre.\n"
         "Afficher le nombre de personnes conservées dont o est parmi les œuvres.", "O\n0\nO\n1");
    {   /* migrations : ajout sur une entité peuplée, retrait refusé tant qu'une liaison existe */
        remove("_essai_mul.grymd");
        const char *etapes[][2] = {
            { "Un genre, conservé, a : un nom (texte).\nUne œuvre, conservée, a : un titre (texte).\n"
              "L'o vaut une nouvelle œuvre :\n    Le titre vaut « X ».\nConserver o.\n", "" },
            { "Un genre, conservé, a : un nom (texte).\nUne œuvre, conservée, a : un titre (texte), des genres (genre).\n"
              "Le g vaut un nouveau genre :\n    Le nom vaut « b ».\nConserver g.\n"
              "Les genres de l'œuvre conservée dont le titre est « X » gagnent g.\n"
              "Afficher le nombre de genres de l'œuvre conservée dont le titre est « X ».\n", "1" },
            { "Un genre, conservé, a : un nom (texte).\nUne œuvre, conservée, a : un titre (texte).\n",
              "~« genres » a disparu de « œuvre » : 1 liaison serait perdue." },
            { "Un genre, conservé, a : un nom (texte).\nUne œuvre, conservée, a : un titre (texte), un genres (genre), facultatif.\n",
              "~« genres » ne peut pas devenir un lien simple" },
            { "Un genre, conservé, a : un nom (texte).\nUne œuvre, conservée, a : un titre (texte), des genres (genre).\n"
              "Les genres de l'œuvre conservée dont le titre est « X » perdent le genre conservé dont le nom est « b ».\n", "" },
            { "Un genre, conservé, a : un nom (texte).\nUne œuvre, conservée, a : un titre (texte).\nAfficher 1.\n", "1" },
        };
        for (int i = 0; i < 6; i++) {
            total++;
            Portee *p = portee_creer();
            Machine *m = machine_creer();
            machine_base(m, "_essai_mul.grymd");
            char *r = executer_source(p, m, etapes[i][0], 0);
            const char *att = etapes[i][1];
            int ok = att[0] == '~' ? strstr(r, att + 1) != NULL : strcmp(r, att) == 0;
            if (!ok) signaler(__LINE__, etapes[i][0], att, r);
            free(r);
            machine_detruire(m);
            portee_detruire(p);
        }
        remove("_essai_mul.grymd");
    }

    /* --- Années (§ 14.5) --- */
#define AN "Une œuvre, conservée, a : un titre (texte), une composition (année), une révision (année), facultative.\n" \
           "Pour créer un titre et une année :\n    L'o vaut une nouvelle œuvre :\n        Le titre vaut titre.\n" \
           "        La composition vaut année.\n    Conserver o.\n" \
           "Créer « A » et 1747.\nCréer « B » et l'année de 21.09.1685.\nCréer « C » et 12.\n" \
           "L'a vaut l'œuvre conservée dont le titre est « A ».\n"
    PROG(AN "Afficher composition de a puis composition de a + 3 puis composition de a − 1000 puis "
         "composition de a − composition de l'œuvre conservée dont le titre est « B ».", "1747 1750 747 62");
    PROG(AN "Afficher composition de a > 1700 puis 1747 = composition de a puis composition de a ≠ 1747,0 puis "
         "l'année de 31.12.2026 puis révision de a.", "vrai vrai faux 2026 absent");
    PROG(AN "Pour chaque œuvre conservée dont la composition < 1700, par composition décroissant, afficher titre de l'œuvre.\n"
         "Afficher le nombre d'œuvres conservées dont la composition ≥ 12.", "B\nC\n3");
    PROG(AN "Pour chaque an de composition de a à 1749, afficher an.\nPour chaque an de 1749 à composition de a par pas de −2, "
         "afficher an.", "1747\n1748\n1749\n1'749\n1'747");
    PROG(AN "Selon composition de a :\n    Cas de 1700 à 1750, afficher « baroque ».\n    Autrement, afficher « autre ».", "baroque");
    PROG("Le premier d'un d vaut l'année de d.\nAfficher le premier de 01.01.2000.", "2000");
    PROG(AN "Afficher composition de a + composition de a.", "~On n'additionne pas deux années.");
    PROG(AN "Afficher composition de a × 2.", "~On ne multiplie pas une année.");
    PROG(AN "Afficher −composition de a.", "~On ne prend pas l'opposé d'une année.");
    PROG(AN "Afficher 3 − composition de a.", "~On ne soustrait pas une année d'un nombre.");
    PROG(AN "Afficher composition de a + 0,5.", "~Une année se décale d'un nombre entier d'années.");
    PROG(AN "Afficher composition de a + 8300.", "~Année hors du calendrier : de 1 à 9999.");
    PROG(AN "Afficher composition de a < 01.01.2000.", "~Une année ne se compare pas à une date");
    PROG(AN "Afficher composition de a = « 1747 ».", "~Comparaison impossible entre une année et un texte.");
    PROG(AN "Le x vaut 2,5.\nLa composition de a devient x.", "~Le champ « composition » attend une année (de 1 à 9999), pas 2,5.");
    PROG(AN "Le x vaut 0.\nLa composition de a devient x.", "~attend une année (de 1 à 9999), pas 0.");
    PROG("Afficher le mois de 01.01.2000.", "~« mois de » inconnu");
    PROG("Le d vaut 01.01.2000.\nAfficher taille de d.", "~Une date n'a pas de champ « taille » : seulement « année ».");
    PROG("Un p a : une taille.\nLa q vaut un nouveau p :\n    La taille vaut 3.\nAfficher 1747 puis taille de q.", "1'747 3");
    {   /* migrations : nombre entier ↔ année */
        remove("_essai_an.grymd");
        const char *etapes[][2] = {
            { "Un livre, conservé, a : un titre (texte), un an (nombre entier).\n"
              "Le l vaut un nouveau livre :\n    Le titre vaut « X ».\n    L'an vaut 20000.\nConserver l.\n", "" },
            { "Un livre, conservé, a : un titre (texte), un an (année).\n",
              "~« an » ne peut pas devenir une année : 1 valeur conservée est hors de 1 à 9999." },
            { "Un livre, conservé, a : un titre (texte), un an (nombre entier).\n"
              "Le l vaut le livre conservé dont le titre est « X ».\nL'an du l devient 1990.\n", "" },
            { "Un livre, conservé, a : un titre (texte), un an (année), une sortie (année), 2000 au départ.\n"
              "Le l vaut le livre conservé dont le titre est « X ».\nAfficher an du l + 1 puis sortie du l.\n", "1991 2000" },
            { "Un livre, conservé, a : un titre (texte), un an (nombre entier), une sortie (année).\n"
              "Le l vaut le livre conservé dont le titre est « X ».\nAfficher an du l + 1.\n", "1'991" },
        };
        for (int i = 0; i < 5; i++) {
            total++;
            Portee *p = portee_creer();
            Machine *m = machine_creer();
            machine_base(m, "_essai_an.grymd");
            char *r = executer_source(p, m, etapes[i][0], 0);
            const char *att = etapes[i][1];
            int ok = att[0] == '~' ? strstr(r, att + 1) != NULL : strcmp(r, att) == 0;
            if (!ok) signaler(__LINE__, etapes[i][0], att, r);
            free(r);
            machine_detruire(m);
            portee_detruire(p);
        }
        remove("_essai_an.grymd");
    }

    /* --- Questions à l'utilisateur (§ 17) --- */
#define SAISIE_INTER(src, att)  verifier(__LINE__, src, att, 1)
#define SAISIE(src, att, ...) do { static const char *const L[] = { __VA_ARGS__ }; \
    verifier_saisie(__LINE__, src, L, sizeof L / sizeof *L, att); } while (0)
    SAISIE("Le nom vaut la réponse à « Nom ? ».\nAfficher « Bonjour » puis nom.", "Nom ? Bonjour Ana", "  Ana  ");
    SAISIE("L'âge vaut la réponse en nombre entier à « Âge ? ».\nAfficher âge + 1.", "Âge ? 34", "33");
    SAISIE("Le x vaut la réponse en nombre à « x ? ».\nAfficher x × 2.", "x ? 2'469,00", "1'234,50");
    SAISIE("Le d vaut la réponse en date à « Né le ? ».\nAfficher d + 1.", "Né le ? 02.02.1990", "01.02.1990");
    SAISIE("L'an vaut la réponse en année à « An ? ».\nAfficher an.", "An ? 1747", "1747");
    SAISIE("Si la réponse en vrai ou faux à « Sûr ? », afficher « oui ». Sinon, afficher « non ».",
           "Sûr ? non", "NON");
    SAISIE("La q vaut « Qui ? ».\nAfficher la réponse à (q).", "Qui ? Ana", "Ana");
    /* relance : la réponse fautive est annoncée, la question se repose */
    SAISIE("L'âge vaut la réponse en nombre entier à « Âge ? ».\nAfficher âge.",
           "Âge ? « x » n'est pas un nombre.\nÂge ? « 2,5 » n'est pas un nombre entier.\nÂge ? 7", "x", "2,5", "7");
    SAISIE("Le d vaut la réponse en date à « Jour ? ».\nAfficher d.",
           "Jour ? « 21.9.26 » n'est pas une date : écrivez jour.mois.année (21.09.2026).\nJour ? 21.09.2026",
           "21.9.26", "21.09.2026");
    SAISIE("L'an vaut la réponse en année à « An ? ».\nAfficher an.",
           "An ? « 12000 » n'est pas une année : de 1 à 9999.\nAn ? 1900", "12000", "1900");
    SAISIE("Le x vaut la réponse en vrai ou faux à « ? ».\nAfficher x.",
           "? Répondez par oui ou non.\n? vrai", "peut-être", "vrai");
    SAISIE("Le x vaut la réponse en nombre à « ? ».\nAfficher x.", "? Une réponse est attendue.\n? 3", "", "3");
    SAISIE("Le x vaut la réponse à « ? ».\nAfficher « [ » puis x puis « ] ».", "? [  ]", "");
    /* fin de l'entrée, pureté, boucle interactive */
    {   /* fin de l'entrée : aucune réponse à lire */
        static const char *const L[] = { "" };
        verifier_saisie(__LINE__, "Le x vaut la réponse à « Nom ? ».\nAfficher x.", L, 0,
                        "~Plus rien à lire : la réponse à « Nom ? » manque.");
    }
    PROG("Le x vaut la réponse à « Nom ? ».\nAfficher x.", "~Aucune entrée : la question ne peut pas être posée ici.");
    PROG("Le double d'un x vaut la réponse en nombre à « ? ».\nAfficher le double de 1.",
         "~Un calcul ne pose pas de question : demandez dans une action.");
    SAISIE_INTER("Le x vaut la réponse à « ? ».", "~La question se pose dans un programme lancé, pas dans la boucle interactive.");
    {   /* une question valide ce qui la précède ; l'erreur suivante n'annule que depuis là */
        remove("_essai_q.grymd");
        static const char *const L[] = { "Ana", "Bob" };
        const char *src = "Un client, conservé, a : un nom (texte).\n"
                          "Le a vaut un nouveau client :\n    Le nom vaut la réponse à « 1 ? ».\nConserver a.\n"
                          "Le b vaut un nouveau client :\n    Le nom vaut la réponse à « 2 ? ».\nConserver b.\n"
                          "Afficher 1 ÷ 0.\n";
        total++;
        Portee *p = portee_creer();
        Machine *m = machine_creer();
        Script sc = { L, 2, 0 };
        machine_lecteur(m, reponses, &sc);
        machine_base(m, "_essai_q.grymd");
        char *r = executer_source(p, m, src, 0);
        char *annule = machine_annulation(m, 0);
        int ok = strstr(r, "Division par zéro") != NULL && annule
                 && strcmp(annule, "Exécution annulée : rien n'a été conservé depuis la dernière question.") == 0;
        if (!ok) signaler(__LINE__, src, "annulation depuis la dernière question", annule ? annule : r);
        free(annule);
        free(r);
        machine_detruire(m);
        portee_detruire(p);

        total++;
        p = portee_creer();
        m = machine_creer();
        machine_base(m, "_essai_q.grymd");
        r = executer_source(p, m, "Un client, conservé, a : un nom (texte).\n"
                                  "Pour chaque client conservé, par nom, afficher nom du client.", 0);
        if (strcmp(r, "Ana") != 0) signaler(__LINE__, "clients conservés", "Ana", r);
        free(r);
        machine_detruire(m);
        portee_detruire(p);
        remove("_essai_q.grymd");
    }

    /* --- Mise en forme de l'affichage (§ 4.1, § 4.2) --- */
    PROG("Afficher « Berthoud » sur 5 puis « |ab ».", "Berth |ab");
    PROG("Afficher « ab » sur 5 puis « | ».", "ab    |");
    PROG("Afficher « ab » sur 5 à droite puis « | ».", "   ab |");
    PROG("Afficher 12,5 sur 6 puis « | ».", "  12,5 |");
    PROG("Afficher 12,5 sur 6 à gauche puis « | ».", "12,5   |");
    PROG("Afficher « été » sur 4 puis « | ».", "été  |");   /* caractères, pas octets */
    PROG("Le l vaut 3.\nAfficher « ab » sur l + 1 puis « | ».", "ab   |");
    PROG("Afficher « a » sur 3, sans passer à la ligne.\nAfficher « b ».", "a  b");
    PROG("Afficher « a ».\nAfficher « ».\nAfficher « b ».", "a\n\nb");
    PROG("Le x vaut « ab » sur 4.\nAfficher x puis « | ».", "ab   |");
    PROG("Afficher 1 sur 0.", "~Largeur invalide : un nombre entier de 1 à 1000 est attendu.");
    PROG("Afficher 1 sur 2,5.", "~Largeur invalide");
    PROG("Afficher 1 sur 1001.", "~Largeur invalide");
    PROG("Afficher 1 sur « a ».", "~Largeur invalide");
    /* style des nombres (§ 4.1) */
    PROG("Afficher 1234567,25.", "1'234'567,25");
    PROG("Les nombres s'affichent sans séparateur.\nAfficher 1234567,25.", "1234567,25");
    PROG("Les nombres s'affichent à la suisse.\nAfficher 1234567,25.", "1'234'567,25");
    PROG("Les nombres s'affichent à la française.\nAfficher 1234567,25 puis −1000.", "1\u202f234\u202f567,25 −1\u202f000");
    PROG("Les nombres s'affichent à la française.\nLe d vaut 01.02.1990.\nAfficher l'année de d puis d.",
         "1990 01.02.1990");   /* une année et une date gardent leur forme */
    PROG("Afficher 1.\nLes nombres s'affichent à la suisse.", "~se déclare avant le premier affichage");
    PROG("Les nombres s'affichent à la suisse.\nLes nombres s'affichent à la française.", "~une seule fois");
    PROG("Pour f :\n    Les nombres s'affichent à la suisse.", "~au premier niveau du programme");
    PROG("Les nombres s'affichent à la belge.", "~Style attendu");
    PROG("Afficher 1 à droite.", "~« à gauche » et « à droite » suivent une largeur");

    /* --- Effacer l'écran (§ 4.3) : hors d'un terminal, la phrase n'écrit rien --- */
    PROG("Afficher « a ».\nEffacer l'écran.\nAfficher « b ».", "a\nb");
    PROG("Le double d'un x :\n    Effacer l'écran.\n    Rendre x.", "~Un calcul n'affiche rien");
    PROG("Effacer.", "~Écrivez « Effacer l'écran. ».");
    PROG("Effacer l'écran et le reste.", "~« et » inattendu, attendu : un point final.");

    printf("%d/%d tests réussis\n", total - echecs, total);
    return echecs ? EXIT_FAILURE : EXIT_SUCCESS;
}

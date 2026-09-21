/* Tests de l'analyseur GrymoiR v0.1.
 * Chaque test compare la forme parenthésée de l'arbre, ou le message
 * et la position de la première erreur.
 */
#include "analyseur.h"
#include "texte.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#endif

static int total = 0, echecs = 0;

/* Arbre sous forme de texte, une phrase par ligne,
 * ou « ERREUR l:c message » si l'analyse échoue. */
static char *arbre(Portee *portee, const char *src, int interactif) {
    Programme p;
    Diagnostic d;
    Chaine c = {0};
    if (!analyser(src, strlen(src), portee, interactif, &p, &d)) {
        char *m = grym_formater("ERREUR %d:%d %s", d.ligne, d.colonne, d.message);
        diagnostic_liberer(&d);
        return m;
    }
    for (size_t i = 0; i < p.nb; i++) {
        char *s = noeud_decrire(p.phrases[i]);
        if (i) chaine_ajouter(&c, "\n");
        chaine_ajouter(&c, s);
        free(s);
    }
    programme_liberer(&p);
    return chaine_rendre(&c);
}

static void signaler(int l, const char *src, const char *attendu, const char *obtenu) {
    echecs++;
    printf("ÉCHEC (test ligne %d)\n  source  : %s\n  attendu : %s\n  obtenu  : %s\n",
           l, src, attendu, obtenu);
}

/* Programme complet, portée neuve, mode fichier. */
static void verifier(int l, const char *src, const char *attendu) {
    total++;
    Portee *p = portee_creer();
    char *r = arbre(p, src, 0);
    if (strcmp(r, attendu) != 0) signaler(l, src, attendu, r);
    free(r);
    portee_detruire(p);
}

/* L'analyse échoue, à la position donnée, avec un message contenant le fragment. */
static void verifier_erreur(int l, const char *src, int ligne, int colonne, const char *fragment) {
    total++;
    Portee *p = portee_creer();
    char *r = arbre(p, src, 0);
    char prefixe[64];
    snprintf(prefixe, sizeof prefixe, "ERREUR %d:%d ", ligne, colonne);
    if (strncmp(r, prefixe, strlen(prefixe)) != 0 || !strstr(r, fragment)) {
        char *att = grym_formater("%s… %s …", prefixe, fragment);
        signaler(l, src, att, r);
        free(att);
    }
    free(r);
    portee_detruire(p);
}

/* Boucle interactive : plusieurs saisies qui partagent la même portée. */
static void verifier_session(int l, const char **saisies, const char **attendus, int nb) {
    total++;
    Portee *p = portee_creer();
    for (int i = 0; i < nb; i++) {
        char *r = arbre(p, saisies[i], 1);
        int ok = attendus[i][0] == '~' ? strstr(r, attendus[i] + 1) != NULL
                                       : strcmp(r, attendus[i]) == 0;
        if (!ok) {
            signaler(l, saisies[i], attendus[i], r);
            free(r);
            break;
        }
        free(r);
    }
    portee_detruire(p);
}

#define V(src, att)            verifier(__LINE__, src, att)
#define VE(src, li, co, frag)  verifier_erreur(__LINE__, src, li, co, frag)
#define SESSION(s, a)          verifier_session(__LINE__, s, a, (int)(sizeof s / sizeof *s))

int main(void) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    /* --- Exemple de la spécification (§ 2.1, § 4) --- */
    V("Le prix unitaire vaut 12,50.\n"
      "La quantité vaut 3.\n"
      "Le total vaut prix unitaire × quantité.\n"
      "Le total devient total + 5.\n"
      "Afficher « Total à payer : » puis le total.",
      "(créer [prix unitaire] 12.50)\n"
      "(créer [quantité] 3)\n"
      "(créer [total] (× [prix unitaire] [quantité]))\n"
      "(modifier [total] (+ [total] 5))\n"
      "(afficher «Total à payer :» [total])");
    V("", "");
    V("Remarque : seulement une remarque", "(remarque «seulement une remarque»)");

    /* --- Priorités et associativité (§ 3.1) --- */
    V("Le x vaut 1 + 2 × 3.", "(créer [x] (+ 1 (× 2 3)))");
    V("Le x vaut 10 − 3 − 2.", "(créer [x] (− (− 10 3) 2))");
    V("Le x vaut 8 ÷ 4 ÷ 2.", "(créer [x] (÷ (÷ 8 4) 2))");
    V("Le x vaut 2 ^ 3 ^ 2.", "(créer [x] (^ 2 (^ 3 2)))");
    V("Le x vaut 2 × 3 ^ 2.", "(créer [x] (× 2 (^ 3 2)))");
    V("Le x vaut −2 ^ 2.", "(créer [x] (^ (− 2) 2))");
    V("Le x vaut 2 ^ −1.", "(créer [x] (^ 2 (− 1)))");
    V("Le x vaut - - 3.", "(créer [x] (− (− 3)))");
    V("Le x vaut (1 + 2) × 3.", "(créer [x] (× (groupe (+ 1 2)) 3))");
    V("Le x vaut 2 * 3 / 4 - 1.", "(créer [x] (− (÷ (× 2 3) 4) 1))");

    /* --- Noms composés, plus longue correspondance (§ 2.2) --- */
    V("Le prix vaut 1.\nLe prix unitaire vaut 2.\n"
      "Le x vaut prix unitaire × 2.\nLe y vaut prix × 2.",
      "(créer [prix] 1)\n(créer [prix unitaire] 2)\n"
      "(créer [x] (× [prix unitaire] 2))\n(créer [y] (× [prix] 2))");
    V("Le prix de l'article vaut 5.\nLa date de la vente vaut 3.\n"
      "Afficher le prix de l'article puis la date de la vente.",
      "(créer [prix de l'article] 5)\n(créer [date de la vente] 3)\n"
      "(afficher [prix de l'article] [date de la vente])");

    /* --- Articles, élision, casse (§ 2.3, charte art. 4) --- */
    V("Le prix vaut 2.\nLa quantité vaut 3.\nLe total vaut le prix × la quantité.",
      "(créer [prix] 2)\n(créer [quantité] 3)\n(créer [total] (× [prix] [quantité]))");
    V("L'addition vaut 5.\nLe x vaut l'addition + 1.",
      "(créer [addition] 5)\n(créer [x] (+ [addition] 1))");
    V("LE TOTAL VAUT 3.\nAfficher Le Total.", "(créer [total] 3)\n(afficher [total])");
    V("L’addition vaut 5.\nLa addition devient 6.\nLa addition devient 7.",
      "(créer [addition] 5)\n(modifier [addition] 6)\n(modifier [addition] 7)");

    /* --- Afficher (§ 4) --- */
    V("Afficher « a » puis 1 + 1 puis « b ».", "(afficher «a» (+ 1 1) «b»)");
    V("Afficher 3.", "(afficher 3)");

    /* --- Remarques (§ 1.6) --- */
    V("Remarque : début\nLe x vaut 1.\nRemarque : fin",
      "(remarque «début»)\n(créer [x] 1)\n(remarque «fin»)");

    /* --- Plusieurs phrases sur une ligne, une phrase sur plusieurs lignes --- */
    V("Le a vaut 1. Le b vaut a + 1.", "(créer [a] 1)\n(créer [b] (+ [a] 1))");
    V("Le a vaut 1\n  + 2.", "(créer [a] (+ 1 2))");

    /* --- Erreurs de référence (§ 6) --- */
    VE("Le total vaut 3.\nLe total vaut 4.", 2, 4,
       "« total » existe déjà (ligne 1). Pour le modifier, écrivez : Le total devient …");
    VE("Le total devient 4.", 1, 4,
       "« total » n'existe pas. Pour le créer, écrivez : Le total vaut …");
    VE("Le total vaut 3.\nLe totl devient 4.", 2, 4, "« totl » inconnu, vouliez-vous « total » ?");
    VE("Le total vaut 3.\nLe x vaut totl + 1.", 2, 11, "« totl » inconnu, vouliez-vous « total » ?");
    VE("Le solde vaut 3.\nAfficher le soldee.", 2, 13, "vouliez-vous « solde » ?");
    VE("Le total vaut 3.\nLa total devient 4.", 2, 1, "« total » est masculin (déclaré ligne 1).");
    VE("L'addition vaut 3.\nLa addition devient 4.\nLe x vaut le addition.", 3, 11,
       "« addition » est féminin (déclaré ligne 2).");
    VE("Le x vaut 3", 1, 12, "Point final manquant (ligne 1).");
    VE("Le x vaut 3\nLe y vaut 4.", 1, 12, "Point final manquant (ligne 1).");
    VE("Le x vaut 1 / 0.\nLe x vaut 3.5.", 2, 11, "Écrivez « 3,5 »");

    /* --- Autres erreurs --- */
    VE("Le prix unitaire vaut 1.\nLe x vaut prix unitair × 2.", 2, 11,
       "« prix unitair » inconnu, vouliez-vous « prix unitaire » ?");
    VE("Le x vaut y.", 1, 11, "« y » inconnu.");
    VE("Le total vaut total + 1.", 1, 15, "« total » inconnu");
    VE("3 + 4.", 1, 1, "une phrase commence par Le, La, L' ou Afficher");
    VE("Le x vaut 3. Remarque : non", 1, 14, "Une remarque doit commencer une ligne");
    VE("Le x vaut 1 +\nRemarque : coupe\n2.", 2, 1, "ne peut pas couper une phrase");
    VE("Le x vaut (1 + 2.", 1, 17, "Parenthèse fermante manquante");
    VE("Le x vaut 1 +.", 1, 14, "Expression incomplète");
    VE("Le x vaut « a ».", 1, 11, "Un texte ne peut apparaître que dans une phrase Afficher.");
    VE("Afficher.", 1, 9, "Rien à afficher");
    VE("Afficher 1 puis.", 1, 16, "Élément manquant après « puis »");
    VE("Le vaut 3.", 1, 4, "Nom manquant entre « le » et « vaut ».");
    VE("Le x 3.", 1, 1, "Verbe manquant");
    VE("Le x vaut 1 2.", 1, 13, "« 2 » inattendu");
    VE("Le prix puis vaut 3.", 1, 9, "« puis » est un mot réservé");
    VE("Le la vaut 3.", 1, 4, "Un nom ne peut pas commencer par « la ».");
    VE("Le x vaut 1.\nAfficher le x puis le.", 2, 20, "Nom attendu après « le ».");
    VE("Le x 3 vaut 2.", 1, 6, "« 3 » ne peut pas faire partie d'un nom.");
    VE("Afficher « a » « b ».", 1, 16, "attendu un opérateur, « puis » ou un point final");

    /* --- Boucle interactive (§ 3.3) --- */
    {
        const char *s[] = { "1,1 + 2,2" };
        const char *a[] = { "(évaluer (+ 1.1 2.2))" };
        SESSION(s, a);
    }
    {   /* les noms persistent d'une saisie à l'autre */
        const char *s[] = { "Le total vaut 3.", "le total", "Le total devient total × 2", "total + 1" };
        const char *a[] = { "(créer [total] 3)", "(évaluer [total])",
                            "(modifier [total] (× [total] 2))", "(évaluer (+ [total] 1))" };
        SESSION(s, a);
    }
    {   /* une saisie ratée ne déclare rien, même la phrase réussie qui la précède */
        const char *s[] = { "Le a vaut 1. Le b vaut c.", "Le a vaut 2.", "b" };
        const char *a[] = { "~« c » inconnu", "(créer [a] 2)", "~« b » inconnu" };
        SESSION(s, a);
    }
    {   /* un genre fixé par une saisie ratée est annulé aussi */
        const char *s[] = { "L'addition vaut 1.", "La addition devient c.", "Le addition devient 2." };
        const char *a[] = { "(créer [addition] 1)", "~« c » inconnu", "(modifier [addition] 2)" };
        SESSION(s, a);
    }

    printf("%d/%d tests réussis\n", total - echecs, total);
    return echecs ? EXIT_FAILURE : EXIT_SUCCESS;
}
